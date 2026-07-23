# A Multiboot kernel that faults with an unusable stack.
#
# It points ESP into unmapped space and then takes an exception. The CPU
# cannot push the fault frame, so a recoverable fault becomes a triple fault.
# Mini-KVM should point at the stack rather than at the original exception.

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

.section .text
.global _start
_start:
    mov     $0xF0000000, %esp   # far past the end of guest memory
    ud2                         # #UD, which needs a stack to dispatch
