/*
 * Multiboot entry stub, following the conventional "Bare Bones" layout used
 * by most hobby kernels and by the OSDev wiki tutorial.
 *
 * Nothing here is Mini-KVM specific: this is the standard sequence a
 * Multiboot-compliant bootloader such as GRUB expects, which is the point of
 * the example.
 */

.set ALIGN,    1 << 0              /* align loaded modules on page boundaries */
.set MEMINFO,  1 << 1              /* provide a memory map */
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002          /* identifies this as a Multiboot kernel */
.set CHECKSUM, -(MAGIC + FLAGS)    /* the three must sum to zero */

/* The bootloader scans the first 8KB for this header, 4-byte aligned. */
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/*
 * Multiboot leaves ESP undefined, so the kernel provides its own stack before
 * doing anything that needs one. 16KB, 16-byte aligned as the ABI requires.
 */
.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp

    /* The bootloader left the magic in EAX and the boot information
     * structure address in EBX. Pass both to C. */
    push %ebx
    push %eax
    call kernel_main

    /* kernel_main should not return. If it does, halt for good. */
    cli
1:  hlt
    jmp 1b

.size _start, . - _start
