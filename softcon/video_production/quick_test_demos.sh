#!/bin/bash
# 빠른 데모 테스트 스크립트

cd /home/seolcu/문서/코드/mini-kvm/kvm-vmm-x86

echo "=== 데모 1: Hello World (단일 vCPU) ==="
./kvm-vmm guest/hello.bin
echo ""

echo "=== 데모 2: 4 vCPU 병렬 실행 (5초) ==="
timeout 5 ./kvm-vmm guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin || true
echo ""

echo "=== 데모 3: 1K OS - 곱셈표 ==="
printf '1\n0\n' | ./kvm-vmm --paging os-1k/kernel.bin
echo ""

echo "모든 데모 테스트 완료!"
