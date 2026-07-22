# Hypercall Test Guest
#
# Exercises the full Mini-KVM hypercall ABI (see src/hypercall.h):
#   AL = 0x00  HC_EXIT     terminate
#   AL = 0x01  HC_PUTCHAR  write BL
#   AL = 0x02  HC_GETCHAR  read one character into AL (blocking, -1 at EOF)
#
# Every hypercall returns its result in AL, so GETCHAR is a single OUT.

.code16
.section .text
.global _start

.equ HC_EXIT,         0x00
.equ HC_PUTCHAR,      0x01
.equ HC_GETCHAR,      0x02
.equ HYPERCALL_PORT,  0x500

_start:
    # Test 1: PUTCHAR - print a string via repeated hypercalls
    lea     banner, %si
    call    puts_hc

    # Test 2: PUTCHAR with a computed value - print digits 0-9
    mov     $'0', %bl
digit_loop:
    call    putchar_hc
    inc     %bl
    cmp     $'9', %bl
    jbe     digit_loop

    mov     $'\n', %bl
    call    putchar_hc

    # Test 3: GETCHAR - echo input back until end of input.
    # AL comes back as the character, or -1 (0xFF) when input is exhausted.
    lea     prompt, %si
    call    puts_hc

echo_loop:
    mov     $HC_GETCHAR, %al
    mov     $HYPERCALL_PORT, %dx
    out     %al, (%dx)          # AL = character, or 0xFF at end of input

    cmp     $0xFF, %al
    je      done

    mov     %al, %bl            # echo it back
    call    putchar_hc
    jmp     echo_loop

done:
    lea     bye, %si
    call    puts_hc

    # Test 4: EXIT
    mov     $HC_EXIT, %al
    mov     $HYPERCALL_PORT, %dx
    out     %al, (%dx)

    hlt                         # unreachable

# --- helpers ---------------------------------------------------------------

# putchar_hc: print the character in BL
putchar_hc:
    mov     $HC_PUTCHAR, %al
    mov     $HYPERCALL_PORT, %dx
    out     %al, (%dx)
    ret

# puts_hc: print the NUL-terminated string at SI (clobbers AL, BL, SI)
puts_hc:
    lodsb
    test    %al, %al
    jz      puts_done
    mov     %al, %bl
    call    putchar_hc
    jmp     puts_hc
puts_done:
    ret

.section .rodata
banner:
    .asciz "hypercall test: putchar "
prompt:
    .asciz "echo (ends at EOF): "
bye:
    .asciz "\nexit\n"
