# Minimal x86 Real Mode Guest
# This is the simplest possible guest code for testing KVM

.code16                 # Real mode (16-bit)

.section .text
.global _start

_start:
    # Just halt - simplest possible guest
    hlt
