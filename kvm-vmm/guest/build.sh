#!/bin/bash
set -e

echo "Building guest code..."

# Assemble
riscv64-linux-gnu-as -o minimal.o minimal.S

# Link with custom linker script
riscv64-linux-gnu-ld -T guest.ld -o minimal.elf minimal.o

# Extract raw binary
riscv64-linux-gnu-objcopy -O binary minimal.elf minimal.bin

# Show info
echo "Guest binary info:"
ls -lh minimal.bin
riscv64-linux-gnu-objdump -d minimal.elf | head -30

echo "Guest code built successfully!"
