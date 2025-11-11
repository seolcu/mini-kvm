# KVM-based Virtual Machine Monitor (x86)

A minimal but fully functional Virtual Machine Monitor (VMM) built using Linux KVM API. Demonstrates hardware-assisted virtualization, I/O emulation, and Hypercall interface design.

## Project Overview

This VMM creates and manages virtual machines using the KVM (Kernel-based Virtual Machine) interface. It runs guest code in Real Mode (16-bit) with support for console I/O through UART emulation and a custom Hypercall system for advanced guest services.

### Architecture

```
┌─────────────────────────────────────────┐
│  Guest Programs (Real Mode, 16-bit)     │  ← 5 demo programs
│  - minimal.S: HLT (1 byte)              │
│  - hello.S: UART output (28 bytes)      │
│  - counter.S: Loop & arithmetic (18)    │
│  - hctest.S: Hypercall test (79 bytes)  │
│  - multiplication.S: Nested loops (112)  │
├─────────────────────────────────────────┤
│  KVM VMM (C, ~450 lines)                │
│  - VM/vCPU management                   │
│  - Memory mapping                       │
│  - I/O emulation (UART 0x3f8)           │
│  - Hypercall interface (port 0x500)     │
├─────────────────────────────────────────┤
│  Linux KVM (/dev/kvm)                   │
│  - Hardware virtualization (VT-x/AMD-V) │
│  - VM exit handling                     │
├─────────────────────────────────────────┤
│  x86_64 CPU with VT-x/AMD-V             │
└─────────────────────────────────────────┘
```

## Features

### Implemented ✅

#### Core KVM Features
- **KVM Initialization**: Open `/dev/kvm`, verify API version (must be 12)
- **VM Creation**: Create VM instance with `KVM_CREATE_VM`
- **Memory Management**:
  - Allocate 1MB guest physical memory
  - Map guest memory with `KVM_SET_USER_MEMORY_REGION`
  - Load guest binary into memory
- **vCPU Management**:
  - Create vCPU with `KVM_CREATE_VCPU`
  - Initialize Real Mode segment registers (CS, DS, ES, FS, GS, SS)
  - Set general-purpose registers (RIP, RSP, RFLAGS)
- **Execution Loop**: Run vCPU with `KVM_RUN` and handle VM exits

#### VM Exit Handling
- `KVM_EXIT_HLT`: Guest halt (clean termination)
- `KVM_EXIT_IO`: Port I/O operations
- `KVM_EXIT_MMIO`: Memory-mapped I/O (logged)
- Error exits: `FAIL_ENTRY`, `INTERNAL_ERROR`, `SHUTDOWN`

#### I/O Emulation
- **UART COM1** (port 0x3f8): Character output to host stdout
- **Hypercall Interface** (port 0x500): Custom guest→VMM communication

### Hypercall System ⭐ NEW

A clean interface for guests to request services from the VMM:

```c
#define HC_EXIT       0x00  // Guest requests exit
#define HC_PUTCHAR    0x01  // Output character (BL = char)
#define HC_PUTNUM     0x02  // Output decimal number (BX = number)
#define HC_NEWLINE    0x03  // Output newline
```

**Usage** (guest assembly):
```asm
mov $42, %bx            # BX = number to print
mov $HC_PUTNUM, %al     # AL = hypercall number
mov $HYPERCALL_PORT, %dx
out %al, (%dx)          # Execute hypercall
```

**Advantages**:
- Game-changer for complex output formatting
- VMM handles printf/itoa logic, guest stays simple
- Extensible: easy to add HC_GETCHAR, HC_RANDOM, etc.
- Mimics real OS syscall interface

### Not Implemented ❌

- IN instruction (port input/read)
- Full MMIO device emulation
- Interrupt injection
- Multi-vCPU support
- Protected Mode / Long Mode (can be added)
- Page tables (Real Mode doesn't need them)

## Requirements

### Hardware
- x86_64 CPU with VT-x (Intel) or AMD-V (AMD)
- Verify: `grep -E 'vmx|svm' /proc/cpuinfo`

### Software
- Linux kernel with KVM enabled (`CONFIG_KVM=y` or `CONFIG_KVM=m`)
- GCC compiler
- GNU binutils (as, ld, objcopy)
- Make

### Verify KVM
```bash
ls -l /dev/kvm
# Should show: crw-rw-rw-+ 1 root kvm 10, 232 ... /dev/kvm
```

## Building

### Quick Start
```bash
# Build and run all-in-one
make multiplication      # 2-9 multiplication table
make counter            # Number counter 0-9
make hello              # "Hello, KVM!" output
make hctest             # Hypercall system test
make minimal            # Minimal HLT test
```

### Build Only
```bash
make build-<name>       # e.g., make build-multiplication
```

### Run Only
```bash
make run-<name>         # e.g., make run-multiplication
```

### Full Build
```bash
make all                # Build guest and VMM
make clean              # Remove artifacts
```

## Running

### Test Output Examples

#### multiplication (nested loops)
```bash
$ make multiplication
...
2 x 1 = 2
2 x 2 = 4
...
9 x 9 = 81
[Hypercall] Guest exit request
=== VM execution completed successfully ===
```

#### hctest (hypercall demo)
```bash
$ make hctest
...
Hello!
42
1234
[Hypercall] Guest exit request
=== VM execution completed successfully ===
```

#### counter (loop & arithmetic)
```bash
$ make counter
...
0123456789
VM Exit #11: HLT instruction
Guest halted successfully!
```

## Implementation Details

### Real Mode

This VMM runs guests in **Real Mode** (16-bit), the simplest x86 mode:

**Address Calculation**: `physical_address = segment * 16 + offset`

**Why Real Mode?**
- Simple register initialization (just segment setup)
- No page tables needed
- Maximum 1MB address space (sufficient for our demos)
- Perfect for learning!

### Guest Memory Layout

```
Guest Physical Address Space (1MB):

0x00000000  ┌─────────────────────────┐
            │  Guest Code (.text)     │  ← Entry point (RIP = 0x0)
            ├─────────────────────────┤
            │  Guest Data (.rodata)   │  ← Strings, constants
            ├─────────────────────────┤
            │  Unused / Stack         │
            │                         │
0x000FFFFF  └─────────────────────────┘
```

### VM Exit Flow

```
1. VMM calls ioctl(KVM_RUN)
        ↓
2. CPU enters guest mode
        ↓
3. Guest executes until sensitive operation (OUT, HLT, etc.)
        ↓
4. CPU exits to host, fills kvm_run struct
        ↓
5. VMM handles exit reason
        ↓
6. Return to step 1 (or terminate)
```

### Hypercall Handling

When guest executes `out %al, 0x500` (hypercall):

1. CPU triggers `KVM_EXIT_IO`
2. KVM fills `kvm_run->io` structure
3. VMM detects port 0x500 and calls `handle_hypercall()`
4. Function reads AL (hypercall number) from guest registers
5. Executes corresponding action (printf, etc.)
6. Returns to step 6 above

## Guest Code Examples

### minimal.S (1 byte, simplest)
```asm
hlt
```
→ Demonstrates basic VMM functionality
→ **Output**: Nothing (just exits)

### hello.S (28 bytes, UART I/O)
```asm
mov $message, %si
print_loop:
    lodsb                   # Load string byte
    test %al, %al           # Check null terminator
    jz done
    mov $0x3f8, %dx         # UART port
    out %al, (%dx)          # Output character
    jmp print_loop
done:
    hlt
```
→ **Output**: "Hello, KVM!\n"

### counter.S (18 bytes, loops & arithmetic)
```asm
mov $0, %cl
print_loop:
    add $0x30, %cl          # Convert to ASCII
    mov $0x3f8, %dx
    out %cl, (%dx)
    inc %cl
    cmp $10, %cl
    jl print_loop
```
→ **Output**: "0123456789"

### hctest.S (79 bytes, hypercall demo)
```asm
mov $'H', %bl
call putchar_hc            # Hypercall: output char

mov $42, %bx
call putnum_hc             # Hypercall: output number
```
→ **Output**: "Hello!\n42\n1234\n"

### multiplication.S (112 bytes, nested loops ⭐)
```asm
mov $2, %cl                # dan = 2
outer_loop:
    mov $1, %ch            # multiplier = 1
    inner_loop:
        # Print: "{dan} x {multiplier} = {dan*multiplier}"

        mov %cl, %al       # AL = dan
        mov %ch, %bl       # BL = multiplier (key: use BL not CL!)
        mul %bl            # AX = AL * BL

        ; ... print logic ...

        inc %ch
        cmp $10, %ch
        jl inner_loop
    inc %cl
    cmp $10, %cl
    jl outer_loop
```
→ **Output**: 2-9 multiplication table (18 lines)
→ **Key Learning**: Register management with CL/CH separation

## Performance Analysis

### VM Exit Statistics

```
minimal:        1 exit    (~1ms)
hello:          13 exits  (~5ms)
counter:        11 exits  (~4ms)
hctest:         13 exits  (~6ms)
multiplication: 181 exits (~200ms)
```

### Why So Many Exits?

Multiplication table with current implementation:
- Each line: `dan`, ` `, `x`, ` `, `multiplier`, ` `, `=`, ` `, `result`, `\n`
- That's 10 hypercalls per line
- 18 lines × 10 = 180 exits

### Optimization Opportunities

Real-world VMMs use **batching** and **buffering**:
```c
// Instead of:
for each character: OUT to port  // 180 exits

// Use shared memory ring buffer:
Guest writes to buffer (no exits)
When buffer full: 1 notify hypercall
VMM processes entire buffer
Result: 1 exit instead of 180 → **180× faster**
```

This is why **Virtio** queues are used in production!

## Troubleshooting

### "No such file or directory" for /dev/kvm
```bash
sudo modprobe kvm
sudo modprobe kvm_intel  # or kvm_amd
ls -l /dev/kvm
```

### "Permission denied" on /dev/kvm
```bash
# Option 1: Add user to kvm group
sudo usermod -aG kvm $USER
newgrp kvm

# Option 2: Run with sudo (not recommended)
sudo make multiplication
```

### "KVM_CREATE_VM failed"
Your CPU doesn't have VT-x/AMD-V enabled:
1. Enter BIOS/UEFI
2. Find "Intel VT-x" or "AMD-V" option
3. Enable it
4. Reboot

### Guest outputs wrong numbers
Check register management! Key insight from multiplication.S:
- ❌ Wrong: `mov %ch, %cl; mul %cl` (overwrites dan counter)
- ✅ Right: `mov %ch, %bl; mul %bl` (preserves dan)

## Extending This Project

### Easy (1-2 hours)
1. **Add HC_GETCHAR**: Read input from guest
2. **Fibonacci sequence**: Different algorithm (iterative)
3. **Prime number checker**: More complex arithmetic

### Medium (3-4 hours)
1. **HC_RANDOM**: Random number generation
2. **Simple game**: Guessing game with hints
3. **Performance stats**: Count and report VM exits

### Advanced (1+ week)
1. **Protected Mode**: 32-bit mode with paging
2. **Multi-vCPU**: Multiple virtual CPUs per VM
3. **Real UART**: Actually read/write serial port
4. **PIC/APIC**: Interrupt handling

## References

### Official Documentation
- [KVM API Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Intel® 64 and IA-32 Architectures Software Developer Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Linux KVM Source](https://github.com/torvalds/linux/tree/master/virt/kvm)

### Related Projects
- [kvmtool](https://github.com/kvmtool/kvmtool): Lightweight VMM reference
- [QEMU](https://www.qemu.org/): Full system emulator with KVM support
- [Firecracker](https://firecracker-microvm.github.io/): Secure microVM runtime
- [Cloud Hypervisor](https://github.com/cloud-hypervisor/cloud-hypervisor): Rust-based VMM

### Learning Resources
- Using the KVM API (LWN.net): https://lwn.net/Articles/658511/
- Rust VMM Project: https://github.com/rust-vmm
- x86 Assembly Guide: http://www.cs.virginia.edu/~evans/cs216/guides/x86.html

## Timeline

- **Week 10**: Phase 1 complete (RISC-V Linux + KVM environment)
- **Week 11**: Phase 2 Week 1 (x86 KVM VMM basics)
- **Week 12**: Phase 2 Week 2 (Hypercall system + complex guests)
- **Week 13-16**: Documentation, final report, demo

## License

Educational project for Ajou University coursework.

## Acknowledgments

- Linux KVM developers for the excellent API
- Intel/AMD for hardware virtualization support
- Online communities for documentation and examples

---

**Status**: Phase 2 Complete ✅
**Last Updated**: Week 12
**Next Phase**: Final documentation and demo (Week 13-16)
