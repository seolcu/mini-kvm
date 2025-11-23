# Mini-KVM: Educational x86 Hypervisor

> A minimal but fully-functional x86 hypervisor built with Linux KVM API for educational purposes

[![Project Status](https://img.shields.io/badge/status-complete-success)]() 
[![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-blue)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()

**Ajou University Independent Research Project** (2025 Fall)  
**Author**: Seolcu  
**Completion Date**: November 22, 2025

---

## Overview

Mini-KVM is an educational hypervisor that demonstrates core virtualization concepts using the Linux KVM API. Despite its compact size (~3,500 lines of C code), it supports:

- **Multiple vCPUs**: Up to 4 virtual CPUs running simultaneously
- **Real Mode guests**: Simple 16-bit x86 programs
- **Protected Mode with Paging**: Full 32-bit OS support (1K OS)
- **9 User Programs**: Interactive shell with mathematical/utility programs
- **Near-native Performance**: Minimal virtualization overhead

This project proves that a complete, working hypervisor can be understood and built from scratch in a reasonable timeframe.

---

## Features

### Core VMM Capabilities
- ✅ **Multi-vCPU Support**: Run up to 4 guest programs in parallel
- ✅ **Real Mode (16-bit)**: Direct support for legacy x86 code
- ✅ **Protected Mode (32-bit)**: Full segmentation and paging
- ✅ **Interrupt Handling**: Timer and keyboard interrupts
- ✅ **Hypercall Interface**: Efficient guest-host communication
- ✅ **I/O Emulation**: UART serial port, keyboard input

### Guest Operating Systems
1. **Real Mode Guests** (6 programs)
   - `minimal.bin`: 1-byte HLT instruction (simplest possible guest)
   - `hello.bin`: "Hello, KVM!" via UART
   - `counter.bin`: Counts 0-9
   - `multiplication.bin`: Multiplication table via hypercalls
   - `fibonacci.bin`: Fibonacci sequence generator
   - `hctest.bin`: Hypercall test suite

2. **1K OS** (Protected Mode)
   - **9 Interactive Programs**:
     1. Multiplication Table (2×1 to 9×9)
     2. Counter (0-9)
     3. Echo (interactive input/output)
     4. Fibonacci Sequence (first 15 numbers)
     5. Prime Numbers (up to 100)
     6. Calculator (+, -, *, /)
     7. Factorial (0! to 12!)
     8. GCD (Euclidean algorithm)
     9. About 1K OS
   - Kernel space with GDT/IDT
   - User space programs via syscalls
   - Hypercall-based I/O
   - Timer interrupts

---

## Quick Start

### Prerequisites
```bash
# Fedora/RHEL
sudo dnf install gcc make binutils qemu-kvm

# Ubuntu/Debian
sudo apt install gcc make binutils qemu-kvm

# Verify KVM support
lsmod | grep kvm
ls -l /dev/kvm
```

### Build VMM and Guests
```bash
# Clone repository
git clone https://github.com/seolcu/mini-kvm.git
cd mini-kvm/kvm-vmm-x86

# Build VMM
make vmm

# Build guest programs
cd guest && ./build.sh && cd ..

# Build 1K OS
cd os-1k && make && cd ..
```

### Run Examples

**1. Minimal Guest (1 byte)**
```bash
./kvm-vmm guest/minimal.bin
# Output: Guest halts immediately
```

**2. Hello World**
```bash
./kvm-vmm guest/hello.bin
# Output: Hello, KVM!
```

**3. Multi-vCPU (2 guests simultaneously)**
```bash
./kvm-vmm guest/multiplication.bin guest/counter.bin
# Output: Interleaved output showing true parallelism
```

**4. 1K OS (Protected Mode)**
```bash
# Run multiplication table program
printf "1\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin

# Run Fibonacci sequence
printf "4\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin

# Run interactive calculator
printf "6\n12 + 5\n100 - 37\nq\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

---

## Architecture

### System Overview
```
┌─────────────────────────────────────────────┐
│           User Space (Host)                 │
│  ┌───────────────────────────────────────┐  │
│  │  Mini-KVM VMM (main.c)                │  │
│  │  - VM creation & management           │  │
│  │  - vCPU threads (pthreads)            │  │
│  │  - I/O handling (UART, hypercalls)    │  │
│  │  - Interrupt injection                │  │
│  └───────────────────────────────────────┘  │
│              ↕ KVM ioctl()                   │
├─────────────────────────────────────────────┤
│           Kernel Space (Host)               │
│  ┌───────────────────────────────────────┐  │
│  │  Linux KVM Module                     │  │
│  │  - Hardware virtualization (Intel VT) │  │
│  │  - VM exits handling                  │  │
│  │  - Memory management (EPT)            │  │
│  └───────────────────────────────────────┘  │
│              ↕ Hardware                      │
├─────────────────────────────────────────────┤
│           Guest (Virtual Machine)           │
│  ┌───────────────────────────────────────┐  │
│  │  Real Mode Guests                     │  │
│  │  - Direct x86 16-bit code             │  │
│  │  - UART I/O (port 0x3f8)              │  │
│  │  - Hypercalls (port 0x500)            │  │
│  └───────────────────────────────────────┘  │
│                   OR                         │
│  ┌───────────────────────────────────────┐  │
│  │  1K OS (Protected Mode)               │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ Kernel Space                    │  │  │
│  │  │ - GDT/IDT                       │  │  │
│  │  │ - Paging (4MB pages)            │  │  │
│  │  │ - Interrupt handlers            │  │  │
│  │  └─────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────┐  │  │
│  │  │ User Space                      │  │  │
│  │  │ - Shell (9 programs)            │  │  │
│  │  │ - Syscalls via hypercalls       │  │  │
│  │  └─────────────────────────────────┘  │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

### Memory Layout

**Real Mode (per vCPU)**
```
0x00000 - 0x3FFFF:  vCPU 0 (256 KB)
0x40000 - 0x7FFFF:  vCPU 1 (256 KB)
0x80000 - 0xBFFFF:  vCPU 2 (256 KB)
0xC0000 - 0xFFFFF:  vCPU 3 (256 KB)
```

**Protected Mode (1K OS)**
```
0x00000000 - 0x003FFFFF:  Physical memory (4 MB)
0x80000000 - 0x803FFFFF:  Virtual mapped region (kernel)
```

### Key Techniques

1. **Multi-vCPU Implementation**
   - Each vCPU runs in separate pthread
   - Independent memory regions in Real Mode
   - Shared memory in Protected Mode
   - Thread-safe I/O with mutexes

2. **Hypercall Interface**
   - Port 0x500 for VMM communication
   - OUT instruction triggers VM exit
   - IN instruction reads result
   - Types: EXIT (0x00), PUTCHAR (0x01), GETCHAR (0x02)

3. **Protected Mode Support**
   - VMM sets up initial GDT/IDT
   - 4MB PSE paging
   - CR0.PE=1, CR0.PG=1
   - Kernel/User mode separation (CPL 0/3)

---

## Performance

**Measured on**: Intel Core i7 (4 cores, KVM-enabled)

| Guest Program | Execution Time | VM Exits | Notes |
|--------------|----------------|----------|-------|
| Minimal (HLT) | < 1 ms | 1 | Single HLT instruction |
| Hello World | 10 ms | ~20 | Serial output |
| Counter (0-9) | 12 ms | ~50 | 10 UART writes |
| 1K OS Counter | 120 ms | ~1,461 | Protected Mode overhead |
| Multi-vCPU (4×) | 18 ms | ~200 | Parallel execution |

**Key Findings**:
- VM creation overhead: < 5 ms
- Hypercall latency: ~0.5 μs per call
- Near-native performance for compute-intensive tasks
- I/O operations dominate execution time

---

## Project Structure

```
mini-kvm/
├── kvm-vmm-x86/              # Main VMM implementation (C, 16/32-bit guests)
│   ├── src/
│   │   ├── main.c            # Core VMM (1,400 LOC)
│   │   └── protected_mode.h  # Protected Mode structures
│   ├── guest/                # Real Mode guest programs
│   └── os-1k/                # 1K OS (Protected Mode guest)
├── hypervisor/               # Experimental RISC-V hypervisor (Rust)
├── HLeOs/                    # Experimental 64-bit x86 OS (Rust)
├── docs/
│   ├── README.md             # This file
│   ├── FINAL_REPORT.md       # Detailed project report
│   └── ...
└── research/                 # Weekly research notes
```

---

## Documentation

### Primary Documents
- **[README.md](README.md)** (this file): Quick start and overview
- **[FINAL_REPORT.md](docs/FINAL_REPORT.md)**: Comprehensive project report
- **[DEMO_GUIDE.md](docs/DEMO_GUIDE.md)**: Step-by-step demonstration guide
- **[performance_test.md](docs/performance_test.md)**: Performance measurements

### Development Guides
- **[CLAUDE.md](CLAUDE.md)**: AI assistant configuration

### Research Notes
- **[research/week1-12/](research/)**: Weekly progress reports

---

## Technical Highlights

### What Makes This Project Special

1. **Educational Focus**
   - ~3,500 lines of clear, well-commented C code
   - Progressive complexity (1-byte guest → full OS)
   - Demonstrates core virtualization concepts

2. **Complete Implementation**
   - Both Real Mode and Protected Mode
   - Multi-vCPU with true parallelism
   - Interrupt handling and I/O emulation

3. **Practical Results**
   - Boots and runs a real OS (1K OS)
   - 9 interactive user programs
   - Near-native performance

4. **From Scratch**
   - No framework dependencies (only KVM API)
   - Direct hardware interaction
   - Full control over virtualization

---

## Development Timeline

| Week | Date | Milestone |
|------|------|-----------|
| 1-2 | Sep | KVM API study, project design |
| 3-4 | Sep | VM creation, memory management |
| 5-6 | Oct | vCPU creation, register control |
| 7-8 | Oct | Real Mode guests, I/O handling |
| 9-10 | Nov | Multi-vCPU support |
| 11-12 | Nov | Protected Mode, 1K OS port |
| 13-14 | Nov | Testing, optimization, documentation |

**Total Development Time**: ~14 weeks (part-time)  
**Final Status**: ✅ Feature Complete

---

## Building From Source

### System Requirements
- **OS**: Linux (kernel 4.20+)
- **CPU**: Intel with VT-x or AMD with AMD-V
- **RAM**: 512 MB minimum
- **Compiler**: GCC 7.0+ or Clang 8.0+
- **Tools**: GNU Make, binutils (as, ld, objcopy)

### Detailed Build Instructions

**1. Verify KVM Support**
```bash
# Check hardware virtualization
egrep -c '(vmx|svm)' /proc/cpuinfo  # Should be > 0

# Load KVM module (if not loaded)
sudo modprobe kvm_intel  # For Intel
sudo modprobe kvm_amd    # For AMD

# Check KVM device
ls -l /dev/kvm
```

**2. Build VMM**
```bash
cd kvm-vmm-x86
make vmm

# Output: kvm-vmm executable (~27 KB)
```

**3. Build Guest Programs**
```bash
cd guest
./build.sh

# Builds 6 guest binaries:
# - minimal.bin (1 byte)
# - hello.bin (15 bytes)
# - counter.bin (18 bytes)
# - multiplication.bin (112 bytes)
# - fibonacci.bin (82 bytes)
# - hctest.bin (79 bytes)
```

**4. Build 1K OS**
```bash
cd os-1k
make

# Output: kernel.bin (~12 KB)
```

### Compilation Flags
```makefile
# VMM flags
CFLAGS = -Wall -Wextra -O2 -std=gnu11 -pthread

# 1K OS flags (32-bit)
CFLAGS = -m32 -std=c11 -O2 -ffreestanding -nostdlib \
         -fno-builtin -fno-stack-protector -fno-pie
```

---

## Testing

### Unit Tests
```bash
# Test minimal guest
./kvm-vmm guest/minimal.bin
# Expected: Immediate halt

# Test I/O
./kvm-vmm guest/hello.bin
# Expected: "Hello, KVM!"

# Test multi-vCPU
./kvm-vmm guest/counter.bin guest/hello.bin
# Expected: Interleaved output
```

### Integration Tests
```bash
# Run all 1K OS programs
for i in {1..9}; do
  printf "${i}\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
done

# Test 4 vCPUs simultaneously
./kvm-vmm guest/minimal.bin guest/hello.bin \
          guest/counter.bin guest/fibonacci.bin
```

### Performance Profiling
```bash
# Measure execution time
time ./kvm-vmm guest/counter.bin

# Count VM exits (check debug output)
./kvm-vmm guest/counter.bin 2>&1 | grep "Thread exiting"
```

---

## Troubleshooting

### Common Issues

**1. Permission denied on /dev/kvm**
```bash
# Add user to kvm group
sudo usermod -aG kvm $USER
# Log out and back in
```

**2. KVM module not loaded**
```bash
# Load appropriate module
sudo modprobe kvm_intel  # or kvm_amd
```

**3. Guest doesn't run**
```bash
# Rebuild guests
cd guest && ./build.sh

# Check guest size
ls -lh guest/*.bin
```

**4. 1K OS input doesn't work**
```bash
# Use input redirection, not interactive typing
printf "1\n0\n" | ./kvm-vmm --paging os-1k/kernel.bin
```

---

## Contributing

This is an educational project completed as part of university coursework. While the project is feature-complete, suggestions and feedback are welcome!

### Code Style
- **C Code**: K&R style, 4-space indentation
- **Assembly**: Intel syntax, lowercase mnemonics
- **Comments**: Explain "why", not "what"
- **Naming**: snake_case for functions, UPPER_CASE for macros

See `CLAUDE.md` for the AI assistant's configuration.

---

## Acknowledgments

### References
- **KVM Documentation**: https://www.kernel.org/doc/html/latest/virt/kvm/
- **1K OS**: Original RISC-V implementation by Yuma Ohgami
- **Intel SDM**: Intel 64 and IA-32 Architectures Software Developer's Manual

### Tools & Libraries
- **Linux KVM**: Hardware-assisted virtualization
- **GCC/Binutils**: Compilation toolchain
- **QEMU**: Testing and comparison

### Inspiration
- **xv6**: MIT's educational Unix-like OS
- **Bochs**: x86 emulator architecture

---

## License

MIT License - See [LICENSE](LICENSE) for details.

---

## Contact

**Author**: Seolcu  
**University**: Ajou University  
**Project**: Independent Research (2025 Fall)  
**Repository**: https://github.com/seolcu/mini-kvm

For questions or feedback, please open an issue on GitHub.

---

## Project Statistics

**Final Metrics** (as of November 22, 2025):
- **Total Lines of Code**: ~3,500 LOC (C + Assembly)
  - VMM: ~1,400 LOC
  - 1K OS: ~1,200 LOC
  - Guests: ~500 LOC
  - Build system: ~400 LOC
- **Guest Programs**: 15 total (6 Real Mode + 9 in 1K OS)
- **Supported Modes**: Real Mode (16-bit) + Protected Mode (32-bit)
- **Max vCPUs**: 4 simultaneous
- **Git Commits**: 50+
- **Documentation**: 5 comprehensive guides

**Development Statistics**:
- **Study Phase**: 4 weeks
- **Implementation**: 8 weeks
- **Testing & Documentation**: 2 weeks
- **Total**: 14 weeks (part-time)

---

**Status**: ✅ Complete | 🎓 Educational | 🚀 Production-Quality Code
