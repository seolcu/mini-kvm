/*
 * inspect.h - decode a guest's descriptor tables and page tables
 *
 * The complement to explain.c: rather than working out why a guest died, this
 * shows what it built while it is alive. Descriptor and page table entries are
 * packed bitfields that are tedious and error-prone to read by hand, and a
 * wrong bit in one of them is a common reason a kernel misbehaves without
 * crashing.
 */

#ifndef INSPECT_H
#define INSPECT_H

#include "vcpu.h"

/*
 * Dump the guest's GDT, IDT and page tables, decoded. Reads the tables out of
 * guest memory through the guest's own mappings, so it reflects what the CPU
 * would see rather than what the VMM originally wrote.
 */
void inspect_dump(vcpu_context_t *ctx);

/*
 * Check a freshly loaded image before the first KVM_RUN and warn about the
 * mistakes that stop a kernel before it prints anything: an entry point
 * outside every loaded segment, or one that overlaps the structures the VMM
 * placed in low memory. Returns the number of warnings issued.
 */
int inspect_preflight(vcpu_context_t *ctx);

/*
 * Note the guest's addressing mode, reporting it whenever it changes.
 * Called at VM exits and, under --explain, at every step: the real→protected→
 * long transitions are where kernels most often go wrong, and they are
 * invisible otherwise.
 */
void inspect_note_mode(vcpu_context_t *ctx);

#endif /* INSPECT_H */
