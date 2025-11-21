# AGENTS.md - Mini-KVM Development Guide

## Quick Build/Test Commands
- **VMM**: `cd kvm-vmm-x86 && make vmm && make test`
- **Single guest test**: `make counter` (or hello/minimal/hctest)
- **Multi-vCPU**: `make multi-2` (2 vCPUs), `make multi-4` (4 vCPUs)
- **1K OS**: `cd os-1k && make && cd .. && ./kvm-vmm --paging os-1k/kernel.bin`
- **Individual guest**: `make build-<name>` then `make run-<name>`
- **Clean all**: `make clean` (in kvm-vmm-x86 or os-1k)

## Code Style - C (kvm-vmm-x86, os-1k)
- Format: K&R style, 4-space indent, 80-char lines preferred
- Naming: `snake_case` functions/variables, `UPPER_CASE` macros, `type_t` suffix for structs
- Headers: Group by system/local, always use include guards
- Types: Prefer `<stdint.h>` types (`uint32_t`, `int64_t`)
- Comments: Block comments `/* */`, inline for short explanations
- Compiler flags: `-Wall -Wextra -O2 -std=gnu11` (VMM), add `-m32 -ffreestanding -nostdlib` (1K OS)
- Error handling: Check all syscall returns, use `perror()`, propagate `-1` or `NULL`

## Code Style - Assembly (guests/*.S)
- Syntax: Intel (`mov eax, ebx` not `movl %ebx, %eax`)
- Format: lowercase mnemonics, one instruction per line, comments on separate lines with `;`
- Labels: `label:` format, use `.globl` for externals, `.section` for placement
- Mode: Use `.code16` (Real Mode) or `.code32` (Protected Mode) directives

## Architecture Notes
- **kvm-vmm-x86**: VMM in C, uses KVM API directly, supports Real/Protected Mode, multi-vCPU (max 4)
- **1K OS**: Protected Mode kernel with paging (0x80000000), uses hypercalls (port 0x500) for I/O
- **Hypercalls**: HC_EXIT (0x00), HC_PUTCHAR (0x01), HC_GETCHAR (0x02) - set RAX=type, RBX=arg
- **Memory layout**: Real Mode 256KB/vCPU @ 0x0/0x40000/0x80000/0xC0000, Protected Mode 4MB @ 0x0
- **Guest I/O**: UART @ 0x3f8 (Real Mode), hypercalls @ 0x500 (both modes)
- Document changes in `research/week*/README.md`, update `DEMO_GUIDE.md` for presentation changes
