/* Linker script for x86 Real Mode guest */

OUTPUT_ARCH(i386)

SECTIONS
{
    /* Real mode code starts at address 0 */
    . = 0x0000;

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
