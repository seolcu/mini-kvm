# Multiplication Table Guest (2-9 dan)
# Prints: "2 x 1 = 2" ... "9 x 9 = 81"
#
# Register Layout:
#   CL = dan (outer loop, 2-9)
#   CH = multiplier (inner loop, 1-9)
#       (CX = dan:multiplier, separate from DX so no conflicts!)
#   DX = port number (0x500)
#   AL = hypercall number
#   BX = hypercall argument (number to print)

.code16
.section .text
.global _start

# Hypercall definitions
.equ HC_PUTCHAR, 0x01
.equ HC_PUTNUM, 0x02
.equ HC_NEWLINE, 0x03
.equ HYPERCALL_PORT, 0x500

_start:
    # Initialize outer loop: CL = dan (2-9)
    mov $2, %cl


outer_loop:
    # Initialize inner loop: CH = multiplier (1-9)
    # CH is safe even when we use DX!
    mov $1, %ch


inner_loop:
    # Print: "{dan} x {multiplier} = {result}"

    # Print dan value using hypercall
    movzx %cl, %bx          # BX = dan (extend CL to 16-bit)
    mov $HC_PUTNUM, %al     # AL = hypercall number
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print " x "
    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    mov $'x', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print multiplier value using hypercall
    movzx %ch, %bx          # BX = multiplier (extend CH to 16-bit)
    mov $HC_PUTNUM, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print " = "
    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    mov $'=', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Calculate result = dan * multiplier
    # MUL uses BL to avoid overwriting CL (our loop counter)
    mov %cl, %al            # AL = dan
    mov %ch, %bl            # BL = multiplier (BX is partially used, but we'll overwrite anyway)
    mul %bl                 # AX = AL * BL (dan * multiplier)
    movzx %al, %bx          # BX = result (8-bit is enough for max 81)
                            # CL is preserved!

    # Print result value using hypercall
    mov $HC_PUTNUM, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print newline using hypercall
    mov $HC_NEWLINE, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Continue inner loop: increment multiplier
    inc %ch                 # CH++ (next multiplier)
    cmp $10, %ch            # Check if multiplier == 10
    jl inner_loop           # Jump if multiplier < 10

    # Continue outer loop: increment dan
    inc %cl                 # CL++ (next dan)
    cmp $10, %cl            # Check if dan == 10
    jl outer_loop           # Jump if dan < 10

    # Exit via hypercall
    mov $0x00, %al          # HC_EXIT
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    hlt
