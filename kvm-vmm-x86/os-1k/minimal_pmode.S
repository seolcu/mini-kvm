# Minimal Protected Mode kernel - under 26 bytes to work around Zen 5 issue
# Entry point: 0x80001000 (loaded at physical 0x1000)

.section .text.boot, "ax"
.global _start
.code32

_start:
    # Just output a single character and halt
    # HC_PUTCHAR = 1, character = 'H'
    movb $1, %al        # HC_PUTCHAR
    movb $'O', %bl      # Character 'O'
    movw $0x500, %dx    # Hypercall port
    outb %al, %dx       # Trigger hypercall

    # Halt
    hlt
