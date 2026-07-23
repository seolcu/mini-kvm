/*
 * MSR (Model Specific Register) handling header for Mini-KVM
 */

#ifndef MSR_H
#define MSR_H

#include <stdint.h>

// Setup essential MSRs for 64-bit mode
// Returns number of MSRs set, or -1 on error
int setup_msrs_64bit(int vcpu_fd);

// Read/Write single MSR
int read_msr(int vcpu_fd, uint32_t msr_index, uint64_t *value);
int write_msr(int vcpu_fd, uint32_t msr_index, uint64_t value);

// Dump MSRs for debugging
void dump_msrs(int vcpu_fd);

// Get MSR name (for debugging)
const char *get_msr_name(uint32_t msr_index);

#endif // MSR_H
