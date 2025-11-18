/*
 * test_kernel.c - Simple test kernel for 1K OS x86 port
 *
 * This minimal kernel tests Protected Mode with paging.
 * It prints a message using hypercalls and then halts.
 */

#include <stdint.h>
#include <stdbool.h>

/* Linker-provided symbols */
extern char __bss[];
extern char __bss_end[];
extern char __free_ram[];
extern char __free_ram_end[];

/*
 * Hypercall interface (port 0x500)
 */
#define HYPERCALL_PORT 0x500

#define HC_PUTCHAR    0x01
#define HC_EXIT       0x00

static inline void hypercall_putchar(char c) {
    __asm__ volatile(
        "mov %0, %%bl\n\t"
        "mov %1, %%al\n\t"
        "mov %2, %%dx\n\t"
        "outb %%al, %%dx"
        :
        : "q"((uint8_t)c), "i"(HC_PUTCHAR), "i"(HYPERCALL_PORT)
        : "al", "bl", "dx"
    );
}

static inline void hypercall_exit(void) {
    __asm__ volatile(
        "mov %0, %%al\n\t"
        "mov %1, %%dx\n\t"
        "outb %%al, %%dx"
        :
        : "i"(HC_EXIT), "i"(HYPERCALL_PORT)
        : "al", "dx"
    );
}

/*
 * Simple putchar implementation
 */
void putchar(char c) {
    hypercall_putchar(c);
}

/*
 * Simple puts implementation
 */
void puts(const char *s) {
    while (*s) {
        putchar(*s++);
    }
}

/*
 * Print a hexadecimal number
 */
void print_hex(uint32_t value) {
    const char *hex = "0123456789ABCDEF";
    char buf[11] = "0x00000000";

    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }

    puts(buf);
}

/*
 * Test memory access at virtual addresses
 */
void test_memory(void) {
    puts("Testing memory access...\n");

    // Test writing and reading from kernel space
    volatile uint32_t *test_addr = (volatile uint32_t *)0x80010000;
    *test_addr = 0xDEADBEEF;

    if (*test_addr == 0xDEADBEEF) {
        puts("Memory test passed: 0x80010000 is writable\n");
    } else {
        puts("Memory test FAILED\n");
    }
}

/*
 * Main kernel entry point
 * Called from boot.S after stack and BSS setup
 */
void kernel_main(void) {
    puts("\n");
    puts("=== 1K OS x86 Test Kernel ===\n");
    puts("Protected Mode with Paging Enabled\n\n");

    // Print kernel information
    puts("Kernel base:   ");
    print_hex((uint32_t)0x80001000);
    puts("\n");

    puts("BSS start:     ");
    print_hex((uint32_t)__bss);
    puts("\n");

    puts("BSS end:       ");
    print_hex((uint32_t)__bss_end);
    puts("\n");

    puts("Free RAM:      ");
    print_hex((uint32_t)__free_ram);
    puts(" - ");
    print_hex((uint32_t)__free_ram_end);
    puts("\n\n");

    // Test memory
    test_memory();

    puts("\nTest kernel completed successfully!\n");
    puts("Halting CPU...\n");

    // Halt the CPU
    while (1) {
        __asm__ volatile("hlt");
    }
}
