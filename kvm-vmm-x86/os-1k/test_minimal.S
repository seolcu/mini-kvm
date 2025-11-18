/*
 * Minimal test for Protected Mode with paging
 * Just does a hypercall and halts
 */

.code32
.section .text.boot
.global _start

_start:
    # Try immediate hypercall (putchar 'A')
    movb $'A', %bl
    movb $0x01, %al
    movw $0x500, %dx
    outb %al, %dx

    # Halt
    hlt
    jmp .
