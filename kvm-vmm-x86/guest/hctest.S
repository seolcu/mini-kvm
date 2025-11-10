# Hypercall Test Guest
# Demonstrates all hypercall functions

.code16
.section .text
.global _start

# Hypercall definitions
.equ HC_EXIT, 0x00
.equ HC_PUTCHAR, 0x01
.equ HC_PUTNUM, 0x02
.equ HC_NEWLINE, 0x03
.equ HYPERCALL_PORT, 0x500

_start:
    # Test 1: Output "Hello!"
    mov $'H', %bl
    call putchar_hc

    mov $'e', %bl
    call putchar_hc

    mov $'l', %bl
    call putchar_hc

    mov $'l', %bl
    call putchar_hc

    mov $'o', %bl
    call putchar_hc

    mov $'!', %bl
    call putchar_hc

    call newline_hc

    # Test 2: Output numbers
    mov $42, %bx
    call putnum_hc

    call newline_hc

    mov $1234, %bx
    call putnum_hc

    call newline_hc

    # Test 3: Exit via hypercall
    mov $HC_EXIT, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Should never reach here
    hlt

# Hypercall wrappers
putchar_hc:
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    ret

putnum_hc:
    mov $HC_PUTNUM, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    ret

newline_hc:
    mov $HC_NEWLINE, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    ret
