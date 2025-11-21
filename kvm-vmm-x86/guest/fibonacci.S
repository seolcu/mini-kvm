/* Fibonacci sequence calculation (performance test) */
.code16
.section .text
.global _start

_start:
    /* Calculate Fibonacci sequence up to N=30 */
    /* EAX = 0, EBX = 1, ECX = counter */

    xor %eax, %eax      /* F(0) = 0 */
    mov $1, %ebx        /* F(1) = 1 */
    xor %ecx, %ecx      /* counter = 0 */

    /* Output first number */
    mov %eax, %ebx      /* eax has result to output */
    call putchar_hypercall

loop:
    cmp $30, %ecx       /* Stop at 30 iterations */
    je done

    /* Calculate next Fibonacci number */
    add %ebx, %eax      /* temp = eax + ebx */
    xchg %eax, %ebx     /* swap: eax = ebx, ebx = temp */

    /* Output the number using hypercall */
    mov %ebx, %edx      /* edx = number to output */
    mov $10, %al        /* Output newline first */
    mov $0x500, %dx
    out %al, (%dx)

    mov %edx, %eax      /* eax = number */
    mov $0x500, %dx
    out %al, (%dx)      /* Output low byte */

    mov %eax, %ecx
    shr $8, %ecx
    mov %cl, %al
    out %al, (%dx)      /* Output high byte */

    /* Increment counter */
    inc %ecx
    jmp loop

done:
    /* Exit */
    mov $0, %eax        /* HC_EXIT */
    mov $0x500, %dx
    out %al, (%dx)

    hlt

putchar_hypercall:
    /* Output character in EAX */
    mov $1, %al         /* HC_PUTCHAR */
    mov %ebx, %ecx      /* value to output in ecx */
    mov $0x500, %dx
    mov %cl, %bl        /* bl = char to output */
    out %al, (%dx)
    ret
