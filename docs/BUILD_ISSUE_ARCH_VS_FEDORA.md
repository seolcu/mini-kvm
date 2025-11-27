# 1K OS Build Issue: Arch Linux vs Fedora 43

## Executive Summary

**Problem**: 1K OS kernel built on Arch Linux crashes immediately with `KVM_EXIT_SHUTDOWN` (Double Fault), while the same source code builds and runs successfully on Fedora 43.

**Root Cause**: Arch GCC's default 32-bit target architecture (i386) differs from Fedora's (i686), resulting in different code generation and memory layout.

**Solution**: Add `-march=i686` to compilation flags to match Fedora's code generation.

---

## Timeline

- **Date**: 2025-11-27 (목) 23:00-23:40 KST
- **Hardware**: AMD Ryzen 5 9600X (Zen 5), Arch Linux host
- **Context**: 40 minutes before demo video deadline

---

## Problem Description

### Symptoms

When running the Arch-built kernel on KVM:

```
[kernel] SHUTDOWN at RIP=0xfff0, RSP=0x0
[kernel]   Exception: injected=0 nr=8 has_error=1 error=0x0
```

- Immediate crash with Double Fault (#8)
- Reset vector (RIP=0xfff0) indicates triple fault
- No output from kernel initialization

### Environment Comparison

| Aspect | Fedora 43 | Arch Linux |
|--------|-----------|------------|
| GCC Version | 15.2.1 20251111 (Red Hat) | 15.2.1 20251112 |
| Build Result | ✅ Works | ❌ SHUTDOWN |
| Hardware | Same (AMD Zen 5) | Same (AMD Zen 5) |
| Binary Size | 13K | 13K → 12K (fixed) |

---

## Investigation Process

### 1. Binary Comparison

Initial hexdump comparison revealed different first instructions:

**Fedora build (working)**:
```
00000000  bc 50 75 01 80  |  mov esp, 0x80017550
```

**Arch build (failing)**:
```
00000000  bc 70 76 01 80  |  mov esp, 0x80017670
```

Stack pointer initialization differs by 0x120 bytes (288 bytes).

### 2. ELF Section Layout Analysis

**Fedora**:
- .text: 0xf63 bytes
- .bss at 0x800041e0
- Stack ~0x80017550

**Arch**:
- .text: 0xff3 bytes (144 bytes larger!)
- .bss at 0x80004300
- Stack ~0x80017670

### 3. Function Size Comparison

Using `nm -S kernel.o`:

| Function | Fedora | Arch (original) | Arch (fixed) |
|----------|--------|-----------------|--------------|
| fs_flush | 0x1d8 (472 bytes) | 0x213 (531 bytes) | 0x1aa (426 bytes) |
| common.o total | 0x2c3 bytes | 0x313 bytes | Smaller |

The `fs_flush` function alone was **59 bytes larger** in Arch!

### 4. GCC Configuration Differences

**Fedora GCC**:
```
--with-arch_32=i686
--enable-gnu-indirect-function
--enable-cet
```

**Arch GCC**:
```
--enable-default-pie
--enable-default-ssp
(no --with-arch_32, defaults to i386)
```

Key insight: **Arch defaults to i386, Fedora defaults to i686** for 32-bit code.

### 5. Code Generation Difference

Disassembly of `fs_flush` showed different register allocation:

**Arch (i386)**:
```asm
2e3:  8d b0 00 00 00 00    lea    0x0(%eax),%esi
```

**Fedora (i686)**:
```asm
2e3:  8d 98 00 00 00 00    lea    0x0(%eax),%ebx
```

i686 architecture enables additional optimizations and different register allocation strategies, resulting in more compact code.

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
   - Critical for matching Fedora's memory layout

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
- Status: ❌ SHUTDOWN (Double Fault)

### After Fix
- Binary: 12K (more optimized!)
- ESP: 0x800172b0
- Status: ✅ BOOTS SUCCESSFULLY

### Output

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
```

---

## Why Did This Work?

The root issue wasn't just about addresses or layout - it was about **code quality**.

1. **i686 generates better code**: The P6+ instruction set allows more efficient optimizations
2. **Smaller functions**: Better register allocation reduces code size
3. **Tighter layout**: Smaller code means sections are closer together
4. **No page boundary issues**: The compact layout avoids potential page table setup problems

The original Arch build may have been placing code or data across page boundaries in unexpected ways, or the larger code size was triggering edge cases in the KVM page table setup.

---

## Lessons Learned

1. **Identical GCC versions ≠ identical behavior**: Build-time configuration matters
2. **Check default targets**: `gcc -v` shows crucial configuration differences
3. **Code size affects runtime behavior**: In kernel code, layout matters
4. **Arch != Fedora**: Despite both being Linux, toolchain defaults differ significantly

---

## Recommendations

### For Arch Linux Users

Always specify architecture explicitly when building 32-bit code:
```makefile
CFLAGS += -march=i686  # Or -march=i386 if compatibility is needed
```

### For Cross-Distribution Builds

1. Document the exact GCC configuration used
2. Test binaries on the target distribution
3. Keep working binaries as references (see `backups/working-fedora-build/`)

### For Kernel Development

1. Use explicit architecture targets
2. Disable PIE/stack-protector explicitly with both `-fno-*` and `-no-*` variants
3. Verify section layouts with `readelf -S` and `objdump -h`

---

## Files Modified

- `kvm-vmm-x86/os-1k/Makefile`: Added `-march=i686` and linker flags

---

## References

- GCC i686 vs i386: https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
- KVM_EXIT_SHUTDOWN: Double Fault (#8) indicates CPU exception during boot
- Binary comparison: `hexdump`, `cmp -l`, `readelf`, `objdump`
- Backup location: `/home/seolcu/문서/코드/mini-kvm/backups/working-fedora-build/`

---

## Reproduction

To reproduce the issue:

```bash
# On Arch, WITHOUT the fix:
cd kvm-vmm-x86/os-1k
git checkout <commit-before-fix>
make clean && make all
printf '1\n0\n' | ../kvm-vmm --paging kernel.bin
# Result: SHUTDOWN

# WITH the fix:
git checkout main
make clean && make all
printf '1\n0\n' | ../kvm-vmm --paging kernel.bin
# Result: Shell menu appears
```

---

**Last Updated**: 2025-11-27 23:40 KST
**Status**: ✅ RESOLVED
