# Agent Guidelines for mini-kvm

## Project Context
Research documentation for Ajou University self-directed project: developing a lightweight VMM using Linux KVM API.
Currently documentation-only (meetings, research notes). Code development in progress following RISC-V/Rust VMM tutorial.

## Build/Test Commands
No build system yet. Future: Rust-based (likely `cargo build`, `cargo test`, `cargo run`).

## Code Style (Future Development)
- Language: Rust (planned), previously attempted C/C++ with Bochs
- Documentation: Korean language for all research notes and comments
- Follow standard Rust conventions when code is added
- Reference implementations: QEMU, Bochs, [RISC-V VMM tutorial](https://1000hv.seiya.me/en/)

## File Organization
- `/meetings/` - Weekly advisor meeting notes (Korean)
- `/research/` - Weekly research logs with technical details (Korean)
- `README.md` - Project schedule and reference links
- `GEMINI.md` - Project context for AI assistants

## Development Notes
- Target OS: xv6 educational OS (RISC-V version planned)
- Prior work attempted: Bochs emulation (x86), now pivoting to RISC-V approach
