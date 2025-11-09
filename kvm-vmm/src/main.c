/*
 * Minimal KVM-based VMM for RISC-V
 * Runs a simple guest program using KVM API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <errno.h>

#define GUEST_MEM_SIZE (4096)  /* 4KB - minimal memory */
#define GUEST_LOAD_ADDR (0x0)   /* Load guest at GPA 0 */

/* Load guest binary from file */
static int load_guest_binary(const char *filename, void *mem, size_t mem_size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen guest binary");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > mem_size) {
        fprintf(stderr, "Guest binary too large: %ld > %zu\n", size, mem_size);
        fclose(f);
        return -1;
    }

    size_t read = fread(mem, 1, size, f);
    fclose(f);

    printf("Loaded guest binary: %zu bytes\n", read);
    return 0;
}

int main(int argc, char **argv) {
    int kvm_fd, vm_fd, vcpu_fd;
    struct kvm_userspace_memory_region mem_region;
    void *guest_mem;
    struct kvm_run *run;
    int vcpu_mmap_size;
    int ret;

    printf("===========================================\n");
    printf(" Minimal KVM-based VMM for RISC-V\n");
    printf("===========================================\n\n");

    /* Check arguments */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <guest.bin>\n", argv[0]);
        return 1;
    }

    /* Step 1: Open /dev/kvm */
    printf("[1] Opening /dev/kvm...\n");
    kvm_fd = open("/dev/kvm", O_RDWR);
    if (kvm_fd < 0) {
        perror("open /dev/kvm");
        return 1;
    }

    /* Check KVM API version */
    ret = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (ret != 12) {
        fprintf(stderr, "KVM API version %d, expected 12\n", ret);
        close(kvm_fd);
        return 1;
    }
    printf("    KVM API version: %d\n", ret);

    /* Step 2: Create VM */
    printf("[2] Creating VM...\n");
    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("KVM_CREATE_VM");
        close(kvm_fd);
        return 1;
    }
    printf("    VM created (fd=%d)\n", vm_fd);

    /* Step 3: Allocate guest memory */
    printf("[3] Setting up guest memory (%d bytes)...\n", GUEST_MEM_SIZE);
    guest_mem = mmap(NULL, GUEST_MEM_SIZE,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (guest_mem == MAP_FAILED) {
        perror("mmap guest memory");
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }
    printf("    Guest memory allocated at %p\n", guest_mem);

    /* Load guest binary */
    printf("    Loading guest binary: %s\n", argv[1]);
    if (load_guest_binary(argv[1], guest_mem, GUEST_MEM_SIZE) < 0) {
        munmap(guest_mem, GUEST_MEM_SIZE);
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }

    /* Set up memory region in KVM */
    mem_region.slot = 0;
    mem_region.flags = 0;
    mem_region.guest_phys_addr = GUEST_LOAD_ADDR;
    mem_region.memory_size = GUEST_MEM_SIZE;
    mem_region.userspace_addr = (uint64_t)guest_mem;

    ret = ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region);
    if (ret < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        munmap(guest_mem, GUEST_MEM_SIZE);
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }
    printf("    Memory region set: GPA 0x%llx -> HVA %p\n",
           mem_region.guest_phys_addr, guest_mem);

    /* Step 4: Create vCPU */
    printf("[4] Creating vCPU...\n");
    vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    if (vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        munmap(guest_mem, GUEST_MEM_SIZE);
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }
    printf("    vCPU created (fd=%d)\n", vcpu_fd);

    /* Get size of shared kvm_run structure */
    vcpu_mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (vcpu_mmap_size < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        close(vcpu_fd);
        munmap(guest_mem, GUEST_MEM_SIZE);
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }

    /* Map kvm_run structure */
    run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE,
               MAP_SHARED, vcpu_fd, 0);
    if (run == MAP_FAILED) {
        perror("mmap kvm_run");
        close(vcpu_fd);
        munmap(guest_mem, GUEST_MEM_SIZE);
        close(vm_fd);
        close(kvm_fd);
        return 1;
    }

    /* Step 5: Initialize vCPU registers */
    printf("[5] Initializing vCPU registers...\n");

    /* TODO: Set PC, SP, and other RISC-V registers */
    /* This is the tricky part - RISC-V register setup */
    printf("    NOTE: Register initialization not yet implemented\n");
    printf("    TODO: Set PC=0x0, SP=0x1000\n");

    /* Step 6: Run vCPU */
    printf("[6] Running vCPU...\n");
    printf("    Guest will run until VM exit\n");
    printf("    Press Ctrl+C to stop\n\n");

    int iterations = 0;
    while (1) {
        ret = ioctl(vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                printf("\n    Interrupted by signal\n");
                break;
            }
            perror("KVM_RUN");
            break;
        }

        iterations++;

        /* Handle VM exits */
        switch (run->exit_reason) {
            case KVM_EXIT_HLT:
                printf("    Guest halted (HLT instruction)\n");
                goto cleanup;

            case KVM_EXIT_SHUTDOWN:
                printf("    Guest shutdown\n");
                goto cleanup;

            case KVM_EXIT_FAIL_ENTRY:
                fprintf(stderr, "    KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx\n",
                        run->fail_entry.hardware_entry_failure_reason);
                goto cleanup;

            case KVM_EXIT_INTERNAL_ERROR:
                fprintf(stderr, "    KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x\n",
                        run->internal.suberror);
                goto cleanup;

            default:
                printf("    Unhandled exit reason: %d\n", run->exit_reason);
                /* Show first few iterations for debugging */
                if (iterations < 10) {
                    printf("    (iteration %d)\n", iterations);
                    continue;  /* Try again */
                } else if (iterations == 10) {
                    printf("    Guest seems to be running...\n");
                    printf("    (suppressing further messages)\n");
                }

                /* Stop after many iterations (guest is probably looping) */
                if (iterations > 1000) {
                    printf("\n    Stopping after %d iterations\n", iterations);
                    printf("    (Guest is probably in infinite loop - this is expected!)\n");
                    goto cleanup;
                }
                break;
        }
    }

cleanup:
    printf("\n[7] Cleaning up...\n");
    munmap(run, vcpu_mmap_size);
    close(vcpu_fd);
    munmap(guest_mem, GUEST_MEM_SIZE);
    close(vm_fd);
    close(kvm_fd);

    printf("\nVMM exiting normally.\n");
    return 0;
}
