# Build the bare bones Multiboot kernel.
#
# A real cross-compiler (i686-elf-gcc) is the usual advice, but a host gcc
# with -m32 -ffreestanding produces a working image for an example this size.

CC      := gcc
CFLAGS  := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
           -fno-pie -fno-stack-protector -fno-builtin
ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib --build-id=none

.PHONY: all clean run

all: kernel.elf

kernel.elf: boot.o kernel.o linker.ld
	@ld $(LDFLAGS) -o $@ boot.o kernel.o
	@echo "built $@"

boot.o: boot.S
	@as $(ASFLAGS) -o $@ $<

kernel.o: kernel.c
	@$(CC) $(CFLAGS) -c -o $@ $<

run: kernel.elf
	@cd ../.. && ./kvm-vmm --vga examples/multiboot-barebones/kernel.elf

clean:
	@rm -f *.o kernel.elf
