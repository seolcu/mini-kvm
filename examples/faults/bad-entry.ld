/*
 * Links a Multiboot kernel whose entry point lies outside every loaded
 * segment -- the result of linking a kernel for one address and entering it
 * at another. Nothing is printed and nothing faults in a recognisable way,
 * which is what makes it worth warning about before the guest starts.
 */
_far_entry = 0x0F000000;    /* past the end of guest memory */
ENTRY(_far_entry)

SECTIONS
{
    . = 1M;
    .text BLOCK(4K) : ALIGN(4K) { *(.multiboot) *(.text) }
    .bss  BLOCK(4K) : ALIGN(4K) { *(COMMON) *(.bss) }
}
