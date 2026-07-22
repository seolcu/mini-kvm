# Deliberately broken kernels, used to check that the fault analysis in
# src/explain.c actually identifies the cause.

ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib --build-id=none

FAULTS := no-idt bad-stack

.PHONY: all clean
all: $(addsuffix .elf,$(FAULTS))

%.elf: %.S linker.ld
	@as $(ASFLAGS) -o $*.o $<
	@ld $(LDFLAGS) -o $@ $*.o

clean:
	@rm -f *.o *.elf
