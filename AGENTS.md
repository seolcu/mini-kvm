# Repository Guidelines

## Project Structure & Module Organization
- `kvm-vmm-x86/` — core C VMM (GNU11) with `src/` for KVM setup/paging helpers, `guest/` for 16/32-bit sample programs, `os-1k/` for the protected-mode mini-OS, `test_long_mode.c` for sregs diagnostics, and a top-level `Makefile`.
- `docs/` — final report, demo guide, and supporting write-ups; update here when user-facing behavior changes.
- `experimental/` — RISC-V hypervisor, 64-bit OS, and Linux guest experiments; keep isolated from the main VMM build.
- `research/` — weekly research logs; append new notes rather than rewriting history.
- `meetings/` and `backups/` — reference materials; avoid committing generated artifacts elsewhere.

## Build, Test, and Development Commands
- Core build: `cd kvm-vmm-x86 && make all` (or `make vmm`, `make guests`, `make 1k-os`). Flags default to `gcc -Wall -Wextra -O2 -std=gnu11 -pthread`.
- Run guests: `./kvm-vmm guest/hello.bin` (single) or `./kvm-vmm guest/multiplication.bin guest/counter.bin` (multi-vCPU). Use `--verbose` for VM-exit traces.
- Run 1K OS: `./kvm-vmm --paging os-1k/kernel.bin`; pipe scripted input, e.g., `printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin`.
- KVM-less smoke: `guest/run_qemu.sh guest/hello.bin` and `os-1k/qemu_test.sh` exercise images under QEMU/TCG.
- Cleanup/help: `make clean`, `make help`. Keep build artifacts within `kvm-vmm-x86/`.

## Coding Style & Naming Conventions
- C: 4-space indent, braces on the same line, `lower_snake_case` for functions/vars, ALL_CAPS for macros/consts. Prefer `static` for file-local helpers and fixed-width types for guest memory math.
- Organize headers to match translation units; zero-init paging/segment structs before use and reuse `src/debug.*` for logging instead of ad-hoc prints.
- Guest/OS binaries keep `guest/<name>.bin` and `os-1k/kernel.bin` naming; avoid spaces in filenames.

## Testing Guidelines
- Minimum check before submitting: `make all`, `./kvm-vmm guest/minimal.bin`, and `./kvm-vmm --paging os-1k/kernel.bin` with one menu interaction. Capture console output when touching paging, CPUID, or MSR paths.
- When KVM is unavailable, run the QEMU scripts above and note the lack of hardware acceleration in your notes/PR.
- No formal coverage target yet; document observed VM exits, guest behavior, and any deviations from expected menu flows.

## Commit & Pull Request Guidelines
- Use Conventional Commit style seen in history (e.g., `feat(vmm):`, `fix(guest):`, `docs:`). Keep scopes meaningful to the touched area.
- In PRs, include: purpose, commands executed, notable logs (verbose snippets if relevant), and whether KVM or QEMU was used. Link related issues and call out breaking changes to CLI flags or memory layouts.
- Update `docs/` when user-visible behavior or demos change; keep research updates in `research/` with date-stamped entries.

## Security & Environment Notes
- Target platform is Linux x86_64 with `/dev/kvm`; verify with `lsmod | grep kvm` and `ls -l /dev/kvm`. Ensure the user is in the `kvm` group or use sudo.
- Keep guest memory sizes minimal (default 4MB) and gate verbose logging behind existing flags to avoid noisy defaults. Avoid committing personal build artifacts or trace dumps.
