# 64-bit minimal hypercall guest
# Tests Long Mode setup with a simple hypercall sequence
# Prints "Hello from 64-bit!\n" using hypercalls

.code64
.section .text
.globl _start

_start:
    # Print "Hello from 64-bit!\n" using hypercalls
    # HC_PUTCHAR (0x01): BL = character to print
    # Port 0x500 requires DX register (16-bit port address)
    
    mov $0x500, %dx         # Hypercall port (must use DX for ports > 0xFF)
    
    # 'H'
    mov $0x01, %ax          # Hypercall number: HC_PUTCHAR
    mov $'H', %bl           # Character
    out %al, %dx            # Hypercall via port in DX
    
    # 'e'
    mov $'e', %bl
    out %al, %dx
    
    # 'l'
    mov $'l', %bl
    out %al, %dx
    
    # 'l'
    mov $'l', %bl
    out %al, %dx
    
    # 'o'
    mov $'o', %bl
    out %al, %dx
    
    # ' '
    mov $' ', %bl
    out %al, %dx
    
    # 'f'
    mov $'f', %bl
    out %al, %dx
    
    # 'r'
    mov $'r', %bl
    out %al, %dx
    
    # 'o'
    mov $'o', %bl
    out %al, %dx
    
    # 'm'
    mov $'m', %bl
    out %al, %dx
    
    # ' '
    mov $' ', %bl
    out %al, %dx
    
    # '6'
    mov $'6', %bl
    out %al, %dx
    
    # '4'
    mov $'4', %bl
    out %al, %dx
    
    # '-'
    mov $'-', %bl
    out %al, %dx
    
    # 'b'
    mov $'b', %bl
    out %al, %dx
    
    # 'i'
    mov $'i', %bl
    out %al, %dx
    
    # 't'
    mov $'t', %bl
    out %al, %dx
    
    # '!'
    mov $'!', %bl
    out %al, %dx
    
    # '\n'
    mov $'\n', %bl
    out %al, %dx
    
    # Exit hypercall
    mov $0x00, %ax          # HC_EXIT
    out %al, %dx
    
    # Should never reach here
    hlt
