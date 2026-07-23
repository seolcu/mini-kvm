/* Linker script for 64-bit guest binaries */

OUTPUT_FORMAT("binary")
OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
    . = 0x0;
    
    .text : {
        *(.text)
        *(.text.*)
    }
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    .bss : {
        *(.bss)
        *(.bss.*)
    }
    
    /DISCARD/ : {
        *(.note*)
        *(.comment)
        *(.eh_frame)
    }
}
