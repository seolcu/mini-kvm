.section .text
.global guest_boot
guest_boot:
    li a7, 1
    li a6, 0
    li a0, 'A'
    ecall
    li a0, 'B'
    ecall
    li a0, 'C'
    ecall

halt:
    j halt
