# Test: Exact kernel binary structure
# Same call offset as kernel.bin (e8 c1 0b 00 00)
.code32
.section .text.boot, "ax"
.globl _start

_start:
    # Exact byte sequence from kernel.bin boot
    movl $0x80017670, %esp      # bc 70 76 01 80
    movl $0, %ebp               # bd 00 00 00 00
    movl $0x80004300, %edi      # bf 00 43 00 80
    movl $0x80015664, %ecx      # b9 64 56 01 80
    subl %edi, %ecx             # 29 f9
    xorl %eax, %eax             # 31 c0
    rep stosb                   # f3 aa
    
    # Call with EXACT same offset as kernel.bin
    # From offset 0x1a (after rep stosb), call to 0xbe0
    # Relative offset = 0xbe0 - (0x1a + 5) = 0xbe0 - 0x1f = 0xbc1
    call test_kernel_main       # e8 c1 0b 00 00
    
    # hlt after call (same as kernel.bin)
    hlt                         # f4
    jmp .                       # eb fd

# Pad to match kernel.bin structure (0x22 bytes of padding after hlt)
.org 0x22
padding:
    .fill 0xbbe, 1, 0           # Fill up to 0xbe0

# Put test_kernel_main at EXACT same offset as real kernel_main
.org 0xbe0
test_kernel_main:
    # Output 'M' to show we reached kernel_main
    movb $'M', %al
    movw $0x3f8, %dx
    outb %al, %dx
    ret
