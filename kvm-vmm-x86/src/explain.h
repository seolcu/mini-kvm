/*
 * explain.h - post-mortem analysis of a dead guest
 *
 * This is the feature the project exists for. When a kernel triple-faults,
 * QEMU resets the CPU and tells you nothing; the guest is simply gone. What a
 * kernel developer needs to know is not that it faulted but *why*: which
 * exception, whether a handler was installed for it, whether the address it
 * touched was mapped, whether the stack it pushed to exists.
 *
 * All of that is recoverable after the fact from the vCPU state and the
 * descriptor tables and page tables sitting in guest memory, which is what
 * this module does.
 */

#ifndef EXPLAIN_H
#define EXPLAIN_H

#include "vcpu.h"

/*
 * Report why the guest died, in as much detail as its state supports.
 * Called on KVM_EXIT_SHUTDOWN, which on x86 means a triple fault.
 */
void explain_shutdown(vcpu_context_t *ctx);

/*
 * Report a failed VM entry. Rarer, and usually means the register state we
 * handed KVM was invalid rather than anything the guest did.
 */
void explain_failed_entry(vcpu_context_t *ctx, uint64_t reason);

/*
 * Report a KVM internal error. Suberror 1 is an emulation failure, which in
 * practice nearly always means the guest jumped somewhere with no memory
 * behind it.
 */
void explain_internal_error(vcpu_context_t *ctx, uint32_t suberror);

/*
 * Resolve a guest virtual address to a guest physical one using the guest's
 * own page tables, honouring real mode, 32-bit paging, PAE and long mode.
 * On failure `why` receives the level and entry that stopped the walk.
 */
bool guest_translate(vcpu_context_t *ctx, const struct kvm_sregs *s,
                     uint64_t va, uint64_t *pa_out, char *why, size_t why_len);

/* Human-readable name for the guest's current addressing mode. */
const char *guest_mode_name(const struct kvm_sregs *s);

#endif /* EXPLAIN_H */
