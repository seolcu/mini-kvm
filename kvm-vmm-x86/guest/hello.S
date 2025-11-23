# Guest code that prints "Hello, KVM!" to UART
# Real Mode (16-bit) x86 assembly

.code16                 # Real mode (16-bit)

.section .text
.global _start

_start:
    # Initialize
    lea message, %si    # SI = pointer to message (use lea for position-independent code)

print_loop:
    lodsb               # Load byte from [SI] into AL, increment SI
    test %al, %al       # Check if AL == 0 (null terminator)
    jz done             # If zero, we're done

    # Output character using hypercall
    mov %al, %bl        # BL = character to print
    mov $0x01, %al      # AL = HC_PUTCHAR (0x01)
    mov $0x500, %dx     # DX = HYPERCALL_PORT
    out %al, (%dx)      # Make hypercall

    jmp print_loop      # Continue loop

done:
    hlt                 # Halt CPU

.section .rodata
message:
    .asciz "Hello, KVM!\n"
