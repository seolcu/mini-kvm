# Counter Guest: Print numbers 0-9
# Real Mode (16-bit) x86 assembly
#
# Goal: Output "0123456789" to UART port 0x3f8
# Each digit should be sent as ASCII character

.code16
.section .text
.global _start

_start:
    # TODO(human): Initialize counter
    # Hint: Use CL register to store counter (0-9)
    # You need: mov $?, %cl
    mov $0, %cl
    # TODO(human)


print_loop:
    # TODO(human): Convert number to ASCII
    # Hint: ASCII '0' = 0x30, '1' = 0x31, ..., '9' = 0x39
    # Formula: ASCII = 0x30 + number
    # Store result in AL register
    # You need: mov %cl, %al
    #           add $?, %al
    mov %cl, %al
    add $0x30, %al
    # TODO(human)


    # TODO(human): Send character to UART
    # Hint: Port 0x3f8 (COM1)
    # You need: mov $0x3f8, %dx
    #           out %al, (%dx)
    mov $0x3f8, %dx
    out %al, (%dx)
    # TODO(human)


    # TODO(human): Increment counter
    # Hint: inc %cl
    inc %cl
    # TODO(human)


    # TODO(human): Check if done (counter == 10)
    # Hint: cmp $10, %cl
    #       jl print_loop    (jump if less than)
    cmp $10, %cl
    jl print_loop
    # TODO(human)


    # Done - halt CPU
    hlt
