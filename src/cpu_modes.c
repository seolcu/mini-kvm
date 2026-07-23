/*
 * cpu_modes.c - guest CPU mode setup (real, protected, long)
 */

#include <stdio.h>
#include <string.h>

#include "cpu_modes.h"
#include "long_mode.h"
#include "console.h"
#include "vm.h"
#include "vcpu.h"
#include "debug.h"
#include "cpuid.h"
#include "msr.h"
#include "paging_64.h"
#include <sys/ioctl.h>
#include <linux/kvm.h>

/* --- Descriptor helpers ------------------------------------------------ */

void gdt_set_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t flags)
{
    entry->base_low = base & 0xFFFF;
    entry->base_mid = (base >> 16) & 0xFF;
    entry->base_high = (base >> 24) & 0xFF;
    entry->limit_low = limit & 0xFFFF;
    entry->access = access;
    entry->limit_granular = ((limit >> 16) & 0x0F) | (flags & 0xF0);
}

/*
 * Fill one kvm_segment. Grouping the eleven fields here is what keeps the
 * mode setup readable -- open-coding them per segment is how the original
 * grew six near-identical 12-line blocks.
 */
static void set_segment(struct kvm_segment *seg, uint64_t base, uint32_t limit,
                        uint16_t selector, uint8_t type, uint8_t db, uint8_t g)
{
    seg->base = base;
    seg->limit = limit;
    seg->selector = selector;
    seg->type = type;
    seg->present = 1;
    seg->dpl = 0;
    seg->db = db;
    seg->s = 1;         /* code/data, not a system descriptor */
    seg->l = 0;
    seg->g = g;
    seg->avl = 0;
}

/* --- Real mode --------------------------------------------------------- */

void segments_set_real(struct kvm_sregs *sregs, uint32_t base)
{
    uint16_t selector = (uint16_t)(base / 16);

    /* 0x9b / 0x93 are the real-mode cached descriptor types KVM expects for
     * code and data respectively. 64KB limits, byte granularity. */
    set_segment(&sregs->cs, base, 0xFFFF, selector, 0x9b, 0, 0);
    set_segment(&sregs->ds, base, 0xFFFF, selector, 0x93, 0, 0);

    sregs->es = sregs->fs = sregs->gs = sregs->ss = sregs->ds;
}

/* --- Protected mode ---------------------------------------------------- */

void segments_set_flat32(struct kvm_sregs *sregs)
{
    /* Types 0x0a/0x02 are the descriptor-cache forms of the GDT's 0x9a/0x92
     * access bytes. db=1 for 32-bit operands, g=1 for 4KB granularity. */
    set_segment(&sregs->cs, 0, 0xFFFFFFFF, SEL_KCODE, 0x0a, 1, 1);
    set_segment(&sregs->ds, 0, 0xFFFFFFFF, SEL_KDATA, 0x02, 1, 1);

    sregs->es = sregs->fs = sregs->gs = sregs->ss = sregs->ds;
}

void gdt_setup(void *guest_mem, bool verbose)
{
    gdt_entry_t *gdt = (gdt_entry_t *)((char *)guest_mem + GDT_ADDR);

    gdt_set_entry(&gdt[0], 0, 0, 0, 0);                              /* null */
    gdt_set_entry(&gdt[1], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);   /* ring 0 code */
    gdt_set_entry(&gdt[2], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);   /* ring 0 data */
    gdt_set_entry(&gdt[3], 0, 0xFFFFF, 0xFA, LIMIT_GRAN);            /* ring 3 code */
    gdt_set_entry(&gdt[4], 0, 0xFFFFF, 0xF2, LIMIT_GRAN);            /* ring 3 data */

    if (verbose) {
        printf("GDT setup: %d entries at 0x%x\n", GDT_SIZE, GDT_ADDR);
    }
}

uint32_t idt_setup(void *guest_mem, bool verbose)
{
    uint32_t idt_addr = GDT_ADDR + GDT_TOTAL_SIZE;
    idt_entry_t *idt = (idt_entry_t *)((char *)guest_mem + idt_addr);

    /* All-zero entries are not-present gates. The 1K OS installs the handlers
     * it actually wants (see setup_idt_entry in os-1k/kernel.c). */
    memset(idt, 0, 256 * sizeof(idt_entry_t));

    if (verbose) {
        printf("IDT setup at 0x%x\n", idt_addr);
    }
    return idt_addr;
}

uint32_t page_tables_setup32(void *guest_mem, size_t mem_size, int vcpu_id,
                             const char *vcpu_name, bool verbose)
{
    const uint32_t page_dir_offset      = 0x00100000;   /* 1MB */
    const uint32_t page_table_0_offset  = 0x00101000;
    const uint32_t page_table_512_offset = 0x00102000;

    if (page_table_512_offset + 4096 >= mem_size) {
        console_vcpu_printf(vcpu_id, vcpu_name,
                            "Error: page tables (need 0x%x) exceed %zu bytes of guest memory\n",
                            page_table_512_offset + 4096, mem_size);
        return 0;
    }

    uint32_t *page_dir       = (uint32_t *)((char *)guest_mem + page_dir_offset);
    uint32_t *page_table_0   = (uint32_t *)((char *)guest_mem + page_table_0_offset);
    uint32_t *page_table_512 = (uint32_t *)((char *)guest_mem + page_table_512_offset);

    memset(page_dir, 0, 4096);
    memset(page_table_0, 0, 4096);
    memset(page_table_512, 0, 4096);

    /* 0x03 = Present | Read/Write. The PSE bit is deliberately absent. */
    page_dir[0]   = page_table_0_offset   | 0x03;   /* 0x00000000-0x003FFFFF */
    page_dir[512] = page_table_512_offset | 0x03;   /* 0x80000000-0x803FFFFF */

    /* Both directory entries map the same low 4MB: one identity, one
     * high-half. 1024 4KB pages each. */
    for (int i = 0; i < 1024; i++) {
        uint32_t pte = (uint32_t)(i << 12) | 0x03;
        page_table_0[i] = pte;
        page_table_512[i] = pte;
    }

    if (verbose) {
        console_vcpu_printf(vcpu_id, vcpu_name,
                            "Page directory at GPA 0x%x (4KB pages, PSE off)\n", page_dir_offset);
        console_vcpu_printf(vcpu_id, vcpu_name,
                            "  identity 0x0-0x3FFFFF, kernel 0x80000000-0x803FFFFF\n");
    }

    return page_dir_offset;     /* value for CR3 */
}

/* --- Long mode --------------------------------------------------------- */

void gdt_setup_long(void *guest_mem, uint64_t gdt_base)
{
    gdt_entry_64_t *gdt = (gdt_entry_64_t *)((char *)guest_mem + gdt_base);

    memset(gdt, 0, 5 * sizeof(gdt_entry_64_t));

    /* In 64-bit mode base and limit are ignored for code/data; only the
     * access byte and the L bit matter. */
    gdt[GDT_KERNEL_CODE_64].access =
        GDT_PRESENT | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_RW;
    gdt[GDT_KERNEL_CODE_64].granularity = GDT_LONG_MODE;

    gdt[GDT_KERNEL_DATA_64].access = GDT_PRESENT | GDT_CODE_DATA | GDT_RW;
    gdt[GDT_KERNEL_DATA_64].granularity = 0;
}


/* --- Applying a mode to a live vCPU ------------------------------------ */

/*
 * Setup vCPU for 64-bit Long Mode
 */
int cpu_mode_enter_long(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    
    DEBUG_PRINT(DEBUG_BASIC, "[vCPU %d] Setting up 64-bit Long Mode", ctx->vcpu_id);
    
    // Setup 64-bit page tables (PML4 at 0x2000, PDPT at 0x3000, PD at 0x4000)
    uint64_t cr3 = setup_page_tables_64bit(ctx->guest_mem, ctx->mem_size);
    
    // Setup 64-bit GDT (place at 0x5000 to avoid page table conflict)
    uint64_t gdt_base = 0x5000; // Place GDT at 20KB
    gdt_setup_long(ctx->guest_mem, gdt_base);
    
    // Setup CPUID
    if (setup_cpuid(vm_kvm_fd(), ctx->vcpu_fd) < 0) {
        fprintf(stderr, "[vCPU %d] Failed to setup CPUID\n", ctx->vcpu_id);
        return -1;
    }
    
    // Get current sregs
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS");
        return -1;
    }
    
    // Setup GDT descriptor
    sregs.gdt.base = gdt_base;
    sregs.gdt.limit = 5 * sizeof(gdt_entry_64_t) - 1;
    
    // Setup IDT (empty for now, place after GDT)
    sregs.idt.base = 0x6000;
    sregs.idt.limit = 0;
    
    // Setup control registers for Long Mode
    // Order matters: CR4.PAE → CR3 → EFER.LME+LMA → CR0.PG
    sregs.cr3 = cr3;
    sregs.cr4 = (1 << 5); // CR4.PAE = 1 (Physical Address Extension, required for Long Mode)
    sregs.cr0 = (1ULL << 0)   // PE: Protected mode enable
              | (1ULL << 4)   // ET: Extension type
              | (1ULL << 5)   // NE: Native FPU error reporting
              | (1ULL << 31); // PG: Paging enable
    // KVM requires BOTH LME and LMA to be set explicitly (unlike real hardware)
    sregs.efer = EFER_LME | EFER_LMA;
    
    // Setup code segment for Long Mode
    // Critical: CS.L=1 (64-bit), CS.DB=0 (not 32-bit), CS.G=1 (granular)
    sregs.cs.selector = SELECTOR_KERNEL_CODE_64;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0xb; // 1011 = Execute/Read/Accessed
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 0;  // Must be 0 for Long Mode
    sregs.cs.s = 1;
    sregs.cs.l = 1;   // Long mode
    sregs.cs.g = 1;
    sregs.cs.avl = 0;
    
    // Setup data segments
    sregs.ds.selector = SELECTOR_KERNEL_DATA_64;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.type = 0x3; // Read/Write/Accessed
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 1;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 1;
    sregs.ds.avl = 0;
    
    sregs.es = sregs.ss = sregs.ds;
    sregs.fs = sregs.gs = sregs.ds;
    
    DEBUG_PRINT(DEBUG_DETAILED, "Setting CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx",
               (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
               (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer);
    
    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS (long mode)");
        DEBUG_PRINT(DEBUG_BASIC, "Failed to set special registers");
        DEBUG_PRINT(DEBUG_DETAILED, "CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx",
                   (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
                   (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer);
        return -1;
    }
    
    // Setup general purpose registers
    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->load_offset; // Entry point
    regs.rflags = 0x2; // Bit 1 is always 1
    regs.rsp = 0x8000; // Set up stack
    
    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS (long mode)");
        return -1;
    }
    
    // Setup MSRs (SYSCALL/SYSRET, FS/GS base)
    // Note: EFER is already set via sregs, MSRs are for other features
    if (setup_msrs_64bit(ctx->vcpu_fd) < 0) {
        fprintf(stderr, "[vCPU %d] Warning: Failed to setup MSRs (non-critical)\n", ctx->vcpu_id);
        // Don't fail - MSRs are not critical for basic Long Mode
    }
    
    DEBUG_PRINT(DEBUG_BASIC, "[vCPU %d] 64-bit Long Mode initialized", ctx->vcpu_id);
    DEBUG_PRINT(DEBUG_DETAILED, "  CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx",
               (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
               (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer);
    DEBUG_PRINT(DEBUG_DETAILED, "  RIP=0x%llx RSP=0x%llx",
               (unsigned long long)regs.rip, (unsigned long long)regs.rsp);
    
    if (debug_level >= DEBUG_DETAILED) {
        verify_page_tables_64bit(ctx->guest_mem, regs.rip);
    }
    
    return 0;
}

/*
 * Configure vCPU for Protected Mode with paging
 */
int cpu_mode_enter_protected(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // Setup GDT and IDT in guest memory
    gdt_setup(ctx->guest_mem, verbose_enabled());
    idt_setup(ctx->guest_mem, verbose_enabled());

    // Setup page tables
    uint32_t page_dir_offset = page_tables_setup32(ctx->guest_mem, ctx->mem_size,
                                                   ctx->vcpu_id, ctx->name, verbose_enabled());
    if (page_dir_offset == 0)
    {
        return -1;
    }

    // Get current segment registers
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
    {
        perror("KVM_GET_SREGS (paging)");
        return -1;
    }

    // Set GDTR and IDTR
    sregs.gdt.base = GDT_ADDR;
    sregs.gdt.limit = GDT_TOTAL_SIZE - 1;
    sregs.idt.base = GDT_ADDR + GDT_TOTAL_SIZE;
    sregs.idt.limit = (256 * sizeof(idt_entry_t)) - 1;

    // Set CR3 to page directory
    sregs.cr3 = page_dir_offset;

    // Set CR0: PE (Protected Mode) + PG (Paging) + ET (Extension Type)
    // Clear CD (Cache Disable) and NW (Not Write-through) for proper caching
    // Note: KVM initial CR0 may have CD=1, NW=1 which can cause issues with paging
    sregs.cr0 = 0x80000011; // PG + ET + PE

    // Set CR4: Clear PSE for 4KB paging (Zen 5 compatibility fix)
    sregs.cr4 = 0x00000000; // No PSE, no PAE - standard 4KB pages

    // Setup flat segments
    segments_set_flat32(&sregs);

    if (verbose_enabled())
    {
        vcpu_printf(ctx, "About to set sregs: CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                    sregs.cr0, sregs.cr3, sregs.cr4);
    }

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS (paging)");
        return -1;
    }

    // Update RIP to entry point
    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->entry_point;
    regs.rflags = 0x2;

    /*
     * Give the guest a usable stack. A kernel is expected to install its own
     * (os-1k/boot.S does so immediately), but leaving ESP at 0 means the very
     * first CALL underflows to 0xFFFFFFFC, which is unmapped: page fault, then
     * double fault, then triple fault, before the guest executes anything
     * recognisable. Long mode already avoided this; protected mode did not.
     *
     * PROT_MODE_DEFAULT_STACK sits above the load area and below the page
     * directory at 0x00100000, inside the identity-mapped low 4MB.
     */
    regs.rsp = PROT_MODE_DEFAULT_STACK;
    regs.rbp = regs.rsp;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
    {
        perror("KVM_SET_REGS (paging)");
        return -1;
    }

    if (verbose_enabled())
    {
        // Verify the settings
        struct kvm_sregs verify_sregs;
        struct kvm_regs verify_regs;
        if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &verify_sregs) == 0)
        {
            vcpu_printf(ctx, "Verified: CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                        verify_sregs.cr0, verify_sregs.cr3, verify_sregs.cr4);
        }
        if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &verify_regs) == 0)
        {
            vcpu_printf(ctx, "Verified: RIP=0x%llx RFLAGS=0x%llx\n",
                        verify_regs.rip, verify_regs.rflags);
        }
    }

    if (verbose_enabled())
    {
        vcpu_printf(ctx, "Enabled paging: CR3=0x%llx, EIP=0x%x (Protected Mode)\n",
                    sregs.cr3, ctx->entry_point);
    }

    return 0;
}

int cpu_mode_enter_flat32(vcpu_context_t *ctx, uint32_t entry,
                          uint32_t eax, uint32_t ebx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    /* The descriptor tables still have to exist even though a Multiboot
     * kernel is expected to replace them: KVM validates the segment state we
     * hand it, and a fault before the guest installs an IDT must land
     * somewhere defined. */
    gdt_setup(ctx->guest_mem, verbose_enabled());
    idt_setup(ctx->guest_mem, verbose_enabled());

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS (flat32)");
        return -1;
    }

    sregs.gdt.base = GDT_ADDR;
    sregs.gdt.limit = GDT_TOTAL_SIZE - 1;
    sregs.idt.base = GDT_ADDR + GDT_TOTAL_SIZE;
    sregs.idt.limit = (256 * sizeof(idt_entry_t)) - 1;

    /* PE and ET, but deliberately not PG: Multiboot hands the kernel a
     * machine with paging disabled, and it enables paging itself. */
    sregs.cr0 = 0x00000011;
    sregs.cr3 = 0;
    sregs.cr4 = 0;
    sregs.efer = 0;

    segments_set_flat32(&sregs);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS (flat32)");
        return -1;
    }

    memset(&regs, 0, sizeof(regs));
    regs.rip = entry;
    regs.rax = eax;
    regs.rbx = ebx;
    regs.rflags = 0x2;      /* IF clear, VM clear, bit 1 always set */
    regs.rsp = PROT_MODE_DEFAULT_STACK;
    regs.rbp = regs.rsp;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS (flat32)");
        return -1;
    }

    if (verbose_enabled()) {
        vcpu_printf(ctx, "Protected mode, paging off: EIP=0x%x EAX=0x%x EBX=0x%x\n",
                    entry, eax, ebx);
    }

    return 0;
}
