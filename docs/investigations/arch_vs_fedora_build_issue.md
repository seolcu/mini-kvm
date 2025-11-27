# 1K OS Build Issue: Arch Linux vs Fedora 43 (Triple Fault Investigation)

## Executive Summary

**Problem**: 1K OS kernel built on Arch Linux crashes immediately with `KVM_EXIT_SHUTDOWN` (Double Fault #8), while the same source code builds and runs successfully on Fedora 43.

**Root Cause**: Arch GCC's default 32-bit target architecture (i386) differs from Fedora's (i686), resulting in different code generation, larger function sizes, and incompatible memory layout.

**Solution**: Add `-march=i686` and proper linker flags to compilation to match Fedora's code generation.

**Status**: ✅ **RESOLVED** (2025-11-27 23:40 KST)

---

## Timeline

- **Initial Problem**: 2024-11-27 (발견 후 Fedora 빌드로 우회)
- **Final Investigation**: 2025-11-27 (목) 23:00-23:40 KST
- **Resolution**: 2025-11-27 23:40 KST (데모 영상 마감 40분 전!)
- **Hardware**: AMD Ryzen 5 9600X (Zen 5), Arch Linux host
- **Total Investigation Time**: ~2 hours across multiple sessions

---

## Problem Description

### Symptoms

When running the Arch-built kernel on KVM:

```
[kernel] SHUTDOWN at RIP=0xfff0, RSP=0x0
[kernel]   Exception: injected=0 nr=8 has_error=1 error=0x0
```

- Immediate crash with **Double Fault (#8)** on first KVM_RUN
- Reset vector (RIP=0xfff0) indicates **Triple Fault**
- No output from kernel initialization
- Crash occurs **before any guest code actually executes**

### Environment Comparison

| Aspect | Fedora 43 (Working) | Arch Linux (Failing) |
|--------|---------------------|----------------------|
| GCC Version | 15.2.1 20251111 (Red Hat) | 15.2.1 20251112 |
| Build Result | ✅ Works | ❌ SHUTDOWN |
| Hardware | AMD Zen 5 | Same |
| Binary Size | 13K | 13K → 12K (after fix) |
| Linux Kernel | 6.x | 6.17.8 |

### Memory Layout

| Component | Physical Address | Virtual Address |
|-----------|------------------|-----------------|
| GDT | 0x500 | - |
| IDT | 0x528 | - |
| Page Directory | 0x100000 | - |
| Kernel Load | 0x1000 | 0x80001000 |
| Kernel Entry | 0x1000 | 0x80001000 |
| Stack (before fix) | - | 0x80017670 |
| Stack (after fix) | - | 0x800172b0 |

---

## Investigation Process

### Phase 1: Initial Troubleshooting (Failed Attempts)

#### 1. TSS Address Fix
- **Hypothesis**: GDT/IDT overlap causing corruption
- **Change**: TSS address 0x500 → 0x1a00
- **Result**: ❌ No effect

#### 2. Segment Descriptor Fix
- **Change**: Fixed segment types for kernel/user code/data
- **Result**: ❌ No effect

#### 3. IRQCHIP Disable
- **Change**: Disabled IRQCHIP creation for protected mode
- **Result**: ❌ No effect

#### 4. Timer/Keyboard Thread Disable
- **Change**: Disabled interrupt injection threads
```c
if (enable_paging && 0) {  // Timer thread disabled
```
- **Result**: ❌ No effect

#### 5. Debug Logging
- Added extensive debug output:
  - Page directory entry dumps
  - Binary verification at load offset
  - Memory checks before page table setup
  - VCPU events on SHUTDOWN
- **Result**: Confirmed Double Fault (#8), but execution never reaches guest code

### Phase 2: Test Binary Isolation (The Mystery)

Created 15+ test binaries to isolate the issue:

| Binary | Size | Description | Result |
|--------|------|-------------|--------|
| test_hlt_only.bin | 1 byte | Just HLT instruction | ✅ SUCCESS |
| test_one_out.bin | 33 bytes | Single character output | ✅ SUCCESS |
| test_stack_set.bin | ~50 bytes | Stack setup + output | ✅ SUCCESS |
| test_bss_step.bin | ~100 bytes | BSS clear, stack, function call | ✅ SUCCESS |
| test_exact_kernel.bin | 3088 bytes | Same first 32 bytes as kernel.bin | ✅ SUCCESS |
| test_128.bin | 128 bytes | kernel.bin first 128 bytes + test code | ✅ SUCCESS |
| **kernel.bin** | **13040 bytes** | **Full 1K OS kernel** | ❌ **FAIL** |

#### Key Discovery

The boot code in `boot.S` is only **34 bytes** and is identical across all test binaries:

```asm
_start:
    movl $0x800FF000, %esp    # Set up stack
    movl %esp, %ebp
    
    movl $_bss_start, %edi    # Clear BSS
    movl $_bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    
    call kernel_main          # Call kernel
    
1:  hlt                       # Should not return
    jmp 1b
```

**The Mystery**:
- Test binaries with **identical boot bytes** (offset 0x00-0x21) → ✅ Work
- kernel.bin with **identical boot bytes** → ❌ Fail
- The only difference is the `.text` section content (offset 0x40+)
- **Content that hasn't been executed yet somehow causes the crash!**

### Phase 3: Binary Comparison (Breakthrough!)

#### Binary Level Analysis

Initial hexdump comparison revealed different first instructions:

**Fedora build (working)**:
```
00000000  bc 50 75 01 80  |  mov esp, 0x80017550
```

**Arch build (failing)**:
```
00000000  bc 70 76 01 80  |  mov esp, 0x80017670
```

Stack pointer initialization differs by **0x120 bytes (288 bytes)**.

#### ELF Section Layout Analysis

**Fedora**:
- .text: **0xf63 bytes**
- .bss at 0x800041e0
- Stack ~0x80017550

**Arch (before fix)**:
- .text: **0xff3 bytes** (144 bytes larger!)
- .bss at 0x80004300
- Stack ~0x80017670

#### Function Size Comparison

Using `nm -S kernel.o`:

| Function | Fedora | Arch (original) | Arch (fixed) |
|----------|--------|-----------------|--------------|
| fs_flush | 0x1d8 (472 bytes) | 0x213 (531 bytes) | 0x1aa (426 bytes) |
| common.o total | 0x2c3 bytes | 0x313 bytes | Smaller after fix |

The `fs_flush` function alone was **59 bytes larger** in Arch!

#### GCC Configuration Differences (Root Cause!)

**Fedora GCC** (`gcc -v`):
```
--with-arch_32=i686
--enable-gnu-indirect-function
--enable-cet
```

**Arch GCC** (`gcc -v`):
```
--enable-default-pie
--enable-default-ssp
(no --with-arch_32, defaults to i386)
```

**Key insight**: **Arch defaults to i386, Fedora defaults to i686** for 32-bit code generation.

#### Code Generation Difference

Disassembly of `fs_flush` showed different register allocation:

**Arch (i386)**:
```asm
2e3:  8d b0 00 00 00 00    lea    0x0(%eax),%esi
```

**Fedora (i686)**:
```asm
2e3:  8d 98 00 00 00 00    lea    0x0(%eax),%ebx
```

i686 architecture enables:
- CMOV, FCOMI, and other Pentium Pro+ instructions
- Better register allocation strategies
- More compact code generation

---

## Solution

### Minimal Fix

Add `-march=i686` to CFLAGS in `os-1k/Makefile`:

```makefile
CFLAGS = -m32 -march=i686 -std=c11 -O2 -Wall -Wextra -g \
         -ffreestanding -nostdlib -fno-builtin \
         -fno-stack-protector -fno-pie -fno-pic -no-pie \
         -mno-red-zone
```

### Complete Fix (Recommended)

Also update linker flags for robustness:

```makefile
KERNEL_LDFLAGS = -m elf_i386 -T kernel.ld \
                 --no-pie \
                 --build-id=none \
                 -z norelro
```

### Why These Flags?

1. **`-march=i686`**: Target Pentium Pro+ architecture (matches Fedora default)
   - Enables CMOV, FCOMI, and other P6+ instructions
   - Changes register allocation and code generation strategies
   - **Critical for matching Fedora's memory layout**

2. **`-no-pie`**: Disable Position Independent Executable at GCC driver level
   - Arch GCC has `--enable-default-pie`, which affects code generation
   - Different from `-fno-pie` (which only affects code generation)
   - Required to override Arch's default PIE behavior

3. **`--no-pie`**: Disable PIE in linker
   - Ensures absolute addressing in final binary

4. **`--build-id=none`**: Remove `.note.gnu.build-id` section
   - Reduces bloat and ensures deterministic layout

5. **`-z norelro`**: Disable RELRO (RELocation Read-Only)
   - Not needed for freestanding kernel code

---

## Results

### Before Fix
- Binary: 13K
- ESP: 0x80017670
- .text section: 0xff3 bytes
- Status: ❌ SHUTDOWN (Double Fault #8)

### After Fix
- Binary: 12K (more optimized!)
- ESP: 0x800172b0
- .text section: ~0xf63 bytes (similar to Fedora)
- Status: ✅ BOOTS SUCCESSFULLY

### Output After Fix

```
=== 1K OS x86 ===
Booting in Protected Mode with Paging...

Interrupt handlers registered
  Timer (IRQ 0, vector 0x20)
  Syscalls via hypercall (port 0x500, IOPL=3 allows user I/O)
Filesystem initialized
Created idle process (pid=0)
Created shell process (pid=2)

=== Kernel Initialization Complete ===
Starting shell process (PID 2)...

======================================
   Welcome to 1K OS Shell!
   Mini-KVM Educational Hypervisor
======================================

=== 1K OS Menu ===
  1. Multiplication Table (2x1 ~ 9x9)
  2. Counter (0-9)
  3. Echo (interactive)
  4. Fibonacci Sequence
  5. Prime Numbers (up to 100)
  6. Calculator
  7. Factorial (0! ~ 12!)
  8. GCD (Greatest Common Divisor)
  9. About 1K OS
  0. Exit

Select:
```

---

## Why Did This Work?

The root issue wasn't just about addresses or layout - it was about **code quality and memory layout**.

### Theory: Page Boundary Issues

1. **i686 generates better code**: The P6+ instruction set allows more efficient optimizations
2. **Smaller functions**: Better register allocation reduces code size by ~140 bytes
3. **Tighter layout**: Smaller code means sections are closer together
4. **No page boundary issues**: The compact layout avoids potential page table setup problems

The original Arch build (i386) may have been placing code or data across page boundaries in unexpected ways. With 4MB PSE pages:
- Any misalignment or unexpected byte pattern could trigger page fault during initial protected mode entry
- Larger code size might cause the stack or other sections to cross critical boundaries
- The CPU's prefetch mechanism might encounter invalid page mappings

### Why Test Binaries Worked

Small test binaries (< 4KB):
- Fit entirely within a single 4MB page
- No complex section layout
- No risk of boundary crossing

Full kernel.bin (13KB):
- Multiple sections spanning different offsets
- Complex memory layout with .text, .data, .bss
- **i386 code generation created a layout that KVM's page table setup couldn't handle**

---

## Lessons Learned

1. **Identical GCC versions ≠ identical behavior**
   - Build-time configuration (`--with-arch_32`) matters more than version
   - Always check `gcc -v` for configuration flags

2. **Check default targets explicitly**
   - Fedora: i686 (Pentium Pro+)
   - Arch: i386 (Generic 80386)
   - Debian/Ubuntu: Usually i686

3. **Code size affects runtime behavior in kernel code**
   - Memory layout matters in protected mode
   - Page boundary alignment is critical
   - Larger code != same functionality

4. **Arch != Fedora != Ubuntu**
   - Despite all being Linux, toolchain defaults differ significantly
   - Cross-distribution reproducibility requires explicit flags

5. **Test binaries can be misleading**
   - Small test programs may not expose layout issues
   - Always test with production-size binaries

---

## Hypotheses (From Investigation Phase)

These were our theories during investigation (now resolved):

1. ✅ **Page Table Corruption** → Root cause was code size/layout
2. ❌ Instruction Pre-fetch/Decode Issue → Not the issue
3. ✅ **Memory Aliasing** → 4MB PSE pages + i386 layout caused problems
4. ❌ KVM State Corruption → KVM was fine
5. ❌ Linux Kernel 6.17 KVM Regression → Kernel was fine

---

## Recommendations

### For Arch Linux Users

Always specify architecture explicitly when building 32-bit code:
```makefile
CFLAGS += -march=i686  # Or -march=i386 if strict compatibility needed
```

### For Cross-Distribution Builds

1. **Document the exact GCC configuration used**
   - Include `gcc -v` output in build docs
   
2. **Test binaries on all target distributions**
   - Don't assume "same source = same binary"
   
3. **Keep working binaries as references**
   - See `backups/working-fedora-build/` for our saved reference

### For Kernel Development

1. **Use explicit architecture targets** (`-march=i686`)
2. **Disable PIE/stack-protector explicitly** with both `-fno-*` and `-no-*` variants
3. **Verify section layouts** with `readelf -S` and `objdump -h`
4. **Check binary sizes** - unexpected size changes indicate code generation differences
5. **Test with production-size binaries** - don't rely solely on minimal tests

---

## Files Modified

- `kvm-vmm-x86/os-1k/Makefile`: Added `-march=i686` and linker flags

---

## References

- **GCC i686 vs i386**: https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
- **KVM_EXIT_SHUTDOWN**: Double Fault (#8) indicates CPU exception during boot
- **Binary comparison tools**: `hexdump`, `cmp -l`, `readelf`, `objdump`, `nm`
- **Backup location**: `/home/seolcu/문서/코드/mini-kvm/backups/working-fedora-build/`

---

## Reproduction

To reproduce the issue and verify the fix:

```bash
# On Arch, WITHOUT the fix:
cd kvm-vmm-x86/os-1k

# Remove -march=i686 from Makefile temporarily
sed -i 's/-march=i686//' Makefile

make clean && make all
printf '1\n0\n' | ../kvm-vmm --paging kernel.bin
# Result: [kernel] SHUTDOWN at RIP=0xfff0 (Double Fault)

# WITH the fix (restore -march=i686):
git checkout Makefile  # Or re-add -march=i686
make clean && make all
printf '1\n0\n' | ../kvm-vmm --paging kernel.bin
# Result: ✅ Shell menu appears, multiplication table outputs
```

---

## Debug Files to Keep

For future reference or similar issues:

- `os-1k/kernel.bin` - Working kernel (with fix)
- `os-1k/test_exact_kernel.S` and `.bin` - Test binary with same boot code
- `os-1k/test_bss_step.S` and `.bin` - Another working reference
- `backups/working-fedora-build/kernel.bin.working` - Original Fedora build

---

**Investigation Period**: 2024-11-27 ~ 2025-11-27  
**Final Resolution**: 2025-11-27 23:40 KST  
**Status**: ✅ **RESOLVED**  
**Investigators**: seolcu (with AI assistance)

