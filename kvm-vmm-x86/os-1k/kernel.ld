OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R+X */
    data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    . = 0x80001000;

    __kernel_base = .;

    .text.boot : {
        *(.text.boot)
    } :text

    .text : {
        *(.text*)
    } :text

    .rodata : ALIGN(4) {
        *(.rodata*)
    } :text

    .data : ALIGN(4) {
        *(.data*)
    } :data

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(16);
    __stack_bottom = .;
    . = . + 0x2000;
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    __free_ram_end = 0x803FF000;  /* Leave 4KB margin below 4MB boundary */
}
