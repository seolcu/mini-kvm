# Protected Mode Test Guest
# Native 32-bit Protected Mode code
# VMM sets up Protected Mode before running this code

.code32                            # 32-bit Protected Mode code
.global _start

_start:
    # Initialize segment registers for Protected Mode
    # Segment selectors are already set by VMM
    # Just set the data segment registers

    # Print "P" via UART (port 0x3f8)
    mov $'P', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print "M" via UART
    mov $'M', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print "O" via UART
    mov $'O', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print "D" via UART
    mov $'D', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print "E" via UART
    mov $'E', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print newline via hypercall
    mov $0x03, %al                 # HC_NEWLINE
    mov $0x500, %dx
    out %al, (%dx)

    # Exit via hypercall
    mov $0x00, %al                 # HC_EXIT
    mov $0x500, %dx
    out %al, (%dx)

    # Infinite loop (should not reach here)
    jmp .
