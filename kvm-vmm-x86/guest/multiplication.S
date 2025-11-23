# Multiplication Table Guest (2-9 dan)
# Simplified version: prints "2x1=2 2x2=4 ..." on single line
#
# Register usage:
#   SI = dan (outer loop 2-9)
#   DI = multiplier (inner loop 1-9)
#   AL, BL, DL = temporary

.code16
.section .text
.global _start

.equ HC_PUTCHAR, 0x01
.equ HC_EXIT, 0x00
.equ HYPERCALL_PORT, 0x500

_start:
    mov $2, %si             # SI = dan start (2)

outer:
    cmp $10, %si            # if SI >= 10, done
    jge finish

    mov $1, %di             # DI = multiplier start (1)

inner:
    cmp $10, %di            # if DI >= 10, next dan
    jge next_dan

    # Print SI (dan)
    mov %si, %ax
    add $0x30, %al
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print 'x'
    mov $'x', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print DI (multiplier)
    mov %di, %ax
    add $0x30, %al
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print '='
    mov $'=', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Calculate SI * DI
    mov %si, %ax            # AX = SI (dan)
    mov %di, %bx            # BX = DI (multiplier)
    mul %bx                 # AX = SI * DI (result max 81, fits in AX)
    # Result in AX

    # Print result (1 or 2 digits, max 81)
    cmp $10, %al
    jl print_one_digit

    # Two digit result >= 10: separate tens and ones
    mov %al, %bl            # BL = result (0-81)
    mov $0, %al             # AL = 0
    mov $10, %cl            # CL = 10

divide_loop:
    cmp $10, %bl
    jl divide_done
    sub $10, %bl
    inc %al
    jmp divide_loop

divide_done:
    # AL = tens digit, BL = ones digit
    # Save ones digit to use later
    mov %bl, %cl            # CL = ones digit (save it!)

    # Print tens
    add $0x30, %al          # Convert tens to ASCII
    mov %al, %bl            # BL = tens ASCII
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Print ones (restore from CL)
    mov %cl, %al            # AL = ones digit
    add $0x30, %al          # Convert ones to ASCII
    mov %al, %bl            # BL = ones ASCII
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    jmp print_space

print_one_digit:
    add $0x30, %al
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

print_space:
    # Print space
    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    # Next multiplier
    inc %di
    jmp inner

next_dan:
    # Next dan
    inc %si
    jmp outer

finish:
    # Exit
    mov $HC_EXIT, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    hlt
