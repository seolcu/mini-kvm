#!/bin/bash

set -e

echo "Building x86 Real Mode guest code..."

# List of guest programs to build
GUESTS="minimal hello counter multiplication multiplication_short fibonacci hctest matrix"

for guest in $GUESTS; do
    if [ ! -f "${guest}.S" ]; then
        echo "Warning: ${guest}.S not found, skipping"
        continue
    fi
    
    echo "  Building ${guest}..."
    
    # Assemble
    as --32 ${guest}.S -o ${guest}.o
    
    # Link with linker script to ensure code starts at address 0
    # (Required for newer binutils which ignore -Ttext without -T)
    ld -m elf_i386 -T guest.ld --oformat=binary -o ${guest}.bin ${guest}.o
    
    # Show size
    SIZE=$(stat -c%s ${guest}.bin)
    printf "    %-20s %6d bytes\n" "${guest}.bin:" "$SIZE"
done

echo ""
echo "=== All guest binaries ==="
ls -lh *.bin 2>/dev/null | awk '{printf "  %-20s %s\n", $9, $5}'

echo ""
echo "Guest code built successfully!"
