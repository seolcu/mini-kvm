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
#include <termios.h>
#include "protected_mode.h"
#include "long_mode.h"
#include "debug.h"
#include "cpuid.h"
#include "msr.h"
#include "paging_64.h"
#include "linux_boot.h"

// Guest memory configuration
#define GUEST_MEM_SIZE (4 << 20) // 4MB (expandable for Protected Mode)
#define GUEST_LOAD_ADDR 0x0      // Load guest at address 0

// Mode selection
// Hypercall interface
#define HYPERCALL_PORT 0x500 // Port for hypercalls

// Hypercall numbers (must match 1K OS syscall numbers)
#define HC_EXIT 0x00    // Exit guest
#define HC_PUTCHAR 0x01 // Output character (BL = char)
#define HC_GETCHAR 0x02 // Input character (returns in AL)

// Multi-vCPU configuration
#define MAX_VCPUS 4 // Maximum number of vCPUs

typedef enum
{
    LINUX_ENTRY_SETUP,
    LINUX_ENTRY_CODE32,
    LINUX_ENTRY_BOOT64,
} linux_entry_mode_t;

typedef enum
{
    LINUX_RSI_BASE,
    LINUX_RSI_HDR,
} linux_rsi_mode_t;

#define LINUX_BOOT_CS 0x10
#define LINUX_BOOT_DS 0x18

// Keyboard buffer for interrupt-based input
#define KEYBOARD_BUFFER_SIZE 256
typedef struct
{
    char buffer[KEYBOARD_BUFFER_SIZE];
    int head; // Write position
    int tail; // Read position
    pthread_mutex_t lock;
} keyboard_buffer_t;

static keyboard_buffer_t keyboard_buffer = {
    .head = 0,
    .tail = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER};

// Linux serial console input support (COM1 IRQ4 + RX buffer)
static bool linux_serial_input_enabled = false;

// Stdin monitoring thread
static pthread_t stdin_thread;
static bool stdin_thread_running = false;

// Terminal settings
static struct termios orig_termios;
static bool termios_saved = false;

// Timer thread
static pthread_t timer_thread;
static bool timer_thread_running = false;
static volatile int timer_ticks = 0;

// Per-vCPU context structure
typedef struct
{
    int vcpu_id;              // vCPU index (0, 1, 2, 3)
    int vcpu_fd;              // KVM vCPU file descriptor
    struct kvm_run *kvm_run;  // Per-vCPU run structure
    void *guest_mem;          // Per-guest memory region
    size_t mem_size;          // Memory size (4MB default)
    size_t kvm_run_mmap_size; // Size of kvm_run mmap region
    const char *guest_binary; // Binary filename
    char name[256];           // Display name (e.g., "multiplication")
    int exit_count;           // VM exit counter
    bool running;             // Execution state
    bool use_paging;          // Enable Protected Mode with paging (for 1K OS)
    bool long_mode;           // Enable 64-bit Long Mode
    uint32_t entry_point;     // Entry point address (EIP)
    uint32_t load_offset;     // Offset to load binary in guest memory
    int pending_getchar;      // GETCHAR request pending (0=no, 1=yes)
    int getchar_result;       // Cached GETCHAR result for IN instruction
    bool linux_guest;         // Linux guest (bzImage) special handling
    linux_entry_mode_t linux_entry; // Linux entry strategy
    linux_rsi_mode_t linux_rsi;     // Linux RSI base (boot params vs setup header)
    int singlestep_remaining; // KVM single-step budget (0=disabled)
    bool singlestep_paused;   // Temporarily disable single-step (e.g., REP loops)
    int singlestep_exits;     // Count of KVM_EXIT_DEBUG exits
    uint64_t last_rip;
    uint64_t last_rsi;
    uint64_t last_rbx;
    uint64_t last_rdi;
    uint64_t last_rcx;
    uint64_t last_rsp;
    uint64_t last_rflags;
    uint64_t last_cr0;
    uint16_t last_cs;
    uint16_t last_es;
    uint64_t last_es_base;
    uint32_t last_es_limit;
    uint64_t last_idt_base;
    uint16_t last_idt_limit;
    uint8_t last_bytes[4];
} vcpu_context_t;

// Global KVM state (shared across vCPUs)
static int kvm_fd = -1; // /dev/kvm file descriptor
static int vm_fd = -1;  // VM instance (one VM, multiple vCPUs)

// vCPU array
static vcpu_context_t vcpus[MAX_VCPUS];
static int num_vcpus = 0;

// Thread synchronization
static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Verbose logging control
static bool verbose = false;

// Dynamic color codes for vCPUs (ANSI 256-color)
static int vcpu_colors[MAX_VCPUS];

/*
 * Get ANSI 256-color code from hue (0-360)
 * Uses the 6x6x6 color cube (codes 16-231)
 */
static int hue_to_ansi256(int hue)
{
    hue = hue % 360;
    if (hue < 0) hue += 360;

    int r, g, b;
    int sector = hue / 60;
    int offset = hue % 60;

    switch (sector) {
        case 0: r = 5; g = offset * 5 / 60; b = 0; break;
        case 1: r = 5 - offset * 5 / 60; g = 5; b = 0; break;
        case 2: r = 0; g = 5; b = offset * 5 / 60; break;
        case 3: r = 0; g = 5 - offset * 5 / 60; b = 5; break;
        case 4: r = offset * 5 / 60; g = 0; b = 5; break;
        default: r = 5; g = 0; b = 5 - offset * 5 / 60; break;
    }

    return 16 + 36 * r + 6 * g + b;
}

/*
 * Initialize colors for N vCPUs with maximum contrast
 * Distributes colors evenly around color wheel, avoiding red
 */
static void init_vcpu_colors(int n)
{
    // Start at green (120), span 300 degrees to avoid red zone (0-60)
    int start_hue = 120;
    int span = 300;

    for (int i = 0; i < n && i < MAX_VCPUS; i++) {
        int hue = start_hue + (i * span) / n;
        vcpu_colors[i] = hue_to_ansi256(hue);
    }
}

static int set_guest_singlestep(vcpu_context_t *ctx, bool enable)
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
 * Thread-safe output functions with vCPU identification
 */
static void vcpu_printf(vcpu_context_t *ctx, const char *fmt, ...)
{
    pthread_mutex_lock(&stdout_mutex);

    // Use color only for multi-vCPU (num_vcpus > 1)
    if (num_vcpus > 1)
    {
        // Dynamic colors based on vCPU count for maximum contrast
        printf("\033[38;5;%dm[vCPU %d:%s]\033[0m ",
               vcpu_colors[ctx->vcpu_id],
               ctx->vcpu_id,
               ctx->name);
    }
    else
    {
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
static void vcpu_putchar(vcpu_context_t *ctx, char ch)
{
    pthread_mutex_lock(&stdout_mutex);

    // Use color only for multi-vCPU (num_vcpus > 1)
    if (num_vcpus > 1)
    {
        // Dynamic colors based on vCPU count for maximum contrast
        printf("\033[38;5;%dm%c\033[0m", vcpu_colors[ctx->vcpu_id], ch);
    }
    else
    {
        // Single vCPU: no color
        putchar(ch);
    }
    fflush(stdout);

    pthread_mutex_unlock(&stdout_mutex);
}

/*
 * Load guest binary into guest memory
 */
static int load_guest_binary(const char *filename, void *mem, size_t mem_size, uint32_t load_offset)
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

    if (verbose)
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

    if (verbose)
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
 * Keyboard buffer helper functions
 */
static void keyboard_buffer_push(char ch)
{
    pthread_mutex_lock(&keyboard_buffer.lock);
    int next_head = (keyboard_buffer.head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_head != keyboard_buffer.tail)
    {
        keyboard_buffer.buffer[keyboard_buffer.head] = ch;
        keyboard_buffer.head = next_head;
    }
    pthread_mutex_unlock(&keyboard_buffer.lock);
}

static int keyboard_buffer_pop(void)
{
    pthread_mutex_lock(&keyboard_buffer.lock);
    if (keyboard_buffer.head == keyboard_buffer.tail)
    {
        pthread_mutex_unlock(&keyboard_buffer.lock);
        return -1; // Empty
    }
    char ch = keyboard_buffer.buffer[keyboard_buffer.tail];
    keyboard_buffer.tail = (keyboard_buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
    pthread_mutex_unlock(&keyboard_buffer.lock);
    return (unsigned char)ch;
}

static bool keyboard_buffer_has_data(void)
{
    pthread_mutex_lock(&keyboard_buffer.lock);
    bool has = (keyboard_buffer.head != keyboard_buffer.tail);
    pthread_mutex_unlock(&keyboard_buffer.lock);
    return has;
}

static void pulse_irq_line(uint32_t irq)
{
    if (vm_fd < 0) {
        return;
    }

    struct kvm_irq_level level = {
        .irq = irq,
        .level = 1,
    };
    (void)ioctl(vm_fd, KVM_IRQ_LINE, &level);
    level.level = 0;
    (void)ioctl(vm_fd, KVM_IRQ_LINE, &level);
}

/*
 * Timer thread - generates periodic timer interrupts
 * Injects IRQ0 every 10ms to all vCPUs
 * Currently disabled - causes triple faults before guest IDT setup
 */
__attribute__((unused))
static void *timer_thread_func(void *arg)
{
    (void)arg;

    printf("[Timer] Timer thread started (10ms period)\n");

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    while (timer_thread_running)
    {
        // Sleep for 100ms (reduced frequency to allow more interactive use)
        usleep(100000);

        timer_ticks++;

        // Inject timer interrupt (IRQ0) to all vCPUs
        for (int i = 0; i < num_vcpus; i++)
        {
            if (vcpus[i].running)
            {
                struct kvm_interrupt irq;
                irq.irq = 0x20; // Vector 0x20 = timer interrupt (IRQ0)

                if (ioctl(vcpus[i].vcpu_fd, KVM_INTERRUPT, &irq) < 0)
                {
                    // Interrupt might fail - just continue
                }
            }
        }
    }

    printf("[Timer] Timer thread stopped\n");
    return NULL;
}

/*
 * Set terminal to raw mode for character-by-character input
 * Disables local echo and line buffering
 */
static void set_raw_mode(void)
{
    if (!isatty(STDIN_FILENO))
    {
        // Not a terminal (piped input), skip raw mode
        return;
    }

    // Save original terminal settings
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
    {
        perror("tcgetattr");
        return;
    }
    termios_saved = true;

    struct termios raw = orig_termios;

    // Disable echo (ECHO) and canonical mode (ICANON)
    // ICANON: line buffering
    // ECHO: local echo
    // ISIG: signal generation (Ctrl+C, Ctrl+Z)
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);

    // Disable input processing
    // IXON: Ctrl+S/Ctrl+Q flow control
    // ICRNL: translate CR to NL
    raw.c_iflag &= ~(IXON | ICRNL);

    // Keep output processing enabled for proper newline handling
    // OPOST enables automatic \n to \r\n translation (ONLCR flag)
    // This is essential for correct line breaks in terminal output
    // Without OPOST, each \n only moves cursor down, not to start of line
    // raw.c_oflag &= ~(OPOST);  // DO NOT disable OPOST!

    // Set read to return immediately with at least 1 character
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        perror("tcsetattr");
        termios_saved = false;
    }
}

/*
 * Restore terminal to original settings
 */
static void restore_terminal(void)
{
    if (termios_saved)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_saved = false;
    }
}

/*
 * Stdin monitoring thread - reads from stdin and injects keyboard interrupts
 * This thread runs continuously and injects IRQ1 when a key is pressed
 */
static void *stdin_monitor_thread_func(void *arg)
{
    (void)arg;

    printf("[Keyboard] Stdin monitoring thread started\n");

    // Set stdin to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    while (stdin_thread_running)
    {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds))
        {
            int ch = getchar();
            if (ch != EOF)
            {
                // Store character in buffer
                keyboard_buffer_push((char)ch);

                // For Linux, wake the serial driver by pulsing COM1 IRQ4.
                if (linux_serial_input_enabled)
                {
                    pulse_irq_line(4);
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
 * force_irqchip: override to always create IRQCHIP (Linux mode)
 */
static int init_kvm(bool need_irqchip, bool force_irqchip)
{
    int api_version;

    // 1. Open /dev/kvm
    kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm_fd < 0)
    {
        perror("open /dev/kvm");
        fprintf(stderr, "Make sure KVM is enabled (CONFIG_KVM=y/m)\n");
        return -1;
    }

    // 2. Check KVM API version
    api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0)
    {
        perror("KVM_GET_API_VERSION");
        return -1;
    }

    if (api_version != KVM_API_VERSION)
    {
        fprintf(stderr, "KVM API version mismatch: expected %d, got %d\n",
                KVM_API_VERSION, api_version);
        return -1;
    }

    printf("KVM API version: %d\n", api_version);

    // 3. Create VM
    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0)
    {
        perror("KVM_CREATE_VM");
        return -1;
    }

    printf("Created VM (fd=%d)\n", vm_fd);

    // 3.5. Set TSS address (required on Intel platforms for Protected Mode)
    // This must be called before creating vCPUs
    // Use an address in guest memory that won't conflict with kernel/page tables
    if (need_irqchip || 1)
    { // Always set TSS for protected mode
        // TSS at 0x200000 (2MB) - well above kernel and page directory
        if (ioctl(vm_fd, KVM_SET_TSS_ADDR, (unsigned long)0x200000) < 0)
        {
            // May fail on AMD - that is OK, only strictly required on Intel
            if (verbose)
            {
                perror("KVM_SET_TSS_ADDR (may be OK on AMD)");
            }
        }
        else
        {
            printf("Set TSS address to 0x200000\n");
        }
    }

    // 4. Create interrupt controller (IRQCHIP) only if needed
    // Linux boot requires PIT/IRQ routing; keep disabled for legacy Real Mode guests.
    if (force_irqchip)
    {
        if (ioctl(vm_fd, KVM_CREATE_IRQCHIP) < 0)
        {
            perror("KVM_CREATE_IRQCHIP");
            fprintf(stderr, "Warning: Interrupt controller creation failed. Interrupts disabled.\n");
        }
        else
        {
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
static int setup_vcpu_memory(vcpu_context_t *ctx)
{
    struct kvm_userspace_memory_region mem_region;

    // Linux guests need larger RAM; keep legacy defaults for other paths.
    if (ctx->linux_guest)
    {
        ctx->mem_size = 256 * 1024 * 1024; // 256MB for Linux bzImage
    }
    // Use 4MB per vCPU for 1K OS (with paging), 256KB for Real Mode guests
    else if (ctx->use_paging)
    {
        ctx->mem_size = 4 * 1024 * 1024; // 4MB for Protected Mode
    }
    else
    {
        ctx->mem_size = 256 * 1024; // 256KB for Real Mode (fits in 64K segment)
    }

    // Allocate memory for this vCPU's guest
    ctx->guest_mem = mmap(NULL, ctx->mem_size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctx->guest_mem == MAP_FAILED)
    {
        perror("mmap vcpu guest_mem");
        return -1;
    }

    if (verbose)
    {
        vcpu_printf(ctx, "Allocated guest memory: %zu KB at %p\n",
                    ctx->mem_size / 1024, ctx->guest_mem);
    }

    // Tell KVM about this memory region
    // Each vCPU uses different GPA range: vCPU 0 at 0x0, vCPU 1 at 0x400000 (4MB), etc.
    mem_region.slot = ctx->vcpu_id; // Use vCPU ID as slot number
    mem_region.flags = 0;
    mem_region.guest_phys_addr = ctx->vcpu_id * ctx->mem_size; // Offset by 4MB
    mem_region.memory_size = ctx->mem_size;
    mem_region.userspace_addr = (unsigned long)ctx->guest_mem;

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0)
    {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    if (verbose)
    {
        vcpu_printf(ctx, "Mapped to slot %d: GPA 0x%lx -> HVA %p (%zu bytes)\n",
                    ctx->vcpu_id, mem_region.guest_phys_addr, ctx->guest_mem, ctx->mem_size);
    }

    return 0;
}

/*
 * Setup page tables for Protected Mode with paging (for 1K OS)
 * Uses 3-level page tables with 4KB pages (PSE disabled for Zen 5 compatibility)
 */
static int setup_page_tables(vcpu_context_t *ctx)
{
    // Page directory at GPA 0x00100000 (1MB offset)
    const uint32_t page_dir_offset = 0x00100000;
    const uint32_t page_table_0_offset = 0x00101000;    // 1MB + 4KB
    const uint32_t page_table_512_offset = 0x00102000;  // 1MB + 8KB

    if (page_table_512_offset + 4096 >= ctx->mem_size)
    {
        vcpu_printf(ctx, "Error: Page table offsets exceed memory size\n");
        return -1;
    }

    // Get pointers to page directory and page tables
    uint32_t *page_dir = (uint32_t *)(ctx->guest_mem + page_dir_offset);
    uint32_t *page_table_0 = (uint32_t *)(ctx->guest_mem + page_table_0_offset);
    uint32_t *page_table_512 = (uint32_t *)(ctx->guest_mem + page_table_512_offset);

    // Clear all structures
    memset(page_dir, 0, 4096);
    memset(page_table_0, 0, 4096);
    memset(page_table_512, 0, 4096);

    // Setup PDE[0] -> Page Table 0 (covers 0x0-0x3FFFFF, 4MB)
    // Flags: 0x03 = Present | R/W (no PSE bit)
    page_dir[0] = page_table_0_offset | 0x03;

    // Setup PDE[512] -> Page Table 512 (covers 0x80000000-0x803FFFFF, 4MB)
    page_dir[512] = page_table_512_offset | 0x03;

    // Fill Page Table 0: Identity map 0x0-0x3FF000 (4MB, 1024 x 4KB pages)
    for (int i = 0; i < 1024; i++)
    {
        // Each PTE maps 4KB: physical address = i * 4096
        page_table_0[i] = (i << 12) | 0x03;  // Present, R/W, 4KB pages
    }

    // Fill Page Table 512: Map 0x80000000+ to physical 0x0+ (4MB, 1024 x 4KB pages)
    for (int i = 0; i < 1024; i++)
    {
        // Virtual 0x80000000 + (i * 4KB) -> Physical 0x0 + (i * 4KB)
        page_table_512[i] = (i << 12) | 0x03;  // Present, R/W, 4KB pages
    }

    if (verbose)
    {
        vcpu_printf(ctx, "Page directory at GPA 0x%x (4KB paging, no PSE)\n",
                    page_dir_offset);
        vcpu_printf(ctx, "  PDE[0]   = 0x%08x -> Page Table 0 at 0x%x\n", page_dir[0], page_table_0_offset);
        vcpu_printf(ctx, "  PDE[512] = 0x%08x -> Page Table 512 at 0x%x\n", page_dir[512], page_table_512_offset);
        vcpu_printf(ctx, "  Identity map: 0x0-0x3FFFFF (1024 x 4KB pages)\n");
        vcpu_printf(ctx, "  Kernel map: 0x80000000-0x803FFFFF -> 0x0-0x3FFFFF\n");
    }

    return page_dir_offset; // Return offset for CR3
}

/*
 * Create a GDT entry
 */
static void create_gdt_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
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
 * Setup GDT in guest memory for Protected Mode
 * Called when paging is enabled
 */
static int setup_gdt(void *guest_mem_ptr)
{
    gdt_entry_t *gdt = (gdt_entry_t *)(guest_mem_ptr + GDT_ADDR);

    // Entry 0: Null descriptor (required)
    create_gdt_entry(&gdt[0], 0, 0, 0, 0);

    // Entry 1: Kernel code segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[1], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);

    // Entry 2: Kernel data segment (32-bit, base=0, limit=4GB)
    create_gdt_entry(&gdt[2], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);

    // Entry 3: User code segment (32-bit, ring 3)
    create_gdt_entry(&gdt[3], 0, 0xFFFFF, 0xFA, LIMIT_GRAN); // Ring 3 code

    // Entry 4: User data segment (32-bit, ring 3)
    create_gdt_entry(&gdt[4], 0, 0xFFFFF, 0xF2, LIMIT_GRAN); // Ring 3 data

    printf("GDT setup: %d entries at 0x%x\n", GDT_SIZE, GDT_ADDR);
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
    create_gdt_entry(&gdt[0], 0, 0, 0, 0);

    // Entry 1: Unused (keep null)
    create_gdt_entry(&gdt[1], 0, 0, 0, 0);

    // Entry 2: __BOOT_CS (0x10) - 32-bit code, base=0, limit=4GB
    create_gdt_entry(&gdt[2], 0, 0xFFFFF, ACCESS_CODE_R, LIMIT_GRAN);

    // Entry 3: __BOOT_DS (0x18) - 32-bit data, base=0, limit=4GB
    create_gdt_entry(&gdt[3], 0, 0xFFFFF, ACCESS_DATA_W, LIMIT_GRAN);

    // Entry 4: Unused (keep null)
    create_gdt_entry(&gdt[4], 0, 0, 0, 0);

    printf("Linux boot GDT setup: __BOOT_CS=0x%x __BOOT_DS=0x%x\n",
           LINUX_BOOT_CS, LINUX_BOOT_DS);
    return 0;
}

/*
 * Setup IDT in guest memory for Protected Mode
 * Called when paging is enabled
 */
static int setup_idt(void *guest_mem_ptr)
{
    // Place IDT right after GDT
    uint32_t idt_addr = GDT_ADDR + GDT_TOTAL_SIZE;
    idt_entry_t *idt = (idt_entry_t *)(guest_mem_ptr + idt_addr);

    // Create a simple IDT with 256 entries (all pointing to dummy handler)
    // For now, just zero-initialize (invalid entries)
    memset(idt, 0, 256 * sizeof(idt_entry_t));

    printf("IDT setup at 0x%x\n", idt_addr);
    return 0;
}

/*
 * Setup 64-bit GDT for Long Mode
 */
static void setup_gdt_64bit(void *guest_mem, uint64_t gdt_base)
{
    gdt_entry_64_t *gdt = (gdt_entry_64_t *)((char *)guest_mem + gdt_base);
    
    // Clear GDT region
    memset(gdt, 0, 5 * sizeof(gdt_entry_64_t));
    
    // Entry 0: Null descriptor (required)
    memset(&gdt[GDT_NULL_ENTRY], 0, sizeof(gdt_entry_64_t));
    
    // Entry 1: 64-bit kernel code segment
    gdt[GDT_KERNEL_CODE_64].limit_low = 0;
    gdt[GDT_KERNEL_CODE_64].base_low = 0;
    gdt[GDT_KERNEL_CODE_64].base_mid = 0;
    gdt[GDT_KERNEL_CODE_64].access = GDT_PRESENT | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_RW;
    gdt[GDT_KERNEL_CODE_64].granularity = GDT_LONG_MODE; // L bit set for 64-bit
    gdt[GDT_KERNEL_CODE_64].base_high = 0;
    
    // Entry 2: 64-bit kernel data segment
    gdt[GDT_KERNEL_DATA_64].limit_low = 0;
    gdt[GDT_KERNEL_DATA_64].base_low = 0;
    gdt[GDT_KERNEL_DATA_64].base_mid = 0;
    gdt[GDT_KERNEL_DATA_64].access = GDT_PRESENT | GDT_CODE_DATA | GDT_RW;
    gdt[GDT_KERNEL_DATA_64].granularity = 0;
    gdt[GDT_KERNEL_DATA_64].base_high = 0;
    
    DEBUG_PRINT(DEBUG_DETAILED, "64-bit GDT setup at 0x%llx", (unsigned long long)gdt_base);
}

/*
 * Setup vCPU for 64-bit Long Mode
 */
static int setup_vcpu_longmode(int kvm_fd, vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    
    DEBUG_PRINT(DEBUG_BASIC, "[vCPU %d] Setting up 64-bit Long Mode", ctx->vcpu_id);
    
    // Setup 64-bit page tables (PML4 at 0x2000, PDPT at 0x3000, PD at 0x4000)
    uint64_t cr3 = setup_page_tables_64bit(ctx->guest_mem, ctx->mem_size);
    
    // Setup 64-bit GDT (place at 0x5000 to avoid page table conflict)
    uint64_t gdt_base = 0x5000; // Place GDT at 20KB
    setup_gdt_64bit(ctx->guest_mem, gdt_base);
    
    // Setup CPUID
    if (setup_cpuid(kvm_fd, ctx->vcpu_fd) < 0) {
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

    if (verbose) {
        printf("Set registers: RIP=0x%llx\n", regs.rip);
    }

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
#endif // OLD SINGLE-VCPU CODE

/*
 * Setup segment registers for Real Mode
 */
static void setup_realmode_segments(struct kvm_sregs *sregs, vcpu_context_t *ctx)
{
    if (ctx->linux_guest)
    {
        sregs->cs.base = REAL_MODE_KERNEL_ADDR;
        sregs->cs.selector = (uint16_t)(REAL_MODE_KERNEL_ADDR / 16);
        sregs->cs.limit = 0xFFFF;
        sregs->cs.type = 0x9b;
        sregs->cs.present = 1;
        sregs->cs.dpl = 0;
        sregs->cs.db = 0;
        sregs->cs.s = 1;
        sregs->cs.l = 0;
        sregs->cs.g = 0;
        sregs->cs.avl = 0;

        // For bzImage setup, DS=CS is expected (setup uses far ret to DS:xxxx).
        sregs->ds.base = REAL_MODE_KERNEL_ADDR;
        sregs->ds.selector = (uint16_t)(REAL_MODE_KERNEL_ADDR / 16);
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
        return;
    }

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

    // DS must also point to the vCPU's memory region for data access
    sregs->ds.base = cs_value * 16;
    sregs->ds.selector = cs_value;
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
static void setup_protectedmode_segments(struct kvm_sregs *sregs)
{
    // Code segment (flat, base=0, limit=4GB)
    // Type 0x0a = Execute/Read code segment (matches GDT entry with access 0x9a)
    sregs->cs.base = 0;
    sregs->cs.limit = 0xFFFFFFFF;
    sregs->cs.selector = 0x08;
    sregs->cs.type = 0x0a; // Execute/Read
    sregs->cs.present = 1;
    sregs->cs.dpl = 0;
    sregs->cs.db = 1;
    sregs->cs.s = 1;
    sregs->cs.l = 0;
    sregs->cs.g = 1;
    sregs->cs.avl = 0;

    // Data segment (flat, base=0, limit=4GB)
    // Type 0x02 = Read/Write data segment (matches GDT entry with access 0x92)
    sregs->ds.base = 0;
    sregs->ds.limit = 0xFFFFFFFF;
    sregs->ds.selector = 0x10;
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

static void setup_linux_boot_segments(struct kvm_sregs *sregs)
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

/*
 * Configure vCPU for Protected Mode with paging
 */
static int configure_protected_mode(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;

    // Setup GDT and IDT in guest memory
    setup_gdt(ctx->guest_mem);
    setup_idt(ctx->guest_mem);

    // Setup page tables
    int page_dir_offset = setup_page_tables(ctx);
    if (page_dir_offset < 0)
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
    setup_protectedmode_segments(&sregs);

    vcpu_printf(ctx, "About to set sregs: CR0=0x%llx CR3=0x%llx CR4=0x%llx\n",
                sregs.cr0, sregs.cr3, sregs.cr4);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS (paging)");
        return -1;
    }

    // Update RIP to entry point
    memset(&regs, 0, sizeof(regs));
    regs.rip = ctx->entry_point;
    regs.rflags = 0x2;

    if (ioctl(ctx->vcpu_fd, KVM_SET_REGS, &regs) < 0)
    {
        perror("KVM_SET_REGS (paging)");
        return -1;
    }

    if (verbose)
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

    vcpu_printf(ctx, "Enabled paging: CR3=0x%llx, EIP=0x%x (Protected Mode)\n",
                sregs.cr3, ctx->entry_point);

    return 0;
}

static void setup_linux_prot_idt(void *guest_mem);

/*
 * Configure vCPU for Linux protected-mode entry (no paging).
 *
 * Linux boot protocol requires protected mode with paging disabled at code32_start,
 * with RSI/ESI pointing to the boot_params ("zero page").
 */
static int configure_linux_code32_entry(vcpu_context_t *ctx, uint32_t boot_params_addr)
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

    setup_linux_boot_segments(&sregs);

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

static int configure_linux_boot64_entry(vcpu_context_t *ctx)
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
 * Setup vCPU context (multi-vCPU version)
 */
static int setup_vcpu_context(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int mmap_size_ret;

    // Create vCPU
    ctx->vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, ctx->vcpu_id);
    if (ctx->vcpu_fd < 0)
    {
        perror("KVM_CREATE_VCPU");
        return -1;
    }

    if (verbose)
    {
        vcpu_printf(ctx, "Created vCPU (fd=%d)\n", ctx->vcpu_fd);
    }

    // Get kvm_run structure size and mmap it
    mmap_size_ret = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size_ret < 0)
    {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        return -1;
    }
    ctx->kvm_run_mmap_size = (size_t)mmap_size_ret;

    ctx->kvm_run = mmap(NULL, ctx->kvm_run_mmap_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, ctx->vcpu_fd, 0);
    if (ctx->kvm_run == MAP_FAILED)
    {
        perror("mmap kvm_run");
        return -1;
    }

    if (verbose)
    {
        vcpu_printf(ctx, "Mapped kvm_run structure: %zu bytes\n", ctx->kvm_run_mmap_size);
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
            if (configure_linux_boot64_entry(ctx) < 0)
            {
                return -1;
            }
        }
        else if (ctx->linux_entry == LINUX_ENTRY_CODE32)
        {
            if (configure_linux_code32_entry(ctx, LINUX_BOOT_PARAMS_ADDR) < 0)
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

            setup_realmode_segments(&sregs, ctx);

            if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
            {
                perror("KVM_SET_SREGS (linux real mode)");
                return -1;
            }

            if (setup_cpuid(kvm_fd, ctx->vcpu_fd) < 0)
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
    setup_realmode_segments(&sregs, ctx);

    if (ioctl(ctx->vcpu_fd, KVM_SET_SREGS, &sregs) < 0)
    {
        perror("KVM_SET_SREGS");
        return -1;
    }

    // Set CPUID entries (required for Linux to see LM/PAE/etc.)
    if (setup_cpuid(kvm_fd, ctx->vcpu_fd) < 0)
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

    if (verbose)
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
            extern int kvm_fd; // Access global KVM fd
            if (setup_vcpu_longmode(kvm_fd, ctx) < 0)
            {
                return -1;
            }
        }
        // Otherwise, if paging is enabled, switch to Protected Mode (32-bit)
        else if (ctx->use_paging)
        {
            if (configure_protected_mode(ctx) < 0)
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
 * Handle hypercall OUT request
 */
static int handle_hypercall_out(vcpu_context_t *ctx, struct kvm_regs *regs)
{
    unsigned char hc_num = regs->rax & 0xFF;

    // Log hypercalls if verbose mode is enabled
    if (verbose)
    {
        static int hc_count = 0;
        if (hc_count++ < 100)
        {
            vcpu_printf(ctx, "HC[%d] type=0x%02x RAX=0x%llx RBX=0x%llx\n",
                        hc_count, hc_num, regs->rax, regs->rbx);
        }
    }

    switch (hc_num)
    {
    case HC_EXIT:
        if (verbose)
        {
            vcpu_printf(ctx, "Exit request\n");
        }
        ctx->running = false;
        return 0;

    case HC_PUTCHAR:
    {
        char ch = regs->rbx & 0xFF;
        vcpu_putchar(ctx, ch);
        break;
    }

    case HC_GETCHAR:
    {
        int ch = keyboard_buffer_pop();
        ctx->getchar_result = ch;
        ctx->pending_getchar = 1;
        break;
    }

    default:
        if (verbose)
        {
            vcpu_printf(ctx, "Unknown hypercall: 0x%02x\n", hc_num);
        }
        return -1;
    }

    return 0;
}

/*
 * Handle hypercall IN response
 */
static void handle_hypercall_in(vcpu_context_t *ctx, char *data)
{
    if (ctx->pending_getchar)
    {
        data[0] = (ctx->getchar_result == -1) ? 0xFF : (unsigned char)ctx->getchar_result;

        if (verbose)
        {
            static int in_count = 0;
            if (in_count++ < 50)
            {
                vcpu_printf(ctx, "IN[%d] from 0x500: returning ch=%d (0x%02x)\n",
                            in_count, ctx->getchar_result, data[0]);
            }
        }
        ctx->pending_getchar = 0;
    }
    else
    {
        if (verbose)
        {
            static int unexpected_in = 0;
            if (unexpected_in++ < 20)
            {
                vcpu_printf(ctx, "WARN[%d]: IN from 0x500 without pending_getchar!\n", unexpected_in);
            }
        }
        data[0] = 0;
    }
}

// Minimal 16550 UART emulation for COM1 (0x3f8-0x3ff)
typedef struct
{
    uint8_t ier;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t dll;
    uint8_t dlh;
} uart16550_t;

static uart16550_t uart0 = {
    .ier = 0x00,
    .lcr = 0x03, // 8N1
    .mcr = 0x00,
    .dll = 0x01,
    .dlh = 0x00,
};

static bool is_uart_port(uint16_t port)
{
    return port >= 0x3f8 && port <= 0x3ff;
}

static void uart_write(uint16_t port, const char *data)
{
    uint16_t offset = port - 0x3f8;
    bool dlab = (uart0.lcr & 0x80) != 0;

    switch (offset)
    {
    case 0: // THR or DLL
        if (dlab)
        {
            uart0.dll = data[0];
        }
        else
        {
            putchar(data[0]);
            fflush(stdout);
            if (linux_serial_input_enabled && (uart0.ier & 0x02))
            {
                // THR empty interrupt (TX) to drain kernel/userland buffers.
                pulse_irq_line(4);
            }
        }
        break;
    case 1: // IER or DLH
        if (dlab)
        {
            uart0.dlh = data[0];
        }
        else
        {
            uart0.ier = data[0];
            if (linux_serial_input_enabled && (uart0.ier & 0x02))
            {
                // On real 16550, enabling THRE while THR is empty triggers an IRQ immediately.
                pulse_irq_line(4);
            }
        }
        break;
    case 3: // LCR
        uart0.lcr = data[0];
        break;
    case 4: // MCR
        uart0.mcr = data[0];
        break;
    default:
        break;
    }
}

static void uart_read(uint16_t port, char *data)
{
    uint16_t offset = port - 0x3f8;
    bool dlab = (uart0.lcr & 0x80) != 0;

    switch (offset)
    {
    case 0: // RBR or DLL
        if (dlab)
        {
            data[0] = uart0.dll;
        }
        else
        {
            int ch = keyboard_buffer_pop();
            data[0] = (ch < 0) ? 0x00 : (char)ch;
        }
        break;
    case 1: // IER or DLH
        data[0] = dlab ? uart0.dlh : uart0.ier;
        break;
    case 2: // IIR
        if (keyboard_buffer_has_data() && (uart0.ier & 0x01))
        {
            data[0] = 0x04; // Received Data Available
        }
        else if (uart0.ier & 0x02)
        {
            data[0] = 0x02; // THR Empty
        }
        else
        {
            data[0] = 0x01; // No pending interrupts
        }
        break;
    case 3: // LCR
        data[0] = uart0.lcr;
        break;
    case 4: // MCR
        data[0] = uart0.mcr;
        break;
    case 5: // LSR
        data[0] = 0x60; // THR empty | TEMT
        if (keyboard_buffer_has_data())
        {
            data[0] |= 0x01; // Data Ready
        }
        break;
    case 6: // MSR
        data[0] = 0x00;
        break;
    case 7: // SCR
        data[0] = 0x00;
        break;
    default:
        data[0] = 0x00;
        break;
    }
}

static void setup_linux_ivt(void *guest_mem)
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

// Minimal legacy port emulation for Linux setup (A20/8042/CMOS/PIC/etc.)
static uint8_t cmos_index = 0;
static uint8_t port92 = 0x02; // A20 enabled bit set by default

static void misc_port_out(uint16_t port, const char *data, int size)
{
    (void)size;
    uint8_t value = (uint8_t)data[0];

    switch (port)
    {
    case 0x92: // Fast A20 gate
        port92 = value | 0x02;
        break;
    case 0x70: // CMOS index
        cmos_index = value;
        break;
    case 0x20: // PIC1 command
    case 0x21: // PIC1 data
    case 0xA0: // PIC2 command
    case 0xA1: // PIC2 data
    case 0x80: // POST delay port
    case 0x60: // 8042 data
    case 0x64: // 8042 status/command
        break;
    default:
        break;
    }
}

static void misc_port_in(uint16_t port, char *data, int size)
{
    // Default: return 0 for unknown ports so polling loops can progress
    memset(data, 0, (size_t)size);

    switch (port)
    {
    case 0x92:
        data[0] = port92;
        break;
    case 0x64:
        // 8042 status: input buffer empty (bit1=0), output buffer empty (bit0=0)
        data[0] = 0x00;
        break;
    case 0x60:
        data[0] = 0x00;
        break;
    case 0x71:
        // CMOS data - return 0
        data[0] = 0x00;
        break;
    case 0x20:
    case 0x21:
    case 0xA0:
    case 0xA1:
        data[0] = 0x00;
        break;
    default:
        break;
    }
}

/*
 * Handle I/O port operations
 */
static int handle_io(vcpu_context_t *ctx)
{
    char *data = (char *)ctx->kvm_run + ctx->kvm_run->io.data_offset;

    // Log I/O operations if verbose mode is enabled
    if (verbose)
    {
        static int io_count = 0;
        if (io_count++ < 100)
        {
            vcpu_printf(ctx, "IO[%d]: dir=%s port=0x%x size=%d\n",
                        io_count,
                        (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT) ? "OUT" : "IN",
                        ctx->kvm_run->io.port,
                        ctx->kvm_run->io.size);
        }
    }

    if (ctx->kvm_run->io.direction == KVM_EXIT_IO_OUT)
    {
        if (ctx->kvm_run->io.port == HYPERCALL_PORT)
        {
            struct kvm_regs regs;
            if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) < 0)
            {
                perror("KVM_GET_REGS");
                return -1;
            }
            return handle_hypercall_out(ctx, &regs);
        }
        else if (is_uart_port(ctx->kvm_run->io.port))
        {
            // UART output
            for (int i = 0; i < ctx->kvm_run->io.size; i++)
            {
                uart_write(ctx->kvm_run->io.port + i, &data[i]);
            }
        }
        else
        {
            misc_port_out(ctx->kvm_run->io.port, data, ctx->kvm_run->io.size);
        }
    }
    else
    {
        // IN instruction
        if (ctx->kvm_run->io.port == HYPERCALL_PORT)
        {
            handle_hypercall_in(ctx, data);
        }
        else if (is_uart_port(ctx->kvm_run->io.port))
        {
            for (int i = 0; i < ctx->kvm_run->io.size; i++)
            {
                uart_read(ctx->kvm_run->io.port + i, &data[i]);
            }
        }
        else
        {
            misc_port_in(ctx->kvm_run->io.port, data, ctx->kvm_run->io.size);
        }
    }

    return 0;
}

/*
 * Handle VM exit for a specific vCPU context
 */
static int handle_vm_exit(vcpu_context_t *ctx)
{
    ctx->exit_count++;

    // If we temporarily disabled single-step (e.g., to let REP instructions complete),
    // re-enable it on the next non-debug exit while the budget remains.
    if (ctx->singlestep_paused && ctx->kvm_run->exit_reason != KVM_EXIT_DEBUG)
    {
        ctx->singlestep_paused = false;
        if (ctx->singlestep_remaining > 0)
        {
            set_guest_singlestep(ctx, true);
        }
    }

    // Log exit reasons if verbose mode is enabled
    if (verbose)
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
        if (verbose)
        {
            vcpu_printf(ctx, "Guest halted after %d exits\n", ctx->exit_count);
        }
        ctx->running = false;
        return 0;

    case KVM_EXIT_IO:
        return handle_io(ctx);

    case KVM_EXIT_DEBUG:
    {
        if (ctx->singlestep_remaining > 0)
        {
            ctx->singlestep_exits++;
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

                ctx->last_rip = regs.rip;
                ctx->last_rsi = regs.rsi;
                ctx->last_rbx = regs.rbx;
                ctx->last_rdi = regs.rdi;
                ctx->last_rcx = regs.rcx;
                ctx->last_rsp = regs.rsp;
                ctx->last_rflags = regs.rflags;
                ctx->last_cr0 = sregs.cr0;
                ctx->last_cs = sregs.cs.selector;
                ctx->last_es = sregs.es.selector;
                ctx->last_es_base = sregs.es.base;
                ctx->last_es_limit = sregs.es.limit;
                ctx->last_idt_base = sregs.idt.base;
                ctx->last_idt_limit = sregs.idt.limit;
                ctx->last_bytes[0] = b0;
                ctx->last_bytes[1] = b1;
                ctx->last_bytes[2] = b2;
                ctx->last_bytes[3] = b3;

                bool should_log = (ctx->singlestep_exits <= 50) || ((ctx->singlestep_exits % 50) == 0);
                if (should_log)
                {
                    vcpu_printf(ctx, "STEP: RIP=0x%llx CS=0x%x linear=0x%llx CR0=0x%llx RSI=0x%llx RBX=0x%llx RDI=0x%llx bytes=%02x %02x %02x %02x\n",
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
                if (!ctx->singlestep_paused && (b0 == 0xF3 || b0 == 0xF2))
                {
                    ctx->singlestep_paused = true;
                    set_guest_singlestep(ctx, false);
                    vcpu_printf(ctx, "STEP: pausing single-step for REP instruction\n");
                    return 0;
                }
            }
            ctx->singlestep_remaining--;
            if (ctx->singlestep_remaining == 0)
            {
                set_guest_singlestep(ctx, false);
                vcpu_printf(ctx, "STEP: disabled single-step\n");
            }
            return 0;
        }
        return 0;
    }

    case KVM_EXIT_FAIL_ENTRY:
        vcpu_printf(ctx, "FAIL_ENTRY: reason 0x%llx\n",
                    ctx->kvm_run->fail_entry.hardware_entry_failure_reason);
        return -1;

    case KVM_EXIT_MMIO:
        if (verbose)
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
        if (ctx->singlestep_exits > 0)
        {
            vcpu_printf(ctx, "  Last step: RIP=0x%llx CS=0x%x ES=0x%x ES.base=0x%llx ES.limit=0x%x IDT.base=0x%llx IDT.limit=0x%x CR0=0x%llx RFLAGS=0x%llx RSI=0x%llx RBX=0x%llx RCX=0x%llx RDI=0x%llx RSP=0x%llx bytes=%02x %02x %02x %02x\n",
                        (unsigned long long)ctx->last_rip,
                        (unsigned)ctx->last_cs,
                        (unsigned)ctx->last_es,
                        (unsigned long long)ctx->last_es_base,
                        (unsigned)ctx->last_es_limit,
                        (unsigned long long)ctx->last_idt_base,
                        (unsigned)ctx->last_idt_limit,
                        (unsigned long long)ctx->last_cr0,
                        (unsigned long long)ctx->last_rflags,
                        (unsigned long long)ctx->last_rsi,
                        (unsigned long long)ctx->last_rbx,
                        (unsigned long long)ctx->last_rcx,
                        (unsigned long long)ctx->last_rdi,
                        (unsigned long long)ctx->last_rsp,
                        ctx->last_bytes[0], ctx->last_bytes[1], ctx->last_bytes[2], ctx->last_bytes[3]);

            // If the guest installed an IDT in RAM, dump a few key exception vectors.
            const uint8_t vectors[] = {0, 6, 8, 13, 14};
            uint8_t *mem = (uint8_t *)ctx->guest_mem;
            for (size_t vi = 0; vi < sizeof(vectors); vi++)
            {
                uint8_t vec = vectors[vi];
                uint64_t entry_addr = ctx->last_idt_base + (uint64_t)vec * sizeof(idt_entry_t);
                if (entry_addr + sizeof(idt_entry_t) <= ctx->mem_size)
                {
                    idt_entry_t *e = (idt_entry_t *)(mem + entry_addr);
                    uint32_t off = (uint32_t)e->offset_low | ((uint32_t)e->offset_high << 16);
                    vcpu_printf(ctx, "  IDT[%u]: sel=0x%x off=0x%x flags=0x%x\n",
                                (unsigned)vec, (unsigned)e->selector, off, (unsigned)e->flags);
                }
            }
        }

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

    // Safety limit (disabled for Protected Mode to allow indefinite interactive use with 1K OS)
    if (!ctx->use_paging && ctx->exit_count > 100000)
    {
        vcpu_printf(ctx, "Too many exits (%d), stopping\n", ctx->exit_count);
        return -1;
    }

    return 0;
}

/*
 * vCPU thread entry point
 */
static void *vcpu_thread(void *arg)
{
    vcpu_context_t *ctx = (vcpu_context_t *)arg;
    int ret;

    if (verbose)
    {
        vcpu_printf(ctx, "Thread started\n");
    }

    // Debug: check vCPU state before first run
    if (verbose && ctx->use_paging)
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

    if (verbose)
    {
        vcpu_printf(ctx, "Thread exiting (total exits: %d)\n", ctx->exit_count);
    }
    return NULL;
}

/*
 * Cleanup vCPU resources
 */
static void cleanup_vcpu(vcpu_context_t *ctx)
{
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
#endif // OLD SINGLE-VCPU CLEANUP

/*
 * Extract guest name from binary filename
 */
static const char *extract_guest_name(const char *filename)
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

int main(int argc, char **argv)
{
    int ret = 0;
    pthread_t threads[MAX_VCPUS];
    bool enable_paging = false;
    bool enable_long_mode = false;
    bool linux_boot = false;
    linux_entry_mode_t linux_entry = LINUX_ENTRY_CODE32;
    linux_rsi_mode_t linux_rsi = LINUX_RSI_BASE;
    const char *linux_cmdline = NULL;
    const char *initrd_path = NULL;
    const char *bzimage_path = NULL;
    uint32_t entry_point = 0x80001000; // Default entry point for paging mode
    uint32_t load_offset = 0x1000;     // Default load offset for paging mode
    int guest_arg_start = 1;

    // Parse command line arguments
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s [OPTIONS] <guest_binary> | --linux <bzImage> [--linux-entry setup|code32|boot64] [--linux-rsi base|hdr] [--cmdline \"...\"] [--initrd <file>]\n", argv[0]);
        fprintf(stderr, "  Run 1-4 guests simultaneously in separate vCPUs or boot Linux kernel\n");
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  --paging            Enable Protected Mode with paging\n");
        fprintf(stderr, "  --long-mode         Enable 64-bit Long Mode\n");
        fprintf(stderr, "  --linux <bzImage>   Boot Linux kernel (bzImage format)\n");
        fprintf(stderr, "  --linux-entry MODE  Linux entry (setup|code32|boot64, default: code32)\n");
        fprintf(stderr, "  --linux-rsi MODE    Linux RSI base (base|hdr, default: base)\n");
        fprintf(stderr, "  --cmdline \"...\"     Kernel command line (for --linux)\n");
        fprintf(stderr, "  --initrd <file>     Initrd image to load (for --linux)\n");
        fprintf(stderr, "  --entry ADDR        Set entry point (default: 0x80001000)\n");
        fprintf(stderr, "  --load OFFSET       Set load offset (default: 0x1000)\n");
        fprintf(stderr, "  --verbose, -v       Enable basic debug logging (VM exits, hypercalls)\n");
        fprintf(stderr, "  --debug LEVEL       Set debug verbosity (0=none, 1=basic, 2=detailed, 3=all)\n");
        fprintf(stderr, "  --dump-regs         Dump all registers on each VM exit\n");
        fprintf(stderr, "  --dump-mem FILE     Dump guest memory to file on exit\n");
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s guest/multiplication.bin guest/counter.bin\n", argv[0]);
        fprintf(stderr, "  %s --paging --verbose os-1k/kernel.bin\n", argv[0]);
        fprintf(stderr, "  %s --linux bzImage --cmdline \"console=ttyS0\"\n", argv[0]);
        return 1;
    }

    // Parse flags
    int i;
    for (i = 1; i < argc && argv[i][0] == '-'; i++)
    {
        if (strcmp(argv[i], "--paging") == 0)
        {
            enable_paging = true;
        }
        else if (strcmp(argv[i], "--long-mode") == 0)
        {
            enable_long_mode = true;
            enable_paging = true; // Long mode requires paging
        }
        else if (strcmp(argv[i], "--linux") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --linux requires a bzImage path\n");
                return 1;
            }
            bzimage_path = argv[i + 1];
            linux_boot = true;
            // Guest binary path will be used as bzImage path
            i++;
        }
        else if (strcmp(argv[i], "--linux-entry") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --linux-entry requires an argument (setup|code32|boot64)\n");
                return 1;
            }
            if (strcmp(argv[i + 1], "setup") == 0)
            {
                linux_entry = LINUX_ENTRY_SETUP;
            }
            else if (strcmp(argv[i + 1], "code32") == 0)
            {
                linux_entry = LINUX_ENTRY_CODE32;
            }
            else if (strcmp(argv[i + 1], "boot64") == 0)
            {
                linux_entry = LINUX_ENTRY_BOOT64;
            }
            else
            {
                fprintf(stderr, "Error: invalid --linux-entry '%s' (expected setup|code32|boot64)\n", argv[i + 1]);
                return 1;
            }
            i++;
        }
        else if (strcmp(argv[i], "--linux-rsi") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --linux-rsi requires an argument (base|hdr)\n");
                return 1;
            }
            if (strcmp(argv[i + 1], "base") == 0)
            {
                linux_rsi = LINUX_RSI_BASE;
            }
            else if (strcmp(argv[i + 1], "hdr") == 0)
            {
                linux_rsi = LINUX_RSI_HDR;
            }
            else
            {
                fprintf(stderr, "Error: invalid --linux-rsi '%s' (expected base|hdr)\n", argv[i + 1]);
                return 1;
            }
            i++;
        }
        else if (strcmp(argv[i], "--cmdline") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --cmdline requires an argument\n");
                return 1;
            }
            linux_cmdline = argv[i + 1];
            i++;
        }
        else if (strcmp(argv[i], "--initrd") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --initrd requires an argument\n");
                return 1;
            }
            initrd_path = argv[i + 1];
            i++;
        }
        else if (strcmp(argv[i], "--entry") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --entry requires an argument\n");
                return 1;
            }
            entry_point = strtoul(argv[i + 1], NULL, 0);
            i++;
        }
        else if (strcmp(argv[i], "--load") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --load requires an argument\n");
                return 1;
            }
            load_offset = strtoul(argv[i + 1], NULL, 0);
            i++;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            verbose = true;
            debug_level = DEBUG_BASIC;
        }
        else if (strcmp(argv[i], "--debug") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --debug requires a level (0-3)\n");
                return 1;
            }
            int level = atoi(argv[i + 1]);
            if (level < 0 || level > 3)
            {
                fprintf(stderr, "Error: debug level must be 0-3\n");
                return 1;
            }
            debug_level = (debug_level_t)level;
            verbose = (level > 0);
            i++;
        }
        else if (strcmp(argv[i], "--dump-regs") == 0)
        {
            // Flag will be checked in VM exit handler
        }
        else if (strcmp(argv[i], "--dump-mem") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --dump-mem requires a filename\n");
                return 1;
            }
            // Store filename for later use
            i++;
        }
        else
        {
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            return 1;
        }
    }
    guest_arg_start = i;

    if (linux_boot)
    {
        if (!bzimage_path)
        {
            fprintf(stderr, "Error: --linux requires a bzImage path\n");
            return 1;
        }
        num_vcpus = 1;
    }
    else
    {
        // Determine number of guests
        num_vcpus = argc - guest_arg_start;
        if (num_vcpus == 0)
        {
            fprintf(stderr, "Error: No guest binaries specified\n");
            return 1;
        }
        if (num_vcpus > MAX_VCPUS)
        {
            fprintf(stderr, "Error: Too many guests (max %d)\n", MAX_VCPUS);
            return 1;
        }
    }

    printf("=== Multi-vCPU KVM VMM (x86) ===\n");
    if (linux_boot)
    {
        printf("Mode: Linux Boot Protocol\n");
        printf("bzImage: %s\n", bzimage_path);
        if (linux_cmdline)
        {
            printf("Command line: %s\n", linux_cmdline);
        }
        if (initrd_path)
        {
            printf("Initrd: %s\n", initrd_path);
        }
    }
    else if (enable_paging)
    {
        printf("Mode: Protected Mode with Paging\n");
        printf("Entry point: 0x%x\n", entry_point);
        printf("Load offset: 0x%x\n", load_offset);
    }
    else
    {
        printf("Mode: Real Mode\n");
    }
    if (!linux_boot)
    {
        printf("Starting %d vCPU(s)\n\n", num_vcpus);
    }

    // Set terminal to raw mode for character-by-character input (Paging guests + Linux console)
    if (enable_paging || linux_boot)
    {
        set_raw_mode();
    }

    // Step 1: Initialize KVM and create VM
    // Only create IRQCHIP for Protected Mode (paging enabled)
    if (init_kvm(enable_paging, linux_boot) < 0)
    {
        ret = 1;
        goto cleanup_early;
    }

    // Step 1.5: Linux Boot Protocol Setup
    if (linux_boot)
    {
        printf("\n=== Linux Boot Protocol Setup ===\n");

        // Setup single vCPU context for Linux kernel
        vcpu_context_t *ctx = &vcpus[0];
        memset(ctx, 0, sizeof(*ctx));
        ctx->vcpu_id = 0;
        ctx->guest_binary = bzimage_path;
        snprintf(ctx->name, sizeof(ctx->name), "Linux");
        ctx->vcpu_fd = -1;
        ctx->use_paging = false;  // Enter protected mode (no paging) at code32_start
        ctx->long_mode = false;
        ctx->entry_point = 0;     // Will be set to code32_start after load
        ctx->load_offset = 0;
        ctx->linux_guest = true;
        ctx->linux_entry = linux_entry;
        ctx->linux_rsi = linux_rsi;

        // Allocate guest memory
        if (setup_vcpu_memory(ctx) < 0)
        {
            ret = 1;
            goto cleanup_vcpus;
        }

        struct boot_params *boot_params = (struct boot_params *)(ctx->guest_mem + LINUX_BOOT_PARAMS_ADDR);
        memset(boot_params, 0, sizeof(*boot_params));

        // Setup a minimal IVT so bzImage setup code can execute basic interrupts safely
        setup_linux_ivt(ctx->guest_mem);

        // Load Linux kernel bzImage
        printf("Loading bzImage...\n");
        if (load_linux_kernel(ctx->guest_binary, ctx->guest_mem, ctx->mem_size, boot_params) < 0)
        {
            fprintf(stderr, "Error: Failed to load Linux kernel\n");
            ret = 1;
            goto cleanup_vcpus;
        }

        // Setup boot parameters (E820 memory map, etc.)
        printf("Setting up boot parameters...\n");
        setup_linux_boot_params(boot_params, ctx->mem_size, linux_cmdline);

        // Load initrd if provided
        if (initrd_path)
        {
            printf("Loading initrd...\n");
            if (load_initrd(initrd_path, ctx->guest_mem, ctx->mem_size, boot_params) < 0)
            {
                fprintf(stderr, "Error: Failed to load initrd\n");
                ret = 1;
                goto cleanup_vcpus;
            }
        }

        // Detect 64-bit kernel
        if (boot_params->hdr.xloadflags & XLF_KERNEL_64)
        {
            printf("Detected 64-bit Linux kernel\n");
            ctx->long_mode = true;
            enable_long_mode = true;
        }
        else
        {
            printf("Detected 32-bit Linux kernel\n");
        }

        if (linux_entry == LINUX_ENTRY_BOOT64)
        {
            if (!(boot_params->hdr.xloadflags & XLF_KERNEL_64))
            {
                fprintf(stderr, "Error: --linux-entry boot64 requires a 64-bit kernel (XLF_KERNEL_64)\n");
                ret = 1;
                goto cleanup_vcpus;
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
        printf("linux RSI mode: %s\n", (linux_rsi == LINUX_RSI_BASE) ? "base" : "hdr");
        printf("Real-mode setup: 0x%x:0x0200\n", (unsigned)(REAL_MODE_KERNEL_ADDR / 16));

        // Copy command line to guest memory if provided
        if (linux_cmdline)
        {
            size_t cmdline_len = strlen(linux_cmdline) + 1;
            if (cmdline_len > 256)
            {
                fprintf(stderr, "Warning: Command line truncated to 255 characters\n");
                cmdline_len = 256;
            }
            memcpy(ctx->guest_mem + COMMAND_LINE_ADDR, linux_cmdline, cmdline_len);
            printf("Command line copied to 0x%x\n", COMMAND_LINE_ADDR);
        }

        // Create and initialize vCPU
        printf("Initializing vCPU for Linux kernel...\n");
        if (setup_vcpu_context(ctx) < 0)
        {
            ret = 1;
            goto cleanup_vcpus;
        }

        // Optional: enable KVM single-step for early Linux bring-up debugging
        if (debug_level == DEBUG_ALL)
        {
            ctx->singlestep_remaining = 2000;
            ctx->singlestep_paused = false;
            ctx->singlestep_exits = 0;
            if (set_guest_singlestep(ctx, true) < 0)
            {
                ret = 1;
                goto cleanup_vcpus;
            }
        }

        printf("Linux boot setup complete!\n\n");
        num_vcpus = 1;  // Linux boot uses single vCPU
    }

    // Step 2: Setup each vCPU (skip if Linux boot mode - already set up)
    if (!linux_boot)
    {
        for (int i = 0; i < num_vcpus; i++)
        {
            vcpu_context_t *ctx = &vcpus[i];

            // Initialize context
            memset(ctx, 0, sizeof(*ctx));
            ctx->vcpu_id = i;
            ctx->guest_binary = argv[guest_arg_start + i];
            snprintf(ctx->name, sizeof(ctx->name), "%s", extract_guest_name(ctx->guest_binary));
            ctx->vcpu_fd = -1;

            // Set paging mode settings
            ctx->use_paging = enable_paging;
            ctx->long_mode = enable_long_mode;
            ctx->entry_point = entry_point;
            ctx->load_offset = enable_paging ? load_offset : 0;

            if (verbose)
            {
                printf("[Setup vCPU %d: %s]\n", i, ctx->name);
            }

            // Allocate and map memory for this vCPU
            if (setup_vcpu_memory(ctx) < 0)
            {
                ret = 1;
                goto cleanup_vcpus;
            }

            // Load guest binary into this vCPU's memory
            if (load_guest_binary(ctx->guest_binary, ctx->guest_mem, ctx->mem_size, ctx->load_offset) < 0)
            {
                ret = 1;
                goto cleanup_vcpus;
            }

            // Create and initialize vCPU
            if (setup_vcpu_context(ctx) < 0)
            {
                ret = 1;
                goto cleanup_vcpus;
            }

            printf("\n");
        }
    }

    // Initialize dynamic colors for vCPUs (maximum contrast based on count)
    init_vcpu_colors(num_vcpus);

    // Step 3: Start stdin thread (Paging guests + Linux console)
    // Real Mode guests don't use interactive input, so skip these threads
    // NOTE: Timer thread is disabled - it injects IRQ0 that causes triple faults
    // if guest IDT is not properly set up. stdin thread is safe (no interrupt injection).
    if (enable_paging || linux_boot)
    {
        // Timer thread disabled - causes triple faults before IDT setup
        // timer_thread_running = true;
        // if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0)
        // {
        //     fprintf(stderr, "Warning: Failed to create timer thread. Timer interrupts disabled.\n");
        //     timer_thread_running = false;
        // }

        stdin_thread_running = true;
        linux_serial_input_enabled = linux_boot;
        if (pthread_create(&stdin_thread, NULL, stdin_monitor_thread_func, NULL) != 0)
        {
            fprintf(stderr, "Warning: Failed to create stdin monitoring thread. Keyboard input disabled.\n");
            stdin_thread_running = false;
            linux_serial_input_enabled = false;
        }
    }

    // Step 4: Spawn vCPU threads
    printf("=== Starting VM execution (%d vCPUs) ===\n", num_vcpus);

    // Print color legend for multi-vCPU mode
    if (num_vcpus > 1)
    {
        printf("Legend: ");
        for (int i = 0; i < num_vcpus && i < MAX_VCPUS; i++)
        {
            printf("\033[38;5;%dm[%s]\033[0m ", vcpu_colors[i], vcpus[i].name);
        }
        printf("\n");
    }
    printf("\n");

    for (int i = 0; i < num_vcpus; i++)
    {
        if (pthread_create(&threads[i], NULL, vcpu_thread, &vcpus[i]) != 0)
        {
            fprintf(stderr, "Failed to create thread for vCPU %d\n", i);
            ret = 1;
            goto cleanup_stdin;
        }
    }

    // Step 5: Wait for all vCPUs to finish
    for (int i = 0; i < num_vcpus; i++)
    {
        pthread_join(threads[i], NULL);
    }

    printf("\n=== All vCPUs completed ===\n");

cleanup_stdin:
    // Stop monitoring threads immediately after vCPUs complete
    if (timer_thread_running)
    {
        timer_thread_running = false;
        pthread_join(timer_thread, NULL);
    }

    if (stdin_thread_running)
    {
        stdin_thread_running = false;
        pthread_join(stdin_thread, NULL);
    }
    linux_serial_input_enabled = false;

cleanup_vcpus:
    // Cleanup all vCPUs
    for (int i = 0; i < num_vcpus; i++)
    {
        cleanup_vcpu(&vcpus[i]);
    }

cleanup_early:
    // Restore terminal settings
    restore_terminal();

    // Cleanup global resources
    if (vm_fd >= 0)
        close(vm_fd);
    if (kvm_fd >= 0)
        close(kvm_fd);

    return ret;
}
