# Multiplication Table Guest (Short Version - 2x table only)
# Real Mode (16-bit) x86 assembly
#
# Output: "2x1=2 2x2=4 2x3=6 2x4=8 2x5=10 2x6=12 2x7=14 2x8=16 2x9=18 "
#
# Uses direct UART output like counter.S, not hypercalls

.code16
.section .text
.global _start

_start:
    mov $1, %cl                 # Counter for 1-9

loop:
    cmp $10, %cl
    je done

    # Output "2"
    mov $0x32, %al              # ASCII '2'
    mov $0x3f8, %dx             # UART port COM1
    out %al, (%dx)

    # Output "x"
    mov $0x78, %al              # ASCII 'x'
    out %al, (%dx)

    # Output multiplicand (1-9)
    mov %cl, %al
    add $0x30, %al              # Convert to ASCII
    out %al, (%dx)

    # Output "="
    mov $0x3d, %al              # ASCII '='
    out %al, (%dx)

    # Calculate 2 * cl
    mov %cl, %al
    shl $1, %al                 # al = cl * 2

    # Output result (may be 1 or 2 digits)
    mov $0, %ah                 # Clear ah for division
    mov $10, %bl                # Divisor for 10
    div %bl                     # al = al / 10, ah = al % 10

    # If tens digit is 0, skip it
    cmp $0, %al
    je output_ones

    # Output tens digit
    add $0x30, %al
    mov $0x3f8, %dx
    out %al, (%dx)
    mov %ah, %al
    jmp output_last

output_ones:
    mov %ah, %al

output_last:
    # Output ones digit
    add $0x30, %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Output space
    mov $0x20, %al              # ASCII space
    out %al, (%dx)

    # Next iteration
    inc %cl
    jmp loop

done:
    hlt
