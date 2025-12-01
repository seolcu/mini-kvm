# 64-bit minimal hypercall guest
# Tests Long Mode setup with a simple hypercall sequence
# Prints "Hello from 64-bit!\n" using hypercalls

.code64
.section .text
.globl _start

_start:
    # Print "Hello from 64-bit!\n" using hypercalls
    # HC_PUTCHAR (0x01): BL = character to print
    
    # 'H'
    mov $0x01, %ax          # Hypercall number: HC_PUTCHAR
    mov $'H', %bl           # Character
    out %al, $0x500         # Hypercall port
    
    # 'e'
    mov $'e', %bl
    out %al, $0x500
    
    # 'l'
    mov $'l', %bl
    out %al, $0x500
    
    # 'l'
    mov $'l', %bl
    out %al, $0x500
    
    # 'o'
    mov $'o', %bl
    out %al, $0x500
    
    # ' '
    mov $' ', %bl
    out %al, $0x500
    
    # 'f'
    mov $'f', %bl
    out %al, $0x500
    
    # 'r'
    mov $'r', %bl
    out %al, $0x500
    
    # 'o'
    mov $'o', %bl
    out %al, $0x500
    
    # 'm'
    mov $'m', %bl
    out %al, $0x500
    
    # ' '
    mov $' ', %bl
    out %al, $0x500
    
    # '6'
    mov $'6', %bl
    out %al, $0x500
    
    # '4'
    mov $'4', %bl
    out %al, $0x500
    
    # '-'
    mov $'-', %bl
    out %al, $0x500
    
    # 'b'
    mov $'b', %bl
    out %al, $0x500
    
    # 'i'
    mov $'i', %bl
    out %al, $0x500
    
    # 't'
    mov $'t', %bl
    out %al, $0x500
    
    # '!'
    mov $'!', %bl
    out %al, $0x500
    
    # '\n'
    mov $'\n', %bl
    out %al, $0x500
    
    # Exit hypercall
    mov $0x00, %ax          # HC_EXIT
    out %al, $0x500
    
    # Should never reach here
    hlt
