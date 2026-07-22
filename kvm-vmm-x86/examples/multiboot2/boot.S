/*
 * Multiboot 2 entry stub.
 *
 * The header is a fixed 16-byte prologue followed by 8-byte aligned tags,
 * terminated by an end tag, rather than Multiboot 1's single flags word.
 */

.set MAGIC,        0xE85250D6
.set ARCH,         0                   /* i386 protected mode */

.section .multiboot
.align 8
header_start:
    .long MAGIC
    .long ARCH
    .long header_end - header_start
    .long -(MAGIC + ARCH + (header_end - header_start))

    /* End tag: type 0, flags 0, size 8. */
    .align 8
    .short 0
    .short 0
    .long 8
header_end:

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov     $stack_top, %esp
    push    %ebx                       /* boot information block */
    push    %eax                       /* bootloader magic */
    call    kernel_main
    cli
1:  hlt
    jmp 1b
.size _start, . - _start
