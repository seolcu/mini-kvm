/*
 * A Multiboot 2 kernel that walks the tag list it was handed.
 *
 * Multiboot 2 replaces the fixed information structure of Multiboot 1 with a
 * length-prefixed list of 8-byte-aligned tags. Walking it and printing what
 * is there is the most direct way to check that a bootloader built it
 * correctly, which is what this example is for.
 */

#include <stdint.h>
#include <stddef.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

enum {
    TAG_END           = 0,
    TAG_CMDLINE       = 1,
    TAG_LOADER_NAME   = 2,
    TAG_MODULE        = 3,
    TAG_BASIC_MEMINFO = 4,
    TAG_MMAP          = 6,
    TAG_FRAMEBUFFER   = 8,
};

struct tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct tag_basic_meminfo {
    struct tag t;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

struct tag_module {
    struct tag t;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
} __attribute__((packed));

struct tag_mmap {
    struct tag t;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct tag_framebuffer {
    struct tag t;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
} __attribute__((packed));

/* --- VGA text terminal -------------------------------------------------- */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static uint16_t *const vga = (uint16_t *)0xB8000;
static size_t row, col;
static uint8_t color = 0x07;

static void putchar(char c)
{
    if (c == '\n') {
        col = 0;
        row++;
        return;
    }
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        vga[row * VGA_WIDTH + col] = (uint16_t)c | (uint16_t)color << 8;
    }
    if (++col == VGA_WIDTH) {
        col = 0;
        row++;
    }
}

static void print(const char *s)
{
    while (*s) {
        putchar(*s++);
    }
}

static void print_dec(uint64_t v)
{
    char buf[21];
    int i = 0;
    if (v == 0) {
        putchar('0');
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i-- > 0) {
        putchar(buf[i]);
    }
}

static void print_hex(uint64_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    print("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        putchar(digits[(v >> shift) & 0xF]);
    }
}

static void clear(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = (uint16_t)' ' | (uint16_t)color << 8;
    }
    row = col = 0;
}

/* --- Entry point -------------------------------------------------------- */

void kernel_main(uint32_t magic, uint32_t info_addr)
{
    clear();

    color = 0x1F;
    print(" Multiboot 2 tag walker                                        \n");
    color = 0x07;
    print("\n");

    print("Bootloader magic: ");
    print_hex(magic);
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        color = 0x0C;
        print("  BAD\n");
        return;
    }
    color = 0x0A;
    print("  OK\n");
    color = 0x07;

    /* The block starts with total_size and a reserved word; tags follow. */
    uint32_t total = *(uint32_t *)info_addr;
    print("Info block size:  ");
    print_dec(total);
    print(" bytes\n\nTags:\n");

    for (struct tag *t = (struct tag *)(info_addr + 8);
         t->type != TAG_END;
         t = (struct tag *)((uint8_t *)t + ((t->size + 7) & ~7u))) {

        switch (t->type) {
        case TAG_CMDLINE:
            print("  cmdline: ");
            print((const char *)(t + 1));
            print("\n");
            break;

        case TAG_LOADER_NAME:
            print("  loader: ");
            print((const char *)(t + 1));
            print("\n");
            break;

        case TAG_BASIC_MEMINFO: {
            const struct tag_basic_meminfo *m = (const void *)t;
            print("  memory: ");
            print_dec(m->mem_lower);
            print(" KB low, ");
            print_dec(m->mem_upper / 1024);
            print(" MB high\n");
            break;
        }

        case TAG_MODULE: {
            const struct tag_module *m = (const void *)t;
            print("  module: ");
            print_hex(m->mod_start);
            print(" + ");
            print_dec(m->mod_end - m->mod_start);
            print(" bytes\n");
            break;
        }

        case TAG_MMAP: {
            const struct tag_mmap *m = (const void *)t;
            print("  memory map:\n");
            const uint8_t *p = (const uint8_t *)(m + 1);
            const uint8_t *end = (const uint8_t *)t + t->size;
            while (p + m->entry_size <= end) {
                const struct mmap_entry *e = (const struct mmap_entry *)p;
                print("    ");
                print_hex(e->addr);
                print(" + ");
                print_hex(e->len);
                print(e->type == 1 ? "  available\n" : "  reserved\n");
                p += m->entry_size;
            }
            break;
        }

        case TAG_FRAMEBUFFER: {
            const struct tag_framebuffer *f = (const void *)t;
            print("  framebuffer: ");
            print_hex((uint32_t)f->addr);
            print(" ");
            print_dec(f->width);
            print("x");
            print_dec(f->height);
            print(f->type == 2 ? " EGA text\n" : " graphics\n");
            break;
        }

        default:
            print("  tag type ");
            print_dec(t->type);
            print("\n");
            break;
        }
    }

    color = 0x0A;
    print("\nWalked the whole tag list. Halting.\n");
}
