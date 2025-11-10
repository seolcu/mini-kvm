# KVM-based Virtual Machine Monitor (x86)

A minimal but fully functional Virtual Machine Monitor (VMM) built using Linux KVM API. This project demonstrates the core concepts of hardware-assisted virtualization on x86 architecture.

## Project Overview

This VMM creates and manages virtual machines using the KVM (Kernel-based Virtual Machine) interface. It runs guest code in Real Mode (16-bit) with support for console I/O through UART emulation.

### Architecture

```
┌─────────────────────────────────────┐
│  Guest Code (Real Mode, 16-bit)     │  ← Runs in VM guest mode
│  - minimal.S: HLT instruction       │
│  - hello.S: "Hello, KVM!" output    │
├─────────────────────────────────────┤
│  KVM VMM (C, userspace)             │  ← This project
│  - VM/vCPU management               │
│  - Memory mapping                   │
│  - I/O emulation (UART 0x3f8)       │
├─────────────────────────────────────┤
│  Linux KVM (/dev/kvm)               │  ← Kernel module
│  - Hardware virtualization (VT-x)   │
│  - VM exit handling                 │
├─────────────────────────────────────┤
│  x86_64 CPU with VT-x               │  ← Hardware support
└─────────────────────────────────────┘
```

## Features

### Implemented ✅

- **KVM Initialization**: Open `/dev/kvm`, verify API version
- **VM Creation**: Create VM instance with `KVM_CREATE_VM`
- **Memory Management**:
  - Allocate 1MB guest physical memory
  - Map guest memory with `KVM_SET_USER_MEMORY_REGION`
  - Load guest binary into memory
- **vCPU Management**:
  - Create vCPU with `KVM_CREATE_VCPU`
  - Initialize Real Mode segment registers (CS, DS, ES, FS, GS, SS)
  - Set general-purpose registers (RIP, RSP, RFLAGS)
- **Execution Loop**: Run vCPU with `KVM_RUN` and handle exits
- **VM Exit Handling**:
  - `KVM_EXIT_HLT`: Guest halt
  - `KVM_EXIT_IO`: Port I/O operations
  - `KVM_EXIT_MMIO`: Memory-mapped I/O (logged)
  - Error exits: `FAIL_ENTRY`, `INTERNAL_ERROR`, `SHUTDOWN`
- **I/O Emulation**:
  - UART COM1 port (0x3f8) emulation
  - Character output to host stdout

### Not Implemented ❌

- IN instruction (port input)
- MMIO device emulation
- Interrupt injection
- Multi-vCPU support
- Protected Mode / Long Mode
- Page table setup (Real Mode doesn't need it)

## Requirements

### Hardware
- x86_64 CPU with VT-x support (Intel) or AMD-V (AMD)
- Check: `grep -E 'vmx|svm' /proc/cpuinfo`

### Software
- Linux kernel with KVM enabled (`CONFIG_KVM=y` or `CONFIG_KVM=m`)
- GCC compiler
- GNU binutils (as, ld, objcopy, objdump)
- Make

### Verify KVM
```bash
# Check if KVM device exists
ls -l /dev/kvm

# Should show:
# crw-rw-rw-+ 1 root kvm 10, 232 ... /dev/kvm
```

## Building

### Build Everything
```bash
make all
```

This builds:
1. Guest code (`guest/minimal.bin` - 1 byte HLT)
2. VMM executable (`kvm-vmm`)

### Build Specific Targets
```bash
make guest       # Build minimal guest only
make hello       # Build hello guest only
make vmm         # Build VMM only
make clean       # Remove build artifacts
```

## Running

### Run with Minimal Guest (HLT only)
```bash
make test
```

Expected output:
```
=== Minimal KVM VMM (x86 Real Mode) ===

KVM API version: 12
Created VM (fd=5)
Allocated guest memory: 1 MB at 0x...
...
VM Exit #1: HLT instruction
Guest halted successfully!
```

### Run with Hello Guest (Console output)
```bash
make test-hello
```

Expected output:
```
=== Starting VM execution ===

Hello, KVM!
VM Exit #13: HLT instruction
Guest halted successfully!
```

### Run Manually
```bash
./kvm-vmm <guest_binary>

# Examples:
./kvm-vmm guest/minimal.bin
./kvm-vmm guest/hello.bin
```

## Implementation Details

### Real Mode

This VMM runs guests in **Real Mode** (16-bit), the simplest x86 CPU mode:

- **Address Calculation**: `physical_address = segment * 16 + offset`
- **Memory Limit**: 1MB (20-bit addressing)
- **No Protection**: No privilege levels, no virtual memory
- **Simple Setup**: Only need to initialize segment registers

Why Real Mode?
- Minimal register initialization required
- No page tables or GDT/IDT setup needed
- Perfect for learning KVM basics

### Guest Memory Layout

```
Guest Physical Address Space (1MB):

0x00000000  ┌─────────────────────────┐
            │  Guest Code (.text)     │  ← Entry point (RIP = 0x0)
            ├─────────────────────────┤
            │  Guest Data (.rodata)   │  ← Strings, constants
            ├─────────────────────────┤
            │  Unused                 │
            │                         │
0x000FFFFF  └─────────────────────────┘

Mapped to host virtual address via mmap()
```

### VM Exit Flow

```
1. VMM calls ioctl(KVM_RUN)
        ↓
2. CPU enters guest mode (VM entry)
        ↓
3. Guest code executes
        ↓
4. Sensitive operation (HLT, I/O, etc.)
        ↓
5. CPU exits to host (VM exit)
        ↓
6. KVM fills kvm_run structure
        ↓
7. VMM handles exit reason
        ↓
8. Loop back to step 1 (or terminate)
```

### Port I/O Handling

When guest executes `out %al, 0x3f8`:

1. CPU triggers VM exit (`KVM_EXIT_IO`)
2. KVM fills `kvm_run->io` structure:
   - `port`: 0x3f8 (UART COM1)
   - `direction`: `KVM_EXIT_IO_OUT`
   - `size`: 1 byte
   - `data_offset`: offset to data in kvm_run
3. VMM reads data and outputs to stdout
4. VMM calls `KVM_RUN` again to resume guest

## Code Structure

```
kvm-vmm-x86/
├── src/
│   └── main.c              # VMM implementation (~350 lines)
│       ├── init_kvm()      # KVM initialization
│       ├── setup_guest_memory()  # Memory allocation & mapping
│       ├── load_guest_binary()   # Load guest code
│       ├── setup_vcpu()    # vCPU creation & register init
│       └── run_vm()        # Execution loop & exit handling
│
├── guest/
│   ├── minimal.S           # Minimal guest (HLT)
│   ├── hello.S             # Hello guest (console output)
│   ├── guest.ld            # Linker script (load at 0x0)
│   └── build.sh            # Guest build script
│
├── Makefile                # Build system
└── README.md               # This file
```

## Guest Code Examples

### Minimal Guest (minimal.S)

```asm
.code16
.section .text
.global _start

_start:
    hlt                     # Halt CPU
```

**Binary size**: 1 byte (`0xf4`)

### Hello Guest (hello.S)

```asm
.code16
.section .text
.global _start

_start:
    mov $message, %si       # SI = pointer to message

print_loop:
    lodsb                   # Load byte from [SI] into AL, increment SI
    test %al, %al           # Check if null terminator
    jz done

    mov $0x3f8, %dx         # DX = UART port
    out %al, (%dx)          # Send character to port

    jmp print_loop

done:
    hlt

.section .rodata
message:
    .asciz "Hello, KVM!\n"
```

**Binary size**: 28 bytes (15 bytes code + 13 bytes data)

## Learning Points

### Core Concepts Demonstrated

1. **Hardware Virtualization**
   - CPU automatically switches between guest/host modes
   - VM exits on sensitive instructions (HLT, I/O)
   - No software emulation needed for most instructions

2. **KVM API Usage**
   - File descriptor hierarchy: `kvm_fd` → `vm_fd` → `vcpu_fd`
   - ioctl-based interface for all operations
   - Shared memory (`kvm_run`) for exit information

3. **Memory Virtualization**
   - Guest Physical Address (GPA) → Host Virtual Address (HVA) mapping
   - VMM allocates memory with `mmap()`, tells KVM with ioctl
   - No guest page tables needed in Real Mode

4. **I/O Virtualization**
   - Port I/O traps to VMM automatically
   - VMM emulates devices in software
   - Each `out` instruction = one VM exit (performance consideration!)

5. **CPU Virtualization**
   - Segment registers control address translation
   - Real Mode: simple but limited to 1MB
   - General-purpose registers shared with guest

### Performance Insights

From the hello guest output:
```
Hello, KVM!
VM Exit #13: HLT instruction
```

- 13 characters = 13 VM exits (one per `out` instruction)
- Each exit involves:
  - Saving guest state
  - Loading host state
  - Running VMM handler
  - Restoring guest state
  - VM entry

**Real-world optimization**: Batch I/O operations, use virtio queues, paravirtualization

## Troubleshooting

### "No such file or directory" for /dev/kvm

**Problem**: KVM module not loaded

**Solution**:
```bash
# Load KVM module
sudo modprobe kvm
sudo modprobe kvm_intel  # or kvm_amd

# Verify
ls -l /dev/kvm
```

### "Permission denied" on /dev/kvm

**Problem**: User doesn't have KVM access

**Solution**:
```bash
# Add user to kvm group
sudo usermod -aG kvm $USER

# Or run with sudo (not recommended)
sudo ./kvm-vmm guest/hello.bin
```

### "KVM_CREATE_VM failed"

**Problem**: CPU virtualization not enabled

**Solution**:
1. Enter BIOS/UEFI settings
2. Enable Intel VT-x (or AMD-V)
3. Reboot

### Guest doesn't output anything

**Problem**: Guest code might be incorrect or VMM I/O handling broken

**Debug**:
```bash
# Check guest binary
hexdump -C guest/hello.bin

# Check VMM is handling KVM_EXIT_IO
# Add debug prints in run_vm() KVM_EXIT_IO case
```

## Extending This Project

### Suggested Exercises

1. **Add more guest programs**
   - Arithmetic operations and output results
   - Loop counting
   - Pattern printing

2. **Implement IN instruction**
   - Handle `KVM_EXIT_IO` with direction `KVM_EXIT_IO_IN`
   - Allow guest to read from emulated devices

3. **Add hypercall interface**
   - Use VMCALL instruction
   - Handle `KVM_EXIT_HYPERCALL` (x86) or `KVM_EXIT_VMCALL`
   - Custom syscall-like interface

4. **Implement Protected Mode**
   - Set up GDT (Global Descriptor Table)
   - Initialize CR0, CR3, CR4 registers
   - Enable 32-bit mode

5. **Multi-vCPU support**
   - Create multiple vCPUs
   - Use threads to run each vCPU
   - Handle synchronization

6. **MMIO device emulation**
   - Handle `KVM_EXIT_MMIO`
   - Emulate simple MMIO device (e.g., RTC)

## References

### Official Documentation
- [KVM API Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Intel® 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Linux KVM Source Code](https://github.com/torvalds/linux/tree/master/virt/kvm)

### Tutorials & Examples
- [Using the KVM API (LWN.net)](https://lwn.net/Articles/658511/)
- [kvmtool](https://github.com/kvmtool/kvmtool) - Lightweight VMM reference implementation
- [Rust VMM](https://github.com/rust-vmm) - Modern VMM components

### Related Projects
- QEMU: Full system emulator with KVM support
- Firecracker: Lightweight microVM for serverless
- Cloud Hypervisor: Modern VMM in Rust

## License

This is an educational project for university coursework (Ajou University).

## Acknowledgments

- Linux KVM developers for the excellent API
- Intel/AMD for hardware virtualization support
- Online resources and documentation

---

**Project**: Mini KVM VMM
**Course**: Self-directed University Project
**Timeline**: Week 10-11 of 16-week project
**Status**: Phase 2 Complete ✅
