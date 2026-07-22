# Makefile for the x86 guest programs
#
# Each guest is a flat binary linked at address 0 (guest.ld), loaded straight
# into guest memory by the VMM. No ELF loader is involved.

AS       := as
LD       := ld
ASFLAGS  := --32
LDFLAGS  := -m elf_i386 -T guest.ld --oformat=binary

# 16-bit real-mode guests
GUESTS    := minimal hello counter multiplication multiplication_short \
             fibonacci hctest matrix
# 64-bit long-mode guests (different assembler/linker flags)
GUESTS_64 := hello_64

.PHONY: all clean

all: $(GUESTS) $(GUESTS_64)
	@echo
	@echo "=== Guest binaries ==="
	@for g in $(GUESTS) $(GUESTS_64); do \
		printf "  %-22s %6d bytes\n" "$$g" $$(stat -c%s $$g); \
	done

# A static pattern rule, rather than a plain `%` implicit rule: `%: %.S` would
# also match the .S files themselves and confuse make. Naming the targets
# explicitly avoids that without repeating a rule per guest.
$(GUESTS): %: %.S guest.ld
	@$(AS) $(ASFLAGS) $< -o $*.o
	@$(LD) $(LDFLAGS) -o $@ $*.o

$(GUESTS_64): %: %.S guest_64.ld
	@$(AS) --64 $< -o $*.o
	@$(LD) -m elf_x86_64 -T guest_64.ld --oformat=binary -o $@ $*.o

clean:
	@rm -f *.o *.elf $(GUESTS) $(GUESTS_64)
