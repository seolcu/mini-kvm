# Build the interactive Multiboot kernel example.

CC      := gcc
CFLAGS  := -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
           -fno-pie -fno-stack-protector -fno-builtin
ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib --build-id=none -z noexecstack

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
	@cd ../.. && ./kvm-vmm --vga examples/multiboot-keyboard/kernel.elf

clean:
	@rm -f *.o kernel.elf
