# AGENTS.md

## Build Commands
- **kvm-vmm-x86**: `cd kvm-vmm-x86 && make vmm` (build VMM), `make <guest>` (build+run, e.g., `make counter`, `make hello`), `make multi-2` (multi-vCPU)
- **hypervisor**: `cd hypervisor && ./build.sh` (build guest + hypervisor), `python3 test_qemu.py` (run QEMU test)
- **HLeOs**: `cd HLeOs/HLeOs && make` (build bootimage), `make run` (run with KVM), `make gdb` (debug with GDB)

## Test Commands
- **kvm-vmm-x86**: `make test` (minimal guest), `make test-hello` (hello guest), `make pmode_test` (Protected Mode)
- **hypervisor**: `python3 test_qemu.py` (automated QEMU test)
- **HLeOs**: `make run` (manual testing in QEMU)

## Code Style
- **C (kvm-vmm-x86)**: K&R style, 4-space indent, snake_case for functions/variables, UPPER_CASE for macros/constants, block comments with `/* */`, include guards, use `<stdint.h>` types, group headers by system/local
- **Rust (hypervisor, HLeOs)**: Standard rustfmt, snake_case for functions/variables, PascalCase for types, use `unsafe` blocks explicitly, `#![no_std]` for bare-metal, prefer explicit types over inference in function signatures
- **Assembly**: Intel syntax, lowercase mnemonics, comments on separate lines with `;`, label format `label:`, use `.section`, `.globl`, `.code16/.code32`

## Error Handling
- **C**: Check return values, use `perror()` for syscall errors, return -1 or NULL on error, use `errno`
- **Rust**: Use `Result<T, E>` for fallible operations, `Option<T>` for nullable values, explicit `panic!()` for unrecoverable errors

## Naming Conventions
- Files: lowercase with underscores (e.g., `guest_page_table.rs`, `main.c`)
- Guest binaries: descriptive names (e.g., `counter.S`, `multiplication.S`)
- Structs: suffix `_t` in C (e.g., `vcpu_context_t`), PascalCase in Rust (e.g., `GuestPageTable`)

## Project Notes
- This is a research project with weekly milestones - document changes in `research/week*/README.md`
- Three independent components: x86 KVM VMM (C), RISC-V hypervisor (Rust), toy OS (Rust)
- Use hypercalls (port 0x500) for guest-VMM communication in kvm-vmm-x86
- Memory layout: vCPU 0 @ 0x00000, vCPU 1 @ 0x40000, vCPU 2 @ 0x80000, vCPU 3 @ 0xC0000
