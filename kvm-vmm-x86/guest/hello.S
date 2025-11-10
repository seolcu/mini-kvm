# Guest code that prints "Hello, KVM!" to UART
# Real Mode (16-bit) x86 assembly

.code16                 # Real mode (16-bit)

.section .text
.global _start

_start:
    # Initialize
    mov $message, %si   # SI = pointer to message

print_loop:
    lodsb               # Load byte from [SI] into AL, increment SI
    test %al, %al       # Check if AL == 0 (null terminator)
    jz done             # If zero, we're done

    # Output character to UART port 0x3f8
    mov $0x3f8, %dx     # DX = port number
    out %al, (%dx)      # Send AL to port DX

    jmp print_loop      # Continue loop

done:
    hlt                 # Halt CPU

.section .rodata
message:
    .asciz "Hello, KVM!\n"
