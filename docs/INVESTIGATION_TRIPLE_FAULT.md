# Triple Fault Investigation Report

This document records the investigation of a Triple Fault (Double Fault #8) issue that occurs when running the 1K OS kernel on the KVM VMM after migrating from Fedora 43 to Arch Linux.

## Environment

- **Host OS**: Arch Linux (kernel 6.17.8)
- **Previous Working Host**: Fedora 43
- **CPU**: AMD Ryzen 7 PRO 6850U
- **VMM**: Custom KVM-based VMM for x86 32-bit Protected Mode with Paging

## Symptom

- `kernel.bin` immediately crashes with **Double Fault (Exception #8)** on the first KVM_RUN
- Real-mode guest programs work correctly
- Individual protected-mode test binaries work correctly
- The crash occurs **before any guest code actually executes**

## Memory Layout

| Component | Physical Address | Virtual Address |
|-----------|------------------|-----------------|
| GDT | 0x500 | - |
| IDT | 0x528 | - |
| Page Directory | 0x100000 | - |
| Kernel Load | 0x1000 | 0x80001000 |
| Kernel Entry | 0x1000 | 0x80001000 |

- 4MB PSE pages are used for paging
- Identity mapping for kernel space (0x80000000+)

## What Was Tried

### 1. TSS Address Fix
- Changed TSS address from 0x500 to 0x1a00 to avoid GDT/IDT overlap
- **Result**: No effect

### 2. Segment Type Fix
- Fixed segment descriptors for proper kernel/user code/data types
- **Result**: No effect

### 3. IRQCHIP Disable
- Initially disabled IRQCHIP creation for protected mode
- **Result**: No effect

### 4. Timer/Keyboard Thread Disable
- Disabled timer and keyboard injection threads entirely:
  ```c
  if (enable_paging && 0) {  // Timer thread disabled
  ```
- **Result**: No effect

### 5. Debug Logging
- Added extensive debug output:
  - Page directory entry dumps
  - Binary verification at load offset
  - Memory checks before page table setup
  - VCPU events on SHUTDOWN (exception info)
- **Result**: Confirmed Double Fault (#8), but execution never reaches guest code

### 6. Test Binary Isolation

Created 15+ test binaries to isolate the issue:

| Binary | Description | Result |
|--------|-------------|--------|
| test_hlt_only.bin | Just HLT instruction | SUCCESS |
| test_one_out.bin | Single character output | SUCCESS |
| test_stack_set.bin | Stack setup + output | SUCCESS |
| test_bss_step.bin | BSS clear, stack, call to function | SUCCESS |
| test_exact_kernel.bin | Same first 32 bytes as kernel.bin | **SUCCESS** |
| kernel.bin | Full 1K OS kernel (13040 bytes) | **FAIL** |

## Key Discovery

### Experiment: Byte-by-byte Comparison

1. **test_exact_kernel.bin** (3088 bytes): Uses identical boot code (first 32 bytes) as kernel.bin
   - **Result**: Works perfectly, outputs "M"

2. **kernel_boot_only.bin**: kernel.bin's first 64 bytes + test_exact_kernel remainder
   - **Result**: Works, outputs "M"

3. **test_128.bin**: kernel.bin's first 128 bytes + test_exact_kernel remainder
   - **Result**: Works, outputs "M"

4. **kernel.bin** (13040 bytes): Full kernel
   - **Result**: Double Fault immediately

### The Mystery

The boot code in `boot.S` is only **34 bytes**:

```asm
.section .boot, "ax"
.global _start
.code32
_start:
    # Set up stack
    movl $0x800FF000, %esp
    movl %esp, %ebp
    
    # Clear BSS
    movl $_bss_start, %edi
    movl $_bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    
    # Call kernel_main
    call kernel_main
    
    # Should not return
1:  hlt
    jmp 1b
```

**But**:
- Test binaries with identical boot bytes (offset 0x00-0x21) **work**
- kernel.bin with identical boot bytes **fails**
- The only difference is the `.text` section content (offset 0x40+)

This means: **Content that hasn't been executed yet somehow causes the crash during initial VM entry.**

## Hypotheses for Further Investigation

### 1. Page Table Corruption
The page directory is at 0x100000, and kernel.bin is loaded at 0x1000. Perhaps some aspect of the binary content at specific offsets is being misinterpreted by KVM or the CPU during page table walk.

### 2. Instruction Pre-fetch/Decode Issue
Modern CPUs prefetch and speculatively decode instructions. Perhaps some byte pattern in kernel.bin's .text section causes an issue during speculative execution or prefetch.

### 3. Memory Aliasing
With 4MB PSE pages, there might be aliasing issues where physical memory contains unexpected values that interfere with protected mode setup.

### 4. KVM State Corruption
The binary size or content might affect how KVM sets up internal state before the first instruction executes.

### 5. Linux Kernel 6.17 KVM Regression
This issue appeared after migrating from Fedora 43 to Arch Linux with kernel 6.17.8. There might be a regression in KVM handling of protected mode entry.

## Suggested Next Steps

1. **Bisect the kernel.bin**
   - Find the exact byte offset where adding more kernel.bin content causes failure
   - Currently known: 128 bytes works, 13040 bytes fails

2. **Test with QEMU**
   - Try running the same kernel.bin with QEMU to see if it's KVM-specific
   - Use `./guest/run_qemu.sh` if available

3. **Minimal KVM Test**
   - Create a minimal KVM program that only does protected mode entry
   - Remove all multi-vCPU, threading, and I/O handling code

4. **Linux Kernel Bisect**
   - If the issue is kernel-version specific, bisect Linux kernel versions

5. **Check KVM Capabilities**
   - Compare `/proc/cpuinfo` and KVM capabilities between working/non-working systems

## Files to Keep for Further Investigation

- `os-1k/kernel.bin` - The failing kernel
- `os-1k/test_exact_kernel.S` and `.bin` - Working reference with same boot code
- `os-1k/test_bss_step.S` and `.bin` - Another working reference

## Debug Code Location

Debug logging was added to `src/main.c`:
- Page directory dumps in `setup_page_tables()`
- Binary verification in `load_binary()`
- VCPU events dump in `run_vcpu()` on SHUTDOWN

The timer thread creation is currently disabled with `if (enable_paging && 0)`.

---

**Date**: 2024-11-27
**Status**: Unresolved - Handoff to next investigator
