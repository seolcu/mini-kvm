.PHONY: all guest vmm clean

# Cross-compiler for RISC-V
CC = riscv64-linux-gnu-gcc
CFLAGS = -Wall -O2 -static
LDFLAGS = -static

# Targets
all: guest vmm

# Build guest code
guest:
	@echo "Building guest..."
	@cd guest && ./build.sh

# Build VMM
vmm: src/main.c
	@echo "Building VMM..."
	$(CC) $(CFLAGS) -o kvm-vmm src/main.c $(LDFLAGS)
	@echo "VMM built successfully!"
	@ls -lh kvm-vmm

# Clean
clean:
	rm -f kvm-vmm
	rm -f guest/*.o guest/*.elf guest/*.bin
	@echo "Cleaned."

# Run (for documentation - won't work on x86 host)
run:
	@echo "To run this, boot RISC-V Linux with KVM and execute:"
	@echo "  ./kvm-vmm guest/minimal.bin"
