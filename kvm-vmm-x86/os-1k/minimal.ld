/* Minimal linker script for tiny protected mode kernel */
OUTPUT_FORMAT("binary")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x80001000;

    .text.boot : {
        *(.text.boot)
    }

    /* Strip all other sections */
    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame*)
        *(.gnu*)
    }
}
