#include <stdint.h>

static inline void hypercall_putchar(char c) {
    __asm__ volatile(
        "mov %0, %%bl\n\t"
        "mov %1, %%al\n\t"
        "mov %2, %%dx\n\t"
        "outb %%al, %%dx"
        :
        : "q"((uint8_t)c), "i"(0x01), "i"(0x500)
        : "al", "bl", "dx"
    );
}

void kernel_main(void) {
    hypercall_putchar('H');
    hypercall_putchar('i');
    hypercall_putchar('\n');

    while (1) {
        __asm__ volatile("hlt");
    }
}
