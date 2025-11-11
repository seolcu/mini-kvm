# Protected Mode Test Guest
# Native 32-bit Protected Mode code
# VMM sets up Protected Mode before running this code
# This version tests native Protected Mode code (no mode transition from Real Mode)

.code32                            # 32-bit Protected Mode code
.global _start

_start:
    # We're already in Protected Mode from the VMM
    # Segment registers are set by VMM: CS=0x8, DS/ES/FS/GS/SS=0x10
    # GDT is set up at 0x1000 by VMM

    # Print "P" via UART
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
