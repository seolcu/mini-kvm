# Matrix Multiplication Guest (CPU-intensive benchmark)
# Computes C = A × B where A, B, C are 8x8 matrices
# Uses nested loops for O(n³) complexity
#
# Register usage:
#   SI = i (row of A)
#   DI = j (column of B)
#   BP = k (inner loop for dot product)
#   Stack: stores matrices A, B, C

.code16
.section .text
.global _start

.equ HC_PUTCHAR, 0x01
.equ HC_EXIT, 0x00
.equ HYPERCALL_PORT, 0x500
.equ N, 8                    # Matrix size (8x8)

_start:
    # Initialize stack pointer
    mov $0x7000, %sp
    
    # Print header
    call print_header
    
    # Initialize matrices A and B
    call init_matrices
    
    # Perform matrix multiplication C = A × B
    call matrix_multiply
    
    # Print result summary
    call print_result
    
    # Exit
    mov $HC_EXIT, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    hlt

#--------------------------------------
# Print header message
#--------------------------------------
print_header:
    mov $msg_start, %si
print_header_loop:
    lodsb                    # AL = [SI++]
    test %al, %al
    jz print_header_done
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    jmp print_header_loop
print_header_done:
    ret

#--------------------------------------
# Initialize matrices A and B
# A[i][j] = i + j (simple pattern)
# B[i][j] = i - j + N
#--------------------------------------
init_matrices:
    xor %si, %si             # i = 0
init_outer:
    cmp $N, %si
    jge init_done
    
    xor %di, %di             # j = 0
init_inner:
    cmp $N, %di
    jge init_next_row
    
    # A[i][j] = (i + j) % 10 + 1
    mov %si, %ax
    add %di, %ax
    xor %dx, %dx
    mov $10, %cx
    div %cx                  # DX = (i+j) % 10
    inc %dx                  # +1 (values 1-10)
    
    # Calculate address: &A[i][j] = matrix_a + (i*N + j)
    mov %si, %ax
    mov $N, %cx
    mul %cx                  # AX = i * N
    add %di, %ax             # AX = i*N + j
    mov $matrix_a, %bx
    add %ax, %bx
    mov %dl, (%bx)           # Store A[i][j]
    
    # B[i][j] = (i - j + N) % 10 + 1
    mov %si, %ax
    sub %di, %ax
    add $N, %ax
    xor %dx, %dx
    mov $10, %cx
    div %cx
    inc %dx
    
    mov %si, %ax
    mov $N, %cx
    mul %cx
    add %di, %ax
    mov $matrix_b, %bx
    add %ax, %bx
    mov %dl, (%bx)           # Store B[i][j]
    
    inc %di
    jmp init_inner
    
init_next_row:
    inc %si
    jmp init_outer
    
init_done:
    ret

#--------------------------------------
# Matrix multiplication: C = A × B
# C[i][j] = Σ(k=0 to N-1) A[i][k] * B[k][j]
#--------------------------------------
matrix_multiply:
    xor %si, %si             # i = 0
mul_outer:
    cmp $N, %si
    jge mul_done
    
    xor %di, %di             # j = 0
mul_middle:
    cmp $N, %di
    jge mul_next_row
    
    # Compute C[i][j] = sum of A[i][k] * B[k][j]
    xor %cx, %cx             # sum = 0
    xor %bp, %bp             # k = 0
    
mul_inner:
    cmp $N, %bp
    jge mul_store
    
    # Load A[i][k]
    mov %si, %ax
    mov $N, %bx
    mul %bx                  # AX = i * N
    add %bp, %ax             # AX = i*N + k
    mov $matrix_a, %bx
    add %ax, %bx
    mov (%bx), %al           # AL = A[i][k]
    xor %ah, %ah             # AX = A[i][k] (zero-extend)
    push %ax                 # Save A[i][k]
    
    # Load B[k][j]
    mov %bp, %ax
    mov $N, %bx
    mul %bx                  # AX = k * N
    add %di, %ax             # AX = k*N + j
    mov $matrix_b, %bx
    add %ax, %bx
    mov (%bx), %al           # AL = B[k][j]
    xor %ah, %ah             # AX = B[k][j] (zero-extend)
    mov %ax, %dx             # DX = B[k][j]
    pop %ax                  # Restore A[i][k]
    
    # Multiply A[i][k] * B[k][j]
    mul %dx                  # AX = A[i][k] * B[k][j]
    add %ax, %cx             # sum += AX
    
    inc %bp
    jmp mul_inner
    
mul_store:
    # Store C[i][j] = sum
    mov %si, %ax
    mov $N, %bx
    mul %bx
    add %di, %ax
    mov $matrix_c, %bx
    add %ax, %bx
    mov %cx, (%bx)           # C[i][j] = sum (16-bit)
    
    inc %di
    jmp mul_middle
    
mul_next_row:
    inc %si
    jmp mul_outer
    
mul_done:
    ret

#--------------------------------------
# Print result summary
# Prints C[0][0], C[N-1][N-1] and "Done"
#--------------------------------------
print_result:
    # Print "C[0][0] = "
    mov $msg_c00, %si
    call print_string
    
    # Print C[0][0] value
    mov matrix_c, %ax
    call print_number
    
    # Print newline
    mov $'\n', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    
    # Print "C[7][7] = "
    mov $msg_c77, %si
    call print_string
    
    # Print C[N-1][N-1] value
    mov $N-1, %ax
    mov $N, %bx
    mul %bx
    add $N-1, %ax
    shl $1, %ax              # *2 for 16-bit values
    mov $matrix_c, %bx
    add %ax, %bx
    mov (%bx), %ax
    call print_number
    
    # Print done message
    mov $msg_done, %si
    call print_string
    
    ret

#--------------------------------------
# Helper: Print null-terminated string
# Input: SI = string address
#--------------------------------------
print_string:
    lodsb
    test %al, %al
    jz print_string_done
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    jmp print_string
print_string_done:
    ret

#--------------------------------------
# Helper: Print number in AX (decimal)
#--------------------------------------
print_number:
    push %bx
    push %cx
    push %dx
    
    mov %ax, %bx             # BX = number
    mov $10000, %cx          # Start with 10000s place
    xor %dh, %dh             # DH = leading zero flag
    
print_number_loop:
    cmp $1, %cx
    jl print_number_done
    
    mov %bx, %ax
    xor %dx, %dx
    div %cx                  # AX = number / divisor
    
    test %al, %al
    jnz print_number_digit
    test %dh, %dh
    jnz print_number_digit
    jmp print_number_next
    
print_number_digit:
    mov $1, %dh              # Set flag (seen non-zero)
    add $'0', %al
    push %bx
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    pop %bx
    
print_number_next:
    # number = number % divisor
    mov %bx, %ax
    xor %dx, %dx
    div %cx
    mov %dx, %bx
    
    # divisor /= 10
    mov %cx, %ax
    mov $10, %cx
    xor %dx, %dx
    div %cx
    mov %ax, %cx
    
    jmp print_number_loop
    
print_number_done:
    # Print last digit
    add $'0', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    
    pop %dx
    pop %cx
    pop %bx
    ret

#--------------------------------------
# Data section
#--------------------------------------
.section .data

msg_start:
    .asciz "Matrix 8x8 multiplication benchmark\n"

msg_c00:
    .asciz "C[0][0] = "

msg_c77:
    .asciz "C[7][7] = "

msg_done:
    .asciz "\nMatrix multiplication complete!\n"

.section .bss

# Matrix storage (8x8 matrices)
matrix_a:
    .space N*N             # 64 bytes for A
matrix_b:
    .space N*N             # 64 bytes for B
matrix_c:
    .space N*N*2           # 128 bytes for C (16-bit values)
