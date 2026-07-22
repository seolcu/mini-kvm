/*
 * main.c - Mini-KVM entry point
 *
 * An educational x86 hypervisor built directly on the Linux KVM ioctl API.
 * This file does nothing but sequence the phases; each phase lives in its own
 * module:
 *
 *   cli.c         command line -> vmm_config_t
 *   vm.c          /dev/kvm, the VM, guest memory
 *   vcpu.c        vCPU setup, the KVM_RUN loop, VM-exit handling
 *   cpu_modes.c   real / protected / long mode entry
 *   hypercall.c   the port 0x500 guest-host call interface
 *   devices.c     16550 UART and legacy PC port stubs
 *   console.c     terminal, keyboard input, vCPU-tagged output
 *   linux/        experimental bzImage boot, quarantined
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "cli.h"
#include "vm.h"
#include "vcpu.h"
#include "console.h"
#include "devices.h"
#include "debug.h"
#include "linux/linux_entry.h"

static vcpu_context_t vcpus[MAX_VCPUS];

/* Input-arrival hook for the Linux serial console (see console.h). */
static void pulse_serial_irq(void)
{
    vm_pulse_irq(4);
}

/*
 * Startup detail is diagnostic, not output. A plain run stays silent so the
 * guest's own output is all the user sees.
 */
static void report_configuration(const vmm_config_t *cfg)
{
    if (!verbose_enabled()) {
        return;
    }

    printf("=== Mini-KVM ===\n");
    if (cfg->linux_boot) {
        printf("Mode: Linux boot protocol (experimental)\n");
        printf("bzImage: %s\n", cfg->bzimage_path);
        if (cfg->linux_cmdline) printf("Command line: %s\n", cfg->linux_cmdline);
        if (cfg->initrd_path)   printf("Initrd: %s\n", cfg->initrd_path);
    } else if (cfg->long_mode) {
        printf("Mode: 64-bit long mode\n");
        printf("Entry point: 0x%x, load offset: 0x%x\n", cfg->entry_point, cfg->load_offset);
    } else if (cfg->paging) {
        printf("Mode: protected mode with paging\n");
        printf("Entry point: 0x%x, load offset: 0x%x\n", cfg->entry_point, cfg->load_offset);
    } else {
        printf("Mode: real mode\n");
    }
    printf("Starting %d vCPU(s)\n\n", cfg->num_guests);
}

/*
 * Build one vCPU per guest binary: allocate its memory, load the flat binary,
 * and enter the requested CPU mode.
 */
static int prepare_vcpus(const vmm_config_t *cfg)
{
    for (int i = 0; i < cfg->num_guests; i++) {
        vcpu_context_t *ctx = &vcpus[i];

        memset(ctx, 0, sizeof(*ctx));
        ctx->vcpu_id = i;
        ctx->vcpu_fd = -1;
        ctx->guest_binary = cfg->guests[i];
        snprintf(ctx->name, sizeof(ctx->name), "%s", vcpu_extract_name(ctx->guest_binary));

        ctx->use_paging = cfg->paging;
        ctx->long_mode = cfg->long_mode;
        ctx->entry_point = cfg->entry_point;
        ctx->load_offset = cfg->load_offset;

        if (verbose_enabled()) {
            printf("[Setup vCPU %d: %s]\n", i, ctx->name);
        }

        if (vm_map_vcpu_memory(ctx) < 0 ||
            vcpu_load_guest_binary(ctx->guest_binary, ctx->guest_mem,
                                   ctx->mem_size, ctx->load_offset) < 0 ||
            vcpu_setup(ctx) < 0) {
            return -1;
        }

        if (verbose_enabled()) {
            printf("\n");
        }
    }
    return 0;
}

/* Run every vCPU to completion, one pthread each. */
static int run_vcpus(int num_vcpus)
{
    pthread_t threads[MAX_VCPUS];
    int started = 0;
    int ret = 0;

    if (verbose_enabled()) {
        printf("=== Starting VM execution (%d vCPUs) ===\n\n", num_vcpus);
    }

    /* The legend explains the colors, so print it whenever output is
     * actually color-tagged -- that is, for more than one vCPU. */
    const char *legend_names[MAX_VCPUS];
    for (int i = 0; i < num_vcpus; i++) {
        legend_names[i] = vcpus[i].name;
    }
    console_print_legend(num_vcpus, legend_names);

    for (int i = 0; i < num_vcpus; i++) {
        if (pthread_create(&threads[i], NULL, vcpu_thread, &vcpus[i]) != 0) {
            fprintf(stderr, "Failed to create thread for vCPU %d\n", i);
            ret = -1;
            break;
        }
        started++;
    }

    /* Join whatever did start, so a partial failure still tears down cleanly. */
    for (int i = 0; i < started; i++) {
        pthread_join(threads[i], NULL);
    }

    if (verbose_enabled()) {
        printf("\n=== All vCPUs completed ===\n");
    }
    return ret;
}

int main(int argc, char **argv)
{
    vmm_config_t cfg;
    int ret = 0;

    int parse_status = cli_parse(argc, argv, &cfg);
    if (parse_status != 0) {
        return (parse_status == 2) ? 0 : parse_status;      /* 2 == --help */
    }

    debug_level = cfg.debug_level;
    vcpu_set_dump_options(cfg.dump_regs, cfg.dump_mem_path, cfg.num_guests);
    report_configuration(&cfg);

    if (cfg.linux_boot) {
        fprintf(stderr, "Warning: --linux is experimental and does not boot to a shell.\n");
    }

    console_install_signal_handlers();

    /* Raw mode only for interactive guests: real-mode guests need no input,
     * and leaving the tty alone keeps piped runs reproducible. */
    if (cfg.paging || cfg.linux_boot) {
        console_enable_raw_mode();
    }

    /* Only Linux mode gets an interrupt controller; real-mode guests
     * deliberately run without one (see vm_init). */
    if (vm_init(cfg.linux_boot) < 0) {
        ret = 1;
        goto cleanup_early;
    }

    if (cfg.linux_boot ? linux_prepare_vcpu(&vcpus[0], &cfg) < 0
                       : prepare_vcpus(&cfg) < 0) {
        ret = 1;
        goto cleanup_vcpus;
    }

    console_init_colors(cfg.num_guests);

    /* Every mode gets a stdin reader: HC_GETCHAR is available to real-mode
     * guests too, and without one they would block on input that never
     * arrives. The IRQ hook stays NULL outside Linux mode, because an
     * unexpected IRQ hangs a HLT-terminated real-mode guest. */
    devices_set_irq_hook(cfg.linux_boot ? pulse_serial_irq : NULL);
    if (console_start_input_thread(cfg.linux_boot ? pulse_serial_irq : NULL) < 0) {
        devices_set_irq_hook(NULL);
    }

    if (run_vcpus(cfg.num_guests) < 0) {
        ret = 1;
    }

    console_stop_input_thread();
    devices_set_irq_hook(NULL);

cleanup_vcpus:
    for (int i = 0; i < cfg.num_guests; i++) {
        vcpu_cleanup(&vcpus[i]);
    }

cleanup_early:
    console_restore();
    vm_shutdown();

    return ret;
}
