#!/bin/bash
# QEMU로 kernel.bin 테스트 (KVM 없이, 순수 에뮬레이션)
qemu-system-i386 \
    -m 4M \
    -kernel kernel.bin \
    -nographic \
    -no-reboot \
    -d cpu_reset,int \
    2>&1 | head -50
