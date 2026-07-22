/*
 * An interactive Multiboot kernel: PS/2 keyboard input, a VGA text terminal
 * with a tracked hardware cursor, and a tiny command prompt.
 *
 * Where the barebones example proves a kernel can boot and take a timer
 * interrupt, this one proves it can talk to the user — the scancode path
 * through the i8042 on IRQ1, and the CRTC cursor registers that every
 * text-mode driver writes.
 *
 * Like the other example it uses no Mini-KVM facilities; it is an ordinary
 * hobby kernel.
 */

#include <stdint.h>
#include <stddef.h>

/* --- Port I/O ----------------------------------------------------------- */

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* --- VGA text terminal -------------------------------------------------- */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static uint16_t *const vga = (uint16_t *)0xB8000;
static size_t row, col;
static uint8_t color = 0x07;

/* Move the hardware cursor by writing the CRTC location registers. */
static void cursor_move(void)
{
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

static void scroll_if_needed(void)
{
    if (row < VGA_HEIGHT) {
        return;
    }
    for (size_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga[i] = vga[i + VGA_WIDTH];
    }
    for (size_t i = 0; i < VGA_WIDTH; i++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = (uint16_t)' ' | (uint16_t)color << 8;
    }
    row = VGA_HEIGHT - 1;
}

static void putchar(char c)
{
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            vga[row * VGA_WIDTH + col] = (uint16_t)' ' | (uint16_t)color << 8;
        }
    } else {
        vga[row * VGA_WIDTH + col] = (uint16_t)c | (uint16_t)color << 8;
        if (++col == VGA_WIDTH) {
            col = 0;
            row++;
        }
    }
    scroll_if_needed();
    cursor_move();
}

static void print(const char *s)
{
    while (*s) {
        putchar(*s++);
    }
}

static void clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)color << 8;
    }
    row = col = 0;
    cursor_move();
}

/* --- PS/2 keyboard ------------------------------------------------------ */

/* Set 1 scancode to ASCII, unshifted. Index is the make code. */
static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',
    0, '*', 0, ' ',
};

static const char keymap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',
    0, '*', 0, ' ',
};

#define KEY_BUF 128
static volatile char key_buf[KEY_BUF];
static volatile int key_head, key_tail;
static volatile int shift_held;

static void keyboard_handle_scancode(uint8_t sc)
{
    /* Break codes have bit 7 set; we only track shift releases. */
    if (sc & 0x80) {
        uint8_t make = sc & 0x7F;
        if (make == 0x2A || make == 0x36) {
            shift_held = 0;
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift_held = 1;
        return;
    }

    char c = shift_held ? keymap_shift[sc] : keymap[sc];
    if (c == 0) {
        return;
    }

    int next = (key_head + 1) % KEY_BUF;
    if (next != key_tail) {
        key_buf[key_head] = c;
        key_head = next;
    }
}

static int key_available(void)
{
    return key_head != key_tail;
}

static char key_get(void)
{
    while (!key_available()) {
        __asm__ volatile("hlt");
    }
    char c = key_buf[key_tail];
    key_tail = (key_tail + 1) % KEY_BUF;
    return c;
}

/* --- Interrupts --------------------------------------------------------- */

struct idt_entry {
    uint16_t offset_low, selector;
    uint8_t zero, flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];

/* Read the scancode, then acknowledge the PIC. */
void keyboard_interrupt(void)
{
    keyboard_handle_scancode(inb(0x60));
    outb(0x20, 0x20);
}

extern void keyboard_isr(void);
__asm__(
    ".global keyboard_isr\n"
    "keyboard_isr:\n"
    "  pusha\n"
    "  call keyboard_interrupt\n"
    "  popa\n"
    "  iret\n");

static void idt_set(int vec, void (*handler)(void))
{
    uint32_t addr = (uint32_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;
    idt[vec].zero        = 0;
    idt[vec].flags       = 0x8E;
    idt[vec].offset_high = (addr >> 16) & 0xFFFF;
}

static void pic_remap(void)
{
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFD);       /* unmask IRQ1 only */
    outb(0xA1, 0xFF);
}

/* --- Shell -------------------------------------------------------------- */

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++; b++;
    }
    return *a == *b;
}

static void read_line(char *buf, int max)
{
    int len = 0;
    for (;;) {
        char c = key_get();
        if (c == '\n') {
            putchar('\n');
            buf[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                putchar('\b');
            }
            continue;
        }
        if (len < max - 1) {
            buf[len++] = c;
            putchar(c);
        }
    }
}

void kernel_main(uint32_t magic, uint32_t info_addr)
{
    (void)magic;
    (void)info_addr;

    clear();
    color = 0x0F;
    print("Multiboot keyboard example\n");
    color = 0x07;
    print("Commands: help, clear, echo <text>, halt\n\n");

    idt_set(0x21, keyboard_isr);
    struct idt_ptr idtp = { .limit = sizeof(idt) - 1, .base = (uint32_t)idt };
    __asm__ volatile("lidt %0" :: "m"(idtp));
    pic_remap();
    __asm__ volatile("sti");

    char line[128];
    for (;;) {
        color = 0x0A;
        print("> ");
        color = 0x07;
        read_line(line, sizeof(line));

        if (str_eq(line, "help")) {
            print("help, clear, echo <text>, halt\n");
        } else if (str_eq(line, "clear")) {
            clear();
        } else if (str_eq(line, "halt")) {
            print("Halting.\n");
            break;
        } else if (line[0] == 'e' && line[1] == 'c' && line[2] == 'h' &&
                   line[3] == 'o' && line[4] == ' ') {
            print(line + 5);
            putchar('\n');
        } else if (line[0] != '\0') {
            color = 0x0C;
            print("unknown command: ");
            print(line);
            putchar('\n');
            color = 0x07;
        }
    }

    __asm__ volatile("cli");
}
