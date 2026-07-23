/*
 * hypercall.h - the Mini-KVM guest/host call interface
 *
 * ABI
 * ---
 * A guest issues a hypercall with a single `OUT` to HYPERCALL_PORT, with the
 * call number in AL and arguments in the registers listed below. The VMM
 * writes the result back into RAX before resuming the guest, so a hypercall
 * behaves like an ordinary function call:
 *
 *   AL = HC_EXIT    (0x00)  terminate this vCPU.               returns nothing
 *   AL = HC_PUTCHAR (0x01)  write BL to the console.           returns 1
 *   AL = HC_GETCHAR (0x02)  read one character, blocking.      returns the
 *                           character, or -1 at end of input
 *
 * An unknown call number returns -1 rather than killing the guest.
 *
 * HC_GETCHAR blocks inside the VMM until a key is available. Guests must not
 * poll it in a spin loop; that is what the old two-step OUT/IN protocol
 * forced them to do, and it burned a full host core at an idle prompt.
 *
 * These numbers are duplicated as SYS_* in os-1k/common.h and hand-encoded in
 * the guest assembly sources. Changing them breaks every guest.
 */

#ifndef HYPERCALL_H
#define HYPERCALL_H

#include <stdbool.h>

#define HYPERCALL_PORT 0x500

#define HC_EXIT    0x00
#define HC_PUTCHAR 0x01
#define HC_GETCHAR 0x02

typedef enum {
    HC_CONTINUE,    /* resume the guest */
    HC_EXIT_GUEST,  /* guest asked to terminate */
    HC_FAILED,      /* host-side error; tear the vCPU down */
} hc_outcome_t;

/*
 * Service one hypercall on the vCPU that just exited with an OUT to
 * HYPERCALL_PORT. Reads the guest registers, dispatches, and writes the
 * result back to RAX.
 */
hc_outcome_t hypercall_dispatch(int vcpu_fd, int vcpu_id, const char *name, bool verbose);

#endif /* HYPERCALL_H */
