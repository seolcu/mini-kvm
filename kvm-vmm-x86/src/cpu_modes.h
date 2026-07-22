/*
 * cpu_modes.h - guest CPU mode setup (real, protected, long)
 *
 * Mini-KVM enters protected and long mode on the guest's behalf: before the
 * first KVM_RUN the VMM builds the descriptor tables and page tables in guest
 * memory, then hands the vCPU over already in the target mode. A guest kernel
 * therefore starts executing with paging live and must NOT reload its segment
 * registers -- doing so triple-faults (see os-1k/boot.S).
 *
 * These functions are pure with respect to KVM: they fill in a kvm_sregs or
 * write structures into a guest memory mapping. Applying them via
 * KVM_SET_SREGS is the caller's job.
 */

#ifndef CPU_MODES_H
#define CPU_MODES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/kvm.h>

#include "protected_mode.h"
#include "vcpu.h"

/* --- Descriptor helpers ------------------------------------------------ */

/* Fill in one 32-bit GDT descriptor. */
void gdt_set_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t flags);

/* --- Real mode --------------------------------------------------------- */

/*
 * Point every segment at `base`, which must be paragraph-aligned. In real
 * mode the linear address is segment*16 + offset, so this is how each vCPU is
 * confined to its own slice of guest memory.
 */
void segments_set_real(struct kvm_sregs *sregs, uint32_t base);

/* --- Protected mode ---------------------------------------------------- */

/*
 * Initial ESP handed to a protected-mode guest, chosen to sit above the load
 * area (0x1000) and below the page directory (0x00100000), within the
 * identity-mapped low 4MB. Kernels normally replace this immediately.
 */
#define PROT_MODE_DEFAULT_STACK 0x00080000

/* Flat 4GB ring-0 code/data segments matching the GDT built by gdt_setup(). */
void segments_set_flat32(struct kvm_sregs *sregs);

/*
 * Build the protected-mode GDT at GDT_ADDR in guest memory: null, ring-0
 * code/data, ring-3 code/data.
 */
void gdt_setup(void *guest_mem, bool verbose);

/* Build an empty 256-entry IDT just past the GDT. Returns its guest address. */
uint32_t idt_setup(void *guest_mem, bool verbose);

/*
 * Build 32-bit page tables and return the value for CR3, or 0 on failure.
 *
 * Uses 4KB pages with PSE deliberately disabled: 4MB PSE pages trigger an
 * immediate triple fault on AMD Zen 5 (docs/investigations/).
 * Identity-maps the low 4MB and mirrors it at 0x80000000 for the high-half
 * kernel mapping the 1K OS expects.
 */
uint32_t page_tables_setup32(void *guest_mem, size_t mem_size, int vcpu_id,
                             const char *vcpu_name, bool verbose);

/* --- Long mode --------------------------------------------------------- */

/* Build the 64-bit GDT (null + ring-0 code/data with the L bit set). */
void gdt_setup_long(void *guest_mem, uint64_t gdt_base);

/* --- Applying a mode to a live vCPU ------------------------------------ */

/*
 * Put an already-created vCPU into 32-bit protected mode with paging: build
 * the GDT, IDT, and page tables in guest memory, then set CR0/CR3 and the
 * entry point. The guest resumes with paging already live.
 */
int cpu_mode_enter_protected(vcpu_context_t *ctx);

/* Same, for 64-bit long mode: PAE page tables, EFER.LME/LMA, CS.L. */
int cpu_mode_enter_long(vcpu_context_t *ctx);

#endif /* CPU_MODES_H */
