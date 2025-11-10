# Makefile for KVM VMM (x86)

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS =

TARGET = kvm-vmm
GUEST_BIN = guest/minimal.bin

.PHONY: all clean guest vmm test

all: guest vmm

# Build guest code
guest:
	@echo "=== Building guest code ==="
	cd guest && ./build.sh
	@echo

# Build VMM
vmm: src/main.c
	@echo "=== Building VMM ==="
	$(CC) $(CFLAGS) -o $(TARGET) src/main.c $(LDFLAGS)
	@echo "Built: $(TARGET)"
	@echo

# Run the VMM with guest
test: all
	@echo "=== Running VMM ==="
	./$(TARGET) $(GUEST_BIN)

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
