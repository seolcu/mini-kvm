/*
 * boot.S - x86 Protected Mode boot code for 1K OS
 *
 * This is the entry point for the kernel. It runs in Protected Mode (32-bit)
 * with paging enabled by the VMM.
 */

.code32
.section .text.boot
.global _start

_start:
    /*
     * At this point, the VMM has already:
     * - Enabled Protected Mode (CR0.PE = 1)
     * - Enabled paging (CR0.PG = 1, CR3 = page directory)
     * - Setup flat segments (CS=0x08, DS/ES/SS=0x10)
     * - Set EIP to _start
     *
     * IMPORTANT: Do NOT reload segment registers here!
     * The VMM sets them up directly without a GDT, and trying
     * to reload them would cause a triple fault.
     */

    # Setup stack pointer
    # Stack grows downward from __stack_top
    movl $__stack_top, %esp
    movl $0, %ebp

    # Clear BSS section
    # BSS is uninitialized data that needs to be zeroed
    movl $__bss, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx          # ECX = size of BSS in bytes
    xorl %eax, %eax          # EAX = 0
    rep stosb                # Fill [EDI..EDI+ECX] with AL (0)

    # Call kernel_main
    # This function should never return
    call kernel_main

    # If kernel_main returns, halt the CPU
1:
    hlt
    jmp 1b

.size _start, . - _start
