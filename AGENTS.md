# AGENTS.md

This file provides guidance to coding agents (Claude Code, Codex, and similar) when working with code in this repository. It is the single source of agent guidance; `CLAUDE.md` just points here.

## What this repo is

Mini-KVM is an educational x86 hypervisor (~4,000 lines of C) built directly on the Linux KVM ioctl API, developed as a university self-directed project (2025-2 Ajou SoftCon). Only `kvm-vmm-x86/` is the deliverable; everything else is documentation, research notes, or isolated experiments.

- `kvm-vmm-x86/` — the VMM (`src/`), real-mode guest programs (`guest/`), the protected-mode mini OS (`os-1k/`), Linux-guest initramfs sources (`initramfs/`), verification harness (`tools/`).
- `docs/`, `research/`, `meetings/`, `연구노트.md`, `결과보고서.md` — reports and weekly logs, mostly Korean. Update `docs/` when user-visible behavior changes; append to `research/` rather than rewriting.
- `experimental/` — Rust RISC-V H-extension hypervisor and a Rust x86_64 hobby OS. Not built by the main Makefile; keep isolated.
- `README.md` and `docs/` predate some code changes; where they disagree with this file (`.bin` guest names, 4MB PSE paging), this file is correct.

`src/` is one module per concern; `main.c` only sequences the phases:

| File | Owns |
|---|---|
| `main.c` | phase sequencing, nothing else |
| `cli.c` | argv → `vmm_config_t`; usage text |
| `vm.c` | `/dev/kvm`, the VM, TSS/IRQCHIP, guest memory slots |
| `vcpu.c` | vCPU setup, the `KVM_RUN` loop, VM-exit handling |
| `cpu_modes.c` | GDT/IDT/page tables and real/protected/long/flat32 mode entry |
| `hypercall.c` | the port `0x500` ABI |
| `devices.c` | 16550 UART, VGA CRTC, legacy PC port stubs |
| `ps2.c` | i8042 keyboard: characters → set 1 scancodes |
| `vga.c` | the 0xB8000 text buffer, rendered under `--vga` |
| `console.c` | terminal, keyboard ring, vCPU-tagged output |
| `debug.c` | register/memory dumps behind `--dump-regs` / `--dump-mem` |
| `loader.c` | format detection; ELF, Multiboot 1 and 2, modules |
| `explain.c` | `--explain`: why a guest triple-faulted |
| `inspect.c` | `--inspect`, `--trace-modes`, and pre-boot image checks |
| `linux/` | **quarantined** bzImage boot; reaches a shell with an initramfs |

Keep it that way: no `linux_guest` special cases outside `src/linux/`, and no new globals shared across modules.


## Build and run

Everything runs from `kvm-vmm-x86/`. Bare `make` prints help (`.DEFAULT_GOAL := help`).

```bash
cd kvm-vmm-x86
make all          # vmm + guests + 1k-os;  or make vmm | guests | 1k-os | clean
```

`examples/` holds stock Multiboot kernels that use no Mini-KVM facilities. `tools/third-party.sh` fetches and builds three kernels written by other people (soso, RetrOS-32, os-tutorial); `make test` runs them if present and skips them otherwise. Their sources are never modified — a build that fails on a modern toolchain is given compiler *flags* to restore the older default.

Flags are fixed at `gcc -Wall -Wextra -Wshadow -O2 -std=gnu11 -pthread`; match them for experiments. Objects land in `build/` with `-MMD -MP` header dependency tracking, so incremental builds are correct — do not reintroduce a hand-maintained header list.

**Guest binaries have no file extension.** `make` produces `guest/hello`, `os-1k/kernel` — README examples using `.bin` are stale.

```bash
./kvm-vmm guest/hello                       # real mode, single vCPU
./kvm-vmm guest/counter guest/hello guest/multiplication   # 1 arg per vCPU, max 4
./kvm-vmm --paging os-1k/kernel             # 1K OS interactive shell
./kvm-vmm --long-mode guest/hello_64        # 64-bit (implies --paging)
./kvm-vmm --linux bzImage --initrd initramfs.cpio \
          --cmdline "console=ttyS0 rdinit=/init"    # boots a stock kernel to a shell
```

Debug flags: `--verbose`/`-v`, `--debug 0|1|2|3`, `--dump-regs`, `--dump-mem FILE`, `--entry ADDR`, `--load OFFSET`. All options accept `--flag value` or `--flag=value` and may appear before or after the guest binaries.

**A plain run prints only the guest's output.** All VMM diagnostics are behind `--verbose`/`--debug`. Do not add unconditional prints; `verbose_enabled()` in `debug.h` is the single check (there is no separate `verbose` global any more).

## Testing

There are no unit tests. Verification is running guests and diffing stdout against a stored baseline:

```bash
make test          # == make all && ./tools/smoke.sh
```

`tools/smoke.sh` runs all 27 cases (real mode, multi-vCPU, long mode, VGA text mode, four Multiboot kernels, fault analysis, Linux to a shell, the full hypercall ABI, and six 1K OS programs) and diffs normalized output against `tools/baseline/`. It refuses to run against stale artifacts — each is compared against the sources that actually build it — so a failed build cannot masquerade as a pass. `./tools/smoke.sh --update` re-baselines — do that only when you have *intended* an output change, and say so in the commit.

`tools/ktest` is the same idea packaged for other projects: boot a kernel, assert on its output, exit non-zero if the assertions fail. `action.yml` wraps it as a GitHub Action. Both exit 77 when KVM is unavailable, which callers must treat as a skip rather than a pass.

`/dev/kvm` is required — guests depend on Mini-KVM hypercalls, there is no QEMU/TCG fallback. The script exits 77 and says so rather than pretending; if KVM is unavailable, report that instead of substituting another runner.

Piped stdin drives the 1K OS menu and skips terminal raw mode, which is what makes these deterministic. Input ends at EOF, and the guest exits cleanly rather than spinning. Note the calculator quits on `q` (not `quit`) and wants spaces around the operator: `printf '6\n10 + 5\nq\n0\n'`.

Capture console output when touching paging, CPUID, or MSR paths.

## Architecture invariants

One VM (`vm_fd`), up to `MAX_VCPUS = 4` vCPUs, each a pthread with its own `vcpu_context_t`, its own mmap'd memory, and its own KVM memory slot. **Each vCPU is mapped at GPA `vcpu_id * mem_size`** — guests are independent programs, not SMP. Sizes: 256KB real mode (fits the 64K-segment model, 4 vCPUs inside 1MB), 4MB with `--paging`, 256MB for Linux. Changing the layout means rebuilding guests and reworking the 1K OS loader.

**Hypercall ABI — port `0x500`, a single `OUT` with the number in AL; the result comes back in RAX.** The VMM writes the result with `KVM_SET_REGS` before resuming, so a hypercall behaves like a function call:

- `HC_EXIT 0x00` — terminate this vCPU
- `HC_PUTCHAR 0x01` — write BL; returns 1
- `HC_GETCHAR 0x02` — **blocking**; returns the character, or -1 at end of input

There is no follow-up `IN` and no `pending_getchar` state machine. `HC_GETCHAR` parks the vCPU thread on a condvar in `console.c` until a key arrives, so an idle prompt costs no host CPU — never reintroduce a guest-side polling loop. Unknown call numbers return -1 rather than tearing the VM down. The numbers are duplicated as `SYS_*` in `os-1k/common.h` and hand-encoded in the guest assembly; every guest breaks if they change. UART COM1 (`0x3f8`–`0x3ff`) is separately emulated and forwarded to host stdout.

**The guest image chooses its own entry mode.** `loader_probe()` runs before
guest memory is allocated, because the format determines the size (128MB for
ELF/Multiboot, which load at 1MB; 4MB with `--paging`; 256KB real mode). A
Multiboot image is entered in 32-bit protected mode with paging **off**
(`cpu_mode_enter_flat32`), because the specification says the kernel enables
paging itself — `--paging` and `--long-mode` do not apply to it. ELF and
Multiboot guests are restricted to one vCPU: memory is mapped at
`vcpu_id * mem_size`, so only vCPU 0 sees its kernel where the image was
linked for.

**Interrupt delivery has three non-obvious constraints.**
- With an in-kernel IRQCHIP, KVM stops reporting `KVM_EXIT_HLT` and blocks
  the vCPU inside `KVM_RUN` instead. After `cli; hlt` no interrupt can ever
  arrive, so the watchdog in `vcpu.c` nudges each vCPU with `SIGUSR1` and
  `vcpu_is_permanently_halted()` recognises the wedge. Without that the
  process cannot be terminated by anything short of `SIGKILL`.
- The PC's 8259 is **edge-triggered**. Holding a line high while the guest
  still has it masked delivers nothing later; `devices.c` drops and re-raises
  IRQ1, and `devices_tick()` keeps re-asserting until the queue drains. Piped
  input arrives before the guest has an IDT, so this is the normal path.
- Scancodes are generated **lazily**, one key at a time, from the console
  ring. Converting eagerly overflows any fixed queue on a scripted run.

**IRQCHIP is created only for kernel images and Linux mode.** Real-mode guests deliberately run without an interrupt controller — an unwanted IRQ0 makes `HLT`-terminated real-mode guests hang. Terminal raw mode is enabled only in paging/Linux mode. The stdin reader thread, by contrast, runs in *every* mode, because `HC_GETCHAR` is available to real-mode guests too.

**Protected mode is entered by the VMM, not the guest.** Before the first `KVM_RUN` the VMM builds the GDT at guest `0x500` (5 descriptors, `gdt_setup()`), the IDT (`idt_setup()`), and page tables (`page_tables_setup32()`: page directory at GPA `0x00100000`, 4KB pages, **PSE deliberately disabled for AMD Zen 5 compatibility**), then sets CR0/CR3 and jumps to the entry point — all in `src/cpu_modes.c`. Consequently `os-1k/boot.S` must **not** reload segment registers — doing so triple-faults. Paging defaults: entry `0x80001000`, load offset `0x1000` (kernel.ld links at `0x80001000`).

**1K OS build is a two-stage embed:** user programs (`shell.c`, `user.c`, `common.c`) link with `user.ld` at `0x01000000` into `shell.bin`, which `objcopy -I binary` wraps into an object linked into the kernel — the final `kernel` is one flat binary containing its own userland. User code issues syscalls as direct `OUT` to `0x500` from ring 3 (IOPL=3), so **there is no in-kernel syscall gate: the VMM is the syscall handler.** Do not add a trap frame or an `INT 0x80` path; an unreachable second dispatch path next to the real one was the most misleading thing in the original code. The kernel also has no filesystem — the old `fs_*`/tar code operated on a zeroed buffer and reported writes that never happened.

**1K OS 32-bit flags are load-bearing:** `-m32 -march=i686 -fno-pie -no-pie --build-id=none -z norelro`. Without `-march=i686` (Arch GCC defaults to i386) the kernel triple-faults immediately — see `docs/investigations/arch_vs_fedora_build_issue.md`. The Zen 5 SHUTDOWN issue in `docs/investigations/INVESTIGATION_1K_OS_SHUTDOWN.md` **no longer reproduces** (verified on Ryzen 5 9600X / 6.12 LTS); switching to 4KB pages with PSE off appears to have fixed it. Keep PSE off.

Real-mode guests are assembled `as --32` and linked `ld -m elf_i386 -T guest.ld --oformat=binary` into flat binaries at address 0. A new guest only needs its name added to `GUESTS` in `guest/Makefile`; a static pattern rule covers the build (a bare `%` implicit rule would also match the `.S` files, which is why the rule names its targets).

## Conventions

- C: 4-space indent, same-line braces, `lower_snake_case` functions/vars, ALL_CAPS macros, `static` for file-local helpers, fixed-width types for guest-memory math, paging/segment structs zero-initialized before use.
- Log through `src/debug.*` (`DEBUG_NONE/BASIC/DETAILED/ALL`) and gate output behind `verbose_enabled()` — never add unconditional prints, least of all in VM-exit hot paths. `HC_GETCHAR` traces are suppressed even under `--verbose` on purpose: at an interactive prompt they drown out the guest.
- vCPU output: single vCPU prints unprefixed; multi-vCPU uses ANSI colors spread over a 300° hue span starting at green, so no vCPU reads as red (i.e. as an error). Output is character-by-character and unbuffered, so interleaving is visible in demos — `tools/smoke.sh` therefore asserts on multi-vCPU *content*, not ordering.
- Commits follow Conventional Commits with area scopes: `feat(vmm):`, `fix(guest):`, `feat(linux-boot):`, `docs:`.
