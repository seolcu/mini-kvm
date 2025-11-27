# Protected Mode guest WITHOUT paging
# Uses flat segments, no page tables
.code32
.section .text
.global _start
_start:
    # Set up stack
    movl $0x7000, %esp
    
    # Print message via hypercall
    movl $message, %esi
print_loop:
    movb (%esi), %bl
    testb %bl, %bl
    jz done
    
    movl $0x01, %eax    # HC_PUTCHAR
    movw $0x500, %dx
    outb %al, %dx
    
    incl %esi
    jmp print_loop

done:
    # Exit
    movl $0x00, %eax    # HC_EXIT  
    movw $0x500, %dx
    outb %al, %dx
    
    hlt
    jmp done

message:
    .asciz "Protected Mode without Paging works!\n"
