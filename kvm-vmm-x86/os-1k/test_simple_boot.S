.code32
.section .text.boot
.global _start

_start:
    # Setup simple stack (no BSS clearing)
    movl $0x80003000, %esp
    movl $0, %ebp

    # Call kernel_main
    call kernel_main

    # Halt
1:
    hlt
    jmp 1b
