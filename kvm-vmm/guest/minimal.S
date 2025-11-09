# Ultra-minimal RISC-V guest code
# Just loads some values and loops forever

.section .text
.global _start

_start:
    # Load some recognizable values into registers
    li a0, 0x42         # Magic value 1
    li a1, 0x1337       # Magic value 2
    li a2, 0xDEADBEEF   # Magic value 3
    li sp, 0x1000       # Set stack pointer

    # Infinite loop (guest will run forever until VMM stops it)
loop:
    j loop
