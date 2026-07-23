/*
 * linux_entry.c - experimental Linux boot support
 *
 * QUARANTINED. This path implements enough of the Linux x86 boot protocol to
 * load a bzImage and jump to it, but it does not boot to a shell. It lives
 * here so that the core VMM (real mode, protected mode, long mode) stays free
 * of Linux-specific branches. No core source file outside this one should
 * grow a `linux_guest` special case.
 *
 * The single-step machinery also lives here: it exists solely to produce an
 * instruction trace for this bring-up work.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "linux_entry.h"
#include "../cpu_modes.h"
#include "../console.h"
#include "../debug.h"
#include "../cpuid.h"
#include "../msr.h"
#include "../paging_64.h"
#include "../protected_mode.h"
#include "../long_mode.h"
#include "../linux_boot.h"
#include "../vm.h"
#include "../cli.h"

/* Selectors the Linux 32-bit boot protocol expects. */
#define LINUX_BOOT_CS 0x10
#define LINUX_BOOT_DS 0x18

/* Resolved from the command line by linux_entry_set_verbose(). */
static bool verbose = false;
static int kvm_fd = -1;

void linux_entry_configure(int kvm, bool verbose_on)
{
    kvm_fd = kvm;
    verbose = verbose_on;
}

int linux_set_singlestep(vcpu_context_t *ctx, bool enable)
{
    struct kvm_guest_debug dbg;
    memset(&dbg, 0, sizeof(dbg));
    if (enable)
    {
        dbg.control = KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_SINGLESTEP;
    }
    if (ioctl(ctx->vcpu_fd, KVM_SET_GUEST_DEBUG, &dbg) < 0)
    {
        perror("KVM_SET_GUEST_DEBUG");
        return -1;
    }
    return 0;
}

/*
 * Setup a minimal GDT for Linux 32-bit boot protocol.
 *
 * Linux expects __BOOT_CS=0x10 and __BOOT_DS=0x18 selectors.
 */
static int setup_linux_boot_gdt(void *guest_mem_ptr)
{
    gdt_entry_t *gdt = (gdt_entry_t *)(guest_mem_ptr + GDT_ADDR);

    memset(gdt, 0, GDT_TOTAL_SIZE);

    // Entry 0: Null descriptor (required)
    gdt_set_entry(&gdt[0], 0, 0, 0, 0);

    // Entry 1: Unused (keep null)
    gdt_set_entry(&gdt[1], 0, 0, 0, 0);

    // Entry 2: __BOOT_CS (0x10) - 32-bit code, base=0, limit=4GB
    gdt_set_entry(&gdt[2], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);

    // Entry 3: __BOOT_DS (0x18) - 32-bit data, base=0, limit=4GB
    gdt_set_entry(&gdt[3], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);

    // Entry 4: Unused (keep null)
    gdt_set_entry(&gdt[4], 0, 0, 0, 0);

    if (verbose) printf("Linux boot GDT setup: __BOOT_CS=0x%x __BOOT_DS=0x%x\n",
                        LINUX_BOOT_CS, LINUX_BOOT_DS);
    return 0;
}

static void setup_linux_boot_gdt_64bit(void *guest_mem, uint64_t gdt_base)
{
    gdt_entry_64_t *gdt = (gdt_entry_64_t *)((char *)guest_mem + gdt_base);

    memset(gdt, 0, 5 * sizeof(gdt_entry_64_t));

    // Entry 2: __BOOT_CS (0x10) - 64-bit code segment
    gdt[2].access = GDT_PRESENT | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_RW;
    gdt[2].granularity = GDT_LONG_MODE;

    // Entry 3: __BOOT_DS (0x18) - data segment
    gdt[3].access = GDT_PRESENT | GDT_CODE_DATA | GDT_RW;
    gdt[3].granularity = 0;
}

void linux_setup_boot_segments(struct kvm_sregs *sregs)
{
    // __BOOT_CS (0x10): flat 32-bit code segment
    sregs->cs.base = 0;
    sregs->cs.limit = 0xFFFFFFFF;
    sregs->cs.selector = LINUX_BOOT_CS;
    sregs->cs.type = 0x0a; // Execute/Read
    sregs->cs.present = 1;
    sregs->cs.dpl = 0;
    sregs->cs.db = 1;
    sregs->cs.s = 1;
    sregs->cs.l = 0;
    sregs->cs.g = 1;
    sregs->cs.avl = 0;

    // __BOOT_DS (0x18): flat 32-bit data segment
    sregs->ds.base = 0;
    sregs->ds.limit = 0xFFFFFFFF;
    sregs->ds.selector = LINUX_BOOT_DS;
    sregs->ds.type = 0x02; // Read/Write
    sregs->ds.present = 1;
    sregs->ds.dpl = 0;
    sregs->ds.db = 1;
    sregs->ds.s = 1;
    sregs->ds.l = 0;
    sregs->ds.g = 1;
    sregs->ds.avl = 0;

    sregs->es = sregs->fs = sregs->gs = sregs->ss = sregs->ds;
}

void linux_setup_ivt(void *guest_mem)
{
    // Place a tiny IRET stub at 0x1000 and point all IVT vectors to it.
    uint8_t *mem = (uint8_t *)guest_mem;

    mem[0x1000] = 0xCF; // IRET

    // Success stub at 0x1100:
    // - clears CF in stacked flags
    // - sets AX=0
    // - iret
    static const uint8_t int_success_stub[] = {
        0x55,             // push bp
        0x89, 0xE5,       // mov bp, sp
        0x81, 0x66, 0x06, 0xFE, 0xFF, // and word [bp+6], 0xfffe
        0x31, 0xC0,       // xor ax, ax
        0x5D,             // pop bp
        0xCF,             // iret
    };
    memcpy(mem + 0x1100, int_success_stub, sizeof(int_success_stub));

    // Failure stub at 0x1200:
    // - sets CF in stacked flags
    // - sets AX=0
    // - iret
    static const uint8_t int_fail_stub[] = {
        0x55,             // push bp
        0x89, 0xE5,       // mov bp, sp
        0x81, 0x4E, 0x06, 0x01, 0x00, // or word [bp+6], 0x0001
        0x31, 0xC0,       // xor ax, ax
        0x5D,             // pop bp
        0xCF,             // iret
    };
    memcpy(mem + 0x1200, int_fail_stub, sizeof(int_fail_stub));

    for (int vec = 0; vec < 256; vec++)
    {
        uint16_t off = 0x1000;
        if (vec == 0x15 || vec == 0x10 || vec == 0x16 || vec == 0x1a)
        {
            off = 0x1100;
        }
        else if (vec == 0x13)
        {
            off = 0x1200;
        }
        uint16_t seg = 0x0000;
        size_t ivt = (size_t)vec * 4;
        mem[ivt + 0] = (uint8_t)(off & 0xFF);
        mem[ivt + 1] = (uint8_t)((off >> 8) & 0xFF);
        mem[ivt + 2] = (uint8_t)(seg & 0xFF);
        mem[ivt + 3] = (uint8_t)((seg >> 8) & 0xFF);
    }
}

static void setup_linux_prot_idt(void *guest_mem)
{
    // Place IDT right after our GDT and point all vectors to a tiny handler.
    uint8_t *mem = (uint8_t *)guest_mem;
    uint32_t idt_addr = GDT_ADDR + GDT_TOTAL_SIZE;
    idt_entry_t *idt = (idt_entry_t *)(mem + idt_addr);

    // Exception handler: print 'E' to COM1 then halt.
    const uint32_t handler_addr = 0x7000;
    static const uint8_t handler_code[] = {
        0x50,                         // push eax
        0x52,                         // push edx
        0xBA, 0xF8, 0x03, 0x00, 0x00, // mov edx, 0x3f8
        0xB0, 0x45,                   // mov al, 'E'
        0xEE,                         // out dx, al
        0x5A,                         // pop edx
        0x58,                         // pop eax
        0xF4,                         // hlt
        0xEB, 0xFE,                   // jmp $
    };
    memcpy(mem + handler_addr, handler_code, sizeof(handler_code));

    for (int vec = 0; vec < 256; vec++)
    {
        uint32_t off = handler_addr;
        idt[vec].offset_low = (uint16_t)(off & 0xFFFF);
        idt[vec].selector = LINUX_BOOT_CS;
        idt[vec].reserved = 0;
        idt[vec].flags = 0x8E; // present, ring0, 32-bit interrupt gate
        idt[vec].offset_high = (uint16_t)((off >> 16) & 0xFFFF);
    }
}

/*
 * Configure vCPU for Linux protected-mode entry (no paging).
 *
 * Linux boot protocol requires protected mode with paging disabled at code32_start,
 * with RSI/ESI pointing to the boot_params ("zero page").
 */
int linux_configure_code32_entry(vcpu_context_t *ctx, uint32_t boot_params_addr)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // Setup Linux boot protocol GDT/IDT (selectors __BOOT_CS/__BOOT_DS)
    setup_linux_boot_gdt(ctx->guest_mem);
    setup_linux_prot_idt(ctx->guest_mem);

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
    {
        perror("KVM_GET_SREGS (linux code32)");
        return -1;
    }

    sregs.gdt.base = GDT_ADDR;
    sregs.gdt.limit = GDT_TOTAL_SIZE - 1;
    sregs.idt.base = GDT_ADDR + GDT_TOTAL_SIZE;
    sregs.idt.limit = (256 * sizeof(idt_entry_t)) - 1;

    // Protected mode, paging OFF
    sregs.cr0 = 0x00000011; // PE + ET
    sregs.cr3 = 0x00000000;
    sregs.cr4 = 0x00000000;
    sregs.efer = 0x0000000000000000;

    linux_setup_boot_segments(&sregs);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS (linux code32)");
        return -1;
    }

    if (setup_cpuid(kvm_fd, ctx->vcpu_fd) < 0)
    {
        return -1;
    }

    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->entry_point;
    uint32_t rsi = boot_params_addr;
    if (ctx->linux_rsi == LINUX_RSI_HDR)
    {
        rsi += 0x1f1;
    }
    regs.rsi = rsi;
    // Linux boot protocol requires %ebp, %edi and %ebx to be zero.
    regs.rsp = 0x9ff00;
    regs.rflags = 0x2;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
    {
        perror("KVM_SET_REGS (linux code32)");
        return -1;
    }

    struct kvm_mp_state mp_state;
    mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
    if (ioctl(ctx->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0)
    {
        perror("KVM_SET_MP_STATE (linux code32)");
        return -1;
    }

    return 0;
}

int linux_configure_boot64_entry(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    uint64_t cr3 = setup_page_tables_64bit(ctx->guest_mem, ctx->mem_size);

    const uint64_t gdt_base = 0x5000;
    setup_linux_boot_gdt_64bit(ctx->guest_mem, gdt_base);

    if (setup_cpuid(kvm_fd, ctx->vcpu_fd) < 0)
    {
        return -1;
    }

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
    {
        perror("KVM_GET_SREGS (linux boot64)");
        return -1;
    }

    sregs.gdt.base = gdt_base;
    sregs.gdt.limit = 5 * sizeof(gdt_entry_64_t) - 1;
    sregs.idt.base = 0;
    sregs.idt.limit = 0;

    sregs.cr3 = cr3;
    sregs.cr4 = (1 << 5); // PAE
    sregs.cr0 = (1ULL << 0)   // PE
              | (1ULL << 4)   // ET
              | (1ULL << 5)   // NE
              | (1ULL << 31); // PG
    sregs.efer = EFER_LME | EFER_LMA;

    // __BOOT_CS/__BOOT_DS selectors
    sregs.cs.selector = LINUX_BOOT_CS;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0xb;
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 0;
    sregs.cs.s = 1;
    sregs.cs.l = 1;
    sregs.cs.g = 1;
    sregs.cs.avl = 0;

    sregs.ds.selector = LINUX_BOOT_DS;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.type = 0x3;
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 1;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 1;
    sregs.ds.avl = 0;

    sregs.es = sregs.ss = sregs.ds;
    sregs.fs = sregs.gs = sregs.ds;

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS (linux boot64)");
        return -1;
    }

    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->entry_point;
    regs.rsi = LINUX_BOOT_PARAMS_ADDR;
    regs.rsp = 0x9ff00;
    regs.rflags = 0x2;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
    {
        perror("KVM_SET_REGS (linux boot64)");
        return -1;
    }

    // Optional: set common MSRs for long mode
    if (setup_msrs_64bit(ctx->vcpu_fd) < 0)
    {
        // Non-fatal
    }

    struct kvm_mp_state mp_state;
    mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
    if (ioctl(ctx->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0)
    {
        perror("KVM_SET_MP_STATE (linux boot64)");
        return -1;
    }

    return 0;
}

/*
 * Record and log one single-stepped instruction. Only active while a step
 * budget remains; otherwise the exit is simply consumed.
 */
int linux_handle_debug_exit(vcpu_context_t *ctx)
{
        if (ctx->singlestep.remaining > 0)
        {
            ctx->singlestep.exits++;
            struct kvm_regs regs;
            struct kvm_sregs sregs;
            if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) == 0 &&
                ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) == 0)
            {
                uint64_t linear = sregs.cs.base + regs.rip;
                uint8_t *mem = (uint8_t *)ctx->guest_mem;

                uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
                if (linear + 3 < ctx->mem_size)
                {
                    b0 = mem[linear + 0];
                    b1 = mem[linear + 1];
                    b2 = mem[linear + 2];
                    b3 = mem[linear + 3];
                }

                ctx->singlestep.rip = regs.rip;
                ctx->singlestep.rsi = regs.rsi;
                ctx->singlestep.rbx = regs.rbx;
                ctx->singlestep.rdi = regs.rdi;
                ctx->singlestep.rcx = regs.rcx;
                ctx->singlestep.rsp = regs.rsp;
                ctx->singlestep.rflags = regs.rflags;
                ctx->singlestep.cr0 = sregs.cr0;
                ctx->singlestep.cs = sregs.cs.selector;
                ctx->singlestep.es = sregs.es.selector;
                ctx->singlestep.es_base = sregs.es.base;
                ctx->singlestep.es_limit = sregs.es.limit;
                ctx->singlestep.idt_base = sregs.idt.base;
                ctx->singlestep.idt_limit = sregs.idt.limit;
                ctx->singlestep.bytes[0] = b0;
                ctx->singlestep.bytes[1] = b1;
                ctx->singlestep.bytes[2] = b2;
                ctx->singlestep.bytes[3] = b3;

                bool should_log = (ctx->singlestep.exits <= 50) || ((ctx->singlestep.exits % 50) == 0);
                if (should_log)
                {
                    console_vcpu_printf(ctx->vcpu_id, ctx->name, "STEP: RIP=0x%llx CS=0x%x linear=0x%llx CR0=0x%llx RSI=0x%llx RBX=0x%llx RDI=0x%llx bytes=%02x %02x %02x %02x\n",
                                (unsigned long long)regs.rip,
                                sregs.cs.selector,
                                (unsigned long long)linear,
                                (unsigned long long)sregs.cr0,
                                (unsigned long long)regs.rsi,
                                (unsigned long long)regs.rbx,
                                (unsigned long long)regs.rdi,
                                b0, b1, b2, b3);
                }

                // REP string ops can generate enormous amounts of single-step exits.
                // Let them run at full speed and resume single-step on the next exit.
                if (!ctx->singlestep.paused && (b0 == 0xF3 || b0 == 0xF2))
                {
                    ctx->singlestep.paused = true;
                    linux_set_singlestep(ctx, false);
                    console_vcpu_printf(ctx->vcpu_id, ctx->name, "STEP: pausing single-step for REP instruction\n");
                    return 0;
                }
            }
            ctx->singlestep.remaining--;
            if (ctx->singlestep.remaining == 0)
            {
                linux_set_singlestep(ctx, false);
                console_vcpu_printf(ctx->vcpu_id, ctx->name, "STEP: disabled single-step\n");
            }
            return 0;
        }
        return 0;
}

/*
 * After a triple fault, dump the last stepped instruction and the exception
 * vectors the guest had installed. This is the whole reason the trace exists.
 */
void linux_report_shutdown(vcpu_context_t *ctx)
{
    if (ctx->singlestep.exits > 0)
    {
        console_vcpu_printf(ctx->vcpu_id, ctx->name, "  Last step: RIP=0x%llx CS=0x%x ES=0x%x ES.base=0x%llx ES.limit=0x%x IDT.base=0x%llx IDT.limit=0x%x CR0=0x%llx RFLAGS=0x%llx RSI=0x%llx RBX=0x%llx RCX=0x%llx RDI=0x%llx RSP=0x%llx bytes=%02x %02x %02x %02x\n",
                    (unsigned long long)ctx->singlestep.rip,
                    (unsigned)ctx->singlestep.cs,
                    (unsigned)ctx->singlestep.es,
                    (unsigned long long)ctx->singlestep.es_base,
                    (unsigned)ctx->singlestep.es_limit,
                    (unsigned long long)ctx->singlestep.idt_base,
                    (unsigned)ctx->singlestep.idt_limit,
                    (unsigned long long)ctx->singlestep.cr0,
                    (unsigned long long)ctx->singlestep.rflags,
                    (unsigned long long)ctx->singlestep.rsi,
                    (unsigned long long)ctx->singlestep.rbx,
                    (unsigned long long)ctx->singlestep.rcx,
                    (unsigned long long)ctx->singlestep.rdi,
                    (unsigned long long)ctx->singlestep.rsp,
                    ctx->singlestep.bytes[0], ctx->singlestep.bytes[1], ctx->singlestep.bytes[2], ctx->singlestep.bytes[3]);

        // If the guest installed an IDT in RAM, dump a few key exception vectors.
        const uint8_t vectors[] = {0, 6, 8, 13, 14};
        uint8_t *mem = (uint8_t *)ctx->guest_mem;
        for (size_t vi = 0; vi < sizeof(vectors); vi++)
        {
            uint8_t vec = vectors[vi];
            uint64_t entry_addr = ctx->singlestep.idt_base + (uint64_t)vec * sizeof(idt_entry_t);
            if (entry_addr + sizeof(idt_entry_t) <= ctx->mem_size)
            {
                idt_entry_t *e = (idt_entry_t *)(mem + entry_addr);
                uint32_t off = (uint32_t)e->offset_low | ((uint32_t)e->offset_high << 16);
                console_vcpu_printf(ctx->vcpu_id, ctx->name, "  IDT[%u]: sel=0x%x off=0x%x flags=0x%x\n",
                            (unsigned)vec, (unsigned)e->selector, off, (unsigned)e->flags);
            }
        }
    }

}

/*
 * Build the single vCPU that boots a bzImage: allocate memory, lay down the
 * IVT and boot parameters, load the kernel and optional initrd, then enter at
 * the requested entry point.
 */
int linux_prepare_vcpu(vcpu_context_t *ctx, const vmm_config_t *cfg)
{
    // Hand the quarantined module the state it needs; it deliberately
    // does not reach back into the core VMM's globals.
    linux_entry_configure(vm_kvm_fd(), verbose_enabled());

    printf("\n=== Linux Boot Protocol Setup ===\n");

    // The caller owns the context; we fill it in for a Linux guest.
    memset(ctx, 0, sizeof(*ctx));
    ctx->vcpu_id = 0;
    ctx->guest_binary = cfg->bzimage_path;
    snprintf(ctx->name, sizeof(ctx->name), "Linux");
    ctx->vcpu_fd = -1;
    ctx->use_paging = false;  // Enter protected mode (no paging) at code32_start
    ctx->long_mode = false;
    ctx->entry_point = 0;     // Will be set to code32_start after load
    ctx->load_offset = 0;
    ctx->linux_guest = true;
    ctx->linux_entry = cfg->linux_entry;
    ctx->linux_rsi = cfg->linux_rsi;

    // Allocate guest memory
    if (vm_map_vcpu_memory(ctx) < 0)
    {
        return -1;
    }

    struct boot_params *boot_params = (struct boot_params *)(ctx->guest_mem + LINUX_BOOT_PARAMS_ADDR);
    memset(boot_params, 0, sizeof(*boot_params));

    // Setup a minimal IVT so bzImage setup code can execute basic interrupts safely
    linux_setup_ivt(ctx->guest_mem);

    // Load Linux kernel bzImage
    printf("Loading bzImage...\n");
    if (load_linux_kernel(ctx->guest_binary, ctx->guest_mem, ctx->mem_size, boot_params) < 0)
    {
        fprintf(stderr, "Error: Failed to load Linux kernel\n");
        return -1;
    }

    // Setup boot parameters (E820 memory map, etc.)
    printf("Setting up boot parameters...\n");
    setup_linux_boot_params(boot_params, ctx->mem_size, cfg->linux_cmdline);

    // Load initrd if provided
    if (cfg->initrd_path)
    {
        printf("Loading initrd...\n");
        if (load_initrd(cfg->initrd_path, ctx->guest_mem, ctx->mem_size, boot_params) < 0)
        {
            fprintf(stderr, "Error: Failed to load initrd\n");
            return -1;
        }
    }

    // Detect 64-bit kernel
    if (boot_params->hdr.xloadflags & XLF_KERNEL_64)
    {
        printf("Detected 64-bit Linux kernel\n");
        ctx->long_mode = true;
    }
    else
    {
        printf("Detected 32-bit Linux kernel\n");
    }

    if (cfg->linux_entry == LINUX_ENTRY_BOOT64)
    {
        if (!(boot_params->hdr.xloadflags & XLF_KERNEL_64))
        {
            fprintf(stderr, "Error: --linux-entry boot64 requires a 64-bit kernel (XLF_KERNEL_64)\n");
            return -1;
        }
        ctx->entry_point = KERNEL_LOAD_ADDR + 0x200;
        printf("64-bit entry (boot64): 0x%x\n", ctx->entry_point);
    }
    else
    {
        ctx->entry_point = boot_params->hdr.code32_start;
        printf("Protected-mode entry (code32_start): 0x%x\n", ctx->entry_point);
    }
    printf("boot_params (zero page): 0x%x\n", LINUX_BOOT_PARAMS_ADDR);
    printf("linux RSI mode: %s\n", (cfg->linux_rsi == LINUX_RSI_BASE) ? "base" : "hdr");
    printf("Real-mode setup: 0x%x:0x0200\n", (unsigned)(REAL_MODE_KERNEL_ADDR / 16));

    // Copy command line to guest memory if provided
    if (cfg->linux_cmdline)
    {
        size_t cmdline_len = strlen(cfg->linux_cmdline) + 1;
        if (cmdline_len > 256)
        {
            fprintf(stderr, "Warning: Command line truncated to 255 characters\n");
            cmdline_len = 256;
        }
        memcpy(ctx->guest_mem + COMMAND_LINE_ADDR, cfg->linux_cmdline, cmdline_len);
        printf("Command line copied to 0x%x\n", COMMAND_LINE_ADDR);
    }

    // Create and initialize vCPU
    printf("Initializing vCPU for Linux kernel...\n");
    if (vcpu_setup(ctx) < 0)
    {
        return -1;
    }

    // Optional: enable KVM single-step for early Linux bring-up debugging
    if (debug_level == DEBUG_ALL)
    {
        ctx->singlestep.remaining = 2000;
        ctx->singlestep.paused = false;
        ctx->singlestep.exits = 0;
        if (linux_set_singlestep(ctx, true) < 0)
        {
            return -1;
        }
    }

    printf("Linux boot setup complete!\n\n");
    
    return 0;
}
