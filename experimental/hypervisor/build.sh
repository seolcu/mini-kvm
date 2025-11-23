#!/bin/sh
set -ev

clang \
    -Wall -Wextra --target=riscv64-unknown-elf -ffreestanding -nostdlib \
    -Wl,-eguest_boot -Wl,-Ttext=0x100000 -Wl,-Map=guest.map \
    guest.S -o guest.elf

llvm-objcopy -O binary guest.elf guest.bin

RUSTFLAGS="-C link-arg=-Thypervisor.ld -C linker=rust-lld" \
  cargo build --bin hypervisor --target riscv64gc-unknown-none-elf

cp target/riscv64gc-unknown-none-elf/debug/hypervisor hypervisor.elf
