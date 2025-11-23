#!/bin/bash
# Run guest in QEMU (TCG mode - no hardware acceleration)

GUEST=$1
if [ -z "$GUEST" ]; then
    echo "Usage: $0 <guest.bin>"
    exit 1
fi

timeout 10s qemu-system-i386 \
    -machine type=pc \
    -cpu qemu32 \
    -accel tcg \
    -m 1M \
    -nographic \
    -no-reboot \
    -kernel "$GUEST" \
    -serial mon:stdio 2>&1
