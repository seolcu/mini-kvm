# Deliberately broken kernels, used to check that the fault analysis in
# src/explain.c actually identifies the cause.

ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib --build-id=none

FAULTS := no-idt bad-stack

.PHONY: all clean
all: $(addsuffix .elf,$(FAULTS)) bad-entry.elf

%.elf: %.S linker.ld
	@as $(ASFLAGS) -o $*.o $<
	@ld $(LDFLAGS) -o $@ $*.o

# Its entry point is set by the linker script, not by the code.
bad-entry.elf: bad-entry.S bad-entry.ld
	@as $(ASFLAGS) -o bad-entry.o bad-entry.S
	@ld -m elf_i386 -T bad-entry.ld -nostdlib --build-id=none -o $@ bad-entry.o

clean:
	@rm -f *.o *.elf
