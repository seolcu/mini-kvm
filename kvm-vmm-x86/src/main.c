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

#include "cli.h"
#include "vm.h"
#include "vcpu.h"
#include "console.h"
#include "devices.h"
#include "debug.h"
#include "vga.h"
#include "loader.h"
#include "linux/linux_entry.h"

static vcpu_context_t vcpus[MAX_VCPUS];

/* Lets devices.c drive a guest IRQ line without depending on the VM module. */
static void set_guest_irq(uint32_t irq, int level)
{
    vm_set_irq_level(irq, level);
}

/* Called by the stdin reader for each character the host receives. */
static void on_host_input(char ch)
{
    devices_notify_input(ch);
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
 * Build one vCPU per guest image: work out the format, allocate memory sized
 * for it, load it, and enter the CPU mode the image expects.
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

        /* The image decides how much memory it needs, so identify it before
         * allocating anything. */
        if (loader_probe(ctx->guest_binary, &ctx->image.format) < 0) {
            return -1;
        }

        bool is_kernel_image = (ctx->image.format == GUEST_ELF ||
                                ctx->image.format == GUEST_MULTIBOOT);

        /* Each vCPU's memory is mapped at guest physical vcpu_id * mem_size,
         * so only vCPU 0 sees its kernel at the physical addresses the image
         * was linked for. Refusing is better than silently misplacing it. */
        if (is_kernel_image && cfg->num_guests > 1) {
            fprintf(stderr,
                    "Error: %s is a %s image, which must be loaded at its own "
                    "physical addresses.\n"
                    "       Only one such guest can run at a time; run it alone.\n",
                    ctx->guest_binary, loader_format_name(ctx->image.format));
            return -1;
        }

        if (verbose_enabled()) {
            printf("[Setup vCPU %d: %s (%s)]\n", i, ctx->name,
                   loader_format_name(ctx->image.format));
        }

        if (vm_map_vcpu_memory(ctx) < 0) {
            return -1;
        }

        if (is_kernel_image) {
            if (loader_load(ctx->guest_binary, ctx->guest_mem, ctx->mem_size,
                            cfg->linux_cmdline, &ctx->image) < 0) {
                return -1;
            }
        } else if (vcpu_load_guest_binary(ctx->guest_binary, ctx->guest_mem,
                                          ctx->mem_size, ctx->load_offset) < 0) {
            return -1;
        }

        if (vcpu_setup(ctx) < 0) {
            return -1;
        }

        if (cfg->explain) {
            vcpu_enable_trace(ctx);
        }

        if (verbose_enabled()) {
            printf("\n");
        }
    }
    return 0;
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

    /*
     * Kernel images and Linux get an interrupt controller and a timer; a
     * kernel's scheduler is useless without one. Real-mode guests must not
     * have one, so probe the first image to find out which we are running.
     */
    guest_format_t first_format = GUEST_FLAT;
    if (!cfg.linux_boot && cfg.num_guests > 0) {
        (void)loader_probe(cfg.guests[0], &first_format);
    }
    bool wants_interrupts = cfg.linux_boot ||
                            first_format == GUEST_ELF ||
                            first_format == GUEST_MULTIBOOT;

    if (vm_init(wants_interrupts) < 0) {
        ret = 1;
        goto cleanup_early;
    }

    if (cfg.linux_boot ? linux_prepare_vcpu(&vcpus[0], &cfg) < 0
                       : prepare_vcpus(&cfg) < 0) {
        ret = 1;
        goto cleanup_vcpus;
    }

    console_init_colors(cfg.num_guests);

    /* VGA rendering reads the guest's text buffer through our own mapping, so
     * it needs the vCPU memory to exist. vCPU 0 owns the display. */
    if (cfg.vga && vga_start(vcpus[0].guest_mem, vcpus[0].mem_size) < 0) {
        ret = 1;
        goto cleanup_vcpus;
    }

    /* Every mode gets a stdin reader: HC_GETCHAR is available to real-mode
     * guests too, and without one they would block on input that never
     * arrives. The IRQ hook stays NULL outside Linux mode, because an
     * unexpected IRQ hangs a HLT-terminated real-mode guest. */
    devices_set_irq_hook(wants_interrupts ? set_guest_irq : NULL);
    devices_enable_serial_irq(cfg.linux_boot);
    /* A kernel guest reads scancodes from the i8042; nothing else does. */
    devices_enable_keyboard(!cfg.linux_boot && wants_interrupts);

    if (console_start_input_thread(on_host_input) < 0) {
        devices_set_irq_hook(NULL);
    }

    if (verbose_enabled()) {
        printf("=== Starting VM execution (%d vCPUs) ===\n\n", cfg.num_guests);
    }

    /* The legend explains the colors, so print it whenever output is actually
     * color-tagged -- that is, for more than one vCPU. */
    const char *legend_names[MAX_VCPUS];
    for (int i = 0; i < cfg.num_guests; i++) {
        legend_names[i] = vcpus[i].name;
    }
    console_print_legend(cfg.num_guests, legend_names);

    if (vcpu_run_all(vcpus, cfg.num_guests) < 0) {
        ret = 1;
    }

    if (verbose_enabled()) {
        printf("\n=== All vCPUs completed ===\n");
    }

    vga_stop();
    console_stop_input_thread();
    devices_set_irq_hook(NULL);
    devices_enable_keyboard(false);

cleanup_vcpus:
    for (int i = 0; i < cfg.num_guests; i++) {
        vcpu_cleanup(&vcpus[i]);
    }

cleanup_early:
    console_restore();
    vm_shutdown();

    return ret;
}
