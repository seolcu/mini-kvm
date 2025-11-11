/*
 * Minimal KVM-based Virtual Machine Monitor (x86)
 *
 * This VMM creates a VM using Linux KVM API and runs a simple guest in Real Mode or Protected Mode.
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
#include "protected_mode.h"

// Guest memory configuration
#define GUEST_MEM_SIZE (4 << 20)  // 4MB (expandable for Protected Mode)
#define GUEST_LOAD_ADDR 0x0       // Load guest at address 0

// Mode selection
typedef enum {
    MODE_REAL,                    // 16-bit Real Mode
    MODE_PROTECTED                // 32-bit Protected Mode
} cpu_mode_t;

static cpu_mode_t cpu_mode = MODE_REAL;  // Default: Real Mode

// Hypercall interface
#define HYPERCALL_PORT 0x500      // Port for hypercalls

// Hypercall numbers
#define HC_EXIT       0x00        // Exit guest
#define HC_PUTCHAR    0x01        // Output character (BL = char)
#define HC_PUTNUM     0x02        // Output number (BX = number, decimal)
#define HC_NEWLINE    0x03        // Output newline

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
    long fsize_long = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize_long < 0) {
        perror("ftell");
        fclose(f);
        return -1;
    }

    size_t fsize = (size_t)fsize_long;

    printf("Guest binary size: %zu bytes\n", fsize);

    if (fsize > mem_size) {
        fprintf(stderr, "Guest binary too large (%zu bytes > %zu bytes)\n",
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
    size_t bytes_to_show = (fsize < 16 ? fsize : 16);
    for (size_t i = 0; i < bytes_to_show; i++) {
        printf("%02x ", ((unsigned char*)(mem + GUEST_LOAD_ADDR))[i]);
    }
    printf("\n");

    return 0;
}

/*
 * Initialize KVM and create VM
 */
static int init_kvm(void) {
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
 * Create a GDT entry
 */
static void create_gdt_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
                             uint8_t access, uint8_t flags) {
    entry->base_low = base & 0xFFFF;
    entry->base_mid = (base >> 16) & 0xFF;
    entry->base_high = (base >> 24) & 0xFF;
    entry->limit_low = limit & 0xFFFF;
    entry->access = access;
    entry->limit_granular = ((limit >> 16) & 0x0F) | (flags & 0xF0);
}

/*
 * Setup GDT in guest memory for Protected Mode
 */
static int setup_gdt(void) {
    if (cpu_mode != MODE_PROTECTED) {
        return 0;  // Skip GDT setup for Real Mode
    }

    gdt_entry_t *gdt = (gdt_entry_t *)(guest_mem + GDT_ADDR);

    // Entry 0: Null descriptor (required)
    create_gdt_entry(&gdt[0], 0, 0, 0, 0);

    // Entry 1: Kernel code segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[1], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);

    // Entry 2: Kernel data segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[2], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);

    // Entry 3: User code segment (32-bit, ring 3)
    gdt_entry_t *uc = &gdt[3];
    create_gdt_entry(uc, 0, 0xFFFFF, 0xFA, LIMIT_GRAN);  // Ring 3 code

    // Entry 4: User data segment (32-bit, ring 3)
    gdt_entry_t *ud = &gdt[4];
    create_gdt_entry(ud, 0, 0xFFFFF, 0xF2, LIMIT_GRAN);  // Ring 3 data

    printf("GDT setup: %d entries at 0x%x\n", GDT_SIZE, GDT_ADDR);
    return 0;
}

/*
 * Setup IDT in guest memory for Protected Mode
 */
static int setup_idt(void) {
    if (cpu_mode != MODE_PROTECTED) {
        return 0;  // Skip IDT setup for Real Mode
    }

    // Place IDT right after GDT
    uint32_t idt_addr = GDT_ADDR + GDT_TOTAL_SIZE;
    idt_entry_t *idt = (idt_entry_t *)(guest_mem + idt_addr);

    // Create a simple IDT with 256 entries (all pointing to dummy handler)
    // For now, just zero-initialize (invalid entries)
    memset(idt, 0, 256 * sizeof(idt_entry_t));

    printf("IDT setup at 0x%x\n", idt_addr);
    return 0;
}

/*
 * Create vCPU and initialize registers for Real Mode or Protected Mode
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
    int mmap_size_ret = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size_ret < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }
    mmap_size = (size_t)mmap_size_ret;

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

    if (cpu_mode == MODE_PROTECTED) {
        // 4a. Setup for Protected Mode
        // GDT must be ready before setting segment registers
        setup_gdt();
        setup_idt();

        // Set GDTR (GDT register)
        sregs.gdt.base = GDT_ADDR;
        sregs.gdt.limit = GDT_TOTAL_SIZE - 1;

        // Set IDTR (IDT register)
        sregs.idt.base = GDT_ADDR + GDT_TOTAL_SIZE;
        sregs.idt.limit = 255;  // 256 entries

        // Set kernel code segment (index 1 = 0x8)
        sregs.cs.selector = SEL_KCODE;
        sregs.cs.base = 0;
        sregs.cs.limit = 0xFFFFFFFF;  // Full 4GB limit (with granularity)
        sregs.cs.type = 11;        // Code segment
        sregs.cs.present = 1;
        sregs.cs.dpl = 0;          // Ring 0
        sregs.cs.db = 1;           // 32-bit mode
        sregs.cs.s = 1;            // Code/Data segment (not system)
        sregs.cs.l = 0;            // Not long mode
        sregs.cs.g = 1;            // Granularity = 4KB
        sregs.cs.avl = 0;

        // Set kernel data segment (index 2 = 0x10)
        sregs.ds.selector = SEL_KDATA;
        sregs.ds.base = 0;
        sregs.ds.limit = 0xFFFFFFFF;  // Full 4GB limit
        sregs.ds.type = 3;         // Data segment
        sregs.ds.present = 1;
        sregs.ds.dpl = 0;          // Ring 0
        sregs.ds.db = 1;           // 32-bit mode
        sregs.ds.s = 1;            // Code/Data segment
        sregs.ds.l = 0;            // Not long mode
        sregs.ds.g = 1;            // Granularity = 4KB
        sregs.ds.avl = 0;

        // Copy DS settings to ES, FS, GS, SS
        sregs.es = sregs.fs = sregs.gs = sregs.ss = sregs.ds;

        // Set SS (stack segment) selector explicitly
        sregs.ss.selector = SEL_KDATA;

        // Enable Protected Mode (set PE flag in CR0)
        sregs.cr0 |= 0x1;          // CR0.PE = 1

        printf("Set Protected Mode segment registers\n");
    } else {
        // 4b. Setup for Real Mode
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

        printf("Set Real Mode segment registers\n");
    }

    if (ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS");
        return -1;
    }

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
 * Handle hypercall from guest
 * Returns: 0 = continue, 1 = exit guest
 */
static int handle_hypercall(struct kvm_regs *regs) {
    unsigned char hc_num = regs->rax & 0xFF;  // AL = hypercall number

    switch (hc_num) {
        case HC_EXIT:
            // Guest requested exit
            printf("[Hypercall] Guest exit request\n");
            return 1;  // Signal to exit

        case HC_PUTCHAR: {
            // Output single character
            char ch = regs->rbx & 0xFF;  // BL = character
            putchar(ch);
            fflush(stdout);
            break;
        }

        case HC_PUTNUM: {
            // Output number in decimal
            unsigned short num = regs->rbx & 0xFFFF;  // BX = number
            printf("%u", num);
            fflush(stdout);
            break;
        }

        case HC_NEWLINE:
            // Output newline
            putchar('\n');
            fflush(stdout);
            break;

        default:
            fprintf(stderr, "[Hypercall] Unknown hypercall: 0x%02x\n", hc_num);
            return -1;
    }

    return 0;  // Continue execution
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

            case KVM_EXIT_IO: {
                // Handle port I/O
                char *data = (char *)kvm_run + kvm_run->io.data_offset;

                if (kvm_run->io.direction == KVM_EXIT_IO_OUT) {
                    // OUT instruction: guest writing to port
                    if (kvm_run->io.port == HYPERCALL_PORT) {
                        // Hypercall port - get registers and handle
                        struct kvm_regs regs;
                        if (ioctl(vcpu_fd, KVM_GET_REGS, &regs) < 0) {
                            perror("KVM_GET_REGS");
                            return -1;
                        }

                        int hc_result = handle_hypercall(&regs);
                        if (hc_result == 1) {
                            // Guest requested exit
                            return 0;
                        } else if (hc_result < 0) {
                            // Hypercall error
                            return -1;
                        }
                    } else if (kvm_run->io.port == 0x3f8) {
                        // UART COM1 port - output character
                        for (int i = 0; i < kvm_run->io.size; i++) {
                            putchar(data[i]);
                        }
                        fflush(stdout);
                    } else {
                        // Unknown port
                        printf("VM Exit #%d: OUT to unknown port 0x%x\n",
                               exit_count, kvm_run->io.port);
                    }
                } else {
                    // IN instruction: guest reading from port
                    printf("VM Exit #%d: IN from port 0x%x (not implemented)\n",
                           exit_count, kvm_run->io.port);
                }
                break;
            }

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
        if (exit_count > 1000) {
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

    // Parse command line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-p|--protected] <guest_binary>\n", argv[0]);
        fprintf(stderr, "  -p, --protected    Run in Protected Mode (default: Real Mode)\n");
        return 1;
    }

    // Check for mode selection flag
    if (argc >= 3 && (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "--protected") == 0)) {
        cpu_mode = MODE_PROTECTED;
    } else if (argc >= 3 && (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "--real") == 0)) {
        cpu_mode = MODE_REAL;
    }

    // Get guest binary filename (last argument)
    const char *guest_binary = argv[argc - 1];

    // Check if a flag was used
    if (cpu_mode != MODE_REAL && argc < 3) {
        fprintf(stderr, "Error: Invalid arguments\n");
        return 1;
    }

    printf("=== Minimal KVM VMM (x86");
    if (cpu_mode == MODE_PROTECTED) {
        printf(" - Protected Mode) ===\n\n");
    } else {
        printf(" - Real Mode) ===\n\n");
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
    if (load_guest_binary(guest_binary, guest_mem, GUEST_MEM_SIZE) < 0) {
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
