# Agent Guidelines for mini-kvm

## Build/Test Commands
```bash
cd my-hypervisor
./run.sh                           # Build and run hypervisor in QEMU
cargo build --target riscv64gc-unknown-none-elf  # Build only
```
No automated tests yet. Single test: Run `./run.sh` and verify "Booting hypervisor..." and "ABC" output.

## Code Style
- **Language**: Rust (no_std, bare-metal), RISC-V assembly for guest code
- **Imports**: Group `core::` first, then external crates (`spin`), then local modules
- **Formatting**: Standard `rustfmt` (2021 edition). 4-space indent
- **Types**: Explicit types for CSR values (`u64`), use `*mut u8` for raw pointers
- **Naming**: `snake_case` for functions/variables, `PascalCase` for types, `SCREAMING_SNAKE_CASE` for constants
- **Error handling**: `panic!` for unrecoverable errors, `assert!` for invariants. No Result/Option in bare-metal code
- **Unsafe**: Mark all unsafe blocks with clear comments explaining why unsafe is needed
- **CSR access**: Use `asm!` macro with explicit register constraints
- **NO comments** in code unless explaining complex CSR manipulation or RISC-V-specific behavior

## Documentation
- All research notes, meetings, and markdown files MUST be in Korean
- Code comments (when needed) can be in English for technical clarity
