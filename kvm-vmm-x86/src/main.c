/*
 * Minimal KVM-based Virtual Machine Monitor (x86)
 *
 * This VMM creates a VM using Linux KVM API and runs a simple guest in Real Mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include <errno.h>

// Guest memory configuration
#define GUEST_MEM_SIZE (1 << 20)  // 1MB (Real mode maximum)
#define GUEST_LOAD_ADDR 0x0       // Load guest at address 0

// File descriptors
static int kvm_fd = -1;
static int vm_fd = -1;
static int vcpu_fd = -1;

// Memory pointers
static void *guest_mem = NULL;
static struct kvm_run *kvm_run = NULL;

/*
 * Load guest binary into guest memory
 */
static int load_guest_binary(const char *filename, void *mem, size_t mem_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return -1;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("Guest binary size: %ld bytes\n", fsize);

    if (fsize > mem_size) {
        fprintf(stderr, "Guest binary too large (%ld bytes > %zu bytes)\n",
                fsize, mem_size);
        fclose(f);
        return -1;
    }

    // Load binary at GUEST_LOAD_ADDR offset
    size_t nread = fread(mem + GUEST_LOAD_ADDR, 1, fsize, f);
    if (nread != fsize) {
        perror("fread");
        fclose(f);
        return -1;
    }

    fclose(f);

    printf("Loaded guest binary: %zu bytes at offset 0x%x\n", nread, GUEST_LOAD_ADDR);

    // Show first few bytes
    printf("First bytes: ");
    for (int i = 0; i < (fsize < 16 ? fsize : 16); i++) {
        printf("%02x ", ((unsigned char*)(mem + GUEST_LOAD_ADDR))[i]);
    }
    printf("\n");

    return 0;
}

/*
 * Initialize KVM and create VM
 */
static int init_kvm(void) {
    int ret;
    int api_version;

    // 1. Open /dev/kvm
    kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm_fd < 0) {
        perror("open /dev/kvm");
        fprintf(stderr, "Make sure KVM is enabled (CONFIG_KVM=y/m)\n");
        return -1;
    }

    // 2. Check KVM API version
    api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0) {
        perror("KVM_GET_API_VERSION");
        return -1;
    }

    if (api_version != KVM_API_VERSION) {
        fprintf(stderr, "KVM API version mismatch: expected %d, got %d\n",
                KVM_API_VERSION, api_version);
        return -1;
    }

    printf("KVM API version: %d\n", api_version);

    // 3. Create VM
    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("KVM_CREATE_VM");
        return -1;
    }

    printf("Created VM (fd=%d)\n", vm_fd);

    return 0;
}

/*
 * Allocate and map guest memory
 */
static int setup_guest_memory(void) {
    struct kvm_userspace_memory_region mem_region;

    // Allocate guest memory
    guest_mem = mmap(NULL, GUEST_MEM_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (guest_mem == MAP_FAILED) {
        perror("mmap guest_mem");
        return -1;
    }

    printf("Allocated guest memory: %d MB at %p\n",
           GUEST_MEM_SIZE / (1024*1024), guest_mem);

    // Tell KVM about this memory region
    mem_region.slot = 0;
    mem_region.flags = 0;
    mem_region.guest_phys_addr = 0;  // GPA starts at 0
    mem_region.memory_size = GUEST_MEM_SIZE;
    mem_region.userspace_addr = (unsigned long)guest_mem;

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    printf("Mapped guest memory: GPA 0x0 -> HVA %p (size: %d bytes)\n",
           guest_mem, GUEST_MEM_SIZE);

    return 0;
}

/*
 * Create vCPU and initialize registers for Real Mode
 */
static int setup_vcpu(void) {
    size_t mmap_size;
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // 1. Create vCPU
    vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    if (vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        return -1;
    }

    printf("Created vCPU (fd=%d)\n", vcpu_fd);

    // 2. Get kvm_run structure size and mmap it
    mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }

    kvm_run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, vcpu_fd, 0);
    if (kvm_run == MAP_FAILED) {
        perror("mmap kvm_run");
        return -1;
    }

    printf("Mapped kvm_run structure: %zu bytes\n", mmap_size);

    // 3. Get current special registers (segment registers, etc.)
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS");
        return -1;
    }

    // 4. Set up Real Mode segment registers
    // In Real Mode: physical_address = segment * 16 + offset
    // We want CS:IP = 0x0000:0x0000
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    sregs.cs.limit = 0xFFFF;      // 64KB segment limit (Real Mode)
    sregs.cs.type = 0x9b;         // Code segment, executable, readable
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;             // Privilege level 0
    sregs.cs.db = 0;              // 16-bit mode
    sregs.cs.s = 1;               // Code/data segment
    sregs.cs.l = 0;               // Not 64-bit
    sregs.cs.g = 0;               // Byte granularity
    sregs.cs.avl = 0;

    // Set up data segments similarly
    sregs.ds.base = 0;
    sregs.ds.selector = 0;
    sregs.ds.limit = 0xFFFF;
    sregs.ds.type = 0x93;         // Data segment, writable
    sregs.ds.present = 1;
    sregs.ds.dpl = 0;
    sregs.ds.db = 0;
    sregs.ds.s = 1;
    sregs.ds.l = 0;
    sregs.ds.g = 0;
    sregs.ds.avl = 0;

    // Copy DS settings to ES, FS, GS, SS
    sregs.es = sregs.fs = sregs.gs = sregs.ss = sregs.ds;

    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS");
        return -1;
    }

    printf("Set Real Mode segment registers\n");

    // 5. Set general purpose registers
    memset(&regs, 0, sizeof(regs));

    // Set instruction pointer to guest entry point
    regs.rip = GUEST_LOAD_ADDR;   // IP = 0x0000 (with CS = 0x0000)
    regs.rflags = 0x2;            // Bit 1 is always 1 in EFLAGS

    if (ioctl(vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS");
        return -1;
    }

    printf("Set registers: RIP=0x%llx\n", regs.rip);

    return 0;
}

/*
 * Run vCPU and handle VM exits
 */
static int run_vm(void) {
    int ret;
    int exit_count = 0;

    printf("\n=== Starting VM execution ===\n\n");

    while (1) {
        // Run vCPU
        ret = ioctl(vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            perror("KVM_RUN");
            return -1;
        }

        exit_count++;

        // Handle VM exit
        switch (kvm_run->exit_reason) {
            case KVM_EXIT_HLT:
                printf("VM Exit #%d: HLT instruction\n", exit_count);
                printf("Guest halted successfully!\n");
                return 0;  // Normal exit

            case KVM_EXIT_IO:
                printf("VM Exit #%d: I/O operation\n", exit_count);
                printf("  Direction: %s\n",
                       kvm_run->io.direction == KVM_EXIT_IO_OUT ? "OUT" : "IN");
                printf("  Port: 0x%x\n", kvm_run->io.port);
                printf("  Size: %d bytes\n", kvm_run->io.size);

                // TODO: Handle I/O (will implement later for UART)
                break;

            case KVM_EXIT_MMIO:
                printf("VM Exit #%d: MMIO access\n", exit_count);
                printf("  Address: 0x%llx\n", kvm_run->mmio.phys_addr);
                printf("  Is write: %d\n", kvm_run->mmio.is_write);
                break;

            case KVM_EXIT_FAIL_ENTRY:
                fprintf(stderr, "VM Exit #%d: FAIL_ENTRY\n", exit_count);
                fprintf(stderr, "  Hardware entry failure reason: 0x%llx\n",
                        kvm_run->fail_entry.hardware_entry_failure_reason);
                return -1;

            case KVM_EXIT_INTERNAL_ERROR:
                fprintf(stderr, "VM Exit #%d: INTERNAL_ERROR\n", exit_count);
                fprintf(stderr, "  Suberror: 0x%x\n", kvm_run->internal.suberror);
                return -1;

            case KVM_EXIT_SHUTDOWN:
                printf("VM Exit #%d: SHUTDOWN\n", exit_count);
                return 0;

            default:
                printf("VM Exit #%d: Unknown reason %d\n",
                       exit_count, kvm_run->exit_reason);
                return -1;
        }

        // Safety: exit after too many iterations
        if (exit_count > 100) {
            fprintf(stderr, "Too many VM exits (%d), stopping\n", exit_count);
            return -1;
        }
    }

    return 0;
}

/*
 * Cleanup resources
 */
static void cleanup(void) {
    if (kvm_run != NULL && kvm_run != MAP_FAILED) {
        munmap(kvm_run, sizeof(*kvm_run));
    }
    if (guest_mem != NULL && guest_mem != MAP_FAILED) {
        munmap(guest_mem, GUEST_MEM_SIZE);
    }
    if (vcpu_fd >= 0) close(vcpu_fd);
    if (vm_fd >= 0) close(vm_fd);
    if (kvm_fd >= 0) close(kvm_fd);
}

int main(int argc, char **argv) {
    int ret = 0;

    printf("=== Minimal KVM VMM (x86 Real Mode) ===\n\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <guest_binary>\n", argv[0]);
        return 1;
    }

    // Step 1: Initialize KVM and create VM
    if (init_kvm() < 0) {
        ret = 1;
        goto cleanup;
    }

    // Step 2: Set up guest memory
    if (setup_guest_memory() < 0) {
        ret = 1;
        goto cleanup;
    }

    // Step 3: Load guest binary
    if (load_guest_binary(argv[1], guest_mem, GUEST_MEM_SIZE) < 0) {
        ret = 1;
        goto cleanup;
    }

    // Step 4: Create and initialize vCPU
    if (setup_vcpu() < 0) {
        ret = 1;
        goto cleanup;
    }

    // Step 5: Run VM
    if (run_vm() < 0) {
        ret = 1;
        goto cleanup;
    }

    printf("\n=== VM execution completed successfully ===\n");

cleanup:
    cleanup();
    return ret;
}
