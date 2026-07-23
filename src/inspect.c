/*
 * inspect.c - decode a guest's descriptor tables and page tables
 */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>

#include "inspect.h"
#include "explain.h"
#include "console.h"
#include "protected_mode.h"
#include "loader.h"

#define say(ctx, ...) console_vcpu_printf((ctx)->vcpu_id, (ctx)->name, __VA_ARGS__)

#define CR0_PE (1u << 0)
#define CR0_PG (1u << 31)

/* --- GDT ---------------------------------------------------------------- */

/*
 * Describe a segment descriptor's access byte. The four cases that matter are
 * whether it is a system descriptor, code or data, which way it grows, and
 * what ring may use it -- a mismatch in any of them is a triple fault waiting
 * to happen.
 */
static void describe_access(uint8_t access, char *out, size_t len)
{
    bool present = (access & 0x80) != 0;
    unsigned dpl = (access >> 5) & 3;
    bool system = (access & 0x10) == 0;
    bool executable = (access & 0x08) != 0;
    bool direction = (access & 0x04) != 0;
    bool rw = (access & 0x02) != 0;
    bool accessed = (access & 0x01) != 0;

    if (system) {
        snprintf(out, len, "%s system type 0x%x, ring %u",
                 present ? "present" : "NOT PRESENT", access & 0x0F, dpl);
        return;
    }

    snprintf(out, len, "%s %s, %s, ring %u%s%s",
             present ? "present" : "NOT PRESENT",
             executable ? "code" : "data",
             executable ? (rw ? "readable" : "execute-only")
                        : (rw ? "writable" : "read-only"),
             dpl,
             direction ? (executable ? ", conforming" : ", grows down") : "",
             accessed ? ", accessed" : "");
}

static void dump_gdt(vcpu_context_t *ctx, const struct kvm_sregs *s)
{
    unsigned count = (s->gdt.limit + 1u) / sizeof(gdt_entry_t);
    char why[160];

    say(ctx, "GDT at 0x%llx, limit 0x%x (%u entries)\n",
        (unsigned long long)s->gdt.base, s->gdt.limit, count);

    if (count == 0) {
        say(ctx, "  empty\n");
        return;
    }
    if (count > 32) {
        count = 32;         /* a kernel with more than this is not hand-built */
    }

    for (unsigned i = 0; i < count; i++) {
        uint64_t va = s->gdt.base + (uint64_t)i * sizeof(gdt_entry_t);
        uint64_t pa;
        if (!guest_translate(ctx, s, va, &pa, why, sizeof(why)) ||
            pa + sizeof(gdt_entry_t) > ctx->mem_size) {
            say(ctx, "  [%u] unreadable: %s\n", i, why[0] ? why : "outside guest memory");
            continue;
        }

        const gdt_entry_t *e = (const gdt_entry_t *)((const char *)ctx->guest_mem + pa);
        uint32_t base = (uint32_t)e->base_low | ((uint32_t)e->base_mid << 16) |
                        ((uint32_t)e->base_high << 24);
        uint32_t limit = (uint32_t)e->limit_low |
                         ((uint32_t)(e->limit_granular & 0x0F) << 16);
        bool granular = (e->limit_granular & 0x80) != 0;
        bool op32 = (e->limit_granular & 0x40) != 0;
        bool longmode = (e->limit_granular & 0x20) != 0;

        if (e->access == 0 && base == 0 && limit == 0) {
            say(ctx, "  [%u] selector 0x%02x  null\n", i, i * 8);
            continue;
        }

        char desc[160];
        describe_access(e->access, desc, sizeof(desc));

        say(ctx, "  [%u] selector 0x%02x  base 0x%08x  limit 0x%05x%s  %s%s\n",
            i, i * 8, base, limit,
            granular ? " (x4K)" : "",
            longmode ? "64-bit " : (op32 ? "32-bit " : "16-bit "),
            desc);
    }
}

/* --- IDT ---------------------------------------------------------------- */

static const char *vector_name(unsigned v)
{
    switch (v) {
    case 0:  return "#DE";
    case 1:  return "#DB";
    case 3:  return "#BP";
    case 6:  return "#UD";
    case 8:  return "#DF";
    case 13: return "#GP";
    case 14: return "#PF";
    case 32: return "IRQ0 timer";
    case 33: return "IRQ1 keyboard";
    case 36: return "IRQ4 serial";
    default: return NULL;
    }
}

static void dump_idt(vcpu_context_t *ctx, const struct kvm_sregs *s)
{
    unsigned count = (s->idt.limit + 1u) / sizeof(idt_entry_t);
    char why[160];

    say(ctx, "\nIDT at 0x%llx, limit 0x%x (%u entries)\n",
        (unsigned long long)s->idt.base, s->idt.limit, count);

    if (count == 0) {
        say(ctx, "  empty - no exception can be dispatched\n");
        return;
    }
    if (count > 256) {
        count = 256;
    }

    unsigned present = 0;
    for (unsigned i = 0; i < count; i++) {
        uint64_t va = s->idt.base + (uint64_t)i * sizeof(idt_entry_t);
        uint64_t pa;
        if (!guest_translate(ctx, s, va, &pa, why, sizeof(why)) ||
            pa + sizeof(idt_entry_t) > ctx->mem_size) {
            say(ctx, "  table becomes unreadable at entry %u: %s\n",
                i, why[0] ? why : "outside guest memory");
            return;
        }

        const idt_entry_t *e = (const idt_entry_t *)((const char *)ctx->guest_mem + pa);
        if (!(e->flags & 0x80)) {
            continue;           /* not present: the common case, not worth a line */
        }
        present++;

        uint32_t offset = (uint32_t)e->offset_low | ((uint32_t)e->offset_high << 16);
        unsigned type = e->flags & 0x0F;
        unsigned dpl = (e->flags >> 5) & 3;
        const char *kind = (type == 0x0E) ? "32-bit interrupt gate"
                         : (type == 0x0F) ? "32-bit trap gate"
                         : (type == 0x06) ? "16-bit interrupt gate"
                         : (type == 0x05) ? "task gate"
                                          : "unknown gate";
        const char *name = vector_name(i);

        say(ctx, "  [%u]%s%s selector 0x%02x offset 0x%08x  %s, ring %u\n",
            i, name ? " " : "", name ? name : "",
            e->selector, offset, kind, dpl);
    }

    say(ctx, "  %u of %u entries present\n", present, count);
}

/* --- Page tables -------------------------------------------------------- */

/*
 * Summarise the 32-bit page directory by collapsing runs of consecutive
 * entries that map consecutive physical pages with the same flags. Listing
 * 1024 entries individually is unreadable; a kernel's mapping is a handful of
 * ranges.
 */
static void dump_paging32(vcpu_context_t *ctx, const struct kvm_sregs *s)
{
    uint32_t pd_base = (uint32_t)s->cr3 & 0xFFFFF000u;

    say(ctx, "\nPage directory at 0x%08x (32-bit, %s)\n", pd_base,
        (s->cr4 & (1u << 4)) ? "PSE available" : "4KB pages");

    if ((uint64_t)pd_base + 4096 > ctx->mem_size) {
        say(ctx, "  outside guest memory\n");
        return;
    }
    const uint32_t *pd = (const uint32_t *)((const char *)ctx->guest_mem + pd_base);

    unsigned shown = 0;
    for (unsigned i = 0; i < 1024 && shown < 24; i++) {
        if (!(pd[i] & 1)) {
            continue;
        }
        uint32_t va = (uint32_t)i << 22;

        if (pd[i] & 0x80) {     /* 4MB page */
            say(ctx, "  0x%08x-0x%08x -> 0x%08x  4MB page%s%s\n",
                va, va + 0x3FFFFF, pd[i] & 0xFFC00000u,
                (pd[i] & 2) ? ", writable" : ", read-only",
                (pd[i] & 4) ? ", user" : "");
            shown++;
            continue;
        }

        /* Collapse the page table into contiguous runs. */
        uint32_t pt_base = pd[i] & 0xFFFFF000u;
        if ((uint64_t)pt_base + 4096 > ctx->mem_size) {
            say(ctx, "  PDE[%u] -> 0x%08x (outside guest memory)\n", i, pt_base);
            shown++;
            continue;
        }
        const uint32_t *pt = (const uint32_t *)((const char *)ctx->guest_mem + pt_base);

        int run_start = -1;
        uint32_t run_phys = 0;
        for (unsigned j = 0; j <= 1024; j++) {
            bool present = (j < 1024) && (pt[j] & 1);
            bool contiguous = present && run_start >= 0 &&
                              (pt[j] & 0xFFFFF000u) ==
                              run_phys + (uint32_t)(j - (unsigned)run_start) * 0x1000u;

            if (present && run_start < 0) {
                run_start = (int)j;
                run_phys = pt[j] & 0xFFFFF000u;
            } else if (run_start >= 0 && (!present || !contiguous)) {
                uint32_t from = va | ((uint32_t)run_start << 12);
                uint32_t to = va | ((j - 1) << 12) | 0xFFF;
                if (shown < 24) {
                    say(ctx, "  0x%08x-0x%08x -> 0x%08x  %u pages\n",
                        from, to, run_phys, j - (unsigned)run_start);
                    shown++;
                }
                run_start = present ? (int)j : -1;
                run_phys = present ? (pt[j] & 0xFFFFF000u) : 0;
            }
        }
    }

    if (shown >= 24) {
        say(ctx, "  ... (truncated)\n");
    } else if (shown == 0) {
        say(ctx, "  nothing mapped\n");
    }
}

void inspect_dump(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        say(ctx, "inspect: cannot read guest state\n");
        return;
    }

    say(ctx, "\n=== Guest state (%s) ===\n", guest_mode_name(&sregs));
    say(ctx, "CR0=0x%llx CR3=0x%llx CR4=0x%llx EFER=0x%llx\n",
        (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr3,
        (unsigned long long)sregs.cr4, (unsigned long long)sregs.efer);
    say(ctx, "\n");

    dump_gdt(ctx, &sregs);
    dump_idt(ctx, &sregs);

    if (sregs.cr0 & CR0_PG) {
        if (sregs.cr4 & (1u << 5)) {
            /* PAE and long mode have deep tables; the ranges that matter are
             * better answered by asking about a specific address. */
            say(ctx, "\nPaging: PAE/long mode, CR3=0x%llx\n",
                (unsigned long long)sregs.cr3);
        } else {
            dump_paging32(ctx, &sregs);
        }
    } else {
        say(ctx, "\nPaging is off; virtual addresses are physical.\n");
    }
    say(ctx, "\n");
}

/* --- Mode transitions --------------------------------------------------- */

void inspect_note_mode(vcpu_context_t *ctx)
{
    struct kvm_sregs sregs;

    if (ioctl(ctx->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
        return;
    }

    const char *now = guest_mode_name(&sregs);
    if (ctx->last_mode == NULL) {
        ctx->last_mode = now;
        say(ctx, "mode: starting in %s\n", now);
        return;
    }
    if (ctx->last_mode == now) {
        return;                 /* the names are static strings */
    }

    say(ctx, "mode: %s -> %s  (CR0=0x%llx CR4=0x%llx EFER=0x%llx)\n",
        ctx->last_mode, now,
        (unsigned long long)sregs.cr0, (unsigned long long)sregs.cr4,
        (unsigned long long)sregs.efer);
    ctx->last_mode = now;
}

/* --- Preflight ---------------------------------------------------------- */

int inspect_preflight(vcpu_context_t *ctx)
{
    int warnings = 0;

    if (ctx->image.format != GUEST_ELF &&
        ctx->image.format != GUEST_MULTIBOOT &&
        ctx->image.format != GUEST_MULTIBOOT2) {
        return 0;               /* a flat binary carries nothing to check */
    }

    uint32_t entry = ctx->image.entry;

    /* The entry point must be inside something that was actually loaded.
     * A kernel linked for one address and entered at another produces a
     * fault with no output at all, which is hard to tell from a hang. */
    if (entry < ctx->image.load_low || entry >= ctx->image.load_high) {
        say(ctx, "warning: entry point 0x%08x is outside the loaded image "
                 "(0x%08x-0x%08x).\n",
            entry, ctx->image.load_low, ctx->image.load_high);
        say(ctx, "         The kernel will start executing whatever happens to "
                 "be there.\n");
        warnings++;
    }

    /* The VMM's own structures live in low memory. An image that lands on top
     * of them corrupts the GDT it is about to run on. */
    if (ctx->image.load_low < 0x10000) {
        say(ctx, "warning: image loads at 0x%08x, over the GDT and IDT the VMM "
                 "placed at 0x%x.\n", ctx->image.load_low, GDT_ADDR);
        warnings++;
    }

    if (ctx->image.load_high > ctx->mem_size) {
        say(ctx, "warning: image extends to 0x%08x, past the %zu MB of guest "
                 "memory.\n", ctx->image.load_high, ctx->mem_size / (1024 * 1024));
        warnings++;
    }

    return warnings;
}
