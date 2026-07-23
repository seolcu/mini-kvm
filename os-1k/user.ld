OUTPUT_FORMAT("elf32-i386")
ENTRY(start)

SECTIONS {
    . = 0x01000000;  /* USER_BASE */

    .text.start : { *(.text.start) }
    .text : { *(.text*) }
    .rodata : ALIGN(4) { *(.rodata*) }
    .data : ALIGN(4) { *(.data*) }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    __stack_bottom = .;
    . += 8192;  /* 8KB user stack */
    __stack_top = .;
}
