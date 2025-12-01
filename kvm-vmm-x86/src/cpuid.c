/*
 * CPUID emulation for Mini-KVM
 *
 * Provides CPUID responses to guest queries about CPU features.
 * This is critical for Linux boot as the kernel checks CPU capabilities.
 */

#include "cpuid.h"
#include "long_mode.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>

// Setup CPUID entries for a vCPU
// kvm_fd: /dev/kvm file descriptor for KVM_GET_SUPPORTED_CPUID
// vcpu_fd: vCPU file descriptor for KVM_SET_CPUID2
// Returns number of entries set, or -1 on error
int setup_cpuid(int kvm_fd, int vcpu_fd) {
    struct kvm_cpuid2 *cpuid;
    int nent = 100; // Maximum number of CPUID entries
    
    // Allocate CPUID structure
    size_t size = sizeof(*cpuid) + nent * sizeof(cpuid->entries[0]);
    cpuid = calloc(1, size);
    if (!cpuid) {
        perror("Failed to allocate CPUID structure");
        return -1;
    }
    
    cpuid->nent = nent;
    
    // Get supported CPUID entries from KVM (uses /dev/kvm fd)
    if (ioctl(kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid) < 0) {
        perror("KVM_GET_SUPPORTED_CPUID failed");
        free(cpuid);
        return -1;
    }
    
    DEBUG_PRINT(DEBUG_DETAILED, "KVM supports %d CPUID entries", cpuid->nent);
    
    // Modify CPUID entries to match our VMM capabilities
    for (unsigned int i = 0; i < cpuid->nent; i++) {
        struct kvm_cpuid_entry2 *entry = &cpuid->entries[i];
        
        switch (entry->function) {
            case 0x0: // Get Vendor ID and Maximum Function
                // Leave as-is (reports maximum standard function)
                DEBUG_PRINT(DEBUG_ALL, "CPUID[0x0]: Max function = 0x%x", entry->eax);
                break;
                
            case 0x1: // Processor Info and Feature Bits
                // EDX: Standard feature flags
                entry->edx |= CPUID_FEAT_FPU;    // x87 FPU
                entry->edx |= CPUID_FEAT_PSE;    // Page Size Extension
                entry->edx |= CPUID_FEAT_TSC;    // Time Stamp Counter
                entry->edx |= CPUID_FEAT_MSR;    // Model Specific Registers
                entry->edx |= CPUID_FEAT_PAE;    // Physical Address Extension
                entry->edx |= CPUID_FEAT_APIC;   // APIC
                entry->edx |= CPUID_FEAT_SEP;    // SYSENTER/SYSEXIT
                entry->edx |= CPUID_FEAT_MTRR;   // Memory Type Range Registers
                entry->edx |= CPUID_FEAT_PGE;    // Page Global Enable
                entry->edx |= CPUID_FEAT_CMOV;   // Conditional Move
                entry->edx |= CPUID_FEAT_PAT;    // Page Attribute Table
                entry->edx |= CPUID_FEAT_CLFLUSH;// CLFLUSH
                entry->edx |= CPUID_FEAT_MMX;    // MMX
                entry->edx |= CPUID_FEAT_FXSR;   // FXSAVE/FXRSTOR
                entry->edx |= CPUID_FEAT_SSE;    // SSE
                entry->edx |= CPUID_FEAT_SSE2;   // SSE2
                
                // ECX: Additional feature flags
                entry->ecx |= CPUID_FEAT_SSE3;   // SSE3
                entry->ecx |= CPUID_FEAT_SSSE3;  // SSSE3
                entry->ecx |= CPUID_FEAT_CX16;   // CMPXCHG16B
                entry->ecx |= CPUID_FEAT_SSE41;  // SSE4.1
                entry->ecx |= CPUID_FEAT_SSE42;  // SSE4.2
                entry->ecx |= CPUID_FEAT_POPCNT; // POPCNT
                
                DEBUG_PRINT(DEBUG_DETAILED, "CPUID[0x1]: EDX=0x%x ECX=0x%x", 
                           entry->edx, entry->ecx);
                break;
                
            case 0x2: // Cache and TLB Information
                // Leave as-is (KVM provides reasonable defaults)
                break;
                
            case 0x4: // Deterministic Cache Parameters
                // Leave as-is
                break;
                
            case 0x6: // Thermal and Power Management
                // Leave as-is
                break;
                
            case 0x7: // Extended Features
                // Leave as-is for now (AVX, AVX2, etc.)
                break;
                
            case 0xD: // Processor Extended State Enumeration
                // Leave as-is (XSAVE support)
                break;
                
            case 0x80000000: // Get Maximum Extended Function
                DEBUG_PRINT(DEBUG_ALL, "CPUID[0x80000000]: Max ext function = 0x%x", 
                           entry->eax);
                break;
                
            case 0x80000001: // Extended Processor Info and Feature Bits
                // EDX: Extended features
                entry->edx |= CPUID_EXT_SYSCALL; // SYSCALL/SYSRET
                entry->edx |= CPUID_EXT_NX;      // NX bit
                entry->edx |= CPUID_EXT_PDPE1GB; // 1GB pages
                entry->edx |= CPUID_EXT_RDTSCP;  // RDTSCP
                entry->edx |= CPUID_EXT_LM;      // Long Mode (x86-64)
                
                // ECX: Extended features
                entry->ecx |= CPUID_EXT_LAHF;    // LAHF/SAHF in 64-bit
                
                DEBUG_PRINT(DEBUG_DETAILED, "CPUID[0x80000001]: EDX=0x%x ECX=0x%x", 
                           entry->edx, entry->ecx);
                break;
                
            case 0x80000002: // Processor Brand String (part 1)
            case 0x80000003: // Processor Brand String (part 2)
            case 0x80000004: // Processor Brand String (part 3)
                // Leave as-is
                break;
                
            case 0x80000008: // Virtual and Physical Address Sizes
                // Leave as-is (reports 48-bit virtual, 40-bit physical typically)
                DEBUG_PRINT(DEBUG_ALL, "CPUID[0x80000008]: Addr sizes = 0x%x", entry->eax);
                break;
        }
    }
    
    // Set CPUID for this vCPU
    if (ioctl(vcpu_fd, KVM_SET_CPUID2, cpuid) < 0) {
        perror("KVM_SET_CPUID2 failed");
        free(cpuid);
        return -1;
    }
    
    int nent_set = cpuid->nent;
    free(cpuid);
    
    DEBUG_PRINT(DEBUG_BASIC, "CPUID configuration set (%d entries)", nent_set);
    return nent_set;
}

// Print CPUID entry for debugging
void print_cpuid_entry(struct kvm_cpuid_entry2 *entry) {
    fprintf(stderr, "CPUID[0x%08x", entry->function);
    if (entry->index != 0) {
        fprintf(stderr, ".%d", entry->index);
    }
    fprintf(stderr, "]: EAX=0x%08x EBX=0x%08x ECX=0x%08x EDX=0x%08x\n",
            entry->eax, entry->ebx, entry->ecx, entry->edx);
}
