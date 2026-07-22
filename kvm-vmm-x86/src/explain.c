/*
 * explain.c - post-mortem analysis of a dead guest
 */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "explain.h"
#include "console.h"
#include "protected_mode.h"

/* CR0 bits we care about. */
#define CR0_PE (1u << 0)
#define CR0_PG (1u << 31)
#define CR4_PAE (1u << 5)
#define EFER_LMA_BIT (1ull << 10)
#define RFLAGS_IF (1ull << 9)

/* Page table entry bits. */
#define PTE_PRESENT (1u << 0)
#define PTE_RW      (1u << 1)
#define PTE_USER    (1u << 2)
#define PTE_PSE     (1u << 7)

/* Report through the vCPU-tagged console so multi-guest runs stay readable. */
#define say(ctx, ...) console_vcpu_printf((ctx)->vcpu_id, (ctx)->name, __VA_ARGS__)

static const char *exception_name(unsigned nr)
{
    switch (nr) {
    case 0:  return "#DE divide error";
    case 1:  return "#DB debug";
    case 2:  return "NMI";
    case 3:  return "#BP breakpoint";
    case 4:  return "#OF overflow";
    case 5:  return "#BR bound range exceeded";
    case 6:  return "#UD invalid opcode";
    case 7:  return "#NM device not available";
    case 8:  return "#DF double fault";
    case 10: return "#TS invalid TSS";
    case 11: return "#NP segment not present";
    case 12: return "#SS stack fault";
    case 13: return "#GP general protection";
    case 14: return "#PF page fault";
    case 16: return "#MF x87 floating point";
    case 17: return "#AC alignment check";
    case 18: return "#MC machine check";
    case 19: return "#XM SIMD floating point";
    default: return "unknown exception";
    }
}

/* Describes the guest's current addressing mode in one phrase. */
static const char *mode_name(const struct kvm_sregs *s)
{
    if (s->efer & EFER_LMA_BIT) {
        return "long mode";
    }
    if (!(s->cr0 & CR0_PE)) {
        return "real mode";
    }
    return (s->cr0 & CR0_PG) ? "protected mode with paging"
                             : "protected mode, paging off";
}

/* Safe read of a 32-bit word from guest physical memory. */
static bool read_phys32(vcpu_context_t *ctx, uint64_t pa, uint32_t *out)
{
    if (pa + 4 > ctx->mem_size) {
        return false;
    }
    memcpy(out, (const char *)ctx->guest_mem + pa, 4);
    return true;
}

/*
 * Translate a guest virtual address using the guest's own 32-bit page tables,
 * reporting where the walk failed. Returns false if unmapped.
 */
static bool walk32(vcpu_context_t *ctx, const struct kvm_sregs *s,
                   uint32_t va, uint32_t *pa_out, char *why, size_t why_len)
{
    uint32_t pd_base = (uint32_t)s->cr3 & 0xFFFFF000u;
    uint32_t pde_index = va >> 22;
    uint32_t pte_index = (va >> 12) & 0x3FF;

    uint32_t pde;
    if (!read_phys32(ctx, pd_base + pde_index * 4u, &pde)) {
        snprintf(why, why_len, "page directory at 0x%08x is outside guest memory", pd_base);
        return false;
    }
    if (!(pde & PTE_PRESENT)) {
        snprintf(why, why_len, "PDE[%u] = 0x%08x (not present)", pde_index, pde);
        return false;
    }
    if (pde & PTE_PSE) {
        /* 4MB page: the directory entry maps it directly. */
        *pa_out = (pde & 0xFFC00000u) | (va & 0x003FFFFFu);
        return true;
    }

    uint32_t pt_base = pde & 0xFFFFF000u;
    uint32_t pte;
    if (!read_phys32(ctx, pt_base + pte_index * 4u, &pte)) {
        snprintf(why, why_len, "page table at 0x%08x is outside guest memory", pt_base);
        return false;
    }
    if (!(pte & PTE_PRESENT)) {
        snprintf(why, why_len, "PDE[%u] = 0x%08x, PTE[%u] = 0x%08x (not present)",
                 pde_index, pde, pte_index, pte);
        return false;
    }

    *pa_out = (pte & 0xFFFFF000u) | (va & 0xFFFu);
    return true;
}

/*
 * Resolve a guest virtual address to a physical one, honouring the current
 * mode. With paging off the two are the same.
 */
static bool translate(vcpu_context_t *ctx, const struct kvm_sregs *s,
                      uint64_t va, uint64_t *pa_out, char *why, size_t why_len)
{
    why[0] = '\0';

    if (!(s->cr0 & CR0_PG)) {
        if (va >= ctx->mem_size) {
            snprintf(why, why_len, "0x%llx is beyond the %zu MB of guest memory",
                     (unsigned long long)va, ctx->mem_size / (1024 * 1024));
            return false;
        }
        *pa_out = va;
        return true;
    }

    if (s->cr4 & CR4_PAE) {
        /* PAE and long mode use a different walk; not decoded yet. */
        snprintf(why, why_len, "PAE paging is enabled; this walk is not implemented");
        return false;
    }

    uint32_t pa32;
    if (!walk32(ctx, s, (uint32_t)va, &pa32, why, why_len)) {
        return false;
    }
    *pa_out = pa32;
    return true;
}

/* How many IDT entries are present, and whether the given vector has one. */
static void inspect_idt(vcpu_context_t *ctx, const struct kvm_sregs *s,
                        unsigned vector, int *valid_out, int *vector_present_out,
                        uint32_t *handler_out)
{
    *valid_out = 0;
    *vector_present_out = -1;      /* unknown */
    *handler_out = 0;

    unsigned entries = (s->idt.limit + 1u) / sizeof(idt_entry_t);
    if (entries > 256) {
        entries = 256;
    }

    for (unsigned i = 0; i < entries; i++) {
        uint64_t addr = s->idt.base + (uint64_t)i * sizeof(idt_entry_t);
        if (addr + sizeof(idt_entry_t) > ctx->mem_size) {
            break;
        }
        const idt_entry_t *e =
            (const idt_entry_t *)((const char *)ctx->guest_mem + addr);

        bool present = (e->flags & 0x80) != 0;
        if (present) {
            (*valid_out)++;
        }
        if (i == vector) {
            *vector_present_out = present ? 1 : 0;
            *handler_out = (uint32_t)e->offset_low |
                           ((uint32_t)e->offset_high << 16);
        }
    }
}

/*
 * Work out what the instruction about to execute would have raised.
 *
 * KVM does not report the exception behind a triple fault, but for the
 * handful of instructions that commonly cause one the answer is readable
 * straight off the opcode -- and naming it is the difference between a
 * register dump and a diagnosis.
 *
 * Returns the vector, or -1 if the instruction is not one of them.
 */
static int likely_exception(const uint8_t *b, const struct kvm_regs *r,
                            const char **detail)
{
    *detail = NULL;

    if (b[0] == 0x0F && b[1] == 0x0B) {
        *detail = "ud2 raises it deliberately";
        return 6;                               /* #UD */
    }
    if (b[0] == 0xCC) {
        *detail = "int3";
        return 3;                               /* #BP */
    }

    /* F7 /6 = div r/m32, F7 /7 = idiv r/m32. The divisor is the operand, and
     * the only common reason these fault is that it is zero. */
    if (b[0] == 0xF7 || b[0] == 0xF6) {
        unsigned reg = (b[1] >> 3) & 7;
        unsigned mod = (b[1] >> 6) & 3;
        unsigned rm  = b[1] & 7;
        if (reg == 6 || reg == 7) {
            if (mod == 3) {
                static const uint64_t *regs_by_rm[8];
                uint64_t v = 0;
                switch (rm) {
                case 0: v = r->rax; break;
                case 1: v = r->rcx; break;
                case 2: v = r->rdx; break;
                case 3: v = r->rbx; break;
                case 4: v = r->rsp; break;
                case 5: v = r->rbp; break;
                case 6: v = r->rsi; break;
                default: v = r->rdi; break;
                }
                (void)regs_by_rm;
                if ((uint32_t)v == 0) {
                    *detail = "the divisor register is zero";
                    return 0;                   /* #DE */
                }
                return -1;                      /* divisor non-zero */
            }
            *detail = "a divide instruction";
            return 0;
        }
    }

    return -1;
}

/* Print the bytes at the faulting instruction, if we can reach them. */
static void show_instruction(vcpu_context_t *ctx, const struct kvm_sregs *s,
                             uint64_t rip)
{
    uint64_t linear = s->cs.base + rip;
    uint64_t pa;
    char why[160];

    if (!translate(ctx, s, linear, &pa, why, sizeof(why))) {
        say(ctx, "  Instruction bytes: unavailable - %s\n", why);
        return;
    }
    if (pa + 8 > ctx->mem_size) {
        say(ctx, "  Instruction bytes: unavailable - past end of memory\n");
        return;
    }

    const unsigned char *p = (const unsigned char *)ctx->guest_mem + pa;
    say(ctx, "  Instruction bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

void explain_shutdown(vcpu_context_t *ctx)
{
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    struct kvm_vcpu_events events;
    char why[160];

    if (ioctl(ctx->vcpu_fd, KVM_GET_REGS, &regs) < 0 ||
        ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        say(ctx, "Guest died, but its registers could not be read.\n");
        return;
    }
    bool have_events = (ioctl(ctx->vcpu_fd, KVM_GET_VCPU_EVENTS, &events) == 0);

    /*
     * A triple fault resets the CPU before KVM reports it, so the live
     * registers describe the reset vector, not the fault. If --explain was
     * given we single-stepped and kept the state from just before, which is
     * the state that actually explains anything.
     */
    bool was_reset = (regs.rip == 0xfff0 && sregs.cs.selector == 0xf000);
    bool from_trace = false;

    if (was_reset && ctx->trace.valid) {
        regs = ctx->trace.regs;
        sregs = ctx->trace.sregs;
        was_reset = false;
        from_trace = true;
    }

    say(ctx, "\n");
    say(ctx, "Guest triple-faulted (the CPU gave up and reset).\n");

    if (from_trace) {
        say(ctx, "  Last instruction before the fault, at step %lu:\n",
            ctx->trace.steps);
        say(ctx, "    CS:EIP = 0x%04x:0x%08llx, in %s\n",
            sregs.cs.selector, (unsigned long long)regs.rip, mode_name(&sregs));
        say(ctx, "    bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
            ctx->trace.bytes[0], ctx->trace.bytes[1], ctx->trace.bytes[2],
            ctx->trace.bytes[3], ctx->trace.bytes[4], ctx->trace.bytes[5],
            ctx->trace.bytes[6], ctx->trace.bytes[7]);
    } else if (was_reset) {
        say(ctx, "  The CPU reset before KVM reported this, so the state below is\n");
        say(ctx, "  the reset vector, not the fault. Re-run with --explain to\n");
        say(ctx, "  single-step and capture the state that caused it.\n");
    } else {
        say(ctx, "  At CS:EIP = 0x%04x:0x%08llx, in %s\n",
            sregs.cs.selector, (unsigned long long)regs.rip, mode_name(&sregs));
    }

    /* --- What the CPU was trying to deliver ---------------------------- */

    unsigned vector = 0;
    bool know_vector = false;

    if (have_events && events.exception.injected) {
        vector = events.exception.nr;
        know_vector = true;
        /* One say() per line: the macro prefixes each call with the vCPU
         * name, so a line assembled from several calls comes out fragmented. */
        if (events.exception.has_error_code) {
            say(ctx, "  Pending exception: %s (vector %u), error code 0x%x\n",
                exception_name(vector), vector, events.exception.error_code);
        } else {
            say(ctx, "  Pending exception: %s (vector %u)\n",
                exception_name(vector), vector);
        }
    } else if (from_trace) {
        /* KVM reports no exception behind a triple fault, but the opcode
         * often says exactly what it would have been. */
        const char *detail = NULL;
        int guess = likely_exception(ctx->trace.bytes, &regs, &detail);
        if (guess >= 0) {
            vector = (unsigned)guess;
            know_vector = true;
            if (detail) {
                say(ctx, "  That instruction raises %s - %s.\n",
                    exception_name(vector), detail);
            } else {
                say(ctx, "  That instruction raises %s.\n", exception_name(vector));
            }
        }
    }

    /* --- The most common causes, checked directly ---------------------- */

    if (was_reset) {
        /* Nothing below would describe the guest, only the reset vector. */
        say(ctx, "\n");
        return;
    }

    say(ctx, "\n");

    /* A triple fault is a fault while handling a fault, so the state of the
     * IDT is nearly always the interesting question. */
    if (sregs.idt.limit == 0) {
        say(ctx, "  No IDT: IDTR limit is 0. The guest has not installed an\n");
        say(ctx, "  interrupt descriptor table, so any exception is fatal.\n");
    } else {
        int valid = 0, vector_present = -1;
        uint32_t handler = 0;
        inspect_idt(ctx, &sregs, know_vector ? vector : 256,
                    &valid, &vector_present, &handler);

        say(ctx, "  IDT at 0x%llx, limit 0x%x: %d of %u entries present.\n",
            (unsigned long long)sregs.idt.base, sregs.idt.limit,
            valid, (sregs.idt.limit + 1u) / (unsigned)sizeof(idt_entry_t));

        if (valid == 0) {
            say(ctx, "  Not one entry is present, so no exception of any kind can\n");
            say(ctx, "  be dispatched. The guest needs to build an IDT and lidt it\n");
            say(ctx, "  before it can survive a fault.\n");
        } else if (know_vector && vector_present == 0) {
            say(ctx, "  Entry %u (%s) has P=0 - no handler is installed for the\n",
                vector, exception_name(vector));
            say(ctx, "  exception that was raised. That is what killed it.\n");
        } else if (know_vector && vector_present == 1) {
            say(ctx, "  Entry %u (%s) points at 0x%08x, so the first fault was\n",
                vector, exception_name(vector), handler);
            say(ctx, "  dispatchable; something inside the handler faulted again.\n");
        }
    }

    /* Page faults name the address they could not translate. */
    if (know_vector && vector == 14 && sregs.cr2 != 0) {
        uint64_t pa;
        say(ctx, "\n");
        if (translate(ctx, &sregs, sregs.cr2, &pa, why, sizeof(why))) {
            say(ctx, "  CR2 = 0x%llx maps to physical 0x%llx, so the fault was a\n",
                (unsigned long long)sregs.cr2, (unsigned long long)pa);
            say(ctx, "  permission violation rather than a missing mapping.\n");
        } else {
            say(ctx, "  CR2 = 0x%llx is not mapped: %s\n",
                (unsigned long long)sregs.cr2, why);
        }
    }

    /* A stack that cannot be written turns any fault into a triple fault,
     * because the CPU cannot push the exception frame. */
    if (sregs.cr0 & CR0_PE) {
        uint64_t sp_linear = sregs.ss.base + regs.rsp;
        uint64_t pa;
        say(ctx, "\n");
        if (regs.rsp == 0) {
            say(ctx, "  ESP is 0. The first push wraps to the top of the address\n");
            say(ctx, "  space, which is normally unmapped - a guest must set up a\n");
            say(ctx, "  stack before calling anything.\n");
        } else if (!translate(ctx, &sregs, sp_linear, &pa, why, sizeof(why))) {
            say(ctx, "  Stack pointer 0x%llx is not usable: %s\n",
                (unsigned long long)sp_linear, why);
            say(ctx, "  The CPU cannot push an exception frame, so any fault\n");
            say(ctx, "  becomes a triple fault.\n");
        } else {
            say(ctx, "  Stack pointer 0x%llx is mapped, so pushing a fault frame\n",
                (unsigned long long)sp_linear);
            say(ctx, "  would have worked.\n");
        }
    }

    /* --- Raw state, for whatever the analysis missed -------------------- */

    say(ctx, "\n");
    say(ctx, "  CR0=0x%llx CR2=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx\n",
        (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr2,
        (unsigned long long)sregs.cr3, (unsigned long long)sregs.cr4,
        (unsigned long long)sregs.efer);
    say(ctx, "  EFLAGS=0x%llx (IF=%d)  ESP=0x%llx  EBP=0x%llx\n",
        (unsigned long long)regs.rflags,
        (regs.rflags & RFLAGS_IF) ? 1 : 0,
        (unsigned long long)regs.rsp, (unsigned long long)regs.rbp);
    say(ctx, "  EAX=0x%llx EBX=0x%llx ECX=0x%llx EDX=0x%llx\n",
        (unsigned long long)regs.rax, (unsigned long long)regs.rbx,
        (unsigned long long)regs.rcx, (unsigned long long)regs.rdx);
    say(ctx, "  CS=0x%x DS=0x%x SS=0x%x  GDT at 0x%llx limit 0x%x\n",
        sregs.cs.selector, sregs.ds.selector, sregs.ss.selector,
        (unsigned long long)sregs.gdt.base, sregs.gdt.limit);

    show_instruction(ctx, &sregs, regs.rip);
    say(ctx, "\n");
}

void explain_failed_entry(vcpu_context_t *ctx, uint64_t reason)
{
    struct kvm_sregs sregs;

    say(ctx, "\n");
    say(ctx, "KVM refused to enter the guest (hardware entry failure 0x%llx).\n",
        (unsigned long long)reason);
    say(ctx, "  This is usually invalid register state rather than a guest bug:\n");
    say(ctx, "  a segment descriptor inconsistent with the current mode, or\n");
    say(ctx, "  control registers in a combination the CPU does not allow.\n");

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) == 0) {
        say(ctx, "  CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx\n",
            (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
            (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer);
        say(ctx, "  CS base=0x%llx limit=0x%x type=0x%x db=%d l=%d g=%d\n",
            (unsigned long long)sregs.cs.base, sregs.cs.limit,
            sregs.cs.type, sregs.cs.db, sregs.cs.l, sregs.cs.g);
    }
    say(ctx, "\n");
}
