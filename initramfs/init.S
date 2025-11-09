.section .text
.global _start

_start:
    # Write message to stdout
    # syscall number for write is 64
    li a7, 64                # syscall number: write
    li a0, 1                 # fd: stdout
    la a1, message           # buffer
    la a2, message_end       # end address
    sub a2, a2, a1           # calculate length
    ecall

    # Sleep for 5 seconds (just loop for visibility)
    li t0, 50000000          # Large number for delay
delay_loop:
    addi t0, t0, -1
    bnez t0, delay_loop

    # Exit
    li a7, 93                # syscall number: exit
    li a0, 0                 # exit code: 0
    ecall

    # Should never reach here
halt:
    j halt

.section .rodata
message:
    .ascii "\n"
    .ascii "========================================\n"
    .ascii " RISC-V Linux with KVM - Init Started\n"
    .ascii "========================================\n"
    .ascii "\n"
    .ascii "Kernel booted successfully!\n"
    .ascii "\n"
    .ascii "To check KVM, you need to mount /dev:\n"
    .ascii "  mount -t devtmpfs none /dev\n"
    .ascii "  ls -l /dev/kvm\n"
    .ascii "\n"
    .ascii "System will halt now...\n"
    .ascii "========================================\n"
    .ascii "\n"
message_end:
