# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a self-directed university project (Ajou University) to develop a minimal Virtual Machine Monitor (VMM) using Linux KVM API.

**Development Approach (Dual-Track):**

The project evolved through multiple phases:
1. **Phase 0 (Week 1-8)**: Bare-metal RISC-V hypervisor (reference implementation)
2. **Phase 1 (Week 10)**: RISC-V Linux + KVM environment setup (complete)
3. **Phase 2 (Week 11)**: x86 KVM VMM implementation (COMPLETE - current primary focus)

**Primary Implementation - x86 KVM VMM:**
```
┌─────────────────────────────────────┐
│  Guest Programs (Real/Protected)    │  ← 7 assembly test programs
├─────────────────────────────────────┤
│  x86 KVM VMM (C, 621 lines)         │  ← Primary implementation
├─────────────────────────────────────┤
│  Linux KVM (/dev/kvm)               │  ← KVM API interface
├─────────────────────────────────────┤
│  x86_64 host (Fedora 43)            │  ← Native execution
└─────────────────────────────────────┘
```

**Reference Implementation - RISC-V:**
```
┌─────────────────────────────────────┐
│  Guest code (assembly)              │
├─────────────────────────────────────┤
│  RISC-V KVM VMM (Rust, incomplete)  │  ← Reference only
├─────────────────────────────────────┤
│  RISC-V Linux + KVM                 │
├─────────────────────────────────────┤
│  QEMU (H-extension enabled)         │
├─────────────────────────────────────┤
│  x86_64 host (Fedora 43)            │
└─────────────────────────────────────┘
```

The project builds a userspace VMM using KVM ioctls to create and manage virtual machines. The x86 implementation is complete and functional, while RISC-V work serves as reference material.

## Repository Structure

### Primary Implementation (x86)

- **kvm-vmm-x86/** - x86 KVM VMM (Phase 2, COMPLETE)
  - `src/main.c` - VMM implementation (621 lines)
  - `src/protected_mode.h` - GDT/IDT structures for Protected Mode
  - `guest/` - 7 assembly guest programs:
    - **Real Mode (16-bit)**:
      - `minimal.S` - Minimal HLT test
      - `hello.S` - "Hello, KVM!" console output via hypercall
      - `counter.S` - Count 0-9 and display
      - `hctest.S` - Hypercall system test (4 operations)
      - `multiplication.S` - 2-9 multiplication table
    - **Protected Mode (32-bit)**:
      - `pmode_simple.S` - Basic Protected Mode with GDT
      - `pmode_test.S` - Protected Mode console output test
  - `Makefile` - Pattern-based build system with shortcuts
  - `README.md` - Comprehensive documentation (428 lines)

### Reference Implementations (RISC-V)

- **linux-6.17.7/** - RISC-V Linux kernel (Phase 1, complete)
  - `.config` - Kernel configuration with `CONFIG_KVM=y`
  - `arch/riscv/boot/Image` - Built kernel image (27MB)
- **initramfs/** - Minimal root filesystem (Phase 1, complete)
  - `init.S` - Assembly init program
  - `init` - Compiled init binary
- **initramfs.cpio.gz** - Packed initramfs archive (2.1KB)
- **hypervisor/** - Bare-metal RISC-V hypervisor (Phase 0, reference only)
  - `src/` - Rust source code (main.rs, vcpu.rs, trap.rs, etc.)
  - `guest.S` - Guest assembly code
- **kvm-vmm/** - RISC-V KVM VMM in Rust (Phase 2, deprecated/reference only)
  - Contains incomplete Rust implementation with kvm-ioctls dependencies
  - Abandoned in favor of x86 C implementation

### Supporting Files

- **busybox-1.36.1/** - BusyBox (explored for richer initramfs, not pursued)
- **config-vm** - Minimal Linux config provided by supervisor
- **HLeOs/** - Reference x86_64 educational OS project
- **research/** - Weekly research notes documenting learning progress
  - `week10/` - Phase 2 completion report (x86 KVM VMM implementation)
  - `week11/` - Multi-guest and OS porting planning
- **meetings/** - Weekly supervisor meeting logs
  - `week11.md` - Latest meeting (2024-11-11)
- **README.md** - 16-week project timeline and references

**Current Implementation Status:**
- ✅ Phase 0: Bare-metal RISC-V hypervisor (Week 1-8)
- ✅ Phase 1: RISC-V Linux with KVM support (Week 10)
- ✅ Phase 2: x86 KVM VMM with hypercalls and Protected Mode (Week 11)
- 🚧 Phase 3: Multi-guest vCPU support (Week 12, in progress)
- ⏳ Phase 4: 1K OS porting and performance analysis (Week 12-13)
- ⏳ Phase 5: Documentation and demo (Week 14-16)

## Build Commands

### Phase 2: x86 KVM VMM (Primary Implementation)

#### Quick Build and Run (Shortcuts)
```bash
cd kvm-vmm-x86

# Build and run in one command (recommended)
make multiplication    # 2-9 multiplication table
make counter          # Count 0-9 with display
make hello            # "Hello, KVM!" via hypercall
make hctest           # Hypercall system test (4 operations)
make minimal          # Minimal HLT test
make pmode_simple     # Protected Mode with GDT
make pmode_test       # Protected Mode console output
```

#### Manual Build and Run
```bash
cd kvm-vmm-x86

# Build specific guest
make build-<name>     # Compile guest/<name>.S to guest/<name>.bin
make build-counter    # Example: build counter guest

# Run specific guest (assumes already built)
make run-<name>       # Execute guest/<name>.bin in VMM
make run-counter      # Example: run counter guest

# Build VMM only (automatic when running guests)
make kvm-vmm         # Compile src/main.c to kvm-vmm executable

# Clean build artifacts
make clean           # Remove all .bin, .o, and executable files
```

**Output Examples:**
```
$ make multiplication
2 x 1 = 2
2 x 2 = 4
...
9 x 9 = 81

$ make hello
Hello, KVM!
```

**Important Notes:**
- Guests execute in Real Mode (16-bit) or Protected Mode (32-bit)
- VMM handles 4 hypercall operations: putchar, getchar, puts, exit
- Protected Mode guests require GDT/IDT setup in guest code
- VM exits captured and handled by VMM on each hypercall

### Phase 1: RISC-V Linux Kernel (Reference Only)

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

### x86 Toolchain (Primary, Fedora 43)
- **C Compiler**: `gcc` (native x86_64)
- **Assembler**: `as` (GNU assembler for x86)
- **Linker**: `ld` (GNU linker)
- **KVM Interface**: `/dev/kvm` (requires Linux kernel with KVM support)
- **Dependencies**: None (pure C with standard library and KVM API)

Installation (if needed):
```bash
sudo dnf install -y gcc binutils make
```

**Verify KVM availability:**
```bash
ls -l /dev/kvm    # Should show character device (major 10, minor 232)
lsmod | grep kvm  # Should show kvm and kvm_intel/kvm_amd modules
```

### Cross-Compilation Toolchain for RISC-V (Reference Only)
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

### Rust Toolchain (Reference Only)

**Phase 0 (Bare-metal RISC-V):**
- **Edition**: 2024
- **Target**: riscv64gc-unknown-none-elf (bare-metal)
- **Toolchain**: stable
- **Dependencies**: `spin = "0.10.0"`

**Phase 2 RISC-V (Deprecated):**
- **Target**: riscv64gc-unknown-linux-gnu (Linux userspace)
- **Dependencies**: `kvm-ioctls = "0.24"`, `kvm-bindings = "0.10"`, `libc = "0.2"`
- **Status**: Incomplete, abandoned in favor of x86 C implementation

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

### Phase 2: x86 KVM VMM (Primary Implementation)

**VM Lifecycle:**
1. Open `/dev/kvm` and check API version (KVM_GET_API_VERSION)
2. Create VM (KVM_CREATE_VM)
3. Allocate guest memory (2MB) and map via KVM_SET_USER_MEMORY_REGION
4. Load guest binary into memory (Real Mode starts at 0x0, Protected Mode varies)
5. Create vCPU (KVM_CREATE_VCPU)
6. Initialize registers (CS:IP for Real Mode, or GDT/IDT for Protected Mode)
7. Run VM in loop (KVM_RUN) until guest exits or halts
8. Handle VM exits: hypercalls (IN/OUT), HLT, shutdown

**Operating Modes:**
- **Real Mode (16-bit)**: Direct execution from 0x0, simple flat memory model
- **Protected Mode (32-bit)**: Requires GDT setup, segmentation, privilege levels

**Hypercall Interface:**
- Port 0xE9: Single character output (putchar)
- Port 0xEA: String output (puts)
- Port 0xEB: Character input (getchar)
- Port 0xEC: Exit VM (with status code)

**Memory Layout:**
```
0x00000000 - 0x001FFFFF (2MB guest memory)
  Real Mode:
    0x0000: Entry point (CS=0, IP=0)
    Stack grows down from 0x7C00
  Protected Mode:
    Variable entry based on GDT setup
    GDT/IDT defined by guest
```

**VM Exit Handling:**
- KVM_EXIT_IO: Handle hypercall ports (0xE9-0xEC)
- KVM_EXIT_HLT: Guest executed HLT instruction
- KVM_EXIT_SHUTDOWN: Triple fault or shutdown event

### Phase 1: RISC-V Linux + KVM (Reference)

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

**Current Week**: Week 12 (of 16-week timeline)
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
  - KVM hypervisor extension confirmed available
- ✅ **Phase 2** (Week 11): x86 KVM VMM implementation (COMPLETE)
  - Full VMM in C (621 lines): VM creation, memory setup, vCPU management
  - 7 guest programs (5 Real Mode + 2 Protected Mode)
  - Hypercall system (4 operations: putchar, getchar, puts, exit)
  - Protected Mode support with GDT/IDT
  - Pattern-based Makefile build system
- 🚧 **Phase 3** (Week 12, current): Multi-guest and OS porting
  - Multi-vCPU support (2-4 guests running simultaneously)
  - 1K OS porting to x86 VMM
  - Concurrent guest execution and scheduling
- ⏳ **Phase 4** (Week 12-13): Performance analysis
  - Performance comparison: KVM vs QEMU emulation
  - Matrix multiplication benchmark
  - VM exit statistics and optimization
- ⏳ **Phase 5** (Week 14-16): Documentation and demo
  - Demo video showcasing multi-guest execution
  - Final report with performance analysis
  - Code cleanup and documentation

**Current Focus**: Implementing multi-vCPU support for simultaneous guest execution and porting 1K OS.

**Remaining Timeline**:
- **Week 12**: Multi-guest vCPU support + 1K OS porting (8-10 hours)
- **Week 12-13**: Performance benchmarking and comparison (6-8 hours)
- **Week 13-14**: Interrupt handling improvements if needed (4-6 hours)
- **Week 14-15**: Documentation, testing, refinement (8-10 hours)
- **Week 16**: Demo video and final report (6-8 hours)

**Risk Assessment**: ✅ Ahead of schedule (Phase 2 complete early; 4 weeks remaining, 32-42 hours estimated work)

## References

### Official Documentation

**KVM and Virtualization:**
- **KVM API**: https://www.kernel.org/doc/html/latest/virt/kvm/api.html
- **KVM x86 Specifics**: https://www.kernel.org/doc/html/latest/virt/kvm/x86/index.html
- **Linux KVM**: https://www.linux-kvm.org/

**x86 Architecture:**
- **Intel 64 and IA-32 Architectures Software Developer Manual**: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- **x86 Assembly Guide**: http://www.cs.virginia.edu/~evans/cs216/guides/x86.html
- **OSDev Wiki - x86**: https://wiki.osdev.org/X86

**RISC-V (Reference):**
- **RISC-V Privileged Spec**: https://github.com/riscv/riscv-isa-manual
- **RISC-V H-extension**: https://github.com/riscv/riscv-isa-manual/blob/master/src/hypervisor.tex

### Tutorials & Examples

**x86 KVM VMM:**
- **KVM Sample Rust**: https://github.com/keiichiw/kvm-sample-rust
- **rust-vmm**: https://github.com/rust-vmm (kvm-ioctls, kvm-bindings)
- **Firecracker**: https://github.com/firecracker-microvm/firecracker (microVM runtime)
- **Cloud Hypervisor**: https://github.com/cloud-hypervisor/cloud-hypervisor (Rust-based VMM)

**RISC-V (Reference):**
- **RISC-V Bare-metal Hypervisor** (Phase 0): https://1000hv.seiya.me/en/

### Project Documentation

**Internal Documentation:**
- **x86 VMM README**: `kvm-vmm-x86/README.md` - Comprehensive 428-line documentation
- **Research notes**:
  - `research/week10/README.md` - Phase 2 completion report (x86 VMM)
  - `research/week11/` - Multi-guest planning
- **Meeting logs**:
  - `meetings/week11.md` - Latest meeting (2024-11-11)
  - `meetings/week9.md` - Architecture change decision
- **Weekly progress**: All documented in `research/` and `meetings/` directories

### Development Notes
- Commit regularly (see global CLAUDE.md instructions)
- Document key decisions in weekly research notes
- Update CLAUDE.md when making significant changes
- Use concise commit messages without credits or emojis