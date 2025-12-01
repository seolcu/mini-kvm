// Minimal test to diagnose Long Mode KVM_SET_SREGS issue
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>

#define EFER_LME (1 << 8)
#define EFER_LMA (1 << 10)

int main() {
    int kvm_fd, vm_fd, vcpu_fd;
    struct kvm_sregs sregs;
    void *mem;
    size_t mem_size = 4 * 1024 * 1024;
    
    // Open KVM
    kvm_fd = open("/dev/kvm", O_RDWR);
    if (kvm_fd < 0) { perror("open /dev/kvm"); return 1; }
    
    // Create VM
    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) { perror("KVM_CREATE_VM"); return 1; }
    
    // Allocate guest memory
    mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, 
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }
    
    // Map memory to guest
    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .guest_phys_addr = 0,
        .memory_size = mem_size,
        .userspace_addr = (unsigned long)mem,
    };
    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION"); return 1;
    }
    
    // Create vCPU
    vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    if (vcpu_fd < 0) { perror("KVM_CREATE_VCPU"); return 1; }
    
    // Setup simple page tables at 0x2000
    uint64_t *pml4 = (uint64_t *)(mem + 0x2000);
    uint64_t *pdpt = (uint64_t *)(mem + 0x3000);
    uint64_t *pd = (uint64_t *)(mem + 0x4000);
    memset(pml4, 0, 0x3000);
    pml4[0] = 0x3003; // PDPT at 0x3000, Present+Write
    pdpt[0] = 0x4003; // PD at 0x4000, Present+Write
    pd[0] = 0x83;     // 2MB page at 0, Present+Write+PSE
    pd[1] = 0x200083; // 2MB page at 2MB
    
    printf("Page tables set up\n");
    
    // Get current sregs
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS"); return 1;
    }
    
    printf("Initial state:\n");
    printf("  CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx\n",
           sregs.cr0, sregs.cr3, sregs.cr4, sregs.efer);
    printf("  CS: sel=0x%x base=0x%llx limit=0x%x type=0x%x present=%d dpl=%d db=%d s=%d l=%d g=%d\n",
           sregs.cs.selector, sregs.cs.base, sregs.cs.limit, 
           sregs.cs.type, sregs.cs.present, sregs.cs.dpl, 
           sregs.cs.db, sregs.cs.s, sregs.cs.l, sregs.cs.g);
    
    // Test 1: Try setting just EFER.LME without paging
    printf("\nTest 1: Set EFER.LME only (no paging)\n");
    sregs.efer = EFER_LME;
    sregs.cr4 = 0x20; // PAE
    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("  FAILED");
    } else {
        printf("  OK\n");
    }
    
    // Test 2: Try enabling paging with LME
    printf("\nTest 2: Enable paging with LME (should activate Long Mode)\n");
    ioctl(vcpu_fd, KVM_GET_SREGS, &sregs); // Reset
    sregs.cr3 = 0x2000;
    sregs.cr4 = 0x20; // PAE
    sregs.efer = EFER_LME;
    sregs.cr0 = 0x80000011; // PG + PE + ET
    // Keep default segments for now
    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("  FAILED");
    } else {
        printf("  OK\n");
        ioctl(vcpu_fd, KVM_GET_SREGS, &sregs);
        printf("  After: EFER=0x%llx (LMA should be set)\n", sregs.efer);
    }
    
    // Test 3: Try with 64-bit CS
    printf("\nTest 3: Set 64-bit CS segment\n");
    ioctl(vcpu_fd, KVM_GET_SREGS, &sregs); // Reset
    sregs.cr3 = 0x2000;
    sregs.cr4 = 0x20;
    sregs.efer = EFER_LME;
    sregs.cr0 = 0x80000011;
    // 64-bit code segment
    sregs.cs.selector = 0x08;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0x0b; // Execute/Read/Accessed
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 0;  // Must be 0 for Long Mode
    sregs.cs.s = 1;
    sregs.cs.l = 1;   // Long mode
    sregs.cs.g = 1;
    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("  FAILED");
    } else {
        printf("  OK\n");
    }
    
    // Test 4: Full Long Mode setup with GDT
    printf("\nTest 4: Full setup with GDT\n");
    ioctl(vcpu_fd, KVM_GET_SREGS, &sregs); // Reset
    
    // Setup GDT at 0x1000
    uint64_t *gdt = (uint64_t *)(mem + 0x1000);
    gdt[0] = 0;                    // Null
    gdt[1] = 0x00af9a000000ffff;   // 64-bit code: base=0, limit=0xfffff, G=1, L=1, P=1, DPL=0, S=1, type=0xa
    gdt[2] = 0x00cf92000000ffff;   // 64-bit data: base=0, limit=0xfffff, G=1, D=1, P=1, DPL=0, S=1, type=0x2
    
    sregs.gdt.base = 0x1000;
    sregs.gdt.limit = 0x17; // 3 entries
    
    sregs.cr3 = 0x2000;
    sregs.cr4 = 0x20;
    sregs.efer = EFER_LME;
    sregs.cr0 = 0x80000011;
    
    // CS from GDT entry 1
    sregs.cs.selector = 0x08;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0x0b;
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 0;
    sregs.cs.s = 1;
    sregs.cs.l = 1;
    sregs.cs.g = 1;
    
    // Data segments from GDT entry 2
    sregs.ds.selector = 0x10;
    sregs.ds.base = 0;
    sregs.ds.limit = 0xFFFFFFFF;
    sregs.ds.type = 0x03;
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 1;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 1;
    sregs.es = sregs.ss = sregs.fs = sregs.gs = sregs.ds;
    
    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("  FAILED");
    } else {
        printf("  OK - Long Mode activated!\n");
        ioctl(vcpu_fd, KVM_GET_SREGS, &sregs);
        printf("  EFER=0x%llx CR0=0x%llx\n", sregs.efer, sregs.cr0);
    }
    
    close(vcpu_fd);
    close(vm_fd);
    close(kvm_fd);
    munmap(mem, mem_size);
    
    return 0;
}
