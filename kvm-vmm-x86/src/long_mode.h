/*
 * Long Mode (64-bit) structures and constants for Mini-KVM
 *
 * This file provides definitions for x86-64 Long Mode operation:
 * - 4-level paging (PML4 → PDPT → PD → PT)
 * - 64-bit GDT descriptors
 * - MSR definitions
 * - CPUID feature flags
 */

#ifndef LONG_MODE_H
#define LONG_MODE_H

#include <stdint.h>

// ==================== Page Table Structures (4-level paging) ====================

// Page table entry flags (same for all levels)
#define PTE_PRESENT    (1ULL << 0)   // Page is present in memory
#define PTE_WRITE      (1ULL << 1)   // Writable
#define PTE_USER       (1ULL << 2)   // User-accessible
#define PTE_PWT        (1ULL << 3)   // Page-level write-through
#define PTE_PCD        (1ULL << 4)   // Page-level cache disable
#define PTE_ACCESSED   (1ULL << 5)   // Page has been accessed
#define PTE_DIRTY      (1ULL << 6)   // Page has been written to (PT only)
#define PTE_PSE        (1ULL << 7)   // Page size extension (2MB/1GB pages)
#define PTE_GLOBAL     (1ULL << 8)   // Global page (not flushed on CR3 write)
#define PTE_NX         (1ULL << 63)  // No execute

// Page table physical address mask (bits 12-51)
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

// 4-level paging structure
// Each entry is 8 bytes (uint64_t)
// Each table has 512 entries (9 bits of addressing)

typedef uint64_t pml4e_t;  // PML4 Entry (Level 4)
typedef uint64_t pdpte_t;  // Page Directory Pointer Table Entry (Level 3)
typedef uint64_t pde_t;    // Page Directory Entry (Level 2)
typedef uint64_t pte_t;    // Page Table Entry (Level 1)

// Page table structure (512 entries × 8 bytes = 4KB)
#define PT_ENTRIES 512

typedef struct {
    pml4e_t entries[PT_ENTRIES];
} pml4_t;

typedef struct {
    pdpte_t entries[PT_ENTRIES];
} pdpt_t;

typedef struct {
    pde_t entries[PT_ENTRIES];
} pd_t;

typedef struct {
    pte_t entries[PT_ENTRIES];
} pt_t;

// Virtual address breakdown for 4-level paging
// [63:48] Sign extension (unused)
// [47:39] PML4 index (9 bits)
// [38:30] PDPT index (9 bits)
// [29:21] PD index (9 bits)
// [20:12] PT index (9 bits)
// [11:0]  Offset (12 bits)

#define VA_PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define VA_PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define VA_PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define VA_PT_INDEX(va)    (((va) >> 12) & 0x1FF)
#define VA_OFFSET(va)      ((va) & 0xFFF)

// ==================== GDT for Long Mode ====================

// 64-bit GDT descriptor
typedef struct __attribute__((packed)) {
    uint16_t limit_low;    // Limit 0:15 (ignored in 64-bit mode)
    uint16_t base_low;     // Base 0:15 (ignored in 64-bit mode)
    uint8_t  base_mid;     // Base 16:23 (ignored in 64-bit mode)
    uint8_t  access;       // Access byte
    uint8_t  granularity;  // Flags and limit 16:19
    uint8_t  base_high;    // Base 24:31 (ignored in 64-bit mode)
} gdt_entry_64_t;

// GDT access byte flags
#define GDT_PRESENT         (1 << 7)  // Segment present
#define GDT_DPL_0           (0 << 5)  // Privilege level 0 (kernel)
#define GDT_DPL_3           (3 << 5)  // Privilege level 3 (user)
#define GDT_CODE_DATA       (1 << 4)  // Code/Data segment (not system)
#define GDT_EXECUTABLE      (1 << 3)  // Executable (code segment)
#define GDT_DC              (1 << 2)  // Direction/Conforming
#define GDT_RW              (1 << 1)  // Readable (code) / Writable (data)
#define GDT_ACCESSED        (1 << 0)  // Accessed bit

// GDT granularity byte flags
#define GDT_GRANULARITY_4K  (1 << 7)  // Limit in 4KB pages
#define GDT_SIZE_32         (1 << 6)  // 32-bit segment
#define GDT_LONG_MODE       (1 << 5)  // 64-bit long mode segment

// Predefined GDT entries for 64-bit mode
#define GDT_NULL_ENTRY      0  // Null descriptor (required)
#define GDT_KERNEL_CODE_64  1  // 64-bit kernel code segment
#define GDT_KERNEL_DATA_64  2  // 64-bit kernel data segment
#define GDT_USER_CODE_64    3  // 64-bit user code segment
#define GDT_USER_DATA_64    4  // 64-bit user data segment

// Segment selectors (index << 3 | RPL)
#define SELECTOR_KERNEL_CODE_64  ((GDT_KERNEL_CODE_64 << 3) | 0)  // 0x08
#define SELECTOR_KERNEL_DATA_64  ((GDT_KERNEL_DATA_64 << 3) | 0)  // 0x10
#define SELECTOR_USER_CODE_64    ((GDT_USER_CODE_64 << 3) | 3)    // 0x1B
#define SELECTOR_USER_DATA_64    ((GDT_USER_DATA_64 << 3) | 3)    // 0x23

// ==================== MSR Definitions ====================

// Extended Feature Enable Register (EFER)
#define MSR_EFER                0xC0000080
#define EFER_SCE                (1 << 0)   // System Call Extensions (SYSCALL/SYSRET)
#define EFER_LME                (1 << 8)   // Long Mode Enable
#define EFER_LMA                (1 << 10)  // Long Mode Active (read-only, set by CPU)
#define EFER_NXE                (1 << 11)  // No-Execute Enable

// SYSCALL/SYSRET MSRs
#define MSR_STAR                0xC0000081  // SYSCALL target address
#define MSR_LSTAR               0xC0000082  // Long mode SYSCALL target
#define MSR_CSTAR               0xC0000083  // Compatibility mode SYSCALL target
#define MSR_FMASK               0xC0000084  // SYSCALL flag mask

// FS/GS base MSRs (per-CPU data)
#define MSR_FS_BASE             0xC0000100
#define MSR_GS_BASE             0xC0000101
#define MSR_KERNEL_GS_BASE      0xC0000102

// APIC MSRs (for future use)
#define MSR_APIC_BASE           0x0000001B
#define MSR_X2APIC_START        0x00000800
#define MSR_X2APIC_END          0x000008FF

// Memory Type Range Registers (MTRRs)
#define MSR_MTRR_CAP            0x000000FE
#define MSR_MTRR_DEF_TYPE       0x000002FF
#define MSR_MTRR_PHYSBASE0      0x00000200
#define MSR_MTRR_PHYSMASK0      0x00000201

// ==================== CPUID Feature Flags ====================

// CPUID.01H:EDX - Processor Info and Feature Bits
#define CPUID_FEAT_FPU          (1 << 0)   // x87 FPU
#define CPUID_FEAT_VME          (1 << 1)   // Virtual 8086 Mode
#define CPUID_FEAT_DE           (1 << 2)   // Debugging Extensions
#define CPUID_FEAT_PSE          (1 << 3)   // Page Size Extension
#define CPUID_FEAT_TSC          (1 << 4)   // Time Stamp Counter
#define CPUID_FEAT_MSR          (1 << 5)   // Model Specific Registers
#define CPUID_FEAT_PAE          (1 << 6)   // Physical Address Extension
#define CPUID_FEAT_MCE          (1 << 7)   // Machine Check Exception
#define CPUID_FEAT_CX8          (1 << 8)   // CMPXCHG8 instruction
#define CPUID_FEAT_APIC         (1 << 9)   // APIC on-chip
#define CPUID_FEAT_SEP          (1 << 11)  // SYSENTER/SYSEXIT
#define CPUID_FEAT_MTRR         (1 << 12)  // Memory Type Range Registers
#define CPUID_FEAT_PGE          (1 << 13)  // Page Global Enable
#define CPUID_FEAT_MCA          (1 << 14)  // Machine Check Architecture
#define CPUID_FEAT_CMOV         (1 << 15)  // Conditional Move
#define CPUID_FEAT_PAT          (1 << 16)  // Page Attribute Table
#define CPUID_FEAT_PSE36        (1 << 17)  // 36-bit PSE
#define CPUID_FEAT_CLFLUSH      (1 << 19)  // CLFLUSH instruction
#define CPUID_FEAT_MMX          (1 << 23)  // MMX Technology
#define CPUID_FEAT_FXSR         (1 << 24)  // FXSAVE/FXRSTOR
#define CPUID_FEAT_SSE          (1 << 25)  // SSE
#define CPUID_FEAT_SSE2         (1 << 26)  // SSE2

// CPUID.01H:ECX - Additional Feature Bits
#define CPUID_FEAT_SSE3         (1 << 0)   // SSE3
#define CPUID_FEAT_PCLMUL       (1 << 1)   // PCLMULQDQ
#define CPUID_FEAT_SSSE3        (1 << 9)   // SSSE3
#define CPUID_FEAT_FMA          (1 << 12)  // FMA
#define CPUID_FEAT_CX16         (1 << 13)  // CMPXCHG16B
#define CPUID_FEAT_SSE41        (1 << 19)  // SSE4.1
#define CPUID_FEAT_SSE42        (1 << 20)  // SSE4.2
#define CPUID_FEAT_X2APIC       (1 << 21)  // x2APIC
#define CPUID_FEAT_POPCNT       (1 << 23)  // POPCNT instruction
#define CPUID_FEAT_AES          (1 << 25)  // AES-NI
#define CPUID_FEAT_XSAVE        (1 << 26)  // XSAVE/XRSTOR
#define CPUID_FEAT_AVX          (1 << 28)  // AVX
#define CPUID_FEAT_RDRAND       (1 << 30)  // RDRAND

// CPUID.80000001H:EDX - Extended Processor Info
#define CPUID_EXT_SYSCALL       (1 << 11)  // SYSCALL/SYSRET
#define CPUID_EXT_NX            (1 << 20)  // NX bit (No-Execute)
#define CPUID_EXT_PDPE1GB       (1 << 26)  // 1GB pages
#define CPUID_EXT_RDTSCP        (1 << 27)  // RDTSCP instruction
#define CPUID_EXT_LM            (1 << 29)  // Long Mode (x86-64)

// CPUID.80000001H:ECX - Extended Feature Bits
#define CPUID_EXT_LAHF          (1 << 0)   // LAHF/SAHF in 64-bit mode
#define CPUID_EXT_ABM           (1 << 5)   // Advanced Bit Manipulation
#define CPUID_EXT_SSE4A         (1 << 6)   // SSE4A
#define CPUID_EXT_PREFETCHW     (1 << 8)   // PREFETCHW

// ==================== Memory Layout for 64-bit Guest ====================

// Suggested memory layout for 64-bit Linux guest
#define GUEST_64_MEM_SIZE       (128ULL << 20)  // 128MB (expandable)
#define GUEST_64_LOAD_ADDR      0x1000000       // Load kernel at 16MB
#define GUEST_64_ENTRY_POINT    0x1000000       // Entry point

// Page table locations in guest physical memory
#define GUEST_64_PML4_ADDR      0x2000          // PML4 at 8KB
#define GUEST_64_PDPT_ADDR      0x3000          // PDPT at 12KB
#define GUEST_64_PD_ADDR        0x4000          // PD at 16KB
#define GUEST_64_PT_ADDR        0x5000          // PT at 20KB (if needed)

#endif // LONG_MODE_H
