#!/bin/sh
set -ev

clang \
    -Wall -Wextra --target=riscv64-unknown-elf -ffreestanding -nostdlib \
    -Wl,-eguest_boot -Wl,-Ttext=0x100000 -Wl,-Map=guest.map \
    guest.S -o guest.elf

llvm-objcopy -O binary guest.elf guest.bin

RUSTFLAGS="-C link-arg=-Thypervisor.ld -C linker=rust-lld" \
  cargo build --target riscv64gc-unknown-none-elf

cp target/riscv64gc-unknown-none-elf/debug/my-hypervisor hypervisor.elf

qemu-system-riscv64 \
    -machine virt \
    -cpu rv64,h=true \
    -bios default \
    -smp 1 \
    -m 128M \
    -nographic \
    -d cpu_reset,unimp,guest_errors,int -D qemu.log \
    -serial mon:stdio \
    --no-reboot \
    -kernel hypervisor.elf
