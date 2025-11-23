/*
 * Minimal KVM-based Virtual Machine Monitor (x86)
 *
 * This VMM creates a VM using Linux KVM API and runs a simple guest in Real Mode or Protected Mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#include <errno.h>
#include <pthread.h>
#include "protected_mode.h"

// Guest memory configuration
#define GUEST_MEM_SIZE (4 << 20)  // 4MB (expandable for Protected Mode)
#define GUEST_LOAD_ADDR 0x0       // Load guest at address 0

// Mode selection
// Hypercall interface
#define HYPERCALL_PORT 0x500      // Port for hypercalls

// Hypercall numbers (must match 1K OS syscall numbers)
#define HC_EXIT       0x00        // Exit guest
#define HC_PUTCHAR    0x01        // Output character (BL = char)
#define HC_GETCHAR    0x02        // Input character (returns in AL)

// Multi-vCPU configuration
#define MAX_VCPUS 4               // Maximum number of vCPUs

// Keyboard buffer for interrupt-based input
#define KEYBOARD_BUFFER_SIZE 256
typedef struct {
    char buffer[KEYBOARD_BUFFER_SIZE];
    int head;  // Write position
    int tail;  // Read position
    pthread_mutex_t lock;
} keyboard_buffer_t;

static keyboard_buffer_t keyboard_buffer = {
    .head = 0,
    .tail = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

// Stdin monitoring thread
static pthread_t stdin_thread;
static bool stdin_thread_running = false;

// Timer thread
static pthread_t timer_thread;
static bool timer_thread_running = false;
static volatile int timer_ticks = 0;

// Per-vCPU context structure
typedef struct {
    int vcpu_id;                  // vCPU index (0, 1, 2, 3)
    int vcpu_fd;                  // KVM vCPU file descriptor
    struct kvm_run *kvm_run;      // Per-vCPU run structure
    void *guest_mem;              // Per-guest memory region
    size_t mem_size;              // Memory size (4MB default)
    size_t kvm_run_mmap_size;     // Size of kvm_run mmap region
    const char *guest_binary;     // Binary filename
    char name[256];               // Display name (e.g., "multiplication")
    int exit_count;               // VM exit counter
    bool running;                 // Execution state
    bool use_paging;              // Enable Protected Mode with paging (for 1K OS)
    uint32_t entry_point;         // Entry point address (EIP)
    uint32_t load_offset;         // Offset to load binary in guest memory
    int pending_getchar;          // GETCHAR request pending (0=no, 1=yes)
    int getchar_result;           // Cached GETCHAR result for IN instruction
} vcpu_context_t;

// Global KVM state (shared across vCPUs)
static int kvm_fd = -1;           // /dev/kvm file descriptor
static int vm_fd = -1;            // VM instance (one VM, multiple vCPUs)

// vCPU array
static vcpu_context_t vcpus[MAX_VCPUS];
static int num_vcpus = 0;

// Thread synchronization
static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Verbose logging control
static bool verbose = false;

/*
 * Thread-safe output functions with vCPU identification
 */
static void vcpu_printf(vcpu_context_t *ctx, const char *fmt, ...) {
    pthread_mutex_lock(&stdout_mutex);

    // Use color only for multi-vCPU (num_vcpus > 1)
    if (num_vcpus > 1) {
        // Cyan, Green, Yellow, Blue (avoid red - looks like error)
        const char *colors[] = {"\033[36m", "\033[32m", "\033[33m", "\033[34m"};
        const char *reset = "\033[0m";

        printf("%s[vCPU %d:%s]%s ",
               colors[ctx->vcpu_id % 4],
               ctx->vcpu_id,
               ctx->name,
               reset);
    } else {
        // Single vCPU: no color, simple prefix
        printf("[%s] ", ctx->name);
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);

    pthread_mutex_unlock(&stdout_mutex);
}

/*
 * Output single character with color for vCPU identification
 */
static void vcpu_putchar(vcpu_context_t *ctx, char ch) {
    pthread_mutex_lock(&stdout_mutex);

    // Use color only for multi-vCPU (num_vcpus > 1)
    if (num_vcpus > 1) {
        // Cyan, Green, Yellow, Blue (avoid red - looks like error)
        const char *colors[] = {"\033[36m", "\033[32m", "\033[33m", "\033[34m"};
        const char *reset = "\033[0m";
        printf("%s%c%s", colors[ctx->vcpu_id % 4], ch, reset);
    } else {
        // Single vCPU: no color
        putchar(ch);
    }
    fflush(stdout);

    pthread_mutex_unlock(&stdout_mutex);
}

/*
 * Load guest binary into guest memory
 */
static int load_guest_binary(const char *filename, void *mem, size_t mem_size, uint32_t load_offset) {
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

    if (fsize + load_offset > mem_size) {
        fprintf(stderr, "Guest binary too large (%zu bytes at offset 0x%x > %zu bytes)\n",
                fsize, load_offset, mem_size);
        fclose(f);
        return -1;
    }

    // Load binary at specified offset
    size_t nread = fread(mem + load_offset, 1, fsize, f);
    if (nread != fsize) {
        perror("fread");
        fclose(f);
        return -1;
    }

    fclose(f);

    printf("Loaded guest binary: %zu bytes at offset 0x%x\n", nread, load_offset);

    // Show first few bytes
    printf("First bytes: ");
    size_t bytes_to_show = (fsize < 16 ? fsize : 16);
    for (size_t i = 0; i < bytes_to_show; i++) {
        printf("%02x ", ((unsigned char*)(mem + load_offset))[i]);
    }
    printf("\n");

    return 0;
}

/*
 * Keyboard buffer helper functions
 */
static void keyboard_buffer_push(char ch) {
    pthread_mutex_lock(&keyboard_buffer.lock);
    int next_head = (keyboard_buffer.head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_head != keyboard_buffer.tail) {
        keyboard_buffer.buffer[keyboard_buffer.head] = ch;
        keyboard_buffer.head = next_head;
    }
    pthread_mutex_unlock(&keyboard_buffer.lock);
}

static int keyboard_buffer_pop(void) {
    pthread_mutex_lock(&keyboard_buffer.lock);
    if (keyboard_buffer.head == keyboard_buffer.tail) {
        pthread_mutex_unlock(&keyboard_buffer.lock);
        return -1;  // Empty
    }
    char ch = keyboard_buffer.buffer[keyboard_buffer.tail];
    keyboard_buffer.tail = (keyboard_buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
    pthread_mutex_unlock(&keyboard_buffer.lock);
    return (unsigned char)ch;
}

/*
 * Timer thread - generates periodic timer interrupts
 * Injects IRQ0 every 10ms to all vCPUs
 */
static void *timer_thread_func(void *arg) {
    (void)arg;

    printf("[Timer] Timer thread started (10ms period)\n");

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    while (timer_thread_running) {
        // Sleep for 100ms (reduced frequency to allow more interactive use)
        usleep(100000);

        timer_ticks++;

        // Inject timer interrupt (IRQ0) to all vCPUs
        for (int i = 0; i < num_vcpus; i++) {
            if (vcpus[i].running) {
                struct kvm_interrupt irq;
                irq.irq = 0x20;  // Vector 0x20 = timer interrupt (IRQ0)

                if (ioctl(vcpus[i].vcpu_fd, KVM_INTERRUPT, &irq) < 0) {
                    // Interrupt might fail - just continue
                }
            }
        }
    }

    printf("[Timer] Timer thread stopped\n");
    return NULL;
}

/*
 * Stdin monitoring thread - reads from stdin and injects keyboard interrupts
 * This thread runs continuously and injects IRQ1 when a key is pressed
 */
static void *stdin_monitor_thread_func(void *arg) {
    (void)arg;

    printf("[Keyboard] Stdin monitoring thread started\n");

    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    while (stdin_thread_running) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms timeout

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            int ch = getchar();
            if (ch != EOF) {
                // Store character in buffer
                keyboard_buffer_push((char)ch);

                // Inject keyboard interrupt (IRQ1) to vCPU 0
                // Only vCPU 0 receives keyboard input (as per meeting notes)
                if (num_vcpus > 0 && vcpus[0].running) {
                    struct kvm_interrupt irq;
                    irq.irq = 0x21;  // Vector 0x21 = keyboard interrupt

                    if (ioctl(vcpus[0].vcpu_fd, KVM_INTERRUPT, &irq) < 0) {
                        // Interrupt might fail if guest is in incompatible state
                        // Just continue - guest will eventually receive it
                    }
                }
            }
        }
    }

    // Restore stdin blocking mode
    fcntl(STDIN_FILENO, F_SETFL, flags);

    printf("[Keyboard] Stdin monitoring thread stopped\n");
    return NULL;
}

/*
 * Initialize KVM and create VM
 * need_irqchip: true for Protected Mode (needs interrupts), false for Real Mode
 */
static int init_kvm(bool need_irqchip) {
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

    // 4. Create interrupt controller (IRQCHIP) only if needed
    // Real Mode guests don't need interrupts, Protected Mode does
    if (need_irqchip) {
        if (ioctl(vm_fd, KVM_CREATE_IRQCHIP) < 0) {
            perror("KVM_CREATE_IRQCHIP");
            fprintf(stderr, "Warning: Interrupt controller creation failed. Interrupts disabled.\n");
            // Continue anyway - hypercall-based I/O will still work
        } else {
            printf("Created interrupt controller (IRQCHIP)\n");
        }
    }

    return 0;
}

/*
 * Allocate and map guest memory for a specific vCPU context
 * Real Mode limitation: Each vCPU gets 256KB at offset vcpu_id * 256KB
 * This allows 4 vCPUs within the 1MB Real Mode address space
 */
static int setup_vcpu_memory(vcpu_context_t *ctx) {
    struct kvm_userspace_memory_region mem_region;

    // Use 4MB per vCPU for 1K OS (with paging), 256KB for Real Mode guests
    if (ctx->use_paging) {
        ctx->mem_size = 4 * 1024 * 1024;  // 4MB for Protected Mode
    } else {
        ctx->mem_size = 256 * 1024;       // 256KB for Real Mode (fits in 64K segment)
    }

    // Allocate memory for this vCPU's guest
    ctx->guest_mem = mmap(NULL, ctx->mem_size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctx->guest_mem == MAP_FAILED) {
        perror("mmap vcpu guest_mem");
        return -1;
    }

    vcpu_printf(ctx, "Allocated guest memory: %zu KB at %p\n",
                ctx->mem_size / 1024, ctx->guest_mem);

    // Tell KVM about this memory region
    // Each vCPU uses different GPA range: vCPU 0 at 0x0, vCPU 1 at 0x400000 (4MB), etc.
    mem_region.slot = ctx->vcpu_id;  // Use vCPU ID as slot number
    mem_region.flags = 0;
    mem_region.guest_phys_addr = ctx->vcpu_id * ctx->mem_size;  // Offset by 4MB
    mem_region.memory_size = ctx->mem_size;
    mem_region.userspace_addr = (unsigned long)ctx->guest_mem;

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    vcpu_printf(ctx, "Mapped to slot %d: GPA 0x%lx -> HVA %p (%zu bytes)\n",
                ctx->vcpu_id, mem_region.guest_phys_addr, ctx->guest_mem, ctx->mem_size);

    return 0;
}

/*
 * Setup page tables for Protected Mode with paging (for 1K OS)
 * Uses 2-level page tables with 4MB pages (PSE enabled)
 */
static int setup_page_tables(vcpu_context_t *ctx) {
    // Page directory at GPA 0x00100000 (1MB offset)
    const uint32_t page_dir_offset = 0x00100000;

    if (page_dir_offset >= ctx->mem_size) {
        vcpu_printf(ctx, "Error: Page directory offset exceeds memory size\n");
        return -1;
    }

    uint32_t *page_dir = (uint32_t *)(ctx->guest_mem + page_dir_offset);
    memset(page_dir, 0, 4096);  // Clear page directory

    // Identity map first 4MB using 4MB pages (PSE)
    // This covers: 0x00000000 - 0x003FFFFF physical memory
    page_dir[0] = 0x00000083;  // Present, R/W, 4MB page at physical 0x0

    // Map kernel space: 0x80000000 - 0x803FFFFF -> 0x00000000 - 0x003FFFFF
    // PDE index for 0x80000000 is 512 (0x80000000 >> 22 = 512)
    page_dir[512] = 0x00000083;  // Present, R/W, 4MB page at physical 0x0

    // Map additional kernel space if available
    // For 4MB memory, we also map PDE[513] to handle addresses at the boundary (0x80400000)
    if (ctx->mem_size >= 4 * 1024 * 1024) {
        // Map 0x80400000-0x807FFFFF, but it aliases to 0x0-0x3FFFFF (wrap around)
        // This is fine for boundary addresses like __free_ram_end
        page_dir[513] = 0x00000083;  // Map to same physical memory (alias)
    }

    vcpu_printf(ctx, "Page directory at GPA 0x%x (identity + kernel mapping)\n",
                page_dir_offset);
    vcpu_printf(ctx, "  PDE[0]   = 0x%08x (physical 0x0 - 0x3FFFFF)\n", page_dir[0]);
    vcpu_printf(ctx, "  PDE[512] = 0x%08x (virtual 0x80000000 -> physical 0x0)\n", page_dir[512]);

    return page_dir_offset;  // Return offset for CR3
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
 * Called when paging is enabled
 */
static int setup_gdt(void *guest_mem_ptr) {
    gdt_entry_t *gdt = (gdt_entry_t *)(guest_mem_ptr + GDT_ADDR);

    // Entry 0: Null descriptor (required)
    create_gdt_entry(&gdt[0], 0, 0, 0, 0);

    // Entry 1: Kernel code segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[1], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);

    // Entry 2: Kernel data segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[2], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);

    // Entry 3: User code segment (32-bit, ring 3)
    create_gdt_entry(&gdt[3], 0, 0xFFFFF, 0xFA, LIMIT_GRAN);  // Ring 3 code

    // Entry 4: User data segment (32-bit, ring 3)
    create_gdt_entry(&gdt[4], 0, 0xFFFFF, 0xF2, LIMIT_GRAN);  // Ring 3 data

    printf("GDT setup: %d entries at 0x%x\n", GDT_SIZE, GDT_ADDR);
    return 0;
}

/*
 * Setup IDT in guest memory for Protected Mode
 * Called when paging is enabled
 */
static int setup_idt(void *guest_mem_ptr) {
    // Place IDT right after GDT
    uint32_t idt_addr = GDT_ADDR + GDT_TOTAL_SIZE;
    idt_entry_t *idt = (idt_entry_t *)(guest_mem_ptr + idt_addr);

    // Create a simple IDT with 256 entries (all pointing to dummy handler)
    // For now, just zero-initialize (invalid entries)
    memset(idt, 0, 256 * sizeof(idt_entry_t));

    printf("IDT setup at 0x%x\n", idt_addr);
    return 0;
}

#if 0  // OLD SINGLE-VCPU CODE (disabled)

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
#endif  // OLD SINGLE-VCPU CODE

/*
 * Setup segment registers for Real Mode
 */
static void setup_realmode_segments(struct kvm_sregs *sregs, vcpu_context_t *ctx) {
    // CS:IP must point to the vCPU's memory region
    // Physical address = CS * 16 + IP
    // For vCPU 0: GPA 0x00000 (CS = 0x0000, IP = 0x0)
    // For vCPU 1: GPA 0x40000 (CS = 0x4000, IP = 0x0)  (256KB spacing)
    // For vCPU 2: GPA 0x80000 (CS = 0x8000, IP = 0x0)
    // For vCPU 3: GPA 0xC0000 (CS = 0xC000, IP = 0x0)
    uint16_t cs_value = ctx->vcpu_id * (ctx->mem_size / 16);

    sregs->cs.base = cs_value * 16;
    sregs->cs.selector = cs_value;
    sregs->cs.limit = 0xFFFF;
    sregs->cs.type = 0x9b;
    sregs->cs.present = 1;
    sregs->cs.dpl = 0;
    sregs->cs.db = 0;
    sregs->cs.s = 1;
    sregs->cs.l = 0;
    sregs->cs.g = 0;
    sregs->cs.avl = 0;

    sregs->ds.base = 0;
    sregs->ds.selector = 0;
    sregs->ds.limit = 0xFFFF;
    sregs->ds.type = 0x93;
    sregs->ds.present = 1;
    sregs->ds.dpl = 0;
    sregs->ds.db = 0;
    sregs->ds.s = 1;
    sregs->ds.l = 0;
    sregs->ds.g = 0;
    sregs->ds.avl = 0;

    sregs->es = sregs->fs = sregs->gs = sregs->ss = sregs->ds;
}

/*
 * Setup segment registers for Protected Mode with paging
 */
static void setup_protectedmode_segments(struct kvm_sregs *sregs) {
    // Code segment (flat, base=0, limit=4GB)
    sregs->cs.base = 0;
    sregs->cs.limit = 0xFFFFFFFF;
    sregs->cs.selector = 0x08;
    sregs->cs.type = 0x0B;
    sregs->cs.present = 1;
    sregs->cs.dpl = 0;
    sregs->cs.db = 1;
    sregs->cs.s = 1;
    sregs->cs.l = 0;
    sregs->cs.g = 1;
    sregs->cs.avl = 0;

    // Data segment (flat, base=0, limit=4GB)
    sregs->ds.base = 0;
    sregs->ds.limit = 0xFFFFFFFF;
    sregs->ds.selector = 0x10;
    sregs->ds.type = 0x03;
    sregs->ds.present = 1;
    sregs->ds.dpl = 0;
    sregs->ds.db = 1;
    sregs->ds.s = 1;
    sregs->ds.l = 0;
    sregs->ds.g = 1;
    sregs->ds.avl = 0;

    sregs->es = sregs->fs = sregs->gs = sregs->ss = sregs->ds;
}

/*
 * Configure vCPU for Protected Mode with paging
 */
static int configure_protected_mode(vcpu_context_t *ctx) {
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // Setup GDT and IDT in guest memory
    setup_gdt(ctx->guest_mem);
    setup_idt(ctx->guest_mem);

    // Setup page tables
    int page_dir_offset = setup_page_tables(ctx);
    if (page_dir_offset < 0) {
        return -1;
    }

    // Get current segment registers
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
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

    // Enable Protected Mode and Paging
    sregs.cr0 |= 0x00000001;  // PE bit
    sregs.cr0 |= 0x80000000;  // PG bit
    sregs.cr4 |= 0x00000010;  // PSE bit
    sregs.cr4 &= ~0x00000020; // Clear PAE bit

    // Setup flat segments
    setup_protectedmode_segments(&sregs);

    vcpu_printf(ctx, "About to set sregs: CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
               sregs.cr0, sregs.cr3, sregs.cr4);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS (paging)");
        return -1;
    }

    vcpu_printf(ctx, "Successfully set sregs for paging\n");

    // Update RIP to entry point
    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->entry_point;
    regs.rflags = 0x2;

    vcpu_printf(ctx, "About to set regs: RIP=0x%x\n", ctx->entry_point);

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS (paging)");
        return -1;
    }

    vcpu_printf(ctx, "Successfully set regs, verifying...\n");

    // Verify the settings
    struct kvm_sregs verify_sregs;
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &verify_sregs) == 0) {
        vcpu_printf(ctx, "Verified: CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                   verify_sregs.cr0, verify_sregs.cr3, verify_sregs.cr4);
    }

    vcpu_printf(ctx, "Enabled paging: CR3=0x%llx, EIP=0x%x (Protected Mode)\n",
                sregs.cr3, ctx->entry_point);

    return 0;
}

/*
 * Setup vCPU context (multi-vCPU version)
 */
static int setup_vcpu_context(vcpu_context_t *ctx) {
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int mmap_size_ret;

    // Create vCPU
    ctx->vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, ctx->vcpu_id);
    if (ctx->vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        return -1;
    }

    vcpu_printf(ctx, "Created vCPU (fd=%d)\n", ctx->vcpu_fd);

    // Get kvm_run structure size and mmap it
    mmap_size_ret = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size_ret < 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }
    ctx->kvm_run_mmap_size = (size_t)mmap_size_ret;

    ctx->kvm_run = mmap(NULL, ctx->kvm_run_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ctx->vcpu_fd, 0);
    if (ctx->kvm_run == MAP_FAILED) {
        perror("mmap kvm_run");
        return -1;
    }

    vcpu_printf(ctx, "Mapped kvm_run structure: %zu bytes\n", ctx->kvm_run_mmap_size);

    // Get current segment registers
    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        perror("KVM_GET_SREGS");
        return -1;
    }

    // Setup Real Mode segments
    setup_realmode_segments(&sregs, ctx);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
        perror("KVM_SET_SREGS");
        return -1;
    }

    // Set general purpose registers
    memset(&regs, 0, sizeof(regs));
    regs.rip = GUEST_LOAD_ADDR;
    regs.rflags = 0x2;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS");
        return -1;
    }

    vcpu_printf(ctx, "Set registers: RIP=0x%llx (Real Mode)\n", regs.rip);

    // Set MP state to runnable
    struct kvm_mp_state mp_state;
    mp_state.mp_state = KVM_MP_STATE_RUNNABLE;
    if (ioctl(ctx->vcpu_fd, KVM_SET_MP_STATE, &mp_state) < 0) {
        perror("KVM_SET_MP_STATE");
        return -1;
    }

    // If paging is enabled, switch to Protected Mode
    if (ctx->use_paging) {
        if (configure_protected_mode(ctx) < 0) {
            return -1;
        }
    }

    ctx->running = true;
    ctx->exit_count = 0;

    return 0;
}

/*
 * Handle hypercall OUT request
 */
static int handle_hypercall_out(vcpu_context_t *ctx, struct kvm_regs *regs) {
    unsigned char hc_num = regs->rax & 0xFF;

    // Log hypercalls if verbose mode is enabled
    if (verbose) {
        static int hc_count = 0;
        if (hc_count++ < 100) {
            vcpu_printf(ctx, "HC[%d] type=0x%02x RAX=0x%llx RBX=0x%llx\n",
                       hc_count, hc_num, regs->rax, regs->rbx);
        }
    }
    
    switch (hc_num) {
        case HC_EXIT:
            vcpu_printf(ctx, "Exit request\n");
            ctx->running = false;
            return 0;

        case HC_PUTCHAR: {
            char ch = regs->rbx & 0xFF;
            vcpu_putchar(ctx, ch);
            break;
        }

        case HC_GETCHAR: {
            int ch = keyboard_buffer_pop();
            ctx->getchar_result = ch;
            ctx->pending_getchar = 1;
            break;
        }

        default:
            vcpu_printf(ctx, "Unknown hypercall: 0x%02x\n", hc_num);
            return -1;
    }
    
    return 0;
}

/*
 * Handle hypercall IN response
 */
static void handle_hypercall_in(vcpu_context_t *ctx, char *data) {
    if (ctx->pending_getchar) {
        data[0] = (ctx->getchar_result == -1) ? 0xFF : (unsigned char)ctx->getchar_result;
        
        if (verbose) {
            static int in_count = 0;
            if (in_count++ < 50) {
                vcpu_printf(ctx, "IN[%d] from 0x500: returning ch=%d (0x%02x)\n", 
                           in_count, ctx->getchar_result, data[0]);
            }
        }
        ctx->pending_getchar = 0;
    } else {
        if (verbose) {
            static int unexpected_in = 0;
            if (unexpected_in++ < 20) {
                vcpu_printf(ctx, "WARN[%d]: IN from 0x500 without pending_getchar!\n", unexpected_in);
            }
        }
        data[0] = 0;
    }
}

/*
 * Handle I/O port operations
 */
static int handle_io(vcpu_context_t *ctx) {
    char *data = (char *)ctx->kvm_run + ctx->kvm_run->io.data_offset;

    // Log I/O operations if verbose mode is enabled
    if (verbose) {
        static int io_count = 0;
        if (io_count++ < 100) {
            vcpu_printf(ctx, "IO[%d]: dir=%s port=0x%x size=%d\n",
                       io_count,
                       (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT) ? "OUT" : "IN",
                       ctx->kvm_run->io.port,
                       ctx->kvm_run->io.size);
        }
    }

    if (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT) {
        if (ctx->kvm_run->io.port == HYPERCALL_PORT) {
            struct kvm_regs regs;
            if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) < 0) {
                perror("KVM_GET_REGS");
                return -1;
            }
            return handle_hypercall_out(ctx, &regs);
        } else if (ctx->kvm_run->io.port == 0x3f8) {
            // UART output
            pthread_mutex_lock(&stdout_mutex);
            for (int i = 0; i < ctx->kvm_run->io.size; i++) {
                putchar(data[i]);
            }
            fflush(stdout);
            pthread_mutex_unlock(&stdout_mutex);
        }
    } else {
        // IN instruction
        if (ctx->kvm_run->io.port == HYPERCALL_PORT) {
            handle_hypercall_in(ctx, data);
        }
    }
    
    return 0;
}

/*
 * Handle VM exit for a specific vCPU context
 */
static int handle_vm_exit(vcpu_context_t *ctx) {
    ctx->exit_count++;

    // Log exit reasons if verbose mode is enabled
    if (verbose) {
        static int debug_exit_count = 0;
        if (debug_exit_count++ < 110) {
            if (ctx->kvm_run->exit_reason != KVM_EXIT_IO || debug_exit_count > 100) {
                vcpu_printf(ctx, "EXIT[%d]: reason=%d\n", debug_exit_count, ctx->kvm_run->exit_reason);
            }
        }
    }

    switch (ctx->kvm_run->exit_reason) {
        case KVM_EXIT_HLT:
            vcpu_printf(ctx, "Guest halted after %d exits\n", ctx->exit_count);
            ctx->running = false;
            return 0;

        case KVM_EXIT_IO:
            return handle_io(ctx);

        case KVM_EXIT_FAIL_ENTRY:
            vcpu_printf(ctx, "FAIL_ENTRY: reason 0x%llx\n",
                       ctx->kvm_run->fail_entry.hardware_entry_failure_reason);
            return -1;

        case KVM_EXIT_INTERNAL_ERROR:
            vcpu_printf(ctx, "INTERNAL_ERROR: suberror 0x%x\n",
                       ctx->kvm_run->internal.suberror);
            return -1;

        case KVM_EXIT_SHUTDOWN: {
            struct kvm_regs regs;
            struct kvm_sregs sregs;
            if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) == 0) {
                vcpu_printf(ctx, "SHUTDOWN at RIP=0x%llx, RSP=0x%llx\n", regs.rip, regs.rsp);
                vcpu_printf(ctx, "  RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n",
                           regs.rax, regs.rbx, regs.rcx, regs.rdx);
            }
            if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) == 0) {
                vcpu_printf(ctx, "  CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                           sregs.cr0, sregs.cr3, sregs.cr4);
                vcpu_printf(ctx, "  CS=0x%x DS=0x%x\n", sregs.cs.selector, sregs.ds.selector);
            }
            ctx->running = false;
            return 0;
        }

        default:
            vcpu_printf(ctx, "Unknown exit reason: %d\n", ctx->kvm_run->exit_reason);
            return -1;
    }

    // Safety limit (disabled for Protected Mode to allow indefinite interactive use with 1K OS)
    if (!ctx->use_paging && ctx->exit_count > 100000) {
        vcpu_printf(ctx, "Too many exits (%d), stopping\n", ctx->exit_count);
        return -1;
    }

    return 0;
}

/*
 * vCPU thread entry point
 */
static void *vcpu_thread(void *arg) {
    vcpu_context_t *ctx = (vcpu_context_t *)arg;
    int ret;

    vcpu_printf(ctx, "Thread started\n");

    while (ctx->running) {
        ret = ioctl(ctx->vcpu_fd, KVM_RUN, 0);
        if (ret < 0) {
            vcpu_printf(ctx, "KVM_RUN failed: %s\n", strerror(errno));
            break;
        }

        if (handle_vm_exit(ctx) < 0) {
            break;
        }
    }

    vcpu_printf(ctx, "Thread exiting (total exits: %d)\n", ctx->exit_count);
    return NULL;
}

/*
 * Cleanup vCPU resources
 */
static void cleanup_vcpu(vcpu_context_t *ctx) {
    if (ctx->kvm_run != NULL && ctx->kvm_run != MAP_FAILED) {
        munmap(ctx->kvm_run, ctx->kvm_run_mmap_size);
    }
    if (ctx->guest_mem != NULL && ctx->guest_mem != MAP_FAILED) {
        munmap(ctx->guest_mem, ctx->mem_size);
    }
    if (ctx->vcpu_fd >= 0) {
        close(ctx->vcpu_fd);
    }
}

#if 0  // OLD SINGLE-VCPU CLEANUP (disabled)
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
#endif  // OLD SINGLE-VCPU CLEANUP

/*
 * Extract guest name from binary filename
 */
static const char *extract_guest_name(const char *filename) {
    const char *name = strrchr(filename, '/');
    if (name) {
        name++;  // Skip '/'
    } else {
        name = filename;
    }

    // Remove .bin extension if present
    static char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s", name);

    char *dot = strrchr(name_buf, '.');
    if (dot && strcmp(dot, ".bin") == 0) {
        *dot = '\0';
    }

    return name_buf;
}

int main(int argc, char **argv) {
    int ret = 0;
    pthread_t threads[MAX_VCPUS];
    bool enable_paging = false;
    uint32_t entry_point = 0x80001000;  // Default entry point for paging mode
    uint32_t load_offset = 0x1000;      // Default load offset for paging mode
    int guest_arg_start = 1;

    // Parse command line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--paging [--entry ADDR] [--load OFFSET]] [--verbose] <guest1.bin> [guest2.bin] [guest3.bin] [guest4.bin]\n", argv[0]);
        fprintf(stderr, "  Run 1-4 guests simultaneously in separate vCPUs\n");
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  --paging            Enable Protected Mode with paging\n");
        fprintf(stderr, "  --entry ADDR        Set entry point (default: 0x80001000)\n");
        fprintf(stderr, "  --load OFFSET       Set load offset (default: 0x1000)\n");
        fprintf(stderr, "  --verbose           Enable debug logging (VM exits, I/O, hypercalls)\n");
        fprintf(stderr, "\nExample:\n");
        fprintf(stderr, "  %s guest/multiplication.bin guest/counter.bin\n", argv[0]);
        fprintf(stderr, "  %s --paging --verbose os-1k/test_kernel.bin\n", argv[0]);
        return 1;
    }

    // Parse flags
    for (int i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "--paging") == 0) {
            enable_paging = true;
            guest_arg_start++;
        } else if (strcmp(argv[i], "--entry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --entry requires an argument\n");
                return 1;
            }
            entry_point = strtoul(argv[i + 1], NULL, 0);
            i++;
            guest_arg_start += 2;
        } else if (strcmp(argv[i], "--load") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --load requires an argument\n");
                return 1;
            }
            load_offset = strtoul(argv[i + 1], NULL, 0);
            i++;
            guest_arg_start += 2;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
            guest_arg_start++;
        } else {
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            return 1;
        }
    }

    // Determine number of guests
    num_vcpus = argc - guest_arg_start;
    if (num_vcpus == 0) {
        fprintf(stderr, "Error: No guest binaries specified\n");
        return 1;
    }
    if (num_vcpus > MAX_VCPUS) {
        fprintf(stderr, "Error: Too many guests (max %d)\n", MAX_VCPUS);
        return 1;
    }

    printf("=== Multi-vCPU KVM VMM (x86) ===\n");
    if (enable_paging) {
        printf("Mode: Protected Mode with Paging\n");
        printf("Entry point: 0x%x\n", entry_point);
        printf("Load offset: 0x%x\n", load_offset);
    } else {
        printf("Mode: Real Mode\n");
    }
    printf("Starting %d vCPU(s)\n\n", num_vcpus);

    // Step 1: Initialize KVM and create VM
    // Only create IRQCHIP for Protected Mode (paging enabled)
    if (init_kvm(enable_paging) < 0) {
        ret = 1;
        goto cleanup_early;
    }

    // Step 2: Setup each vCPU
    for (int i = 0; i < num_vcpus; i++) {
        vcpu_context_t *ctx = &vcpus[i];

        // Initialize context
        memset(ctx, 0, sizeof(*ctx));
        ctx->vcpu_id = i;
        ctx->guest_binary = argv[guest_arg_start + i];
        snprintf(ctx->name, sizeof(ctx->name), "%s", extract_guest_name(ctx->guest_binary));
        ctx->vcpu_fd = -1;

        // Set paging mode settings
        ctx->use_paging = enable_paging;
        ctx->entry_point = entry_point;
        ctx->load_offset = enable_paging ? load_offset : 0;

        printf("[Setup vCPU %d: %s]\n", i, ctx->name);

        // Allocate and map memory for this vCPU
        if (setup_vcpu_memory(ctx) < 0) {
            ret = 1;
            goto cleanup_vcpus;
        }

        // Load guest binary into this vCPU's memory
        if (load_guest_binary(ctx->guest_binary, ctx->guest_mem, ctx->mem_size, ctx->load_offset) < 0) {
            ret = 1;
            goto cleanup_vcpus;
        }

        // Create and initialize vCPU
        if (setup_vcpu_context(ctx) < 0) {
            ret = 1;
            goto cleanup_vcpus;
        }

        printf("\n");
    }

    // Step 3: Start timer and stdin threads (only for Protected Mode with paging)
    // Real Mode guests don't use interrupts, so skip these threads
    if (enable_paging) {
        timer_thread_running = true;
        if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) {
            fprintf(stderr, "Warning: Failed to create timer thread. Timer interrupts disabled.\n");
            timer_thread_running = false;
        }

        stdin_thread_running = true;
        if (pthread_create(&stdin_thread, NULL, stdin_monitor_thread_func, NULL) != 0) {
            fprintf(stderr, "Warning: Failed to create stdin monitoring thread. Keyboard input disabled.\n");
            stdin_thread_running = false;
        }
    }

    // Step 4: Spawn vCPU threads
    printf("=== Starting VM execution (%d vCPUs) ===\n\n", num_vcpus);

    for (int i = 0; i < num_vcpus; i++) {
        if (pthread_create(&threads[i], NULL, vcpu_thread, &vcpus[i]) != 0) {
            fprintf(stderr, "Failed to create thread for vCPU %d\n", i);
            ret = 1;
            goto cleanup_stdin;
        }
    }

    // Step 5: Wait for all vCPUs to finish
    for (int i = 0; i < num_vcpus; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n=== All vCPUs completed ===\n");

cleanup_stdin:
    // Stop monitoring threads immediately after vCPUs complete
    if (timer_thread_running) {
        timer_thread_running = false;
        pthread_join(timer_thread, NULL);
    }

    if (stdin_thread_running) {
        stdin_thread_running = false;
        pthread_join(stdin_thread, NULL);
    }

cleanup_vcpus:
    // Cleanup all vCPUs
    for (int i = 0; i < num_vcpus; i++) {
        cleanup_vcpu(&vcpus[i]);
    }

cleanup_early:
    // Cleanup global resources
    if (vm_fd >= 0) close(vm_fd);
    if (kvm_fd >= 0) close(kvm_fd);

    return ret;
}
