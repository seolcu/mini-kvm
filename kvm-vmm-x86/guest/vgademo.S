# VGA text mode demo
#
# A 32-bit protected-mode guest that writes directly to the VGA text buffer at
# 0xB8000, the way nearly every hobby kernel produces its first output.
#
# The VMM has already entered protected mode with paging by the time this runs,
# and identity-maps the low 4MB, so 0xB8000 is reachable as-is. Run it with:
#
#   ./kvm-vmm --paging --vga guest/vgademo
#
# Each cell is two bytes: the character, then an attribute byte holding a
# 4-bit foreground colour, a 3-bit background colour, and a blink bit.

.code32
.section .text
.global _start

.equ VGA_BASE, 0xB8000
.equ COLS,     80

_start:
    # Clear the screen: 80*25 cells of blank-on-black.
    mov     $VGA_BASE, %edi
    mov     $(80 * 25), %ecx
    mov     $0x0720, %ax            # ' ' with light-grey-on-black
    rep     stosw

    # Row 1: white on blue
    mov     $(VGA_BASE + 1 * COLS * 2), %edi
    lea     msg_title, %esi
    mov     $0x1F, %ah              # fg=white(15) bg=blue(1)
    call    puts

    # Row 3: light green on black
    mov     $(VGA_BASE + 3 * COLS * 2), %edi
    lea     msg_body, %esi
    mov     $0x0A, %ah              # fg=light green
    call    puts

    # Row 5: a colour bar, one cell per VGA colour, to prove the attribute
    # byte is being decoded rather than ignored.
    mov     $(VGA_BASE + 5 * COLS * 2), %edi
    xor     %ecx, %ecx
bar_loop:
    mov     %cl, %ah
    shl     $4, %ah                 # colour index into the background nibble
    mov     $' ', %al
    mov     %ax, (%edi)
    add     $2, %edi
    inc     %ecx
    cmp     $8, %ecx
    jl      bar_loop

    # Row 7: light red on black
    mov     $(VGA_BASE + 7 * COLS * 2), %edi
    lea     msg_done, %esi
    mov     $0x0C, %ah              # fg=light red
    call    puts

    hlt

# puts: write the NUL-terminated string at ESI to the cells at EDI,
# using the attribute already in AH. Clobbers AL, ESI, EDI.
puts:
    lodsb                           # AL = *ESI++
    test    %al, %al
    jz      puts_done
    mov     %ax, (%edi)             # AL = char, AH = attribute
    add     $2, %edi
    jmp     puts
puts_done:
    ret

.section .rodata
msg_title:
    .asciz "  Mini-KVM VGA text mode  "
msg_body:
    .asciz "Written directly to 0xB8000 by the guest."
msg_done:
    .asciz "No VM exits were involved in rendering this screen."
