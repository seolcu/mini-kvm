/*
 * Linker script for 32-bit protected-mode guests.
 *
 * The VMM enters protected mode with paging before the first KVM_RUN and
 * jumps to 0x80001000, which its page tables map to physical 0x1000. Guests
 * built with this script are therefore loaded with --load 0x1000 and entered
 * at --entry 0x80001000 (both are the defaults for --paging).
 */

OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x80001000;

    .text : {
        *(.text*)
    }

    .rodata : ALIGN(4) {
        *(.rodata*)
    }

    .data : ALIGN(4) {
        *(.data*)
    }

    .bss : ALIGN(4) {
        *(.bss*)
        *(COMMON)
    }
}
