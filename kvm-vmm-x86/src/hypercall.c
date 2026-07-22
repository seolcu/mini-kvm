/*
 * hypercall.c - dispatch for the guest/host call interface (see hypercall.h)
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "hypercall.h"
#include "console.h"

hc_outcome_t hypercall_dispatch(int vcpu_fd, int vcpu_id, const char *name, bool verbose)
{
    struct kvm_regs regs;

    if (ioctl(vcpu_fd, KVM_GET_REGS, &regs) < 0) {
        perror("KVM_GET_REGS (hypercall)");
        return HC_FAILED;
    }

    uint8_t nr = (uint8_t)(regs.rax & 0xFF);
    hc_outcome_t outcome = HC_CONTINUE;
    int64_t result = 0;

    if (verbose && nr != HC_GETCHAR) {
        /* GETCHAR traces are suppressed on purpose: at an interactive prompt
         * they would drown out the guest's own output. */
        console_vcpu_printf(vcpu_id, name, "HC 0x%02x (RBX=0x%llx)\n",
                            nr, (unsigned long long)regs.rbx);
    }

    switch (nr) {
    case HC_EXIT:
        outcome = HC_EXIT_GUEST;
        break;

    case HC_PUTCHAR:
        console_vcpu_putchar(vcpu_id, name, (char)(regs.rbx & 0xFF));
        result = 1;
        break;

    case HC_GETCHAR:
        /* Blocks until a key arrives. Returns -1 at end of input or when a
         * termination signal was received, letting the guest shut down
         * rather than spin. */
        result = console_wait_char();
        break;

    default:
        /* Unknown call: report it and hand back -1. Tearing the VM down here
         * would turn a guest-side bug into a host-side abort. */
        console_vcpu_printf(vcpu_id, name, "unknown hypercall 0x%02x (returning -1)\n", nr);
        result = -1;
        break;
    }

    /*
     * Publish the result. The OUT instruction has already retired, so RIP is
     * untouched and KVM's pending-I/O completion still applies cleanly; only
     * RAX changes.
     */
    regs.rax = (uint64_t)result;
    if (ioctl(vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        perror("KVM_SET_REGS (hypercall result)");
        return HC_FAILED;
    }

    return outcome;
}
