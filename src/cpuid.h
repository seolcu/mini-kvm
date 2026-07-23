/*
 * CPUID emulation header for Mini-KVM
 */

#ifndef CPUID_H
#define CPUID_H

#include <stdint.h>
#include <linux/kvm.h>

// Setup CPUID for a vCPU
// kvm_fd: /dev/kvm file descriptor for KVM_GET_SUPPORTED_CPUID
// vcpu_fd: vCPU file descriptor for KVM_SET_CPUID2
// Returns number of entries set, or -1 on error
int setup_cpuid(int kvm_fd, int vcpu_fd);

// Print CPUID entry (for debugging)
void print_cpuid_entry(struct kvm_cpuid_entry2 *entry);

#endif // CPUID_H
