# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a self-directed university project (Ajou University) to develop a minimal Virtual Machine Monitor (VMM) using Linux KVM API. The project is currently implemented as a bare-metal RISC-V hypervisor written in Rust, following the tutorial at https://1000hv.seiya.me/en/. The goal is to learn hypervisor concepts on RISC-V before eventually transitioning to x86 KVM.

The hypervisor implements:
- Guest mode execution using RISC-V H-extension
- 2-stage address translation (guest PA → host PA)
- Basic SBI (Supervisor Binary Interface) hypercalls
- Virtual CPU state management with trap handling

## Build Commands

### Build guest and hypervisor
```bash
cd hypervisor
./build.sh
```

This script:
1. Compiles guest.S (guest code) using clang to guest.elf and guest.bin
2. Builds the hypervisor with custom linker script
3. Copies the output to hypervisor.elf

### Run hypervisor in QEMU
```bash
cd hypervisor
./run.sh
```

This builds and then runs the hypervisor on QEMU with RISC-V H-extension enabled.

### Build only (without running)
```bash
cd hypervisor
RUSTFLAGS="-C link-arg=-Thypervisor.ld -C linker=rust-lld" \
  cargo build --bin hypervisor --target riscv64gc-unknown-none-elf
```

## Architecture

### Memory Layout (defined in hypervisor.ld)

- **0x80200000**: Entry point (boot function)
  - OpenSBI loads the hypervisor here
  - `.text.boot` section placed first to ensure boot function is at entry
  - `.text.stvec` section for trap handler (requires alignment)
- **Stack**: 1MB stack after BSS section
- **Heap**: 100MB heap region for dynamic allocation

### Boot Sequence

1. OpenSBI firmware loads hypervisor at 0x80200000
2. `boot()` function (main.rs:22) sets up stack pointer and jumps to `main()`
3. `main()` initializes BSS, sets up trap handler, initializes heap allocator
4. Guest binary is embedded with `include_bytes!("../guest.bin")` and loaded into allocated memory
5. Guest page table maps guest PA (0x100000) to host PA
6. VCpu structure is created and initialized with CSRs (hstatus, hgatp, sepc)
7. `vcpu.run()` enters guest mode via `sret` instruction

### Core Modules

- **main.rs**: Entry point, boot sequence, guest loading
- **vcpu.rs**: Virtual CPU state structure holding all 32 RISC-V registers plus CSRs
  - `VCpu::new()` initializes guest execution state
  - `VCpu::run()` uses inline assembly to load all registers and CSRs before `sret`
- **trap.rs**: Trap handler for VM exits
  - Uses `#[naked]` function with `naked_asm!` to save/restore registers
  - `handle_trap()` processes VM exits (currently handles scause=10 for VS-mode ecalls)
  - Uses `csrrw a0, sscratch, a0` to swap VCpu pointer in/out
- **guest_page_table.rs**: 2-stage address translation (Sv48x4 mode)
  - 4-level page table implementation
  - `map()` creates mappings from guest PA to host PA
  - `hgatp()` returns the formatted HGATP CSR value
- **allocator.rs**: Bump allocator for heap and page allocation
  - Simple bump allocator that never deallocates
  - `alloc_pages()` allocates page-aligned, zero-initialized memory
- **print.rs**: Console output via OpenSBI
  - `sbi_putchar()` uses `ecall` to call OpenSBI firmware
  - `println!` macro implemented using core::fmt::Write

### Guest Execution Model

The hypervisor uses RISC-V H-extension for nested virtualization:

1. **Guest entry**: CSRs are configured (hstatus.SPV=1 for virtualization mode, hgatp for page table), then `sret` switches to guest
2. **Guest execution**: Code runs at guest PA 0x100000 (mapped to host PA in physical memory)
3. **VM exit**: Guest `ecall` or exceptions trap to `trap_handler`
4. **State save**: All 32 registers saved to VCpu struct (using sscratch to hold VCpu pointer)
5. **Handler**: `handle_trap()` processes the exit (e.g., SBI console putchar)
6. **Resume**: `vcpu.run()` restores state and returns to guest

### Guest Code

Guest code (guest.S) is minimal assembly that makes SBI calls:
- Loads 'A', 'B', 'C' into a0 and calls `ecall` for each
- Hypervisor intercepts these (scause=10) and prints the characters
- Finally enters infinite loop

## Important Implementation Details

### Inline Assembly

The code uses extensive inline assembly for:
- CSR manipulation (csrr, csrw, csrrw)
- Register save/restore in trap handler
- `sret` for mode switching

### Memory Allocation

- `alloc_pages()` takes byte length (must be 4096-aligned) and returns page-aligned zeroed memory
- Unlike the tutorial which uses page count, this implementation uses byte length
- All page allocations should use multiples of 4096

### QEMU Requirements

The hypervisor requires:
- QEMU with RISC-V H-extension: `-cpu rv64,h=true`
- Machine type: `virt`
- OpenSBI firmware (default BIOS)

## Current Project Status

The project is at Week 7-9 (around mid-term report). Chapters 1-7 of the RISC-V tutorial are complete:
- ✅ Boot, memory allocation, guest mode entry, page tables, hypercalls
- ⏸️ Chapters 8-10 (Linux boot) partially implemented but incomplete
- ❌ Chapters 11-13 (MMIO, interrupts) not started

Next steps: Testing executable binaries and preparing for transition to x86 KVM.

## References

Key documentation used in development:
- RISC-V Hypervisor Tutorial: https://1000hv.seiya.me/en/
- KVM API Documentation: https://www.kernel.org/doc/html/latest/virt/kvm/api.html

Research notes and meeting logs are in `research/` and `meetings/` directories.
