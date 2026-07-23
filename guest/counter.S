# Counter Guest: Print numbers 0-9
# Real Mode (16-bit) x86 assembly
#
# Goal: Output "0123456789" to UART port 0x3f8
# Each digit should be sent as ASCII character
#
# Educational Note: This is a learning example showing how to:
# 1. Initialize a counter register
# 2. Convert numbers to ASCII
# 3. Perform port I/O
# 4. Use loops and conditional jumps

.code16
.section .text
.global _start

_start:
    # Step 1: Initialize counter (0-9)
    mov $0, %cl

print_loop:
    # Step 2: Convert number to ASCII (0x30 + digit)
    mov %cl, %al
    add $0x30, %al

    # Step 3: Output to UART port 0x3f8 (COM1)
    mov $0x3f8, %dx
    out %al, (%dx)

    # Step 4: Increment counter
    inc %cl

    # Step 5: Check if done (counter == 10)
    cmp $10, %cl
    jl print_loop


    # Done - halt CPU
    hlt
