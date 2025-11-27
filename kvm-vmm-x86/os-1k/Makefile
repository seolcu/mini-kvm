# Makefile for 1K OS x86 port

CC = gcc
AS = as
LD = ld
OBJCOPY = objcopy

# 32-bit x86 compilation flags
# -no-pie: Disable PIE at GCC driver level (Arch GCC has --enable-default-pie)
# -march=i686: Match Fedora's --with-arch_32=i686 for consistent code generation
CFLAGS = -m32 -march=i686 -std=c11 -O2 -Wall -Wextra -g \
         -ffreestanding -nostdlib -fno-builtin \
         -fno-stack-protector -fno-pie -fno-pic -no-pie \
         -mno-red-zone

ASFLAGS = --32

# Linker flags to ensure deterministic binary layout
# --no-pie: Disable PIE in linker
# --build-id=none: Remove .note.gnu.build-id section
# -z norelro: Disable RELRO (not needed for kernel)
KERNEL_LDFLAGS = -m elf_i386 -T kernel.ld \
                 --no-pie \
                 --build-id=none \
                 -z norelro
USER_LDFLAGS = -m elf_i386 -T user.ld \
               --no-pie \
               --build-id=none \
               -z norelro

.PHONY: all clean test info disasm run

all: kernel

#
# Shell binary (user program)
#
shell.bin: shell.o user.o common.o
	@echo "=== Linking shell.elf ==="
	$(LD) $(USER_LDFLAGS) -o shell.elf shell.o user.o common.o
	$(OBJCOPY) -O binary shell.elf shell.bin
	@ls -lh shell.bin

shell.o: shell.c user.h common.h
	@echo "=== Compiling shell.c ==="
	$(CC) $(CFLAGS) -c -o shell.o shell.c

user.o: user.c user.h common.h
	@echo "=== Compiling user.c ==="
	$(CC) $(CFLAGS) -c -o user.o user.c

#
# Kernel binary (embedded shell)
#
shell.bin.o: shell.bin
	@echo "=== Embedding shell.bin into object ==="
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 \
		--redefine-sym _binary_shell_bin_start=_binary_shell_bin_start \
		--redefine-sym _binary_shell_bin_end=_binary_shell_bin_end \
		--redefine-sym _binary_shell_bin_size=_binary_shell_bin_size \
		shell.bin shell.bin.o

kernel: boot.o kernel.o common.o shell.bin.o
	@echo "=== Linking kernel.elf ==="
	$(LD) $(KERNEL_LDFLAGS) -o kernel.elf boot.o kernel.o common.o shell.bin.o
	@echo ""
	@echo "=== Creating binary ==="
	$(OBJCOPY) -O binary kernel.elf kernel
	@echo ""
	@echo "=== Build complete ==="
	@ls -lh kernel kernel.elf
	@echo ""

boot.o: boot.S
	@echo "=== Assembling boot.S ==="
	$(AS) $(ASFLAGS) -o boot.o boot.S

kernel.o: kernel.c kernel.h common.h
	@echo "=== Compiling kernel.c ==="
	$(CC) $(CFLAGS) -c -o kernel.o kernel.c

common.o: common.c common.h
	@echo "=== Compiling common.c ==="
	$(CC) $(CFLAGS) -c -o common.o common.c

clean:
	rm -f *.o *.elf *.bin *.map *.asm kernel

# Show kernel info
info: kernel.elf
	@echo "=== Kernel sections ==="
	objdump -h kernel.elf
	@echo ""
	@echo "=== Entry point and symbols ==="
	readelf -h kernel.elf | grep Entry
	nm kernel.elf | grep -E '(__kernel_base|__bss|__stack|__free_ram|kernel_main|_start)'

# Disassemble for debugging
disasm: kernel.elf
	objdump -d -M intel kernel.elf > kernel.asm
	@echo "Disassembly written to kernel.asm"

# Run kernel in VMM
run: kernel
	@echo "=== Running kernel in VMM ==="
	cd .. && ./kvm-vmm --paging --entry=0x80001000 --load=0x1000 os-1k/kernel

# Test with simple kernel
test: test_kernel
	@echo "=== Running test kernel in VMM ==="
	cd .. && ./kvm-vmm --paging --entry=0x80001000 --load=0x1000 os-1k/test_kernel
