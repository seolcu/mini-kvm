/*
 * Debug utilities implementation for Mini-KVM
 */

#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include <ctype.h>

// Global debug level
debug_level_t debug_level = DEBUG_NONE;

// Register names for display (unused for now, but may be useful later)
__attribute__((unused)) static const char *reg_names[] = {
    "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RSP", "RBP",
    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15"
};

__attribute__((unused)) static const char *seg_names[] = {
    "CS", "DS", "ES", "FS", "GS", "SS"
};

// VM exit reason strings
const char *get_exit_reason_string(uint32_t exit_reason) {
    switch (exit_reason) {
        case KVM_EXIT_UNKNOWN: return "UNKNOWN";
        case KVM_EXIT_EXCEPTION: return "EXCEPTION";
        case KVM_EXIT_IO: return "IO_INSTRUCTION";
        case KVM_EXIT_HYPERCALL: return "HYPERCALL";
        case KVM_EXIT_DEBUG: return "DEBUG";
        case KVM_EXIT_HLT: return "HLT";
        case KVM_EXIT_MMIO: return "MMIO";
        case KVM_EXIT_IRQ_WINDOW_OPEN: return "IRQ_WINDOW_OPEN";
        case KVM_EXIT_SHUTDOWN: return "SHUTDOWN";
        case KVM_EXIT_FAIL_ENTRY: return "FAIL_ENTRY";
        case KVM_EXIT_INTR: return "INTR";
        case KVM_EXIT_SET_TPR: return "SET_TPR";
        case KVM_EXIT_TPR_ACCESS: return "TPR_ACCESS";
        case KVM_EXIT_S390_SIEIC: return "S390_SIEIC";
        case KVM_EXIT_S390_RESET: return "S390_RESET";
        case KVM_EXIT_DCR: return "DCR";
        case KVM_EXIT_NMI: return "NMI";
        case KVM_EXIT_INTERNAL_ERROR: return "INTERNAL_ERROR";
        case KVM_EXIT_OSI: return "OSI";
        case KVM_EXIT_PAPR_HCALL: return "PAPR_HCALL";
        default: return "UNKNOWN_EXIT_TYPE";
    }
}

// Dump general purpose registers
void dump_general_registers(int vcpu_fd, int vcpu_id) {
    struct kvm_regs regs;
    if (ioctl(vcpu_fd, KVM_GET_REGS, &regs) < 0) {
        perror("KVM_GET_REGS failed");
        return;
    }

    fprintf(stderr, "\n[vCPU %d] General Purpose Registers:\n", vcpu_id);
    fprintf(stderr, "  RAX: 0x%016llx  RBX: 0x%016llx\n", 
            (unsigned long long)regs.rax, (unsigned long long)regs.rbx);
    fprintf(stderr, "  RCX: 0x%016llx  RDX: 0x%016llx\n", 
            (unsigned long long)regs.rcx, (unsigned long long)regs.rdx);
    fprintf(stderr, "  RSI: 0x%016llx  RDI: 0x%016llx\n", 
            (unsigned long long)regs.rsi, (unsigned long long)regs.rdi);
    fprintf(stderr, "  RSP: 0x%016llx  RBP: 0x%016llx\n", 
            (unsigned long long)regs.rsp, (unsigned long long)regs.rbp);
    fprintf(stderr, "  R8:  0x%016llx  R9:  0x%016llx\n", 
            (unsigned long long)regs.r8, (unsigned long long)regs.r9);
    fprintf(stderr, "  R10: 0x%016llx  R11: 0x%016llx\n", 
            (unsigned long long)regs.r10, (unsigned long long)regs.r11);
    fprintf(stderr, "  R12: 0x%016llx  R13: 0x%016llx\n", 
            (unsigned long long)regs.r12, (unsigned long long)regs.r13);
    fprintf(stderr, "  R14: 0x%016llx  R15: 0x%016llx\n", 
            (unsigned long long)regs.r14, (unsigned long long)regs.r15);
    fprintf(stderr, "  RIP: 0x%016llx  RFLAGS: 0x%016llx\n", 
            (unsigned long long)regs.rip, (unsigned long long)regs.rflags);
}

// Dump segment registers
void dump_segment_registers(struct kvm_sregs *sregs, int vcpu_id) {
    fprintf(stderr, "\n[vCPU %d] Segment Registers:\n", vcpu_id);
    
    fprintf(stderr, "  CS: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->cs.base, sregs->cs.limit, sregs->cs.selector, sregs->cs.type);
    fprintf(stderr, "  DS: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->ds.base, sregs->ds.limit, sregs->ds.selector, sregs->ds.type);
    fprintf(stderr, "  ES: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->es.base, sregs->es.limit, sregs->es.selector, sregs->es.type);
    fprintf(stderr, "  FS: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->fs.base, sregs->fs.limit, sregs->fs.selector, sregs->fs.type);
    fprintf(stderr, "  GS: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->gs.base, sregs->gs.limit, sregs->gs.selector, sregs->gs.type);
    fprintf(stderr, "  SS: base=0x%016llx limit=0x%08x sel=0x%04x type=0x%02x\n",
            (unsigned long long)sregs->ss.base, sregs->ss.limit, sregs->ss.selector, sregs->ss.type);
}

// Dump control registers
void dump_control_registers(struct kvm_sregs *sregs, int vcpu_id) {
    fprintf(stderr, "\n[vCPU %d] Control Registers:\n", vcpu_id);
    fprintf(stderr, "  CR0: 0x%016llx ", (unsigned long long)sregs->cr0);
    fprintf(stderr, "[");
    if (sregs->cr0 & (1 << 0)) fprintf(stderr, "PE ");
    if (sregs->cr0 & (1 << 1)) fprintf(stderr, "MP ");
    if (sregs->cr0 & (1 << 2)) fprintf(stderr, "EM ");
    if (sregs->cr0 & (1 << 3)) fprintf(stderr, "TS ");
    if (sregs->cr0 & (1 << 4)) fprintf(stderr, "ET ");
    if (sregs->cr0 & (1 << 5)) fprintf(stderr, "NE ");
    if (sregs->cr0 & (1 << 16)) fprintf(stderr, "WP ");
    if (sregs->cr0 & (1 << 18)) fprintf(stderr, "AM ");
    if (sregs->cr0 & (1 << 29)) fprintf(stderr, "NW ");
    if (sregs->cr0 & (1 << 30)) fprintf(stderr, "CD ");
    if (sregs->cr0 & (1U << 31)) fprintf(stderr, "PG ");
    fprintf(stderr, "]\n");
    
    fprintf(stderr, "  CR2: 0x%016llx (Page Fault Linear Address)\n", (unsigned long long)sregs->cr2);
    fprintf(stderr, "  CR3: 0x%016llx (Page Directory Base)\n", (unsigned long long)sregs->cr3);
    fprintf(stderr, "  CR4: 0x%016llx ", (unsigned long long)sregs->cr4);
    fprintf(stderr, "[");
    if (sregs->cr4 & (1 << 0)) fprintf(stderr, "VME ");
    if (sregs->cr4 & (1 << 1)) fprintf(stderr, "PVI ");
    if (sregs->cr4 & (1 << 2)) fprintf(stderr, "TSD ");
    if (sregs->cr4 & (1 << 3)) fprintf(stderr, "DE ");
    if (sregs->cr4 & (1 << 4)) fprintf(stderr, "PSE ");
    if (sregs->cr4 & (1 << 5)) fprintf(stderr, "PAE ");
    if (sregs->cr4 & (1 << 6)) fprintf(stderr, "MCE ");
    if (sregs->cr4 & (1 << 7)) fprintf(stderr, "PGE ");
    if (sregs->cr4 & (1 << 8)) fprintf(stderr, "PCE ");
    if (sregs->cr4 & (1 << 9)) fprintf(stderr, "OSFXSR ");
    if (sregs->cr4 & (1 << 10)) fprintf(stderr, "OSXMMEXCPT ");
    fprintf(stderr, "]\n");
    
    fprintf(stderr, "  CR8: 0x%016llx (Task Priority)\n", (unsigned long long)sregs->cr8);
    fprintf(stderr, "  EFER: 0x%016llx ", (unsigned long long)sregs->efer);
    fprintf(stderr, "[");
    if (sregs->efer & (1 << 0)) fprintf(stderr, "SCE ");
    if (sregs->efer & (1 << 8)) fprintf(stderr, "LME ");
    if (sregs->efer & (1 << 10)) fprintf(stderr, "LMA ");
    if (sregs->efer & (1 << 11)) fprintf(stderr, "NXE ");
    fprintf(stderr, "]\n");
}

// Dump special registers
void dump_special_registers(int vcpu_fd, int vcpu_id) {
    struct kvm_sregs sregs;
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS failed");
        return;
    }

    dump_segment_registers(&sregs, vcpu_id);
    dump_control_registers(&sregs, vcpu_id);
    
    fprintf(stderr, "\n[vCPU %d] Descriptor Tables:\n", vcpu_id);
    fprintf(stderr, "  GDT: base=0x%016llx limit=0x%04x\n", 
            (unsigned long long)sregs.gdt.base, sregs.gdt.limit);
    fprintf(stderr, "  IDT: base=0x%016llx limit=0x%04x\n", 
            (unsigned long long)sregs.idt.base, sregs.idt.limit);
    fprintf(stderr, "  LDT: base=0x%016llx limit=0x%08x sel=0x%04x\n",
            (unsigned long long)sregs.ldt.base, sregs.ldt.limit, sregs.ldt.selector);
    fprintf(stderr, "  TR:  base=0x%016llx limit=0x%08x sel=0x%04x\n",
            (unsigned long long)sregs.tr.base, sregs.tr.limit, sregs.tr.selector);
}

// Dump all registers
void dump_all_registers(int vcpu_fd, int vcpu_id) {
    fprintf(stderr, "\n========== vCPU %d Register Dump ==========\n", vcpu_id);
    dump_general_registers(vcpu_fd, vcpu_id);
    dump_special_registers(vcpu_fd, vcpu_id);
    fprintf(stderr, "==========================================\n\n");
}

// Dump memory region in hex + ASCII
void dump_memory_region(void *mem, uint64_t guest_addr, size_t size, const char *label) {
    fprintf(stderr, "\n[Memory Dump: %s] GPA 0x%lx, size %zu bytes:\n", 
            label, (unsigned long)guest_addr, size);
    
    unsigned char *bytes = (unsigned char *)mem + guest_addr;
    for (size_t i = 0; i < size; i += 16) {
        fprintf(stderr, "%08lx: ", (unsigned long)(guest_addr + i));
        
        // Hex bytes
        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            fprintf(stderr, "%02x ", bytes[i + j]);
            if (j == 7) fprintf(stderr, " ");
        }
        
        // Padding
        for (size_t j = size - i; j < 16 && j > 0; j--) {
            fprintf(stderr, "   ");
        }
        
        fprintf(stderr, " |");
        
        // ASCII representation
        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            unsigned char c = bytes[i + j];
            fprintf(stderr, "%c", isprint(c) ? c : '.');
        }
        
        fprintf(stderr, "|\n");
    }
}

// Dump entire guest memory to file
void dump_memory_to_file(void *mem, size_t size, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open memory dump file");
        return;
    }
    
    size_t written = fwrite(mem, 1, size, f);
    fclose(f);
    
    fprintf(stderr, "[Memory Dump] Wrote %zu bytes to %s\n", written, filename);
}

// Print guest memory map overview
void dump_guest_memory_map(void *mem __attribute__((unused)), size_t mem_size) {
    fprintf(stderr, "\n[Guest Memory Map] Total size: %zu MB\n", mem_size / (1024*1024));
    fprintf(stderr, "  0x%08x - 0x%08zx: Guest physical memory\n", 0, mem_size - 1);
}

// Walk 32-bit page tables (2-level paging)
void walk_page_tables_32bit(void *mem, uint32_t cr3, uint32_t virt_addr) {
    fprintf(stderr, "\n[Page Table Walk] 32-bit paging, CR3=0x%08x, VA=0x%08x\n", 
            cr3, virt_addr);
    
    uint32_t pd_base = cr3 & ~0xFFF;
    uint32_t pd_index = (virt_addr >> 22) & 0x3FF;
    uint32_t pt_index = (virt_addr >> 12) & 0x3FF;
    uint32_t offset = virt_addr & 0xFFF;
    
    fprintf(stderr, "  PD index: 0x%03x, PT index: 0x%03x, Offset: 0x%03x\n",
            pd_index, pt_index, offset);
    
    // Read PDE
    uint32_t *pd = (uint32_t *)((char *)mem + pd_base);
    uint32_t pde = pd[pd_index];
    
    fprintf(stderr, "  PDE[0x%03x] = 0x%08x ", pd_index, pde);
    if (!(pde & 1)) {
        fprintf(stderr, "[NOT PRESENT]\n");
        return;
    }
    fprintf(stderr, "[P=%d W=%d U=%d PS=%d]\n",
            (pde & 1) ? 1 : 0,
            (pde & 2) ? 1 : 0,
            (pde & 4) ? 1 : 0,
            (pde & 0x80) ? 1 : 0);
    
    // Check for 4MB page (PSE bit)
    if (pde & 0x80) {
        uint32_t phys_addr = (pde & 0xFFC00000) | (virt_addr & 0x3FFFFF);
        fprintf(stderr, "  → 4MB page, Physical Address: 0x%08x\n", phys_addr);
        return;
    }
    
    // Read PTE
    uint32_t pt_base = pde & ~0xFFF;
    uint32_t *pt = (uint32_t *)((char *)mem + pt_base);
    uint32_t pte = pt[pt_index];
    
    fprintf(stderr, "  PTE[0x%03x] = 0x%08x ", pt_index, pte);
    if (!(pte & 1)) {
        fprintf(stderr, "[NOT PRESENT]\n");
        return;
    }
    fprintf(stderr, "[P=%d W=%d U=%d]\n",
            (pte & 1) ? 1 : 0,
            (pte & 2) ? 1 : 0,
            (pte & 4) ? 1 : 0);
    
    uint32_t phys_addr = (pte & ~0xFFF) | offset;
    fprintf(stderr, "  → Physical Address: 0x%08x\n", phys_addr);
}

// TODO: Implement 64-bit and PAE page table walking
void walk_page_tables_pae(void *mem __attribute__((unused)), 
                         uint32_t cr3 __attribute__((unused)), 
                         uint32_t virt_addr __attribute__((unused))) {
    fprintf(stderr, "[Page Table Walk] PAE paging not yet implemented\n");
}

void walk_page_tables_64bit(void *mem __attribute__((unused)), 
                           uint64_t cr3 __attribute__((unused)), 
                           uint64_t virt_addr __attribute__((unused))) {
    fprintf(stderr, "[Page Table Walk] 64-bit paging not yet implemented\n");
}

// Print detailed VM exit information
void print_vm_exit_details(struct kvm_run *run, int vcpu_id) {
    fprintf(stderr, "\n[vCPU %d] ===== VM EXIT DETAILS =====\n", vcpu_id);
    fprintf(stderr, "  Exit Reason: %s (%u)\n", 
            get_exit_reason_string(run->exit_reason), run->exit_reason);
    
    switch (run->exit_reason) {
        case KVM_EXIT_IO:
            fprintf(stderr, "  I/O Details:\n");
            fprintf(stderr, "    Direction: %s\n", 
                    run->io.direction == KVM_EXIT_IO_IN ? "IN" : "OUT");
            fprintf(stderr, "    Size: %u bytes\n", run->io.size);
            fprintf(stderr, "    Port: 0x%x\n", run->io.port);
            fprintf(stderr, "    Count: %u\n", run->io.count);
            break;
            
        case KVM_EXIT_MMIO:
            fprintf(stderr, "  MMIO Details:\n");
            fprintf(stderr, "    Physical Address: 0x%llx\n", (unsigned long long)run->mmio.phys_addr);
            fprintf(stderr, "    Is Write: %s\n", run->mmio.is_write ? "yes" : "no");
            fprintf(stderr, "    Length: %u bytes\n", run->mmio.len);
            break;
            
        case KVM_EXIT_FAIL_ENTRY:
            fprintf(stderr, "  Fail Entry Details:\n");
            fprintf(stderr, "    Hardware Entry Failure Reason: 0x%llx\n", 
                    (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
            break;
            
        case KVM_EXIT_INTERNAL_ERROR:
            fprintf(stderr, "  Internal Error Details:\n");
            fprintf(stderr, "    Suberror: 0x%x\n", run->internal.suberror);
            fprintf(stderr, "    Ndata: %u\n", run->internal.ndata);
            for (unsigned int i = 0; i < run->internal.ndata && i < 16; i++) {
                fprintf(stderr, "    Data[%u]: 0x%llx\n", i, (unsigned long long)run->internal.data[i]);
            }
            break;
            
        case KVM_EXIT_SHUTDOWN:
            fprintf(stderr, "  Guest requested shutdown (triple fault or similar)\n");
            break;
    }
    
    fprintf(stderr, "=============================\n\n");
}

// Dump guest stack
void dump_guest_stack(void *mem, uint32_t esp, uint32_t ss_base, int count) {
    fprintf(stderr, "\n[Stack Dump] ESP=0x%08x, SS.base=0x%08x, showing %d entries:\n",
            esp, ss_base, count);
    
    uint32_t stack_addr = ss_base + esp;
    uint32_t *stack = (uint32_t *)((char *)mem + stack_addr);
    
    for (int i = 0; i < count; i++) {
        fprintf(stderr, "  [ESP+0x%02x] 0x%08x: 0x%08x\n", 
                i * 4, stack_addr + i * 4, stack[i]);
    }
}

// Dump bytes around instruction pointer
void dump_instruction_context(void *mem, uint64_t rip, int bytes_before, int bytes_after) {
    fprintf(stderr, "\n[Instruction Context] RIP=0x%lx:\n", (unsigned long)rip);
    
    uint64_t start = rip - bytes_before;
    uint64_t total = bytes_before + bytes_after;
    
    unsigned char *code = (unsigned char *)mem + start;
    
    for (uint64_t i = 0; i < total; i++) {
        if (i == (uint64_t)bytes_before) {
            fprintf(stderr, " -> ");
        } else {
            fprintf(stderr, "    ");
        }
        fprintf(stderr, "%02x ", code[i]);
        if ((i + 1) % 16 == 0) {
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "\n");
}
