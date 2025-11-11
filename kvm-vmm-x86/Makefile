# Makefile for KVM VMM (x86)

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS =

TARGET = kvm-vmm
GUEST_BIN = guest/minimal.bin
GUEST_HELLO = guest/hello.bin

.PHONY: all clean guest vmm test test-hello counter hello minimal hctest multiplication pmode_test help

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

# Generic pattern: build any .S file
# Usage: make build-<name>
# Example: make build-counter (builds counter.S → counter.bin)
build-%: guest/%.S
	@echo "=== Building guest/$*.S ==="
	cd guest && as -32 -o $*.o $*.S && \
	ld -m elf_i386 -T guest.ld -o $*.elf $*.o && \
	objcopy -O binary -j .text -j .rodata $*.elf $*.bin
	@echo "Built: guest/$*.bin"
	@ls -lh guest/$*.bin
	@echo

# Generic pattern: run any guest binary
# Usage: make run-<name>
# Example: make run-counter (runs counter.bin in VMM)
run-%: vmm guest/%.bin
	@echo "=== Running VMM (guest/$*.bin) ==="
	./$(TARGET) guest/$*.bin

# Convenience shortcuts: build + run in one command
counter: vmm build-counter run-counter

hello: vmm build-hello run-hello

minimal: vmm build-minimal run-minimal

hctest: vmm build-hctest run-hctest

multiplication: vmm build-multiplication run-multiplication

# Protected Mode test guest (requires -p flag)
pmode_test: vmm build-pmode_test
	@echo "=== Running VMM (Protected Mode guest) ==="
	./$(TARGET) -p guest/pmode_test.bin

# Clean build artifacts
clean:
	rm -f $(TARGET)
	cd guest && rm -f *.o *.elf *.bin

# Show help
help:
	@echo "Available targets:"
	@echo "  make all            - Build guest and VMM"
	@echo "  make vmm            - Build VMM only"
	@echo "  make test           - Build and run VMM with minimal guest"
	@echo ""
	@echo "Quick commands (build + run):"
	@echo "  make counter        - Build and run counter.S (Real Mode)"
	@echo "  make hello          - Build and run hello.S (Real Mode)"
	@echo "  make minimal        - Build and run minimal.S (Real Mode)"
	@echo "  make pmode_test     - Build and run pmode_test.S (Protected Mode)"
	@echo ""
	@echo "Manual build/run:"
	@echo "  make build-<name>   - Build guest/<name>.S → guest/<name>.bin"
	@echo "  make run-<name>     - Run guest/<name>.bin in VMM"
	@echo ""
	@echo "Examples:"
	@echo "  make build-counter  - Build counter.S"
	@echo "  make run-counter    - Run counter.bin"
	@echo "  make counter        - Build + run counter.S (shortcut)"
	@echo ""
	@echo "  make clean          - Remove build artifacts"
