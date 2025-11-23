# Makefile for Mini-KVM (x86)
# Unified build and run system

# --- Configuration ---
CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -pthread
VMM = kvm-vmm

# List of real-mode guests from the build script
GUESTS = minimal hello counter multiplication fibonacci hctest pmode_test

# --- Default Goal: Show help ---
.DEFAULT_GOAL := help

# --- Phony Targets (commands that don't produce a file with the same name) ---
.PHONY: all clean help vmm guests 1k-os
.PHONY: $(patsubst %, run-%, $(GUESTS))
.PHONY: run-multi2 run-multi4
.PHONY: run-1k-os-shell run-1k-os-multiplication run-1k-os-counter run-1k-os-echo run-1k-os-fibonacci run-1k-os-primes run-1k-os-calc run-1k-os-factorial run-1k-os-gcd run-1k-os-about

# --- Build Targets ---

# Build everything
all: vmm guests 1k-os
	@echo
	@echo "--- All components built successfully! ---"
	@echo "Use 'make help' to see available run commands."

# Build the VMM
vmm: $(VMM)

$(VMM): src/main.c src/protected_mode.h
	@echo "=> Building VMM..."
	$(CC) $(CFLAGS) -o $(VMM) src/main.c $(LDFLAGS)

# Build all real-mode guest binaries
guests:
	@echo "=> Building all real-mode guests..."
	@cd guest && ./build.sh > /dev/null

# Build the 1K OS
1k-os:
	@echo "=> Building 1K OS..."
	@$(MAKE) -C os-1k > /dev/null

# --- Run Targets ---

# Generic rule for running real-mode guests
run-%: vmm guests
	@echo "=> Running Real Mode Guest: $*..."
	@timeout 5s ./$(VMM) guest/$*.bin

# Rule for running the protected-mode test guest
run-pmode_test: vmm guests
	@echo "=> Running Protected Mode Test Guest: pmode_test..."
	@timeout 5s ./$(VMM) --paging guest/pmode_test.bin

# Rules for multi-vCPU demos
run-multi2: vmm guests
	@echo "=> Running Multi-vCPU Demo (2 guests)..."
	@timeout 5s ./$(VMM) guest/multiplication.bin guest/counter.bin

run-multi4: vmm guests
	@echo "=> Running Multi-vCPU Demo (4 guests)..."
	@timeout 5s ./$(VMM) guest/counter.bin guest/hello.bin guest/multiplication.bin guest/minimal.bin

# Rule for running 1K OS interactive shell
run-1k-os-shell: vmm 1k-os
	@echo "=> Running 1K OS (Interactive Shell)..."
	@./$(VMM) --paging os-1k/kernel.bin

# Rules for running specific 1K OS programs
run-1k-os-multiplication: vmm 1k-os
	@echo "=> Running 1K OS Demo: Multiplication Table..."
	@printf "1\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-counter: vmm 1k-os
	@echo "=> Running 1K OS Demo: Counter..."
	@printf "2\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-echo: vmm 1k-os
	@echo "=> Running 1K OS Demo: Echo..."
	@printf "3\nHello 1K OS\nquit\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-fibonacci: vmm 1k-os
	@echo "=> Running 1K OS Demo: Fibonacci..."
	@printf "4\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-primes: vmm 1k-os
	@echo "=> Running 1K OS Demo: Prime Numbers..."
	@printf "5\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-calc: vmm 1k-os
	@echo "=> Running 1K OS Demo: Calculator..."
	@printf "6\n123+456\nq\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-factorial: vmm 1k-os
	@echo "=> Running 1K OS Demo: Factorial..."
	@printf "7\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-gcd: vmm 1k-os
	@echo "=> Running 1K OS Demo: GCD..."
	@printf "8\n0\n" | ./$(VMM) --paging os-1k/kernel.bin

run-1k-os-about: vmm 1k-os
	@echo "=> Running 1K OS Demo: About..."
	@printf "9\n0\n" | ./$(VMM) --paging os-1k/kernel.bin


# --- Cleanup ---
clean:
	@echo "=> Cleaning build artifacts..."
	@rm -f $(VMM)
	@$(MAKE) -C guest clean > /dev/null
	@$(MAKE) -C os-1k clean > /dev/null
	@echo "Cleanup complete."


# --- Help ---
help:
	@echo "Mini-KVM Build & Run System"
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
	@echo "--- Run Real-Mode Guests (e.g., make run-hello) ---"
	@echo "  run-minimal            run-hello                run-counter"
	@echo "  run-multiplication     run-fibonacci            run-hctest"
	@echo "  run-pmode_test"
	@echo
	@echo "--- Run Multi-vCPU Demos ---"
	@echo "  run-multi2             Run 2 guests simultaneously"
	@echo "  run-multi4             Run 4 guests simultaneously"
	@echo
	@echo "--- Run 1K OS (Protected Mode) ---"
	@echo "  run-1k-os-shell        Run in interactive shell mode"
	@echo "  run-1k-os-multiplication, run-1k-os-counter, run-1k-os-echo, ..."
	@echo "  run-1k-os-fibonacci,    run-1k-os-primes,     run-1k-os-calc, ..."
	@echo "  run-1k-os-factorial,    run-1k-os-gcd,        run-1k-os-about"
	@echo