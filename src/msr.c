/*
 * MSR (Model Specific Register) handling for Mini-KVM
 *
 * Handles MSR reads and writes from guests.
 * Essential for 64-bit Long Mode operation.
 */

#include "msr.h"
#include "long_mode.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <errno.h>

// Setup essential MSRs for 64-bit Long Mode
int setup_msrs_64bit(int vcpu_fd) {
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[20]; // Space for multiple MSRs
    } msr_data = {
        .info.nmsrs = 0,
    };
    
    struct kvm_msrs *msrs = &msr_data.info;
    
    // MSR 0: EFER (Extended Feature Enable Register)
    // Enable Long Mode and SYSCALL/SYSRET
    msrs->entries[msrs->nmsrs].index = MSR_EFER;
    msrs->entries[msrs->nmsrs].data = EFER_LME | EFER_SCE | EFER_NXE;
    msrs->nmsrs++;
    DEBUG_PRINT(DEBUG_DETAILED, "Setting MSR_EFER: LME | SCE | NXE");
    
    // MSR 1-4: SYSCALL/SYSRET configuration (will be set by guest OS)
    // For now, set to zero - guest will configure these
    msrs->entries[msrs->nmsrs].index = MSR_STAR;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    msrs->entries[msrs->nmsrs].index = MSR_LSTAR;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    msrs->entries[msrs->nmsrs].index = MSR_CSTAR;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    msrs->entries[msrs->nmsrs].index = MSR_FMASK;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    // MSR 5-7: FS/GS base (per-CPU data, set to zero initially)
    msrs->entries[msrs->nmsrs].index = MSR_FS_BASE;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    msrs->entries[msrs->nmsrs].index = MSR_GS_BASE;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    msrs->entries[msrs->nmsrs].index = MSR_KERNEL_GS_BASE;
    msrs->entries[msrs->nmsrs].data = 0;
    msrs->nmsrs++;
    
    // Set all MSRs
    if (ioctl(vcpu_fd, KVM_SET_MSRS, msrs) < 0) {
        perror("KVM_SET_MSRS failed");
        return -1;
    }
    
    DEBUG_PRINT(DEBUG_BASIC, "MSR configuration set (%d MSRs)", msrs->nmsrs);
    return msrs->nmsrs;
}

// Read a single MSR value
int read_msr(int vcpu_fd, uint32_t msr_index, uint64_t *value) {
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[1];
    } msr_data = {
        .info.nmsrs = 1,
        .entries[0].index = msr_index,
    };
    
    if (ioctl(vcpu_fd, KVM_GET_MSRS, &msr_data.info) < 0) {
        DEBUG_PRINT(DEBUG_ALL, "Failed to read MSR 0x%x: %s", 
                   msr_index, strerror(errno));
        return -1;
    }
    
    *value = msr_data.entries[0].data;
    DEBUG_PRINT(DEBUG_ALL, "Read MSR 0x%x = 0x%llx", 
               msr_index, (unsigned long long)*value);
    return 0;
}

// Write a single MSR value
int write_msr(int vcpu_fd, uint32_t msr_index, uint64_t value) {
    struct {
        struct kvm_msrs info;
        struct kvm_msr_entry entries[1];
    } msr_data = {
        .info.nmsrs = 1,
        .entries[0] = {
            .index = msr_index,
            .data = value,
        },
    };
    
    if (ioctl(vcpu_fd, KVM_SET_MSRS, &msr_data.info) < 0) {
        DEBUG_PRINT(DEBUG_ALL, "Failed to write MSR 0x%x: %s", 
                   msr_index, strerror(errno));
        return -1;
    }
    
    DEBUG_PRINT(DEBUG_ALL, "Wrote MSR 0x%x = 0x%llx", 
               msr_index, (unsigned long long)value);
    return 0;
}

// Dump essential MSRs for debugging
void dump_msrs(int vcpu_fd) {
    uint64_t value;
    
    fprintf(stderr, "\n========== MSR Dump ==========\n");
    
    if (read_msr(vcpu_fd, MSR_EFER, &value) == 0) {
        fprintf(stderr, "EFER (0x%x): 0x%016llx [", MSR_EFER, 
                (unsigned long long)value);
        if (value & EFER_SCE) fprintf(stderr, "SCE ");
        if (value & EFER_LME) fprintf(stderr, "LME ");
        if (value & EFER_LMA) fprintf(stderr, "LMA ");
        if (value & EFER_NXE) fprintf(stderr, "NXE ");
        fprintf(stderr, "]\n");
    }
    
    if (read_msr(vcpu_fd, MSR_STAR, &value) == 0) {
        fprintf(stderr, "STAR (0x%x): 0x%016llx\n", MSR_STAR, 
                (unsigned long long)value);
    }
    
    if (read_msr(vcpu_fd, MSR_LSTAR, &value) == 0) {
        fprintf(stderr, "LSTAR (0x%x): 0x%016llx\n", MSR_LSTAR, 
                (unsigned long long)value);
    }
    
    if (read_msr(vcpu_fd, MSR_FS_BASE, &value) == 0) {
        fprintf(stderr, "FS_BASE (0x%x): 0x%016llx\n", MSR_FS_BASE, 
                (unsigned long long)value);
    }
    
    if (read_msr(vcpu_fd, MSR_GS_BASE, &value) == 0) {
        fprintf(stderr, "GS_BASE (0x%x): 0x%016llx\n", MSR_GS_BASE, 
                (unsigned long long)value);
    }
    
    fprintf(stderr, "==============================\n\n");
}

// Get MSR name for debugging
const char *get_msr_name(uint32_t msr_index) {
    switch (msr_index) {
        case MSR_EFER: return "EFER";
        case MSR_STAR: return "STAR";
        case MSR_LSTAR: return "LSTAR";
        case MSR_CSTAR: return "CSTAR";
        case MSR_FMASK: return "FMASK";
        case MSR_FS_BASE: return "FS_BASE";
        case MSR_GS_BASE: return "GS_BASE";
        case MSR_KERNEL_GS_BASE: return "KERNEL_GS_BASE";
        case MSR_APIC_BASE: return "APIC_BASE";
        default:
            if (msr_index >= MSR_X2APIC_START && msr_index <= MSR_X2APIC_END) {
                return "X2APIC";
            }
            return "UNKNOWN";
    }
}
