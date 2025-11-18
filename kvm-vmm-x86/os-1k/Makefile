# Makefile for 1K OS x86 port
#
# Builds the test kernel and eventually the full 1K OS

CC = gcc
AS = as
LD = ld
OBJCOPY = objcopy

# 32-bit x86 compilation flags
CFLAGS = -m32 -std=c11 -O2 -Wall -Wextra -g \
         -ffreestanding -nostdlib -fno-builtin \
         -fno-stack-protector -fno-pie -fno-pic \
         -mno-red-zone

ASFLAGS = --32

LDFLAGS = -m elf_i386 -T kernel.ld

.PHONY: all clean test_kernel

all: test_kernel.bin

# Build test kernel
test_kernel.bin: boot.o test_kernel.o
	@echo "=== Linking test_kernel.elf ==="
	$(LD) $(LDFLAGS) -o test_kernel.elf boot.o test_kernel.o
	@echo ""
	@echo "=== Creating binary ==="
	$(OBJCOPY) -O binary test_kernel.elf test_kernel.bin
	@echo ""
	@echo "=== Build complete ==="
	@ls -lh test_kernel.bin test_kernel.elf
	@echo ""

boot.o: boot.S
	@echo "=== Assembling boot.S ==="
	$(AS) $(ASFLAGS) -o boot.o boot.S

test_kernel.o: test_kernel.c
	@echo "=== Compiling test_kernel.c ==="
	$(CC) $(CFLAGS) -c -o test_kernel.o test_kernel.c

clean:
	@echo "=== Cleaning ==="
	rm -f *.o *.elf *.bin *.map
	@echo "Clean complete"

# Show kernel info
info: test_kernel.elf
	@echo "=== Kernel sections ==="
	objdump -h test_kernel.elf
	@echo ""
	@echo "=== Entry point and symbols ==="
	readelf -h test_kernel.elf | grep Entry
	nm test_kernel.elf | grep -E '(__kernel_base|__bss|__stack|__free_ram|kernel_main|_start)'

# Disassemble for debugging
disasm: test_kernel.elf
	objdump -d -M intel test_kernel.elf > test_kernel.asm
	@echo "Disassembly written to test_kernel.asm"
