/*
 * A minimal Multiboot kernel, in the style of the OSDev "Bare Bones"
 * tutorial: write to the VGA text buffer and inspect what the bootloader
 * handed us.
 *
 * This exists to prove that Mini-KVM can run an ordinary hobby kernel that
 * knows nothing about Mini-KVM. It uses no hypercalls and no Mini-KVM
 * headers; the same binary boots under GRUB or QEMU.
 */

#include <stdint.h>
#include <stddef.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Boot information structure, through the fields this example reads. */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
} __attribute__((packed));

struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

/* --- VGA text terminal -------------------------------------------------- */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_LIGHT_GREY = 7, VGA_LIGHT_GREEN = 10, VGA_LIGHT_RED = 12,
    VGA_YELLOW = 14, VGA_WHITE = 15,
};

static uint16_t *const vga_buffer = (uint16_t *)0xB8000;
static size_t term_row;
static size_t term_col;
static uint8_t term_color;

static uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg)
{
    return (uint8_t)fg | (uint8_t)(bg << 4);
}

static void term_setcolor(enum vga_color fg, enum vga_color bg)
{
    term_color = vga_entry_color(fg, bg);
}

static void term_init(void)
{
    term_row = 0;
    term_col = 0;
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = (uint16_t)' ' | (uint16_t)term_color << 8;
    }
}

static void term_putchar(char c)
{
    if (c == '\n') {
        term_col = 0;
        term_row++;
        return;
    }
    vga_buffer[term_row * VGA_WIDTH + term_col] =
        (uint16_t)c | (uint16_t)term_color << 8;
    if (++term_col == VGA_WIDTH) {
        term_col = 0;
        term_row++;
    }
}

static void term_write(const char *s)
{
    while (*s) {
        term_putchar(*s++);
    }
}

static void term_write_dec(uint32_t value)
{
    char buf[11];
    int i = 0;

    if (value == 0) {
        term_putchar('0');
        return;
    }
    while (value > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (i-- > 0) {
        term_putchar(buf[i]);
    }
}

static void term_write_hex(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    term_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        term_putchar(digits[(value >> shift) & 0xF]);
    }
}

/* --- Interrupts: 8259 PIC and the 8254 timer ---------------------------- */

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

/* Read one MC146818 register: latch the index at 0x70, read data at 0x71. */
static uint8_t cmos_get(uint8_t index)
{
    outb(0x70, index);
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static volatile uint32_t ticks;

/* Assembly stub: acknowledge the PIC and return. */
extern void timer_isr(void);
__asm__(
    ".global timer_isr\n"
    "timer_isr:\n"
    "  pusha\n"
    "  incl ticks\n"
    "  movb $0x20, %al\n"        /* end-of-interrupt to the master PIC */
    "  outb %al, $0x20\n"
    "  popa\n"
    "  iret\n");

static void idt_set(int vec, void (*handler)(void))
{
    uint32_t addr = (uint32_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = 0x08;    /* the flat code segment we were given */
    idt[vec].zero        = 0;
    idt[vec].flags       = 0x8E;    /* present, ring 0, 32-bit interrupt gate */
    idt[vec].offset_high = (addr >> 16) & 0xFFFF;
}

/*
 * Remap the PIC so hardware IRQs land on vectors 0x20-0x2F rather than
 * colliding with the CPU's own exception vectors, then unmask IRQ0 only.
 */
static void pic_remap(void)
{
    outb(0x20, 0x11); outb(0xA0, 0x11);     /* start initialisation */
    outb(0x21, 0x20); outb(0xA1, 0x28);     /* vector offsets */
    outb(0x21, 0x04); outb(0xA1, 0x02);     /* master/slave wiring */
    outb(0x21, 0x01); outb(0xA1, 0x01);     /* 8086 mode */
    outb(0x21, 0xFE);                       /* mask all but IRQ0 */
    outb(0xA1, 0xFF);
}

static void timer_init(uint32_t hz)
{
    uint32_t divisor = 1193182 / hz;
    outb(0x43, 0x36);                        /* channel 0, rate generator */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
}

/* --- Entry point -------------------------------------------------------- */

void kernel_main(uint32_t magic, uint32_t info_addr)
{
    term_init();

    term_setcolor(VGA_WHITE, VGA_BLUE);
    term_write(" Multiboot bare bones kernel                                   \n");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    term_write("\n");

    /* Did a Multiboot-compliant loader actually start us? */
    term_write("Bootloader magic: ");
    term_write_hex(magic);
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
        term_write("  OK\n");
    } else {
        term_setcolor(VGA_LIGHT_RED, VGA_BLACK);
        term_write("  BAD\n");
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    const struct multiboot_info *mbi = (const struct multiboot_info *)info_addr;

    term_write("Info structure:   ");
    term_write_hex(info_addr);
    term_write("\n");

    if (mbi->flags & (1 << 0)) {
        term_write("Lower memory:     ");
        term_write_dec(mbi->mem_lower);
        term_write(" KB\n");
        term_write("Upper memory:     ");
        term_write_dec(mbi->mem_upper / 1024);
        term_write(" MB\n");
    }

    if (mbi->flags & (1 << 9)) {
        term_write("Booted by:        ");
        term_write((const char *)mbi->boot_loader_name);
        term_write("\n");
    }

    if (mbi->flags & (1 << 6)) {
        term_write("\nMemory map:\n");
        uint32_t addr = mbi->mmap_addr;
        uint32_t end = mbi->mmap_addr + mbi->mmap_length;
        while (addr < end) {
            const struct multiboot_mmap_entry *e =
                (const struct multiboot_mmap_entry *)addr;

            term_write("  ");
            term_write_hex((uint32_t)e->base_addr);
            term_write(" + ");
            term_write_hex((uint32_t)e->length);
            term_write(e->type == 1 ? "  available\n" : "  reserved\n");

            addr += e->size + sizeof(uint32_t);
        }
    }

    /* Modules are how a Multiboot kernel receives its root filesystem. */
    if (mbi->flags & (1 << 3)) {
        term_write("\nModules: ");
        term_write_dec(mbi->mods_count);
        term_write("\n");
        const struct multiboot_mod_list *mods =
            (const struct multiboot_mod_list *)mbi->mods_addr;
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            term_write("  ");
            term_write_hex(mods[i].mod_start);
            term_write(" + ");
            term_write_dec(mods[i].mod_end - mods[i].mod_start);
            term_write(" bytes, first byte '");
            term_putchar(*(const char *)mods[i].mod_start);
            term_write("'\n");
        }
    }

    /* The RTC gives the guest a wall clock. Printing only the date keeps
     * this reproducible enough to diff while still proving the read works. */
    term_write("\nRTC century field: ");
    term_write_dec(from_bcd(cmos_get(0x32)));
    term_write("  (status B = ");
    term_write_hex(cmos_get(0x0B));
    term_write(")\n");

    /* Set up interrupts and wait for the timer to prove it ticks. */
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    term_write("\nEnabling interrupts...\n");

    idt_set(0x20, timer_isr);
    struct idt_ptr idtp = { .limit = sizeof(idt) - 1, .base = (uint32_t)idt };
    __asm__ volatile("lidt %0" :: "m"(idtp));

    pic_remap();
    timer_init(100);
    __asm__ volatile("sti");

    /* Wait for 10 ticks at 100Hz, i.e. about a tenth of a second. */
    while (ticks < 10) {
        __asm__ volatile("hlt");
    }

    __asm__ volatile("cli");
    term_write("Timer interrupts received: ");
    term_write_dec(ticks);
    term_write("\n");

    term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    term_write("\nKernel reached the end of main. Halting.\n");
}
