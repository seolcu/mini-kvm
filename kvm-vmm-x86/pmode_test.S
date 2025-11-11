# Protected Mode Test Guest
# Starts in Real Mode, transitions to Protected Mode, prints test message, and exits

.code16                            # Real Mode, 16-bit code
.global _start

_start:
    # Initialize Real Mode segment registers
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Disable interrupts during mode transition
    cli

    # Load GDT from guest physical address 0x1000
    # Use absolute address in GDT descriptor
    mov $gdt_descriptor_addr, %eax
    lgdt (%eax)

    # Enable Protected Mode by setting CR0.PE flag
    mov %cr0, %eax
    or $0x1, %eax                  # Set PE (Protection Enable) bit
    mov %eax, %cr0

    # Far jump to Protected Mode code (ljmp $segment, $offset)
    # Use selector 0x08 (kernel code) and jump to protected_start
    ljmp $0x8, $protected_start

.code32                            # 32-bit code (Protected Mode)
.align 4
protected_start:
    # Protected Mode is now active
    # Re-enable interrupts
    sti

    # Set up segment registers for Protected Mode
    mov $0x10, %eax                # Data segment selector (0x10)
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss

    # Print "PMODE" via UART (port 0x3f8)
    mov $'P', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    mov $'M', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    mov $'O', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    mov $'D', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    mov $'E', %al
    mov $0x3f8, %dx
    out %al, (%dx)

    # Print newline via hypercall
    mov $0x03, %al                 # HC_NEWLINE
    mov $0x500, %dx
    out %al, (%dx)

    # Exit via hypercall
    mov $0x00, %al                 # HC_EXIT
    mov $0x500, %dx
    out %al, (%dx)

    # Infinite loop (should not reach here)
    jmp .

.align 4
gdt_descriptor_addr:
    .long gdt_descriptor           # Address of GDT descriptor

.align 4
# GDT descriptor for LGDT instruction (loaded in Real Mode)
# Format: 16-bit limit + 32-bit base address
gdt_descriptor:
    .word 0x27                     # GDT size - 1 (5 entries * 8 - 1 = 39 = 0x27)
    .long 0x1000                   # GDT base physical address (created by VMM)
