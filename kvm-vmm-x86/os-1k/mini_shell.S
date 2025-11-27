# Mini shell - Extended version within 26 bytes
# Shows "1K OS" message

.section .text.boot, "ax"
.global _start
.code32

_start:
    # Print "1K OS!\n"
    movb $1, %al        # HC_PUTCHAR
    movb $'1', %bl
    movw $0x500, %dx
    outb %al, %dx

    movb $'K', %bl
    outb %al, %dx

    movb $'\n', %bl
    outb %al, %dx

    hlt
