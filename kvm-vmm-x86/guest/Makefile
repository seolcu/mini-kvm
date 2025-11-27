# Makefile for x86 Real Mode guest binaries

# --- Configuration ---
AS = as
LD = ld
ASFLAGS = --32
LDFLAGS = -m elf_i386 -T guest.ld --oformat=binary

# List of guest programs
GUESTS = minimal hello counter multiplication multiplication_short fibonacci hctest matrix

# Generate .bin targets from guest list
BINS = $(addsuffix .bin,$(GUESTS))

# --- Phony Targets ---
.PHONY: all clean

# --- Default Target ---
all: $(BINS)
	@echo
	@echo "=== Guest binaries built ==="
	@for bin in $(BINS); do \
		printf "  %-25s %6d bytes\n" "$$bin" $$(stat -c%s $$bin); \
	done

# --- Pattern Rules ---
%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

%.bin: %.o guest.ld
	$(LD) $(LDFLAGS) -o $@ $<

# --- Cleanup ---
clean:
	@rm -f *.o *.elf *.bin
