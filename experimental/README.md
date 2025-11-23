# Experimental Components

This directory contains experimental and research projects related to the Mini-KVM project. These components are **not part of the main deliverable** but represent explorations into advanced virtualization topics.

## Overview

### hypervisor/
**RISC-V H-extension Hypervisor (Rust)**

A minimal hypervisor implementation for RISC-V using the H-extension (hypervisor mode). This project explores different ISA approaches to virtualization beyond x86.

**Status**: Educational/Research
**Language**: Rust
**Target**: RISC-V 64-bit

**Key Files**:
- `src/main.rs` - Hypervisor entry point
- `guest.S` - Simple RISC-V guest program
- `build.sh` - Build and run script

**Requirements**:
- RISC-V toolchain (clang with riscv64 target)
- Rust nightly with riscv64gc-unknown-none-elf target
- QEMU with RISC-V H-extension support

**Note**: This is a research project exploring alternative hypervisor designs. The main VMM project (kvm-vmm-x86) is the primary focus.

---

### HLeOs/
**Rust-based x86_64 Operating System**

A hobby OS implementation in Rust, exploring modern OS design patterns. This demonstrates how a more feature-rich OS can be built for x86_64 using Rust's type system and memory safety features.

**Status**: Educational/Experimental
**Language**: Rust
**Target**: x86_64 (with bootloader)

**Key Features**:
- Custom bootloader
- GDT/IDT with interrupt handling
- Memory management with bitmap allocator
- Kernel threading API
- VGA text mode output

**Build Requirements**:
- Rust nightly toolchain
- bootimage crate
- llvm-tools-preview component

**Note**: This is an experimental project exploring Rust OS development. It is not integrated with the main KVM VMM.

---

## Why These Are Experimental

1. **Different Targets**: The main VMM focuses on x86 with KVM. These explore alternative ISAs and approaches.

2. **Research Nature**: They represent explorations into specific aspects of virtualization (H-extension, Rust OS design) rather than production hypervisor features.

3. **Limited Integration**: They don't integrate with the main kvm-vmm-x86 project and are developed independently.

4. **Educational Purpose**: These components are valuable for learning about different virtualization approaches and OS design patterns.

---

## Future Work

- Complete H-extension hypervisor implementation with trap handling
- Integrate HLeOs as a more complex guest OS for the KVM VMM
- Add Linux guest boot support
- Explore additional ISA implementations (ARM, etc.)

---

## References

- RISC-V Privileged ISA Manual (H-extension)
- OSDev.org resources
- Rust for Linux and other Rust OS projects
