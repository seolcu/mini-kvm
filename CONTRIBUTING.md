# Contributing to Mini-KVM

Thanks for looking. Mini-KVM is a small project with a specific goal — a KVM
runner and diagnostic tool for people writing their own x86 kernels — so the
most useful contributions are usually the ones that come from actually trying to
use it.

## The most valuable thing you can do

**Try to boot your own kernel and tell us what broke.** Mini-KVM cannot yet load
ELF or Multiboot images, has no VGA text buffer, and emulates no interrupt
controller. Those gaps are known (see the roadmap in the README), but a concrete
report — *"here is my kernel, here is what it needs, here is where it died"* —
is worth more than a guess about what people want. Attach the kernel if you can.

## Before you open a pull request

```bash
make clean && make all      # must build with zero warnings
make test                   # 14 cases, must stay green
```

`make test` runs `tools/smoke.sh`, which boots every supported guest path and
diffs the output against stored baselines in `tools/baseline/`.

If your change **intentionally** alters guest-visible output, re-baseline it
deliberately and say so in the commit message:

```bash
./tools/smoke.sh --update
git diff tools/baseline/     # review this — do not update blindly
```

An unexplained baseline change in a PR will be treated as a regression.

`/dev/kvm` is required. There is no QEMU/TCG fallback, because the guests depend
on Mini-KVM's own hypercalls. If KVM is unavailable the script exits 77 and says
so rather than pretending to have verified anything — please don't work around
that.

## Ground rules for code

`AGENTS.md` is the real specification: it documents the architecture invariants,
the hypercall ABI, and the several settings that are load-bearing in
non-obvious ways (PSE stays off for AMD Zen 5; `os-1k/boot.S` must not reload
segment registers; the 1K OS 32-bit compiler flags are exact). Read it first.

Beyond that:

- **One module per concern.** `src/main.c` only sequences phases. If you are
  adding code to `main.c`, it probably belongs somewhere else.
- **No new cross-module globals.** Pass state explicitly.
- **No Linux-boot special cases outside `src/linux/`.** That path is
  quarantined on purpose.
- **Never print unconditionally.** A plain run shows only the guest's output.
  Gate diagnostics behind `verbose_enabled()` — never in a VM-exit hot path.
- Style: 4-space indent, `lower_snake_case`, `ALL_CAPS` macros, `static` for
  file-local helpers, fixed-width types for anything touching guest memory.
- Comments should explain *why*, especially where a value is load-bearing.
  Several settings in this codebase look arbitrary and are not.

## Commits

Conventional Commits with an area scope:

```
feat(vmm): add Multiboot 2 loader
fix(guest): correct hypercall clobber list in hctest
docs: document the port 0x500 ABI
```

## Reporting a bug

Please include your host CPU and kernel version (`uname -a`), the exact command,
and the output of the same run with `--verbose`. For a guest that faults,
`--dump-regs` output is usually what identifies the problem.
