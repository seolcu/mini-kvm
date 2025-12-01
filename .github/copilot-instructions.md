<!-- Auto-generated guidance for AI coding agents working on mini-kvm -->
# Mini-KVM — AI Agent Instructions

These concise notes orient an AI coding agent to the repository's architecture, developer workflows, and codebase conventions so you can be productive immediately.

- **Big picture**: The primary VMM is a small C-based KVM userland program in `kvm-vmm-x86/` (~1,700 lines) that creates a VM and spawns up to 4 vCPU threads. It supports both real-mode (16-bit) and protected-mode (32-bit with paging) x86 guests. Experimental Rust hypervisor projects live under `experimental/` targeting RISC-V H-extension.

- **Key components (where to look first)**:
  - `kvm-vmm-x86/Makefile` — top-level build orchestration for the C VMM and guests; run `make help` for all targets.
  - `kvm-vmm-x86/src/main.c` — main VMM logic: KVM setup, vCPU threading, VM exit handling, hypercall/I/O emulation, interrupt injection, paging setup.
  - `kvm-vmm-x86/src/protected_mode.h` — GDT/IDT structures and constants for protected mode.
  - `kvm-vmm-x86/guest/` — 6 real-mode guest programs (`.S` assembly files), built with `as` and `ld` into flat binaries.
  - `kvm-vmm-x86/os-1k/` — 1K OS: protected-mode kernel with 9 user programs, separate kernel/user linking via `kernel.ld`/`user.ld`.
  - `experimental/hypervisor/` — Rust experimental hypervisor (RISC-V) with `build.sh`/`run.sh` for cross-compilation and QEMU invocation.

- **Essential runtime commands** (copyable):
  - Build everything (VMM, guests, 1K OS): `cd kvm-vmm-x86 && make all`
  - Build only the VMM: `make vmm`
  - Build only guests or 1K OS: `make guests` or `make 1k-os`
  - Run real-mode guest: `./kvm-vmm guest/hello.bin`
  - Run protected-mode 1K OS: `./kvm-vmm --paging os-1k/kernel.bin`
  - Multi-vCPU execution (2-4 guests): `./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin`
  - Verbose debugging: add `--verbose` to print VM-exit/hypercall traces (except HC_GETCHAR IN to avoid spam).
  - Piped input for automated tests: `printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin` (runs program 1, then exits).

- **Project-specific conventions & patterns** (important to preserve):
  - Hypercalls are implemented via port I/O to port `0x500`. Hypercall numbers and behavior are in `src/main.c` (e.g. `HC_PUTCHAR`, `HC_GETCHAR`, `HC_EXIT`).
  - UART output uses COM1 port `0x3f8` and is forwarded to host `stdout` (see `handle_io`).
  - Multi-vCPU mapping: each vCPU gets a separate guest-physical region and is mapped at `vcpu_id * mem_size`. Real-mode uses 256KB per vCPU; protected mode uses 4MB.
  - Protected-mode defaults: entry `0x80001000`, load offset `0x1000`. Command-line flags `--entry` and `--load` override them.
  - **IRQCHIP is created only when `--paging` (protected mode) is enabled** — real-mode guests avoid interrupts for simplicity. This prevents real-mode guests from hanging on HLT due to unwanted timer interrupts (IRQ0).
  - Terminal raw-mode changes (for interactive 1K OS) are enabled only in paging mode; piping input will not require raw mode.
  - **vCPU output formatting**: Single vCPU shows clean `[guest_name]` prefix without colors; multi-vCPU uses color codes (Cyan/Green/Yellow/Blue) for visual distinction. Character-by-character output (no line buffering) to maximize visible parallelism in multi-vCPU demos.
  - **Protected-mode GDT**: Set up at `0x500` with 5 descriptors (Null, kernel code, kernel data, user code, user data). See `setup_gdt()` and `protected_mode.h`.
  - **Paging setup**: Uses 2-level paging with 4MB PSE pages. Page directory at `0x2000`, identity-mapped kernel space (`0x80000000+`). See `setup_page_tables()`.

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
  - Add verbose logs guarded by the existing `--verbose` flag; avoid printing in hot paths unless gated. Note: `HC_GETCHAR` IN operations are specifically suppressed even in verbose mode to prevent log spam.
  - 1K OS tests: Use piped input to automate program selection and input, e.g., `printf '3\ntest\nquit\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin`.

- **Build system details**:
  - `kvm-vmm-x86/guest/Makefile`: Assembles `.S` files with `as --32`, links with `ld -m elf_i386 --oformat=binary` to produce flat binaries starting at address `0x0`.
  - `kvm-vmm-x86/os-1k/Makefile`: Builds shell.bin (user programs) first, embeds it into kernel via `objcopy -I binary`, then links kernel with custom linker scripts. The result is a single `kernel.bin` with embedded user programs.
  - 1K OS uses separate compilation: `boot.S` (assembly entry), `kernel.c` (kernel logic), `shell.c` and `user.c` (user programs), `common.c` (shared utilities). All compiled with `-m32 -ffreestanding -nostdlib`.

- **Educational approach & decision-making guidance**:
  - **The developer is learning**: When faced with architectural decisions or trade-offs, act as a teacher, not just a tool. Explain the reasoning behind recommendations, including pros/cons and industry best practices.
  - **"Problem space minimization" principle**: Always advocate for building minimal test cases before complex integrations. Example: a 20-line hypercall guest to validate 64-bit paging before attempting Linux kernel boot.
  - **Incremental validation strategy**: Each new feature should be testable in isolation with a simple guest program. Create progression: minimal → functional → integrated → complete.
  - **Reference implementation usage**: When suggesting use of existing projects (kvmtool, Firecracker, etc.), emphasize studying them as "textbooks" rather than copy-pasting. The goal is understanding and reimplementation in mini-kvm style.
  - **Debugging infrastructure first**: Advocate strongly for building comprehensive debugging tools (verbose logging, register dumps, GDB stub, memory dumps) before implementing complex features. Good debugging tools multiply development speed by 10x.
  - **Code structure for learning**: Preserve educational value of existing Real/Protected mode code. When adding Linux boot support, recommend `--linux` flag to separate concerns rather than replacing simple examples with complexity.
  - **When explaining decisions**: Use concrete examples, show "bad approach ❌" vs "good approach ✅", explain the "why" behind best practices. Include time estimates and complexity warnings where relevant.

If anything is missing or you want certain conventions expanded (for example, more details from `os-1k/` or `guest/build.sh`), tell me which area to expand and I will iterate.
