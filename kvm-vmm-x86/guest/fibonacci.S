/* Fibonacci sequence calculation */
/* Print first 15 Fibonacci numbers: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377 */
.code16
.section .text
.global _start

.equ HC_PUTCHAR, 0x01
.equ HYPERCALL_PORT, 0x500

_start:
    /* Print header */
    lea header, %si
print_header:
    lodsb
    test %al, %al
    jz fib_start
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    jmp print_header

fib_start:
    /* F(n-2) in BP, F(n-1) in DI */
    mov $0, %bp             /* BP = F(n-2) = 0 */
    mov $1, %di             /* DI = F(n-1) = 1 */
    mov $0, %cx             /* CX = counter (which F we just printed) */

    /* Print F(0) = 0 */
    push %cx
    push %di
    push %bp
    mov %bp, %ax
    call print_number
    pop %bp
    pop %di
    pop %cx

    /* Print separator and F(1) = 1 */
    call print_sep
    inc %cx
    push %cx
    push %di
    push %bp
    mov %di, %ax
    call print_number
    pop %bp
    pop %di
    pop %cx

fib_loop:
    inc %cx
    cmp $15, %cx            /* Print 15 numbers (F0-F14) */
    jge done

    /* Print separator */
    call print_sep

    /* Calculate F(n) = F(n-1) + F(n-2) */
    mov %di, %ax            /* AX = F(n-1) */
    add %bp, %ax            /* AX = F(n-1) + F(n-2) = F(n) */
    mov %di, %bp            /* F(n-2) = old F(n-1) */
    mov %ax, %di            /* F(n-1) = F(n) */

    /* Print F(n) */
    push %cx
    push %di
    push %bp
    call print_number
    pop %bp
    pop %di
    pop %cx

    jmp fib_loop

done:
    /* Print newline */
    mov $'\n', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)

    hlt

/* Print ", " separator */
print_sep:
    mov $',', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    mov $' ', %bl
    mov $HC_PUTCHAR, %al
    out %al, (%dx)
    ret

/* Print number in AX as decimal */
/* Destroys: AX, BX, DX, SI */
print_number:
    mov %ax, %si            /* Save original number */
    mov $0, %bh             /* Digit count */

    /* Special case: 0 */
    test %ax, %ax
    jnz extract_digits
    mov $'0', %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    ret

extract_digits:
    /* Extract digits by repeated division by 10 */
    /* Push digits onto stack (reverse order) */
    mov %si, %ax

divide_loop:
    test %ax, %ax
    jz print_digits
    mov $0, %dx             /* Clear high word for div */
    mov $10, %cx
    div %cx                 /* AX = quotient, DX = remainder */
    push %dx                /* Push digit */
    inc %bh                 /* Count digits */
    jmp divide_loop

print_digits:
    test %bh, %bh
    jz print_done
    pop %ax                 /* Get digit */
    add $'0', %al           /* Convert to ASCII */
    mov %al, %bl
    mov $HC_PUTCHAR, %al
    mov $HYPERCALL_PORT, %dx
    out %al, (%dx)
    dec %bh
    jmp print_digits

print_done:
    ret

.section .rodata
header:
    .asciz "Fibonacci: "
