# Makefile for KVM VMM (x86)

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS =

TARGET = kvm-vmm
GUEST_BIN = guest/minimal.bin
GUEST_HELLO = guest/hello.bin

.PHONY: all clean guest vmm test test-hello

all: guest vmm

# Build guest code
guest:
	@echo "=== Building guest code ==="
	cd guest && ./build.sh
	@echo

# Build hello guest
hello:
	@echo "=== Building hello guest ==="
	cd guest && as -32 -o hello.o hello.S && \
	ld -m elf_i386 -T guest.ld -o hello.elf hello.o && \
	objcopy -O binary -j .text -j .rodata hello.elf hello.bin
	@ls -lh guest/hello.bin
	@echo

# Build VMM
vmm: src/main.c
	@echo "=== Building VMM ==="
	$(CC) $(CFLAGS) -o $(TARGET) src/main.c $(LDFLAGS)
	@echo "Built: $(TARGET)"
	@echo

# Run the VMM with minimal guest
test: all
	@echo "=== Running VMM (minimal guest) ==="
	./$(TARGET) $(GUEST_BIN)

# Run the VMM with hello guest
test-hello: vmm hello
	@echo "=== Running VMM (hello guest) ==="
	./$(TARGET) $(GUEST_HELLO)

# Clean build artifacts
clean:
	rm -f $(TARGET)
	cd guest && rm -f *.o *.elf *.bin

# Show help
help:
	@echo "Available targets:"
	@echo "  make all     - Build guest and VMM"
	@echo "  make guest   - Build guest code only"
	@echo "  make vmm     - Build VMM only"
	@echo "  make test    - Build and run VMM with guest"
	@echo "  make clean   - Remove build artifacts"
