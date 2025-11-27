# Test kernel boot sequence step by step
.code32
.section .text.boot, "ax"
.globl _start

_start:
    # 1. Set stack
    movl $0x80017670, %esp
    
    # Output '1'
    movb $'1', %al
    movw $0x3f8, %dx
    outb %al, %dx
    
    # 2. Set ebp to 0
    movl $0, %ebp
    
    # Output '2'
    movb $'2', %al
    outb %al, %dx
    
    # 3. Set edi = BSS start
    movl $0x80004300, %edi
    
    # Output '3'
    movb $'3', %al
    outb %al, %dx
    
    # 4. Set ecx = BSS end
    movl $0x80015664, %ecx
    
    # Output '4'
    movb $'4', %al
    outb %al, %dx
    
    # 5. Calculate size
    subl %edi, %ecx
    
    # Output '5'
    movb $'5', %al
    outb %al, %dx
    
    # 6. Clear eax
    xorl %eax, %eax
    
    # Output '6'
    movb $'6', %al
    outb %al, %dx
    
    # 7. rep stosb - THIS IS THE CRITICAL INSTRUCTION
    rep stosb
    
    # Output '7' - if we get here, BSS clear succeeded
    movb $'7', %al
    outb %al, %dx
    
    # Output newline
    movb $'\n', %al
    outb %al, %dx
    
    # Halt
    hlt
    jmp .
