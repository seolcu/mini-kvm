/*
 * kernel.ld - Linker script for 1K OS x86 kernel
 *
 * Memory layout:
 *   Physical: 0x00000000 - 0x003FFFFF (4MB)
 *   Virtual:  0x80000000 - 0x803FFFFF (maps to physical 0x0 - 0x3FFFFF)
 *
 * The kernel runs at virtual address 0x80001000 (maps to physical 0x1000)
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at virtual address 0x80001000 */
    /* This maps to physical address 0x00001000 (4KB offset) */
    . = 0x80001000;

    __kernel_base = .;

    /* Boot code (must be first) */
    .text.boot : {
        *(.text.boot)
    }

    /* Code section */
    .text : {
        *(.text*)
    }

    /* Read-only data */
    .rodata : ALIGN(4) {
        *(.rodata*)
    }

    /* Initialized data */
    .data : ALIGN(4) {
        *(.data*)
    }

    /* Uninitialized data (BSS) */
    .bss : ALIGN(4) {
        __bss = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* Kernel stack (8KB) */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x2000;  /* 8KB stack */
    __stack_top = .;

    /* Free RAM starts after stack */
    . = ALIGN(4096);  /* Align to page boundary */
    __free_ram = .;

    /* End of kernel (approximately 0x80103000) */
    /* Free RAM extends from here to 0x803FFFFF (end of mapped 4MB) */
    __free_ram_end = 0x80400000;  /* End of 4MB region */
}
