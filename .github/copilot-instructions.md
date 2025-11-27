<!-- Auto-generated guidance for AI coding agents working on mini-kvm -->
# Mini-KVM — AI Agent Instructions

These concise notes orient an AI coding agent to the repository's architecture, developer workflows, and codebase conventions so you can be productive immediately.

- **Big picture**: The primary VMM is a small C-based KVM userland program in `kvm-vmm-x86/` that creates a VM and spawns up to 4 vCPU threads. Experimental Rust hypervisor projects live under `experimental/` and target RISCV builds.

- **Key components (where to look first)**:
  - `kvm-vmm-x86/Makefile` — top-level build orchestration for the C VMM and guests.
  - `kvm-vmm-x86/src/main.c` — main VMM logic: KVM setup, vCPU threading, VM exit handling, hypercall and I/O emulation.
  - `kvm-vmm-x86/guest/` — scripts and guest binaries (real-mode guests).
  - `kvm-vmm-x86/os-1k/` — 1K OS protected-mode guest and its build system.
  - `experimental/hypervisor/` — Rust experimental hypervisor (RISC-V) with `build.sh` / `run.sh` showing cross-compilation and QEMU invocation.

- **Essential runtime commands** (copyable):
  - Build everything (VMM, guests, 1K OS): `cd kvm-vmm-x86 && make all`
  - Build only the VMM: `make vmm`
  - Run real-mode guest: `./kvm-vmm guest/hello.bin`
  - Run protected-mode 1K OS: `./kvm-vmm --paging os-1k/kernel.bin`
  - Verbose debugging: add `--verbose` to print VM-exit/hypercall traces.

- **Project-specific conventions & patterns** (important to preserve):
  - Hypercalls are implemented via port I/O to port `0x500`. Hypercall numbers and behavior are in `src/main.c` (e.g. `HC_PUTCHAR`, `HC_GETCHAR`, `HC_EXIT`).
  - UART output uses COM1 port `0x3f8` and is forwarded to host `stdout` (see `handle_io`).
  - Multi-vCPU mapping: each vCPU gets a separate guest-physical region and is mapped at `vcpu_id * mem_size`. Real-mode uses 256KB per vCPU; protected mode uses 4MB.
  - Protected-mode defaults: entry `0x80001000`, load offset `0x1000`. Command-line flags `--entry` and `--load` override them.
  - IRQCHIP is created only when `--paging` (protected mode) is enabled — real-mode guests avoid interrupts for simplicity.
  - Terminal raw-mode changes (for interactive 1K OS) are enabled only in paging mode; piping input will not require raw mode.

- **Debugging and checks an agent should run or suggest**:
  - Confirm KVM availability: check `/dev/kvm` and `lsmod | grep kvm` before running the VMM.
  - Reproduce a failing scenario with `--verbose` and small inputs (e.g. `printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin`).
  - Look for VM exit reasons and fault details in `kvm_run->exit_reason` handling inside `src/main.c`.

- **Integration points & external dependencies**:
  - Relies on the host kernel KVM API (`/dev/kvm`) and ioctl calls (`KVM_*`) in `src/main.c`.
  - Uses `qemu-system-riscv64` only for `experimental/hypervisor/run.sh` (RISC-V experimental target).
  - Toolchains: normal Linux toolchain for the C VMM (`gcc`, `make`), and a Rust cross-target toolchain for `experimental/hypervisor` (see `build.sh` and `RUSTFLAGS` usage).

- **What to preserve when editing**:
  - The hypercall ABI and port numbers (`0x500`) — many guest programs depend on these exact semantics.
  - Guest memory layout and per-vCPU mapping scheme — changing these requires updating guest binaries and `os-1k` loader logic.
  - The conditional creation of IRQCHIP for protected mode; tests and interactive behavior depend on this flag.

- **Where to add tests or instrumentation**:
  - Small, deterministic unit-like tests can be created by running `./kvm-vmm guest/<small>.bin` and asserting stdout content (used by CI-style scripts).
  - Add verbose logs guarded by the existing `--verbose` flag; avoid printing in hot paths unless gated.

If anything is missing or you want certain conventions expanded (for example, more details from `os-1k/` or `guest/build.sh`), tell me which area to expand and I will iterate.
