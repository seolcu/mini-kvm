# AGENTS.md

This file provides guidance to coding agents (Claude Code, Codex, and similar) when working with code in this repository. It is the single source of agent guidance; `CLAUDE.md` just points here.

## What this repo is

Mini-KVM is an educational x86 hypervisor (~4,000 lines of C) built directly on the Linux KVM ioctl API, developed as a university self-directed project (2025-2 Ajou SoftCon). Only `kvm-vmm-x86/` is the deliverable; everything else is documentation, research notes, or isolated experiments.

- `kvm-vmm-x86/` — the VMM (`src/`), real-mode guest programs (`guest/`), the protected-mode mini OS (`os-1k/`), Linux-guest initramfs sources (`initramfs/`).
- `docs/`, `research/`, `meetings/`, `연구노트.md`, `결과보고서.md` — reports and weekly logs, mostly Korean. Update `docs/` when user-visible behavior changes; append to `research/` rather than rewriting.
- `experimental/` — Rust RISC-V H-extension hypervisor and a Rust x86_64 hobby OS. Not built by the main Makefile; keep isolated.
- `README.md` and `docs/` predate some code changes; where they disagree with this file (`.bin` guest names, 4MB PSE paging), this file is correct.

## Build and run

Everything runs from `kvm-vmm-x86/`. Bare `make` prints help (`.DEFAULT_GOAL := help`).

```bash
cd kvm-vmm-x86
make all          # vmm + guests + 1k-os;  or make vmm | guests | 1k-os | clean
```

Flags are fixed at `gcc -Wall -Wextra -O2 -std=gnu11 -pthread`; match them for experiments.

**Guest binaries have no file extension.** `make` produces `guest/hello`, `os-1k/kernel` — README examples using `.bin` are stale.

```bash
./kvm-vmm guest/hello                       # real mode, single vCPU
./kvm-vmm guest/counter guest/hello guest/multiplication   # 1 arg per vCPU, max 4
./kvm-vmm --paging os-1k/kernel             # 1K OS interactive shell
./kvm-vmm --long-mode guest/hello_64        # 64-bit (implies --paging)
./kvm-vmm --linux bzImage --cmdline "console=ttyS0"   # experimental, incomplete
```

Debug flags: `--verbose`/`-v`, `--debug 0|1|2|3`, `--dump-regs`, `--dump-mem FILE`, `--entry ADDR`, `--load OFFSET`.

## Testing

There is no test suite. Verification is running guests and reading stdout. `/dev/kvm` is required — guests depend on Mini-KVM hypercalls, there is no QEMU/TCG fallback; if KVM is unavailable, say so explicitly instead of substituting another runner.

Minimum before submitting a change:

```bash
make all
./kvm-vmm guest/minimal                                  # smallest real-mode path
printf '1\n0\n'              | ./kvm-vmm --paging os-1k/kernel   # program 1, then exit
printf '3\nHello\nquit\n0\n' | ./kvm-vmm --paging os-1k/kernel   # echo program
printf '6\n10+5\nquit\n0\n'  | ./kvm-vmm --paging os-1k/kernel   # calculator
```

Piped stdin drives the 1K OS menu and skips terminal raw mode, which makes these deterministic. Capture console output when touching paging, CPUID, or MSR paths.

## Architecture invariants

One VM (`vm_fd`), up to `MAX_VCPUS = 4` vCPUs, each a pthread with its own `vcpu_context_t`, its own mmap'd memory, and its own KVM memory slot. **Each vCPU is mapped at GPA `vcpu_id * mem_size`** — guests are independent programs, not SMP. Sizes: 256KB real mode (fits the 64K-segment model, 4 vCPUs inside 1MB), 4MB with `--paging`, 256MB for Linux. Changing the layout means rebuilding guests and reworking the 1K OS loader.

**Hypercall ABI — port `0x500`, `OUT` with the number in AL:** `HC_EXIT 0x00`, `HC_PUTCHAR 0x01` (char in BL), `HC_GETCHAR 0x02` (result returned in AL via a follow-up `IN`, using `pending_getchar`/`getchar_result` on the context). These numbers are duplicated as `SYS_*` in `os-1k/common.h` and hand-encoded in `guest/*.S`; every guest breaks if they change. UART COM1 (`0x3f8`–`0x3ff`) is separately emulated and forwarded to host stdout.

**IRQCHIP is created only in paging/Linux mode.** Real-mode guests deliberately run without an interrupt controller — an unwanted IRQ0 makes `HLT`-terminated real-mode guests hang. Terminal raw mode is likewise enabled only in paging mode.

**Protected mode is entered by the VMM, not the guest.** Before the first `KVM_RUN` the VMM builds the GDT at guest `0x500` (5 descriptors, `setup_gdt()`), the IDT (`setup_idt()`), and page tables (`setup_page_tables()`: page directory at GPA `0x00100000`, 4KB pages, **PSE deliberately disabled for AMD Zen 5 compatibility**), then sets CR0/CR3 and jumps to the entry point. Consequently `os-1k/boot.S` must **not** reload segment registers — doing so triple-faults. Paging defaults: entry `0x80001000`, load offset `0x1000` (kernel.ld links at `0x80001000`).

**1K OS build is a two-stage embed:** user programs (`shell.c`, `user.c`, `common.c`) link with `user.ld` at `0x01000000` into `shell.bin`, which `objcopy -I binary` wraps into an object linked into the kernel — the final `kernel` is one flat binary containing its own userland. User code issues syscalls as direct `OUT` to `0x500` from ring 3 (IOPL=3), so there is no in-kernel syscall gate; the VMM is the syscall handler.

**1K OS 32-bit flags are load-bearing:** `-m32 -march=i686 -fno-pie -no-pie --build-id=none -z norelro`. Without `-march=i686` (Arch GCC defaults to i386) the kernel triple-faults immediately — see `docs/investigations/arch_vs_fedora_build_issue.md`. A separate unresolved Zen 5 issue causes SHUTDOWN when returning to the 1K OS menu (`docs/investigations/INVESTIGATION_1K_OS_SHUTDOWN.md`).

Real-mode guests are assembled `as --32` and linked `ld -m elf_i386 -T guest.ld --oformat=binary` into flat binaries at address 0; each new guest needs an explicit rule in `guest/Makefile` (the implicit `%` rule conflicts with `.S`).

## Conventions

- C: 4-space indent, same-line braces, `lower_snake_case` functions/vars, ALL_CAPS macros, `static` for file-local helpers, fixed-width types for guest-memory math, paging/segment structs zero-initialized before use.
- Log through `src/debug.*` (`DEBUG_NONE/BASIC/DETAILED/ALL`) and gate output behind existing verbosity flags — never add unconditional prints in VM-exit hot paths. `HC_GETCHAR` `IN` traces are suppressed even under `--verbose` on purpose.
- vCPU output: single vCPU prints a plain `[name]` prefix; multi-vCPU uses ANSI colors (vCPU 0 is cyan, not red, to avoid reading as an error). Output is character-by-character, unbuffered, so interleaving is visible in demos.
- Commits follow Conventional Commits with area scopes: `feat(vmm):`, `fix(guest):`, `feat(linux-boot):`, `docs:`.
