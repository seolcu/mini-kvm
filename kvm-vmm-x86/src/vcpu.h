/*
 * vcpu.h - per-vCPU state
 *
 * Mini-KVM runs one VM with up to MAX_VCPUS vCPUs, each on its own pthread.
 * The vCPUs are NOT an SMP system: each one runs an independent guest program
 * with its own memory mapping and its own KVM memory slot, placed at guest
 * physical address vcpu_id * mem_size. Changing that layout means rebuilding
 * the guests and reworking the 1K OS loader.
 */

#ifndef VCPU_H
#define VCPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/kvm.h>

#include "cli.h"
#include "console.h"
#include "loader.h"

/*
 * Debug state for the experimental Linux bring-up. Single-stepping a kernel
 * produces a register trace to compare against a known-good boot; none of it
 * is used by the real-mode, paging, or long-mode paths.
 */
/*
 * The last instruction the guest executed, captured while single-stepping.
 *
 * A triple fault resets the CPU before KVM tells us about it, so the live
 * register state at KVM_EXIT_SHUTDOWN describes the reset vector and nothing
 * about the fault. Recording each step is the only way to still have the
 * state that mattered, which is why --explain trades speed for it.
 */
typedef struct {
    bool enabled;
    bool valid;                 /* a snapshot has been taken */
    bool exhausted;             /* budget spent; tracing turned itself off */
    unsigned long steps;
    unsigned long budget;
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    uint8_t bytes[16];
} trace_state_t;

typedef struct {
    int remaining;          /* step budget; 0 disables single-step */
    bool paused;            /* temporarily off, e.g. across a REP loop */
    int exits;              /* KVM_EXIT_DEBUG count */

    /* Snapshot of the last stepped instruction, reported on SHUTDOWN. */
    uint64_t rip, rsi, rbx, rdi, rcx, rsp, rflags, cr0;
    uint16_t cs, es;
    uint64_t es_base;
    uint32_t es_limit;
    uint64_t idt_base;
    uint16_t idt_limit;
    uint8_t bytes[4];
} singlestep_state_t;

typedef struct {
    int vcpu_id;                /* 0..MAX_VCPUS-1 */
    int vcpu_fd;
    struct kvm_run *kvm_run;
    size_t kvm_run_mmap_size;

    void *guest_mem;            /* host mapping of this vCPU's guest memory */
    size_t mem_size;

    const char *guest_binary;   /* path as given on the command line */
    char name[256];             /* display name, e.g. "multiplication" */

    int exit_count;
    bool running;

    /* CPU mode */
    bool use_paging;
    bool long_mode;
    uint32_t entry_point;
    uint32_t load_offset;

    /* How the guest image was loaded, and the state it wants at entry. */
    guest_image_t image;

    /* Linux boot (experimental) */
    bool linux_guest;
    linux_entry_mode_t linux_entry;
    linux_rsi_mode_t linux_rsi;
    singlestep_state_t singlestep;

    /* Populated by --explain; consulted by explain_shutdown(). */
    trace_state_t trace;

    /* Last reported addressing mode, so transitions can be spotted.
     * Points at a static string from guest_mode_name(). */
    const char *last_mode;
    bool trace_modes;           /* --trace-modes */
    bool inspect_on_exit;       /* --inspect */
} vcpu_context_t;

/*
 * Output on behalf of a vCPU. Terminal handling, colors, and the keyboard
 * ring live in console.c; these just supply the vCPU identity.
 */
#define vcpu_printf(ctx, ...) \
    console_vcpu_printf((ctx)->vcpu_id, (ctx)->name, __VA_ARGS__)
#define vcpu_putchar(ctx, ch) \
    console_vcpu_putchar((ctx)->vcpu_id, (ctx)->name, (ch))

/* --- vcpu.c ----------------------------------------------------------- */

/* Supply diagnostic options resolved from the command line. Call once. */
void vcpu_set_dump_options(bool dump_regs, const char *dump_mem_path, int num_vcpus);

/* Read a flat guest binary into guest memory at the given offset. */
int vcpu_load_guest_binary(const char *filename, void *mem, size_t mem_size,
                           uint32_t load_offset);

/* Create the vCPU and put it in its target CPU mode, ready for KVM_RUN. */
int vcpu_setup(vcpu_context_t *ctx);

/*
 * Single-step this vCPU, recording the state before each instruction so that
 * explain_shutdown() has something to work with after a triple fault resets
 * the CPU. Costs a VM exit per instruction, hence --explain rather than
 * always on.
 */
void vcpu_enable_trace(vcpu_context_t *ctx, unsigned long budget);

/* pthread entry point: run KVM_RUN until the guest stops. */
void *vcpu_thread(void *arg);

/*
 * Run every vCPU to completion, one thread each, and join them.
 * Also runs a watchdog that periodically interrupts the threads so a guest
 * blocked in KVM_RUN can still be stopped. Returns -1 if a thread failed to
 * start.
 */
int vcpu_run_all(vcpu_context_t *ctxs, int count);

/* Dump memory if requested, then release this vCPU's resources. */
void vcpu_cleanup(vcpu_context_t *ctx);

/* Display name for a guest path ("guest/hello" -> "hello"). */
const char *vcpu_extract_name(const char *filename);

#endif /* VCPU_H */
