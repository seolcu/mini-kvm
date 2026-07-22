/*
 * vcpu.c - vCPU setup, the KVM_RUN loop, and VM-exit handling
 *
 * One pthread per vCPU, each running an independent guest. Mode entry
 * (segments, descriptor tables, page tables) is delegated to cpu_modes.c;
 * device and hypercall emulation to devices.c and hypercall.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#include "vcpu.h"
#include "vm.h"
#include "cpu_modes.h"
#include "console.h"
#include "devices.h"
#include "hypercall.h"
#include "debug.h"
#include "cpuid.h"
#include "msr.h"
#include "paging_64.h"
#include "protected_mode.h"
#include "long_mode.h"
#include "linux_boot.h"
#include "linux/linux_entry.h"

/* Guests enter real mode at physical 0; paging mode overrides this. */
#define GUEST_LOAD_ADDR 0x0

/* Diagnostic options, supplied once by main(). */
static bool dump_regs_on_exit = false;
static const char *dump_mem_path = NULL;
static int total_vcpus = 1;

void vcpu_set_dump_options(bool dump_regs, const char *mem_path, int num_vcpus)
{
    dump_regs_on_exit = dump_regs;
    dump_mem_path = mem_path;
    total_vcpus = num_vcpus;
}

/*
 * Load guest binary into guest memory
 */
int vcpu_load_guest_binary(const char *filename, void *mem, size_t mem_size, uint32_t load_offset)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        perror("fopen");
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize_long = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize_long < 0)
    {
        perror("ftell");
        fclose(f);
        return -1;
    }

    size_t fsize = (size_t)fsize_long;

    if (verbose_enabled())
    {
        printf("Guest binary size: %zu bytes\n", fsize);
    }

    if (fsize + load_offset > mem_size)
    {
        fprintf(stderr, "Guest binary too large (%zu bytes at offset 0x%x > %zu bytes)\n",
                fsize, load_offset, mem_size);
        fclose(f);
        return -1;
    }

    // Load binary at specified offset
    size_t nread = fread(mem + load_offset, 1, fsize, f);
    if (nread != fsize)
    {
        perror("fread");
        fclose(f);
        return -1;
    }

    fclose(f);

    if (verbose_enabled())
    {
        printf("Loaded guest binary: %zu bytes at offset 0x%x\n", nread, load_offset);

        // Show first few bytes
        printf("First bytes: ");
        size_t bytes_to_show = (fsize < 16 ? fsize : 16);
        for (size_t i = 0; i < bytes_to_show; i++)
        {
            printf("%02x ", ((unsigned char *)(mem + load_offset))[i]);
        }
        printf("\n");
    }

    return 0;
}
/*
 * Setup vCPU context (multi-vCPU version)
 */
int vcpu_setup(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // Create vCPU
    if (vm_create_vcpu(ctx) < 0)
    {
        return -1;
    }

    // Get current segment registers
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
    {
        perror("KVM_GET_SREGS");
        return -1;
    }

    // Linux path: enter either real-mode setup or code32_start
    if (ctx->linux_guest)
    {
        if (ctx->linux_entry == LINUX_ENTRY_BOOT64)
        {
            if (linux_configure_boot64_entry(ctx) < 0)
            {
                return -1;
            }
        }
        else if (ctx->linux_entry == LINUX_ENTRY_CODE32)
        {
            if (linux_configure_code32_entry(ctx, LINUX_BOOT_PARAMS_ADDR) < 0)
            {
                return -1;
            }
        }
        else
        {
            sregs.cr0 = 0x00000010; // ET set, PE=0
            sregs.cr3 = 0x00000000;
            sregs.cr4 = 0x00000000;
            sregs.efer = 0x0000000000000000;

            segments_set_real(&sregs, REAL_MODE_KERNEL_ADDR);

            if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
            {
                perror("KVM_SET_SREGS (linux real mode)");
                return -1;
            }

            if (setup_cpuid(vm_kvm_fd(), ctx->vcpu_fd) < 0)
            {
                return -1;
            }

            memset(&regs, 0, sizeof(regs));
            regs.rip = 0x200; // entry is CS:IP = (REAL_MODE_KERNEL_ADDR/16):0x0200
            regs.rsp = 0x9ff00;
            regs.rbp = regs.rsp;
            regs.rsi = 0; // setup code uses DS:SI to locate boot_params
            regs.rflags = 0x2;

            if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
            {
                perror("KVM_SET_REGS (linux real mode)");
                return -1;
            }

            struct kvm_mp_state mp_state;
            mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
            if (ioctl(ctx->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0)
            {
                perror("KVM_SET_MP_STATE");
                return -1;
            }
        }

        ctx->running = true;
        ctx->exit_count = 0;
        return 0;
    }

    // Setup Real Mode segments
    segments_set_real(&sregs,
                      ctx->linux_guest ? REAL_MODE_KERNEL_ADDR
                                       : (uint32_t)(ctx->vcpu_id * ctx->mem_size));

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS");
        return -1;
    }

    // Set CPUID entries (required for Linux to see LM/PAE/etc.)
    if (setup_cpuid(vm_kvm_fd(), ctx->vcpu_fd) < 0)
    {
        return -1;
    }

    // Set general purpose registers
    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->linux_guest ? ctx->entry_point : GUEST_LOAD_ADDR;
    regs.rflags = 0x2;
    if (ctx->linux_guest)
    {
        regs.rsp = 0x9ff00;
        regs.rbp = regs.rsp;
    }

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
    {
        perror("KVM_SET_REGS");
        return -1;
    }

    if (verbose_enabled())
    {
        vcpu_printf(ctx, "Set registers: RIP=0x%llx (Real Mode)\n", regs.rip);
    }

    // Set MP state to runnable
    struct kvm_mp_state mp_state;
    mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
    if (ioctl(ctx->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0)
    {
        perror("KVM_SET_MP_STATE");
        return -1;
    }

    // Skip paging/long-mode setup for Linux real-mode entry
    if (!ctx->linux_guest)
    {
        // If long mode is enabled, use 64-bit setup
        if (ctx->long_mode)
        {
            if (cpu_mode_enter_long(ctx) < 0)
            {
                return -1;
            }
        }
        // Otherwise, if paging is enabled, switch to Protected Mode (32-bit)
        else if (ctx->use_paging)
        {
            if (cpu_mode_enter_protected(ctx) < 0)
            {
                return -1;
            }
        }
    }

    ctx->running = true;
    ctx->exit_count = 0;

    return 0;
}
/*
 * Handle I/O port operations
 */
static int handle_io(vcpu_context_t *ctx)
{
    uint8_t *data = (uint8_t *)ctx->kvm_run + ctx->kvm_run->io.data_offset;
    uint16_t port = ctx->kvm_run->io.port;
    int size = ctx->kvm_run->io.size;
    bool is_out = (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT);

    if (verbose_enabled())
    {
        static int io_count = 0;
        if (io_count++ < 100)
        {
            vcpu_printf(ctx, "IO[%d]: dir=%s port=0x%x size=%d\n",
                        io_count, is_out ? "OUT" : "IN", port, size);
        }
    }

    if (port == HYPERCALL_PORT)
    {
        // Linux guests never issue Mini-KVM hypercalls; a hit here is the
        // kernel probing an unassigned port, so answer benignly.
        if (ctx->linux_guest)
        {
            if (!is_out)
            {
                memset(data, 0, (size_t)size);
            }
            return 0;
        }

        // Hypercalls are OUT-only now: results come back in RAX, so there is
        // no follow-up IN for the guest to get wrong.
        if (!is_out)
        {
            memset(data, 0, (size_t)size);
            return 0;
        }

        switch (hypercall_dispatch(ctx->vcpu_fd, ctx->vcpu_id, ctx->name, verbose_enabled()))
        {
        case HC_EXIT_GUEST:
            ctx->running = false;
            return 0;
        case HC_FAILED:
            return -1;
        case HC_CONTINUE:
            return 0;
        }
        return 0;
    }

    if (devices_is_uart_port(port))
    {
        // A multi-byte access covers consecutive UART registers.
        for (int i = 0; i < size; i++)
        {
            if (is_out)
            {
                devices_uart_write(port + i, data[i]);
            }
            else
            {
                data[i] = devices_uart_read(port + i);
            }
        }
        return 0;
    }

    if (is_out)
    {
        devices_misc_out(port, data, size);
    }
    else
    {
        devices_misc_in(port, data, size);
    }

    return 0;
}
/*
 * Handle VM exit for a specific vCPU context
 */
static int handle_vm_exit(vcpu_context_t *ctx)
{
    ctx->exit_count++;

    // --dump-regs: full CPU state at every exit.
    if (dump_regs_on_exit)
    {
        print_vm_exit_details(ctx->kvm_run, ctx->vcpu_id);
        dump_all_registers(ctx->vcpu_fd, ctx->vcpu_id);
    }

    // If we temporarily disabled single-step (e.g., to let REP instructions complete),
    // re-enable it on the next non-debug exit while the budget remains.
    if (ctx->singlestep.paused && ctx->kvm_run->exit_reason != KVM_EXIT_DEBUG)
    {
        ctx->singlestep.paused = false;
        if (ctx->singlestep.remaining > 0)
        {
            linux_set_singlestep(ctx, true);
        }
    }

    // Log exit reasons if verbose_enabled() mode is enabled
    if (verbose_enabled())
    {
        static int debug_exit_count = 0;
        if (debug_exit_count++ < 110)
        {
            if (ctx->kvm_run->exit_reason != KVM_EXIT_IO || debug_exit_count > 100)
            {
                vcpu_printf(ctx, "EXIT[%d]: reason=%d\n", debug_exit_count, ctx->kvm_run->exit_reason);
            }
        }
    }

    switch (ctx->kvm_run->exit_reason)
    {
    case KVM_EXIT_HLT:
        if (verbose_enabled())
        {
            vcpu_printf(ctx, "Guest halted after %d exits\n", ctx->exit_count);
        }
        ctx->running = false;
        return 0;

    case KVM_EXIT_IO:
        return handle_io(ctx);

    case KVM_EXIT_DEBUG:
        return linux_handle_debug_exit(ctx);

    case KVM_EXIT_FAIL_ENTRY:
        vcpu_printf(ctx, "FAIL_ENTRY: reason 0x%llx\n",
                    ctx->kvm_run->fail_entry.hardware_entry_failure_reason);
        return -1;

    case KVM_EXIT_MMIO:
        if (verbose_enabled())
        {
            static int mmio_log_count = 0;
            if (mmio_log_count++ < 50)
            {
                vcpu_printf(ctx, "MMIO: addr=0x%llx is_write=%d len=%d\n",
                            ctx->kvm_run->mmio.phys_addr,
                            ctx->kvm_run->mmio.is_write,
                            ctx->kvm_run->mmio.len);
            }
        }
        if (!ctx->kvm_run->mmio.is_write)
        {
            // Return zeroed data
            memset(ctx->kvm_run->mmio.data, 0, ctx->kvm_run->mmio.len);
        }
        return 0;

    case KVM_EXIT_IRQ_WINDOW_OPEN:
        // Interrupt window opened; just continue
        return 0;

    case KVM_EXIT_INTR:
        // External interrupt handled by KVM
        return 0;

    case KVM_EXIT_INTERNAL_ERROR:
        vcpu_printf(ctx, "INTERNAL_ERROR: suberror 0x%x ndata=%d\n",
                    ctx->kvm_run->internal.suberror, ctx->kvm_run->internal.ndata);
        for (uint32_t i = 0; i < ctx->kvm_run->internal.ndata && i < 8; i++)
        {
            vcpu_printf(ctx, "  data[%u]=0x%llx\n", i,
                        (unsigned long long)ctx->kvm_run->internal.data[i]);
        }
        return -1;

    case KVM_EXIT_SHUTDOWN:
    {
        struct kvm_regs regs;
        struct kvm_sregs sregs;
        struct kvm_vcpu_events events;

        vcpu_printf(ctx, "SHUTDOWN - Attempting to get exception info...\n");
        linux_report_shutdown(ctx);
        if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) == 0)
        {
            vcpu_printf(ctx, "SHUTDOWN at RIP=0x%llx, RSP=0x%llx\n", regs.rip, regs.rsp);
            vcpu_printf(ctx, "  RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n",
                        regs.rax, regs.rbx, regs.rcx, regs.rdx);
        }
        if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) == 0)
        {
            vcpu_printf(ctx, "  CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                        sregs.cr0, sregs.cr3, sregs.cr4);
            vcpu_printf(ctx, "  CS=0x%x DS=0x%x SS=0x%x\n",
                        sregs.cs.selector, sregs.ds.selector, sregs.ss.selector);
        }
        // Try to get exception info
        if (ioctl(ctx->vcpu_fd, KVM_GET_VCPU_EVENTS, &events) == 0)
        {
            vcpu_printf(ctx, "  Exception: injected=%d nr=%d has_error=%d error=0x%x\n",
                        events.exception.injected, events.exception.nr,
                        events.exception.has_error_code, events.exception.error_code);
            vcpu_printf(ctx, "  Interrupt: injected=%d nr=%d soft=%d\n",
                        events.interrupt.injected, events.interrupt.nr, events.interrupt.soft);
            vcpu_printf(ctx, "  NMI: injected=%d pending=%d masked=%d\n",
                        events.nmi.injected, events.nmi.pending, events.nmi.masked);
        }
        ctx->running = false;
        return 0;
    }

    default:
        vcpu_printf(ctx, "Unknown exit reason: %d\n", ctx->kvm_run->exit_reason);
        return -1;
    }

    return 0;
}
/*
 * vCPU thread entry point
 */
void *vcpu_thread(void *arg)
{
    vcpu_context_t *ctx = (vcpu_context_t *)arg;
    int ret;

    if (verbose_enabled())
    {
        vcpu_printf(ctx, "Thread started\n");
    }

    // Debug: check vCPU state before first run
    if (verbose_enabled() && ctx->use_paging)
    {
        struct kvm_sregs sregs;
        struct kvm_regs regs;
        if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) == 0 &&
            ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) == 0)
        {
            vcpu_printf(ctx, "Pre-run state: RIP=0x%llx CR0=0x%llx CR3=0x%llx CS=0x%x\n",
                        regs.rip, sregs.cr0, sregs.cr3, sregs.cs.selector);

            // Debug: dump page directory and entry point memory
            uint32_t *page_dir = (uint32_t *)(ctx->guest_mem + 0x100000);
            vcpu_printf(ctx, "Page Dir @ 0x100000: PDE[0]=0x%08x PDE[512]=0x%08x\n",
                        page_dir[0], page_dir[512]);

            // Verify entry point memory
            uint8_t *entry = (uint8_t *)(ctx->guest_mem + 0x1000);
            vcpu_printf(ctx, "Entry @ 0x1000: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        entry[0], entry[1], entry[2], entry[3],
                        entry[4], entry[5], entry[6], entry[7]);

            // Verify GDT
            uint8_t *gdt = (uint8_t *)(ctx->guest_mem + 0x500);
            vcpu_printf(ctx, "GDT @ 0x500: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        gdt[0], gdt[1], gdt[2], gdt[3],
                        gdt[4], gdt[5], gdt[6], gdt[7]);
        }
    }

    while (ctx->running)
    {
        ret = ioctl(ctx->vcpu_fd, KVM_RUN, 0);
        if (ret < 0)
        {
            vcpu_printf(ctx, "KVM_RUN failed: %s\n", strerror(errno));
            break;
        }

        if (handle_vm_exit(ctx) < 0)
        {
            break;
        }
    }

    if (verbose_enabled())
    {
        vcpu_printf(ctx, "Thread exiting (total exits: %d)\n", ctx->exit_count);
    }
    return NULL;
}
/*
 * Cleanup vCPU resources
 */
void vcpu_cleanup(vcpu_context_t *ctx)
{
    // --dump-mem: snapshot this guest's memory before we unmap it. With
    // several vCPUs each gets its own file, since each has its own memory.
    if (dump_mem_path != NULL && ctx->guest_mem != NULL && ctx->guest_mem != MAP_FAILED)
    {
        char path[512];
        if (total_vcpus > 1)
        {
            snprintf(path, sizeof(path), "%s.vcpu%d", dump_mem_path, ctx->vcpu_id);
        }
        else
        {
            snprintf(path, sizeof(path), "%s", dump_mem_path);
        }
        dump_memory_to_file(ctx->guest_mem, ctx->mem_size, path);
    }

    if (ctx->kvm_run != NULL && ctx->kvm_run != MAP_FAILED)
    {
        munmap(ctx->kvm_run, ctx->kvm_run_mmap_size);
    }
    if (ctx->guest_mem != NULL && ctx->guest_mem != MAP_FAILED)
    {
        munmap(ctx->guest_mem, ctx->mem_size);
    }
    if (ctx->vcpu_fd >= 0)
    {
        close(ctx->vcpu_fd);
    }
}
/*
 * Extract guest name from binary filename
 */
const char *vcpu_extract_name(const char *filename)
{
    const char *name = strrchr(filename, '/');
    if (name)
    {
        name++; // Skip '/'
    }
    else
    {
        name = filename;
    }

    // Remove .bin extension if present
    static char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s", name);

    char *dot = strrchr(name_buf, '.');
    if (dot && strcmp(dot, ".bin") == 0)
    {
        *dot = '\0';
    }

    return name_buf;
}
