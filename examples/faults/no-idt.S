# A Multiboot kernel that divides by zero with no IDT installed.
#
# The archetypal beginner failure: any exception is fatal because there is
# nowhere for the CPU to dispatch it. Mini-KVM should name the exception and
# say that no IDT exists, rather than reporting a reset vector.

.set ALIGN,    1 << 0
.set MEMINFO,  1 << 1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
_start:
    mov     $stack_top, %esp

    xor     %edx, %edx
    mov     $1, %eax
    xor     %ecx, %ecx
    div     %ecx            # #DE with no handler installed

    hlt
