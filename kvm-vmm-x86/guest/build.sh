#!/bin/bash

set -e

echo "Building x86 Real Mode guest code..."

# List of guest programs to build
GUESTS="minimal hello counter multiplication fibonacci hctest"

for guest in $GUESTS; do
    if [ ! -f "${guest}.S" ]; then
        echo "Warning: ${guest}.S not found, skipping"
        continue
    fi
    
    echo ""
    echo "Building ${guest}..."
    
    # Assemble
    as -32 --defsym REAL_MODE=1 -o ${guest}.o ${guest}.S
    
    # Link with custom linker script
    ld -m elf_i386 -T guest.ld -o ${guest}.elf ${guest}.o
    
    # Extract raw binary (.text and .rodata sections)
    objcopy -O binary -j .text -j .rodata ${guest}.elf ${guest}.bin
    
    echo "  ${guest}.bin: $(stat -c%s ${guest}.bin) bytes"
done

echo ""
echo "=== All guest binaries ==="
ls -lh *.bin 2>/dev/null | awk '{print $9, $5}'

echo ""
echo "Guest code built successfully!"
