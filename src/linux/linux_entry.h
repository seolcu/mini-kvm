/*
 * linux_entry.h - experimental Linux boot support (quarantined)
 *
 * See linux_entry.c. This path is incomplete: it loads a bzImage and enters
 * it, but does not reach a shell. It is kept because the bring-up work is
 * part of the project record, and isolated so the core VMM does not carry
 * Linux-specific branches.
 */

#ifndef LINUX_ENTRY_H
#define LINUX_ENTRY_H

#include <stdbool.h>
#include <stdint.h>
#include <linux/kvm.h>

#include "../vcpu.h"

/* Supply the KVM handle and verbosity this module needs. Call once. */
void linux_entry_configure(int kvm_fd, bool verbose);

/* Place an IVT of IRET stubs so real-mode setup code can take interrupts. */
void linux_setup_ivt(void *guest_mem);

/* Flat __BOOT_CS/__BOOT_DS segments for the 32-bit boot protocol. */
void linux_setup_boot_segments(struct kvm_sregs *sregs);

/* Enter the kernel at code32_start in protected mode, paging off. */
int linux_configure_code32_entry(vcpu_context_t *ctx, uint32_t boot_params_addr);

/* Enter a 64-bit kernel at its long-mode entry point. */
int linux_configure_boot64_entry(vcpu_context_t *ctx);

/* Turn KVM single-step on or off for this vCPU. */
int linux_set_singlestep(vcpu_context_t *ctx, bool enable);

/*
 * Handle a KVM_EXIT_DEBUG, recording the instruction trace. Returns 0 to
 * continue. Only meaningful while a single-step budget remains.
 */
int linux_handle_debug_exit(vcpu_context_t *ctx);

/* Report the recorded trace and IDT vectors after a SHUTDOWN (triple fault). */
void linux_report_shutdown(vcpu_context_t *ctx);

/* Prepare the single vCPU that boots a bzImage. Returns 0 on success. */
int linux_prepare_vcpu(vcpu_context_t *ctx, const vmm_config_t *cfg);

#endif /* LINUX_ENTRY_H */
