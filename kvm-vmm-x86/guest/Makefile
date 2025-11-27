# Makefile for x86 Real Mode guest binaries

# --- Configuration ---
AS = as
LD = ld
ASFLAGS = --32
LDFLAGS = -m elf_i386 -T guest.ld --oformat=binary

# List of guest programs (no extension - directly executable names)
GUESTS = minimal hello counter multiplication multiplication_short fibonacci hctest matrix

# --- Phony Targets ---
.PHONY: all clean $(GUESTS)

# --- Default Target ---
all: $(GUESTS)
	@echo
	@echo "=== Guest binaries built ==="
	@for guest in $(GUESTS); do \
		printf "  %-25s %6d bytes\n" "$$guest" $$(stat -c%s $$guest); \
	done

# --- Explicit Rules for Each Guest ---
# We need explicit rules because Make's implicit % rule conflicts with .S files

minimal: minimal.S guest.ld
	$(AS) $(ASFLAGS) minimal.S -o minimal.o
	$(LD) $(LDFLAGS) -o $@ minimal.o

hello: hello.S guest.ld
	$(AS) $(ASFLAGS) hello.S -o hello.o
	$(LD) $(LDFLAGS) -o $@ hello.o

counter: counter.S guest.ld
	$(AS) $(ASFLAGS) counter.S -o counter.o
	$(LD) $(LDFLAGS) -o $@ counter.o

multiplication: multiplication.S guest.ld
	$(AS) $(ASFLAGS) multiplication.S -o multiplication.o
	$(LD) $(LDFLAGS) -o $@ multiplication.o

multiplication_short: multiplication_short.S guest.ld
	$(AS) $(ASFLAGS) multiplication_short.S -o multiplication_short.o
	$(LD) $(LDFLAGS) -o $@ multiplication_short.o

fibonacci: fibonacci.S guest.ld
	$(AS) $(ASFLAGS) fibonacci.S -o fibonacci.o
	$(LD) $(LDFLAGS) -o $@ fibonacci.o

hctest: hctest.S guest.ld
	$(AS) $(ASFLAGS) hctest.S -o hctest.o
	$(LD) $(LDFLAGS) -o $@ hctest.o

matrix: matrix.S guest.ld
	$(AS) $(ASFLAGS) matrix.S -o matrix.o
	$(LD) $(LDFLAGS) -o $@ matrix.o

# --- Cleanup ---
clean:
	@rm -f *.o *.elf $(GUESTS)
