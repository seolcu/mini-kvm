# A 64-bit guest that dereferences an unmapped address.
#
# Exists to exercise the long-mode side of the fault analysis: resolving the
# faulting address means walking 4-level page tables, which is a different
# code path from the 32-bit walk the protected-mode guests use.

.code64
.section .text
.global _start

_start:
    # Well past anything the VMM mapped for a long-mode guest.
    movabs  $0x0000700000000000, %rax
    movq    (%rax), %rbx            # #PF, with no IDT to dispatch it

    hlt
