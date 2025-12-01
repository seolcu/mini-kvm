# Makefile for Mini-KVM (x86)
# Build system only - run binaries directly

# --- Configuration ---
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -pthread
VMM = kvm-vmm

# --- Default Goal: Show help ---
.DEFAULT_GOAL := help

# --- Phony Targets ---
.PHONY: all clean help vmm guests 1k-os

# --- Build Targets ---

# Build everything
all: vmm guests 1k-os
	@echo
	@echo "--- All components built successfully! ---"
	@echo "Run binaries directly:"
	@echo "  ./kvm-vmm guest/<name>"
	@echo "  ./kvm-vmm --paging os-1k/kernel"

# Build the VMM
vmm: $(VMM)

$(VMM): src/main.c src/debug.c src/cpuid.c src/msr.c src/paging_64.c \
        src/protected_mode.h src/long_mode.h src/debug.h src/cpuid.h src/msr.h src/paging_64.h
	@echo "=> Building VMM..."
	$(CC) $(CFLAGS) -o $(VMM) src/main.c src/debug.c src/cpuid.c src/msr.c src/paging_64.c $(LDFLAGS)

# Build all real-mode guest binaries
guests:
	@echo "=> Building all real-mode guests..."
	@$(MAKE) -C guest > /dev/null

# Build the 1K OS
1k-os:
	@echo "=> Building 1K OS..."
	@$(MAKE) -C os-1k > /dev/null

# --- Cleanup ---
clean:
	@echo "=> Cleaning build artifacts..."
	@rm -f $(VMM)
	@$(MAKE) -C guest clean > /dev/null
	@$(MAKE) -C os-1k clean > /dev/null
	@echo "Cleanup complete."

# --- Help ---
help:
	@echo "Mini-KVM Build System"
	@echo
	@echo "Usage: make <target>"
	@echo
	@echo "--- Build Targets ---"
	@echo "  all                    Build all components (VMM, guests, 1K OS)"
	@echo "  vmm                    Build only the VMM"
	@echo "  guests                 Build all real-mode guests"
	@echo "  1k-os                  Build only the 1K OS"
	@echo "  clean                  Remove all build artifacts"
	@echo
	@echo "--- Running Programs ---"
	@echo "Real Mode Guests:"
	@echo "  ./kvm-vmm guest/minimal"
	@echo "  ./kvm-vmm guest/hello"
	@echo "  ./kvm-vmm guest/counter"
	@echo "  ./kvm-vmm guest/multiplication"
	@echo "  ./kvm-vmm guest/fibonacci"
	@echo "  ./kvm-vmm guest/hctest"
	@echo
	@echo "Multi-vCPU (2-4 guests):"
	@echo "  ./kvm-vmm guest/multiplication guest/counter"
	@echo "  ./kvm-vmm guest/counter guest/hello guest/multiplication guest/minimal"
	@echo
	@echo "1K OS (Protected Mode):"
	@echo "  ./kvm-vmm --paging os-1k/kernel"
	@echo "  printf '1\\n0\\n' | ./kvm-vmm --paging os-1k/kernel  # Run multiplication"
	@echo "  printf '3\\nHello\\nquit\\n0\\n' | ./kvm-vmm --paging os-1k/kernel  # Run echo"
	@echo
