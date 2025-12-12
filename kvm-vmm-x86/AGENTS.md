# Repository Guidelines

## Project Structure & Module Organization
- `src/` — core VMM written in C (GNU11): KVM setup (`main.c`), CPUID/MSR helpers, paging (`paging_64.c`), and Linux boot path (`linux_boot.c`). The built binary is `kvm-vmm`.
- `guest/` — 16/32-bit sample programs assembled with its own `Makefile`; run via `./kvm-vmm guest/<name>`. `run_qemu.sh` can emulate a guest with QEMU/TCG.
- `os-1k/` — minimal protected-mode OS and shell; built as `os-1k/kernel` and booted with `./kvm-vmm --paging os-1k/kernel`. Includes `qemu_test.sh` to exercise the kernel without KVM.
- `test_long_mode.c` — standalone diagnostic for long-mode/KVM sregs setup.
- `Makefile` — top-level entry that orchestrates VMM, guests, and the 1K OS builds.

## Build, Run, and Development Commands
- `make all` — build VMM, all guests, and `os-1k`. Individual targets: `make vmm`, `make guests`, `make 1k-os`.
- `./kvm-vmm guest/hello` (or any guest binary) — run a real-mode guest; pass multiple guests to spawn multiple vCPUs.
- `./kvm-vmm --paging os-1k/kernel` — boot the 1K OS; pipe input (e.g., `printf '1\n0\n' | ...`) to drive menu flows.
- `make clean` — remove build artifacts under the root, `guest/`, and `os-1k/`.
- Build defaults: `gcc -Wall -Wextra -O2 -std=gnu11 -pthread`. Match these flags when compiling experiments.

## Coding Style & Naming Conventions
- C code uses 4-space indentation, braces on the same line, and lower_snake_case for functions/vars; constants/macros stay ALL_CAPS.
- Keep helpers `static` when file-local and place public prototypes in the matching header.
- Favor explicit sizes (`uint32_t`, `size_t`) for guest memory math; keep paging/segment structures zero-initialized before use.
- Logging: reuse the lightweight debug utilities in `src/debug.*` rather than ad-hoc `printf` spam.

## Testing Guidelines
- No formal test suite; verify builds with `make all` and run at least one guest (`./kvm-vmm guest/minimal`) plus a protected-mode path (`./kvm-vmm --paging os-1k/kernel`).
- For KVM-less validation, use `guest/run_qemu.sh guest/hello.bin` and `os-1k/qemu_test.sh` to exercise binaries under QEMU/TCG.
- When changing paging, CPUID, or MSR handling, capture console output and note observed vCPU behavior/VM exits in your PR.

## Commit & Pull Request Guidelines
- Follow a Conventional Commit style seen in history (`feat(linux-boot): ...`, `docs: ...`). Keep scope optional but meaningful.
- PRs should include: summary of guest/OS scenarios executed, exact commands run, notable logs, and any KVM/QEMU requirements.
- Link related issues, describe guest binaries touched, and call out breaking changes to invocation flags or memory layouts.

## Security & Environment Notes
- Running requires Linux with `/dev/kvm` access; ensure the host user is in the `kvm` group or use sudo as needed.
- Keep binaries small (default 4MB guest memory) and avoid shipping debug traces by default; gate verbose output behind existing flags.
