# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a mini-KVM research project for Ajou University, focused on developing minimal Virtual Machine Monitors using different virtualization technologies. The repository contains three main components:

1. **kvm-vmm-x86**: KVM-based VMM for x86 (C implementation)
2. **hypervisor**: RISC-V H-extension hypervisor (Rust implementation)
3. **HLeOs**: Rust-based toy OS for x86_64

## Repository Structure

```
mini-kvm/
├── kvm-vmm-x86/           # Primary x86 KVM VMM implementation
│   ├── src/main.c         # Main VMM code (~450 lines)
│   ├── guest/             # Guest assembly programs (.S files)
│   └── os-1k/             # 1K OS port (Protected Mode)
├── hypervisor/            # RISC-V hypervisor using H-extension
│   ├── src/               # Rust hypervisor implementation
│   ├── guest.S            # RISC-V guest code
│   └── build.sh           # Build script
├── HLeOs/HLeOs/           # Forked Rust OS project
│   ├── src/               # OS kernel source
│   └── Makefile           # Build with bootimage
├── research/              # Weekly research notes
└── initramfs/             # Initial RAM filesystem
```

## Build Commands

### kvm-vmm-x86 (Primary Component)

```bash
cd kvm-vmm-x86

# Quick build + run commands (recommended)
make multiplication    # 2-9 multiplication table demo
make counter          # Number counter 0-9
make hello            # "Hello, KVM!" output
make hctest           # Hypercall system test
make minimal          # Minimal HLT test

# Build only
make vmm              # Build VMM
make build-<name>     # Build specific guest (e.g., make build-counter)

# Run only
make run-<name>       # Run specific guest (e.g., make run-counter)

# Multi-vCPU demos
make multi-2          # Run 2 guests simultaneously
make multi-4          # Run 4 guests simultaneously

# Protected Mode (1K OS)
make pmode_test       # Run Protected Mode guest with -p flag

# Clean
make clean            # Remove all build artifacts
```

### hypervisor (RISC-V)

```bash
cd hypervisor

# Build and run
./build.sh            # Build guest and hypervisor, run in QEMU
./run.sh              # Same as build.sh

# Manual build
clang --target=riscv64-unknown-elf -ffreestanding -nostdlib \
  -Wl,-eguest_boot -Wl,-Ttext=0x100000 guest.S -o guest.elf
cargo build --target riscv64gc-unknown-none-elf
```

### HLeOs (Rust OS)

```bash
cd HLeOs/HLeOs

# Build bootimage
make                  # Build OS bootimage

# Run in QEMU with KVM
make run              # Run with QEMU + KVM acceleration

# Debug
make gdb              # Start QEMU with GDB server (port 1234)
make debug            # Connect GDB to running instance

# Utilities
make dump             # Generate disassembly dump
make clean            # Remove build artifacts
```

## Architecture Details

### kvm-vmm-x86 Architecture

The KVM VMM operates in Real Mode (16-bit) by default, with optional Protected Mode support:

**Key Components:**
- **VM/vCPU Management**: Single VM with up to 4 vCPUs (multi-threading via pthreads)
- **Memory Layout**: Each vCPU gets isolated memory region (256KB default)
  - vCPU 0: GPA 0x00000 - 0x3FFFF
  - vCPU 1: GPA 0x40000 - 0x7FFFF
  - vCPU 2: GPA 0x80000 - 0xBFFFF
  - vCPU 3: GPA 0xC0000 - 0xFFFFF
- **I/O Emulation**:
  - UART COM1 (port 0x3f8): Character output
  - Hypercall interface (port 0x500): Guest-VMM communication
- **Hypercall Numbers** (matches 1K OS syscalls):
  - 0x00: HC_EXIT - Guest exit request
  - 0x01: HC_PUTCHAR - Output character (BL register)
  - 0x02: HC_GETCHAR - Input character (returns in AL)

**VM Exit Handling:**
- KVM_EXIT_HLT: Guest halt
- KVM_EXIT_IO: Port I/O operations
- KVM_EXIT_MMIO: Memory-mapped I/O

**Real Mode Address Calculation:**
```
physical_address = segment * 16 + offset
```

### hypervisor (RISC-V) Architecture

Minimal RISC-V hypervisor using H-extension:

**Key Components:**
- **vCPU**: Single vCPU implementation
- **Memory Management**: Guest page table (PTE flags: R/W/X)
- **Trap Handling**: stvec-based trap handler
- **Guest Entry**: Loaded at 0x100000

**Guest runs in VS-mode**, hypervisor in HS-mode.

### HLeOs Architecture

Rust-based x86_64 OS with custom bootloader:

**Key Modules:**
- `hleos/interrupt.rs`: GDT, IDT, TSS, interrupt handlers
- `hleos/kmalloc.rs`: Kernel memory allocator (bitmap-based)
- `hleos/thread.rs`: Kernel threading API
- `hleos/vga.rs`: VGA text mode output
- `std/bit_map.rs`: Bitmap data structure
- `std/queue.rs`: Queue implementation

**Build System:**
- Uses `bootimage` crate to create bootable image
- Custom target: x86_64-HLeos.json
- Requires nightly Rust toolchain

## Development Workflow

### Adding New Guest Programs (kvm-vmm-x86)

1. Create `guest/myprogram.S` assembly file
2. Use Real Mode (16-bit) instructions
3. Build: `make build-myprogram`
4. Run: `make run-myprogram`
5. Or combine: Add target in Makefile for one-step build+run

**Guest Code Guidelines:**
- Entry point at offset 0
- Use UART (port 0x3f8) or Hypercalls (port 0x500) for output
- Terminate with `hlt` instruction or HC_EXIT hypercall
- Avoid clobbering critical registers (CL/CH for counters, etc.)

### Debugging kvm-vmm-x86

**Enable verbose output in main.c:**
- VM exits are logged with reason and count
- Multi-vCPU: Color-coded output per vCPU (red/green/blue/yellow)

**Common issues:**
- Register management: Use separate registers for nested loops
- Memory isolation: Each vCPU has independent memory space
- Hypercall protocol: AL = hypercall number, BL/BX = arguments

### Debugging HLeOs

```bash
# Terminal 1: Start QEMU with GDB server
make gdb

# Terminal 2: Connect GDB (for 64-bit kernel)
make debug

# For real mode bootloader debugging
make gdb-i386        # Use i386 QEMU
make debug-loader    # Connect with special GDB config
```

## Important Notes

### Requirements

**kvm-vmm-x86:**
- x86_64 CPU with VT-x/AMD-V enabled
- KVM kernel module loaded (`/dev/kvm` accessible)
- GCC, GNU binutils (as, ld, objcopy)

**hypervisor:**
- RISC-V toolchain (clang with riscv64 target)
- Rust nightly with riscv64gc-unknown-none-elf target
- QEMU system emulator for RISC-V with H-extension support

**HLeOs:**
- Rust nightly toolchain
- bootimage: `cargo install bootimage`
- rust-src component
- llvm-tools-preview component

### Protected Mode vs Real Mode (kvm-vmm-x86)

- **Real Mode** (default): 16-bit, 1MB address space, no paging
- **Protected Mode** (`-p` flag): 32-bit, paging enabled, for 1K OS

The VMM automatically configures segment registers based on mode:
- Real Mode: All segments = 0, CS:IP addressing
- Protected Mode: GDT setup, page tables configured

### Multi-vCPU Threading

Each vCPU runs in separate pthread. Output is serialized with mutex and color-coded by vCPU ID.

**Thread-safe functions:**
- `vcpu_putchar()`: Output single character
- `vcpu_printf()`: Formatted output

### Research Notes

Weekly research notes in `research/week*/README.md` document:
- Implementation progress
- Technical decisions
- Debugging insights
- Performance analysis

Current status: Week 12 (Feature complete, documentation finalized)

## Common Tasks

### Run Full Test Suite (kvm-vmm-x86)
```bash
cd kvm-vmm-x86
for guest in minimal hello counter hctest multiplication; do
    echo "=== Testing $guest ==="
    make $guest
done
```

### Build All Components
```bash
# Build x86 VMM
cd kvm-vmm-x86 && make all && cd ..

# Build RISC-V hypervisor
cd hypervisor && ./build.sh && cd ..

# Build HLeOs
cd HLeOs/HLeOs && make && cd ../..
```

### Add KVM Permissions (if needed)
```bash
sudo usermod -aG kvm $USER
newgrp kvm
ls -l /dev/kvm  # Should show rw- permissions for kvm group
```
