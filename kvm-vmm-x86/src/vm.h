/*
 * vm.h - VM lifecycle: /dev/kvm, the VM instance, and guest memory
 *
 * Mini-KVM creates exactly one VM. Each vCPU gets its own memory region and
 * its own KVM memory slot at guest physical address vcpu_id * mem_size, so
 * the guests are isolated programs rather than an SMP system.
 */

#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vcpu.h"

/*
 * Open /dev/kvm, check the API version, create the VM, and set the TSS
 * address.
 *
 * need_irqchip creates an in-kernel interrupt controller. Pass false for
 * real-mode guests: they deliberately run without one, because an unwanted
 * IRQ0 leaves a HLT-terminated guest hung.
 */
int vm_init(bool need_irqchip);

/* Close the VM and /dev/kvm. Safe to call when vm_init() failed. */
void vm_shutdown(void);

/* The /dev/kvm handle, for ioctls that target KVM rather than the VM. */
int vm_kvm_fd(void);

/* The VM handle. */
int vm_get_fd(void);

/* Raise and immediately lower an IRQ line. No-op without an IRQCHIP. */
void vm_pulse_irq(uint32_t irq);

/*
 * Size, allocate, and register this vCPU's guest memory, filling in
 * ctx->guest_mem and ctx->mem_size.
 */
int vm_map_vcpu_memory(vcpu_context_t *ctx);

/* Create the vCPU and mmap its kvm_run structure. */
int vm_create_vcpu(vcpu_context_t *ctx);

#endif /* VM_H */
