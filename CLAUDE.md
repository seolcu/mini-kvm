# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a self-directed university project (Ajou University) to develop a minimal Virtual Machine Monitor (VMM) using Linux KVM API.

**Architecture Change (Week 9):**
- **Previous approach (discontinued)**: Bare-metal RISC-V hypervisor following https://1000hv.seiya.me/en/
- **Current approach**: KVM-based VMM running on RISC-V Linux

**Target Architecture:**
```
┌─────────────────────────────────────┐
│  Guest OS (simple test program)     │  ← Target for hypercalls
├─────────────────────────────────────┤
│  KVM VMM (Rust, IN DEVELOPMENT)     │  ← Main development focus
├─────────────────────────────────────┤
│  RISC-V Linux + KVM (CONFIG_KVM=y)  │  ← /dev/kvm interface
├─────────────────────────────────────┤
│  QEMU (RISC-V H-extension enabled)  │  ← Development environment
├─────────────────────────────────────┤
│  x86 host (Fedora 43)               │  ← Host machine
└─────────────────────────────────────┘
```

The project builds a userspace VMM using KVM ioctls to create and manage virtual machines.

## Repository Structure

- **linux-6.17.7/** - RISC-V Linux kernel (Phase 1, complete)
  - `.config` - Kernel configuration with `CONFIG_KVM=y`
  - `arch/riscv/boot/Image` - Built kernel image (27MB)
- **initramfs/** - Minimal root filesystem (Phase 1, complete)
  - `init.S` - Assembly init program
  - `init` - Compiled init binary
- **initramfs.cpio.gz** - Packed initramfs archive (2.1KB)
- **hypervisor/** - Bare-metal RISC-V hypervisor (Phase 0, reference only)
  - `src/` - Rust source code (main.rs, vcpu.rs, trap.rs, etc.)
  - `guest.S` - Guest assembly code (can be reused for KVM VMM)
  - **Note**: This is the old bare-metal approach, kept for reference
- **kvm-vmm/** - KVM-based VMM in Rust (Phase 2, TODO)
  - Main development area for the new KVM approach
- **HLeOs/** - Reference x86_64 educational OS project (separate codebase)
- **research/** - Weekly research notes documenting learning progress
  - `week10/` - Phase 1 completion report (RISC-V Linux + KVM setup)
- **meetings/** - Weekly supervisor meeting logs
  - `week9.md` - Architecture change decision
- **README.md** - 16-week project timeline and references

**Current Implementation Status:**
- ✅ Phase 1: RISC-V Linux with KVM support (Week 10)
- 🚧 Phase 2: KVM VMM development (Week 11-12, in planning)
- ⏳ Phase 3: Hypercall handling (Week 12-13)
- ⏳ Phase 4: Guest code enhancement (Week 13-14)
- ⏳ Phase 5: Documentation and demo (Week 14-16)

## Build Commands

### Phase 1: RISC-V Linux Kernel (Complete)

#### Build Linux kernel with KVM
```bash
cd linux-6.17.7
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
sed -i 's/CONFIG_KVM=m/CONFIG_KVM=y/' .config
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc)
```

**Output**: `arch/riscv/boot/Image` (27MB)

#### Build initramfs
```bash
cd initramfs
riscv64-linux-gnu-as -o init.o init.S
riscv64-linux-gnu-ld -static -nostdlib -o init init.o
chmod +x init
find . -print0 | cpio --null -o --format=newc | gzip > ../initramfs.cpio.gz
```

**Output**: `initramfs.cpio.gz` (2.1KB)

#### Boot RISC-V Linux in QEMU
```bash
qemu-system-riscv64 \
  -machine virt \
  -cpu rv64,h=true \
  -m 2G \
  -nographic \
  -kernel linux-6.17.7/arch/riscv/boot/Image \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0"
```

**Important**: `-cpu rv64,h=true` enables H-extension (required for KVM)

**Expected output**: Kernel boots and shows:
```
[    0.276728] kvm [1]: hypervisor extension available
[    0.276802] kvm [1]: using Sv57x4 G-stage page table format
```

### Phase 0: Bare-metal Hypervisor (Reference Only)

#### Build guest and hypervisor
```bash
cd hypervisor
./build.sh
```

#### Run hypervisor in QEMU
```bash
cd hypervisor
./run.sh
```

**Note**: This is the old bare-metal approach, kept for reference and guest code reuse.

## Development Tools

### Cross-Compilation Toolchain (Fedora 43)
- **RISC-V GCC**: `riscv64-linux-gnu-gcc` (15.2.1)
- **RISC-V G++**: `riscv64-linux-gnu-g++` (15.2.1)
- **RISC-V Binutils**: `binutils-riscv64-linux-gnu` (2.45)
- **QEMU RISC-V**: `qemu-system-riscv64` (10.1.2)

Installation:
```bash
sudo dnf install -y gcc-riscv64-linux-gnu gcc-c++-riscv64-linux-gnu \
  qemu-system-riscv bc flex bison elfutils-libelf-devel \
  openssl-devel ncurses-devel
```

### Rust Toolchain (Phase 0, reference)
- **Edition**: 2024
- **Target**: riscv64gc-unknown-none-elf (bare-metal)
- **Toolchain**: stable
- **Dependencies**:
  - `spin = "0.10.0"` (for spinlocks/mutexes in no_std environment)

### Rust Toolchain (Phase 2, planned)
- **Target**: riscv64gc-unknown-linux-gnu (Linux userspace)
- **Dependencies** (planned):
  - `kvm-ioctls = "0.17"` - High-level KVM API wrappers
  - `kvm-bindings = "0.8"` - Raw KVM ioctl bindings
  - `libc` - System call interface

### GitHub Actions
This repository has two automated Claude Code workflows:

1. **Claude Code Review** (`.github/workflows/claude-code-review.yml`)
   - Runs on all pull requests
   - Provides automated code review feedback
   - Checks: code quality, bugs, performance, security, test coverage

2. **Claude Code Assistant** (`.github/workflows/claude-code.yml`)
   - Triggered by @claude mentions in issues/PRs
   - Provides interactive assistance with GitHub operations

## Architecture

### Phase 1: RISC-V Linux + KVM (Current)

**Boot Flow:**
1. QEMU starts with OpenSBI firmware
2. OpenSBI loads Linux kernel at 0x80200000
3. Kernel decompresses initramfs to memory
4. Kernel executes `/init` (PID 1)
5. Init program prints status and exits

**KVM Integration:**
- Kernel module: `/dev/kvm` character device (major 10, minor 232)
- API: ioctl-based interface for VM management
- H-extension: Hardware virtualization support (RISC-V Hypervisor Extension)
- 2-stage paging: Sv57x4 G-stage page table format (guest PA → host PA)

**Key Kernel Configuration:**
- `CONFIG_KVM=y` - KVM as built-in (not module)
- `CONFIG_VIRTUALIZATION=y` - Virtualization support
- `CONFIG_KVM_MMIO=y` - MMIO device emulation

### Phase 0: Bare-metal Hypervisor (Reference)

See `hypervisor/` directory for the original bare-metal implementation.

**Key concepts from Phase 0 that apply to Phase 2:**
- Guest binary format (ELF or raw binary)
- Register initialization (PC, SP, GPRs)
- VM exit handling patterns
- SBI hypercall interface

**Memory Layout (bare-metal, reference)**:
- 0x80200000: Entry point
- Stack: 1MB after BSS
- Heap: 100MB for guest memory allocation

## Important Implementation Details

### Phase 1: Linux Kernel + Initramfs

**Kernel Build:**
- Cross-compilation from x86_64 to RISC-V using `riscv64-linux-gnu-gcc`
- Parallel build with `-j$(nproc)` significantly speeds up compilation
- KVM must be built-in (`=y`) not as module (`=m`) for early availability

**Initramfs:**
- Must be in cpio format (newc variant)
- Init program must be named `/init` and be executable
- Static linking (`-nostdlib -static`) required for init programs
- Assembly init: no libc dependency, direct syscalls via `ecall`

**QEMU Requirements:**
- `-cpu rv64,h=true` - **CRITICAL**: Enables H-extension for nested virtualization
- `-machine virt` - Standard virtual RISC-V machine
- `-m 2G` - Recommended minimum memory for Linux kernel
- OpenSBI firmware automatically loaded by QEMU

### Phase 0: Bare-metal (Reference)

See original implementation in `hypervisor/` for:
- Custom linker scripts
- CSR manipulation with inline assembly
- Page allocation strategies

## Current Project Status

**Current Week**: Week 10 (of 16-week timeline)
**Timeline Context**: Mid-term report submitted Week 8 (Oct 24); Final report due Week 16 (Dec 19)

**Progress**:
- ✅ **Phase 0** (Week 1-8): Bare-metal RISC-V hypervisor (reference implementation)
  - Boot sequence, memory allocation, guest mode entry
  - 2-stage page tables (Sv48x4)
  - Basic SBI hypercalls
  - VM exit handling
- ✅ **Phase 1** (Week 10): RISC-V Linux + KVM environment setup
  - Linux kernel 6.17.7 built with `CONFIG_KVM=y`
  - Minimal initramfs with assembly init program
  - Successfully boots in QEMU with KVM enabled
  - **KVM hypervisor extension confirmed available**
- 🚧 **Phase 2** (Week 11-12, planned): KVM VMM implementation
  - Open `/dev/kvm` and query KVM API version
  - Create VM (KVM_CREATE_VM)
  - Set up memory regions (KVM_SET_USER_MEMORY_REGION)
  - Create vCPU (KVM_CREATE_VCPU)
  - Initialize registers and run VM (KVM_RUN)
- ⏳ **Phase 3** (Week 12-13): Hypercall handling
  - Handle VM exits (KVM_EXIT_RISCV_SBI)
  - Implement SBI console putchar
  - Test with simple guest code
- ⏳ **Phase 4** (Week 13-14): Guest code enhancement
  - More interesting guest programs
  - UART access, loops, arithmetic
- ⏳ **Phase 5** (Week 14-16): Documentation and demo
  - Demo video
  - Final report
  - Code cleanup

**Current Focus**: Phase 1 complete! Starting Phase 2 (KVM VMM) next week.

**Timeline Estimate**:
- **Week 11**: Start KVM VMM (Rust project setup, basic VM creation)
- **Week 11-12**: Complete minimal VMM (guest execution)
- **Week 12**: Hypercall handling
- **Week 13**: Guest code improvements
- **Week 14-15**: Documentation and testing
- **Week 16**: Final report and submission

**Risk Assessment**: ✅ On track (6 weeks remaining, 34-40 hours estimated work)

## References

### Official Documentation
- **KVM API**: https://www.kernel.org/doc/html/latest/virt/kvm/api.html
- **RISC-V Privileged Spec**: https://github.com/riscv/riscv-isa-manual
- **RISC-V H-extension**: https://github.com/riscv/riscv-isa-manual/blob/master/src/hypervisor.tex

### Tutorials & Examples
- **RISC-V Bare-metal Hypervisor** (Phase 0 reference): https://1000hv.seiya.me/en/
- **KVM Sample Rust** (Phase 2 reference): https://github.com/keiichiw/kvm-sample-rust
- **rust-vmm**: https://github.com/rust-vmm (kvm-ioctls, kvm-bindings)

### Project Documentation
- **Research notes**: `research/week10/README.md` - Phase 1 completion report
- **Meeting logs**: `meetings/week9.md` - Architecture change decision
- **Weekly progress**: All documented in `research/` and `meetings/` directories

### Development Notes
- Commit and push regularly to maintain backup
- Document key decisions in weekly research notes
- Update CLAUDE.md when making significant changes