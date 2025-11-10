#!/bin/bash

set -e

echo "Building x86 Real Mode guest code..."

# Assemble
as -32 --defsym REAL_MODE=1 -o minimal.o minimal.S

# Link with custom linker script
ld -m elf_i386 -T guest.ld -o minimal.elf minimal.o

# Extract raw binary (.text section only)
objcopy -O binary -j .text minimal.elf minimal.bin

# Show binary info
echo ""
echo "Guest binary info:"
ls -lh minimal.bin

echo ""
echo "Disassembly:"
objdump -D -b binary -m i8086 minimal.bin

echo ""
echo "Guest code built successfully!"
