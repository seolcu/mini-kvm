/*
 * loader.c - guest image loading and format detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

#include "loader.h"
#include "debug.h"

/* --- Multiboot 2 ------------------------------------------------------- */

#define MB2_HEADER_MAGIC      0xE85250D6u   /* in the kernel image */
#define MB2_BOOTLOADER_MAGIC  0x36D76289u   /* handed to the kernel in EAX */
#define MB2_HEADER_SEARCH     32768         /* spec: within the first 32KB */
#define MB2_ARCH_I386         0

/* Boot information tag types we emit. */
#define MB2_TAG_END           0
#define MB2_TAG_CMDLINE       1
#define MB2_TAG_LOADER_NAME   2
#define MB2_TAG_MODULE        3
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_MMAP          6
#define MB2_TAG_FRAMEBUFFER   8

/* Where the Multiboot 2 information block goes. */
#define MB2_INFO_ADDR         0x00008000u
#define MB2_INFO_MAX          0x00001000u

/* --- Multiboot 1 ------------------------------------------------------- */

#define MB_HEADER_MAGIC   0x1BADB002u   /* in the kernel image */
#define MB_BOOTLOADER_MAGIC 0x2BADB002u /* handed to the kernel in EAX */
#define MB_HEADER_SEARCH  8192          /* spec: within the first 8KB */

#define MB_HEADER_FLAG_AOUT_KLUDGE (1u << 16)

/* Boot information flags we populate. */
#define MB_INFO_MEMORY    (1u << 0)     /* mem_lower / mem_upper */
#define MB_INFO_CMDLINE   (1u << 2)
#define MB_INFO_MODS      (1u << 3)
#define MB_INFO_MMAP      (1u << 6)
#define MB_INFO_LOADER    (1u << 9)     /* boot_loader_name */
#define MB_INFO_FRAMEBUFFER (1u << 12)

/* Multiboot header flag: the kernel is asking for a video mode. */
#define MB_HEADER_FLAG_VIDEO (1u << 2)

/* framebuffer_type values. */
#define MB_FRAMEBUFFER_EGA_TEXT 2

/*
 * Where the boot information block goes. Low memory below the traditional
 * 1MB kernel load address, clear of the GDT and IDT the VMM builds at 0x500.
 */
#define MB_INFO_ADDR      0x00007000u
#define MB_MMAP_ADDR      0x00007100u
#define MB_CMDLINE_ADDR   0x00007400u
#define MB_LOADER_ADDR    0x00007600u
#define MB_MODLIST_ADDR   0x00007800u

/* Multiboot 1 boot information structure, through the fields we set. */
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
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

/*
 * One memory map entry. `size` counts the bytes that follow it, so an entry
 * occupies size + 4 bytes -- a quirk of the specification that exists so the
 * structure can grow.
 */
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

/* One entry per loaded module, as the specification lays it out. */
struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed));

#define MB_MEMORY_AVAILABLE 1
#define MB_MEMORY_RESERVED  2

const char *loader_format_name(guest_format_t format)
{
    switch (format) {
    case GUEST_FLAT:       return "flat binary";
    case GUEST_ELF:        return "ELF";
    case GUEST_MULTIBOOT:  return "Multiboot";
    case GUEST_MULTIBOOT2: return "Multiboot 2";
    default:               return "unknown";
    }
}

/* Read a whole file into memory. Caller frees. */
static unsigned char *read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': ", path);
        perror(NULL);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        perror("ftell");
        fclose(f);
        return NULL;
    }
    rewind(f);

    unsigned char *buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) {
        fprintf(stderr, "Error: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "Error: short read on '%s'\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size_out = (size_t)len;
    return buf;
}

static bool is_elf(const unsigned char *buf, size_t size)
{
    return size >= EI_NIDENT && memcmp(buf, ELFMAG, SELFMAG) == 0;
}

/*
 * Find a Multiboot 1 header: magic, 4-byte aligned, within the first 8KB,
 * with magic + flags + checksum summing to zero in 32-bit arithmetic.
 * Returns its offset, or -1.
 */
static long find_multiboot_header(const unsigned char *buf, size_t size)
{
    size_t limit = size < MB_HEADER_SEARCH ? size : MB_HEADER_SEARCH;

    for (size_t off = 0; off + 12 <= limit; off += 4) {
        uint32_t magic, flags, checksum;
        memcpy(&magic, buf + off, 4);
        if (magic != MB_HEADER_MAGIC) {
            continue;
        }
        memcpy(&flags, buf + off + 4, 4);
        memcpy(&checksum, buf + off + 8, 4);

        if ((uint32_t)(magic + flags + checksum) == 0) {
            return (long)off;
        }
    }
    return -1;
}

/*
 * Find a Multiboot 2 header: magic, 8-byte aligned, within the first 32KB,
 * with magic + architecture + length + checksum summing to zero.
 */
static long find_multiboot2_header(const unsigned char *buf, size_t size)
{
    size_t limit = size < MB2_HEADER_SEARCH ? size : MB2_HEADER_SEARCH;

    for (size_t off = 0; off + 16 <= limit; off += 8) {
        uint32_t magic, arch, length, checksum;
        memcpy(&magic, buf + off, 4);
        if (magic != MB2_HEADER_MAGIC) {
            continue;
        }
        memcpy(&arch, buf + off + 4, 4);
        memcpy(&length, buf + off + 8, 4);
        memcpy(&checksum, buf + off + 12, 4);

        if ((uint32_t)(magic + arch + length + checksum) == 0) {
            return (long)off;
        }
    }
    return -1;
}

int loader_probe(const char *path, guest_format_t *format)
{
    size_t size = 0;
    unsigned char *buf = read_file(path, &size);
    if (!buf) {
        return -1;
    }

    if (!is_elf(buf, size)) {
        *format = GUEST_FLAT;
    } else if (find_multiboot2_header(buf, size) >= 0) {
        /* Checked first: an image carrying both headers prefers the newer
         * protocol, which is what GRUB does. */
        *format = GUEST_MULTIBOOT2;
    } else if (find_multiboot_header(buf, size) >= 0) {
        *format = GUEST_MULTIBOOT;
    } else {
        *format = GUEST_ELF;
    }

    free(buf);
    return 0;
}

/*
 * Copy one PT_LOAD segment to its physical address, zeroing the tail that is
 * present in memory but not in the file (.bss).
 */
static int load_segment(void *mem, size_t mem_size, const unsigned char *file,
                        size_t file_size, uint64_t offset, uint64_t filesz,
                        uint64_t memsz, uint64_t paddr, guest_image_t *out)
{
    if (memsz == 0) {
        return 0;
    }
    if (filesz > memsz) {
        fprintf(stderr, "Error: malformed segment (file size %llu > memory size %llu)\n",
                (unsigned long long)filesz, (unsigned long long)memsz);
        return -1;
    }
    if (offset > file_size || filesz > file_size - offset) {
        fprintf(stderr, "Error: segment at file offset %llu+%llu runs past end of file\n",
                (unsigned long long)offset, (unsigned long long)filesz);
        return -1;
    }
    if (paddr > mem_size || memsz > mem_size - paddr) {
        fprintf(stderr,
                "Error: segment wants physical 0x%llx..0x%llx but guest memory is only %zu MB.\n",
                (unsigned long long)paddr, (unsigned long long)(paddr + memsz),
                mem_size / (1024 * 1024));
        return -1;
    }

    memcpy((char *)mem + paddr, file + offset, (size_t)filesz);
    memset((char *)mem + paddr + filesz, 0, (size_t)(memsz - filesz));

    if ((uint32_t)paddr < out->load_low) {
        out->load_low = (uint32_t)paddr;
    }
    if ((uint32_t)(paddr + memsz) > out->load_high) {
        out->load_high = (uint32_t)(paddr + memsz);
    }

    DEBUG_PRINT(DEBUG_DETAILED, "  segment -> 0x%08llx  %llu bytes (+%llu zeroed)",
                (unsigned long long)paddr, (unsigned long long)filesz,
                (unsigned long long)(memsz - filesz));
    return 0;
}

static int load_elf32(void *mem, size_t mem_size, const unsigned char *file,
                      size_t file_size, guest_image_t *out)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)file;

    if (file_size < sizeof(*eh) ||
        eh->e_phoff > file_size ||
        (uint64_t)eh->e_phnum * eh->e_phentsize > file_size - eh->e_phoff) {
        fprintf(stderr, "Error: ELF program headers are out of bounds\n");
        return -1;
    }

    uint32_t entry = eh->e_entry;
    bool entry_translated = false;

    for (unsigned i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph =
            (const Elf32_Phdr *)(file + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (load_segment(mem, mem_size, file, file_size, ph->p_offset,
                         ph->p_filesz, ph->p_memsz, ph->p_paddr, out) < 0) {
            return -1;
        }

        /*
         * A higher-half kernel links its code at a virtual address it cannot
         * reach yet -- 0xC0100000 is typical -- and uses AT() so the segment
         * loads at a low physical address. e_entry is then the *virtual*
         * entry, which is unmapped at the moment we jump to it, because
         * Multiboot hands the kernel a machine with paging off.
         *
         * The segment holding the entry point says how to fix it: the same
         * vaddr-to-paddr difference applies. This is what GRUB does, and
         * without it every higher-half kernel jumps into nothing.
         */
        if (!entry_translated &&
            eh->e_entry >= ph->p_vaddr &&
            eh->e_entry < ph->p_vaddr + ph->p_memsz) {
            entry = eh->e_entry - ph->p_vaddr + ph->p_paddr;
            entry_translated = true;
        }
    }

    if (entry_translated && entry != eh->e_entry) {
        DEBUG_PRINT(DEBUG_BASIC,
                    "higher-half image: entry 0x%08x translated to physical 0x%08x",
                    eh->e_entry, entry);
    }

    out->entry = entry;
    return 0;
}

static int load_elf64(void *mem, size_t mem_size, const unsigned char *file,
                      size_t file_size, guest_image_t *out)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)file;

    if (file_size < sizeof(*eh) ||
        eh->e_phoff > file_size ||
        (uint64_t)eh->e_phnum * eh->e_phentsize > file_size - eh->e_phoff) {
        fprintf(stderr, "Error: ELF program headers are out of bounds\n");
        return -1;
    }

    for (unsigned i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)(file + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) {
            continue;
        }
        if (load_segment(mem, mem_size, file, file_size, ph->p_offset,
                         ph->p_filesz, ph->p_memsz, ph->p_paddr, out) < 0) {
            return -1;
        }
    }

    if (eh->e_entry > 0xFFFFFFFFull) {
        fprintf(stderr, "Error: 64-bit entry point 0x%llx is out of range; Mini-KVM "
                        "enters ELF guests in 32-bit protected mode.\n",
                (unsigned long long)eh->e_entry);
        return -1;
    }
    out->entry = (uint32_t)eh->e_entry;
    return 0;
}

/*
 * Write one memory map entry and return the bytes consumed.
 */
static size_t write_mmap_entry(void *mem, uint32_t addr, uint64_t base,
                               uint64_t length, uint32_t type)
{
    struct multiboot_mmap_entry e = {
        .size = sizeof(struct multiboot_mmap_entry) - sizeof(uint32_t),
        .base_addr = base,
        .length = length,
        .type = type,
    };
    memcpy((char *)mem + addr, &e, sizeof(e));
    return sizeof(e);
}

/*
 * Build the Multiboot boot information block: memory sizes, a memory map, the
 * command line, and our name.
 */
/*
 * Load modules immediately above the kernel, page-aligned, and describe them
 * in a module list. Most Multiboot kernels take their root filesystem this
 * way, so a kernel that needs one simply fails without it.
 *
 * Returns the number loaded, or -1 on error. *end_out receives the first
 * address past the last module, so the kernel can tell where it may allocate.
 */
static int load_modules(void *mem, size_t mem_size, const char *const *modules,
                        int count, uint32_t place_at, uint32_t *end_out)
{
    uint32_t cursor = (place_at + 0xFFFu) & ~0xFFFu;      /* page align */
    struct multiboot_mod_list *list =
        (struct multiboot_mod_list *)((char *)mem + MB_MODLIST_ADDR);

    for (int i = 0; i < count; i++) {
        size_t size = 0;
        unsigned char *data = read_file(modules[i], &size);
        if (!data) {
            return -1;
        }
        if (cursor > mem_size || size > mem_size - cursor) {
            fprintf(stderr, "Error: module '%s' (%zu bytes) does not fit at 0x%08x\n",
                    modules[i], size, cursor);
            free(data);
            return -1;
        }

        memcpy((char *)mem + cursor, data, size);
        free(data);

        list[i].mod_start = cursor;
        list[i].mod_end = cursor + (uint32_t)size;
        list[i].cmdline = 0;
        list[i].pad = 0;

        if (verbose_enabled()) {
            printf("Module %d: %s -> 0x%08x-0x%08x (%zu bytes)\n",
                   i, modules[i], list[i].mod_start, list[i].mod_end, size);
        }

        cursor = (list[i].mod_end + 0xFFFu) & ~0xFFFu;
    }

    *end_out = cursor;
    return count;
}

static void build_multiboot_info(void *mem, size_t mem_size,
                                 const char *cmdline, int num_modules,
                                 bool wants_video, guest_image_t *out)
{
    /* Memory map. Mirrors a conventional PC: low memory, the EBDA and BIOS
     * areas carved out as reserved, then everything above 1MB. */
    uint32_t cursor = MB_MMAP_ADDR;
    cursor += (uint32_t)write_mmap_entry(mem, cursor, 0x00000000, 0x0009FC00,
                                         MB_MEMORY_AVAILABLE);
    cursor += (uint32_t)write_mmap_entry(mem, cursor, 0x0009FC00, 0x00000400,
                                         MB_MEMORY_RESERVED);   /* EBDA */
    cursor += (uint32_t)write_mmap_entry(mem, cursor, 0x000F0000, 0x00010000,
                                         MB_MEMORY_RESERVED);   /* BIOS ROM */
    cursor += (uint32_t)write_mmap_entry(mem, cursor, 0x00100000,
                                         (uint64_t)mem_size - 0x00100000,
                                         MB_MEMORY_AVAILABLE);

    const char *loader_name = "Mini-KVM";
    strcpy((char *)mem + MB_LOADER_ADDR, loader_name);

    struct multiboot_info info;
    memset(&info, 0, sizeof(info));
    info.flags = MB_INFO_MEMORY | MB_INFO_MMAP | MB_INFO_LOADER;

    /* Both are in kilobytes; upper memory is measured from 1MB. */
    info.mem_lower = 640;
    info.mem_upper = (uint32_t)((mem_size - 0x100000) / 1024);

    if (num_modules > 0) {
        info.flags |= MB_INFO_MODS;
        info.mods_count = (uint32_t)num_modules;
        info.mods_addr = MB_MODLIST_ADDR;
    }

    info.mmap_addr = MB_MMAP_ADDR;
    info.mmap_length = cursor - MB_MMAP_ADDR;
    info.boot_loader_name = MB_LOADER_ADDR;

    /*
     * A kernel that sets the video flag is asking to be told what display it
     * has. Mini-KVM renders text, not graphics, so it is answered with the
     * EGA text framebuffer the specification provides for exactly this case:
     * a bootloader may supply text mode when it cannot supply the graphics
     * mode requested. A kernel that insists on a linear framebuffer will
     * still not get one.
     */
    if (wants_video) {
        info.flags |= MB_INFO_FRAMEBUFFER;
        info.framebuffer_addr = 0xB8000;
        info.framebuffer_pitch = 80 * 2;
        info.framebuffer_width = 80;
        info.framebuffer_height = 25;
        info.framebuffer_bpp = 16;
        info.framebuffer_type = MB_FRAMEBUFFER_EGA_TEXT;
    }

    if (cmdline) {
        size_t n = strlen(cmdline);
        if (n > 0x200 - 1) {
            n = 0x200 - 1;
        }
        memcpy((char *)mem + MB_CMDLINE_ADDR, cmdline, n);
        ((char *)mem)[MB_CMDLINE_ADDR + n] = '\0';
        info.cmdline = MB_CMDLINE_ADDR;
        info.flags |= MB_INFO_CMDLINE;
    }

    memcpy((char *)mem + MB_INFO_ADDR, &info, sizeof(info));

    out->boot_eax = MB_BOOTLOADER_MAGIC;
    out->boot_ebx = MB_INFO_ADDR;
}

/*
 * Build the Multiboot 2 information block.
 *
 * Where Multiboot 1 had one fixed structure with a flags word, Multiboot 2
 * uses a length-prefixed list of tags, each 8-byte aligned, terminated by an
 * end tag. That is the whole difference from the loader's point of view: the
 * same facts, in an extensible container.
 */
static void build_multiboot2_info(void *mem, size_t mem_size,
                                  const char *cmdline, int num_modules,
                                  const struct multiboot_mod_list *mods,
                                  bool wants_video, guest_image_t *out)
{
    uint8_t *base = (uint8_t *)mem + MB2_INFO_ADDR;
    uint32_t off = 8;       /* total_size and reserved are filled in last */

    /* Append a tag header and return where its payload starts. */
    #define TAG_BEGIN(type_) \
        uint8_t *tag = base + off; \
        *(uint32_t *)(tag + 0) = (type_); \
        uint32_t tag_start = off; \
        off += 8;
    #define TAG_END() \
        *(uint32_t *)(base + tag_start + 4) = off - tag_start; \
        off = (off + 7u) & ~7u;     /* every tag starts 8-byte aligned */

    {   /* Basic memory information, in kilobytes. */
        TAG_BEGIN(MB2_TAG_BASIC_MEMINFO);
        *(uint32_t *)(base + off) = 640;                            off += 4;
        *(uint32_t *)(base + off) = (uint32_t)((mem_size - 0x100000) / 1024);
        off += 4;
        TAG_END();
    }

    if (cmdline) {
        TAG_BEGIN(MB2_TAG_CMDLINE);
        size_t n = strlen(cmdline);
        memcpy(base + off, cmdline, n + 1);
        off += (uint32_t)n + 1;
        TAG_END();
    }

    {   /* Who booted this. */
        TAG_BEGIN(MB2_TAG_LOADER_NAME);
        const char *name = "Mini-KVM";
        size_t n = strlen(name);
        memcpy(base + off, name, n + 1);
        off += (uint32_t)n + 1;
        TAG_END();
    }

    for (int i = 0; i < num_modules; i++) {
        TAG_BEGIN(MB2_TAG_MODULE);
        *(uint32_t *)(base + off) = mods[i].mod_start;  off += 4;
        *(uint32_t *)(base + off) = mods[i].mod_end;    off += 4;
        *(base + off) = 0;                              off += 1;   /* empty cmdline */
        TAG_END();
    }

    {   /* Memory map. Entry size is fixed at 24 and stated in the tag. */
        TAG_BEGIN(MB2_TAG_MMAP);
        *(uint32_t *)(base + off) = 24;     off += 4;   /* entry_size */
        *(uint32_t *)(base + off) = 0;      off += 4;   /* entry_version */

        struct { uint64_t addr, len; uint32_t type, zero; } entries[] = {
            { 0x00000000, 0x0009FC00, MB_MEMORY_AVAILABLE, 0 },
            { 0x0009FC00, 0x00000400, MB_MEMORY_RESERVED,  0 },
            { 0x000F0000, 0x00010000, MB_MEMORY_RESERVED,  0 },
            { 0x00100000, (uint64_t)mem_size - 0x100000, MB_MEMORY_AVAILABLE, 0 },
        };
        memcpy(base + off, entries, sizeof(entries));
        off += (uint32_t)sizeof(entries);
        TAG_END();
    }

    if (wants_video) {
        /* Same reasoning as Multiboot 1: answer with the text display we do
         * have rather than leaving the kernel to guess. */
        TAG_BEGIN(MB2_TAG_FRAMEBUFFER);
        *(uint64_t *)(base + off) = 0xB8000;    off += 8;   /* addr */
        *(uint32_t *)(base + off) = 80 * 2;     off += 4;   /* pitch */
        *(uint32_t *)(base + off) = 80;         off += 4;   /* width */
        *(uint32_t *)(base + off) = 25;         off += 4;   /* height */
        *(base + off) = 16;                     off += 1;   /* bpp */
        *(base + off) = MB_FRAMEBUFFER_EGA_TEXT; off += 1;  /* type */
        *(uint16_t *)(base + off) = 0;          off += 2;   /* reserved */
        TAG_END();
    }

    {   /* The list is terminated by an end tag of type 0, size 8. */
        TAG_BEGIN(MB2_TAG_END);
        TAG_END();
    }

    #undef TAG_BEGIN
    #undef TAG_END

    *(uint32_t *)(base + 0) = off;      /* total_size */
    *(uint32_t *)(base + 4) = 0;        /* reserved */

    if (off > MB2_INFO_MAX) {
        fprintf(stderr, "Warning: Multiboot 2 info block is %u bytes, past its "
                        "reserved %u.\n", off, MB2_INFO_MAX);
    }

    out->boot_eax = MB2_BOOTLOADER_MAGIC;
    out->boot_ebx = MB2_INFO_ADDR;
}

int loader_load(const char *path, void *mem, size_t mem_size,
                const char *cmdline, const char *const *modules, int num_modules,
                guest_image_t *out)
{
    size_t file_size = 0;
    unsigned char *file = read_file(path, &file_size);
    if (!file) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->load_low = UINT32_MAX;

    int rc = 0;

    if (!is_elf(file, file_size)) {
        /* Flat binary: the caller places it; nothing to interpret. */
        out->format = GUEST_FLAT;
        free(file);
        return 0;
    }

    long mb2_off = find_multiboot2_header(file, file_size);
    long mb_off = (mb2_off >= 0) ? -1 : find_multiboot_header(file, file_size);
    bool wants_video = false;

    if (mb2_off >= 0) {
        out->format = GUEST_MULTIBOOT2;
        /* A framebuffer request is a header tag rather than a flag bit.
         * Scanning for it is cheap and tells us whether to answer. */
        uint32_t hdr_len;
        memcpy(&hdr_len, file + mb2_off + 8, 4);
        for (uint32_t t = 16; t + 8 <= hdr_len && (size_t)mb2_off + t + 8 <= file_size; ) {
            uint16_t type;
            uint32_t size;
            memcpy(&type, file + mb2_off + t, 2);
            memcpy(&size, file + mb2_off + t + 4, 4);
            if (size < 8) {
                break;
            }
            if (type == 5) {            /* framebuffer request tag */
                wants_video = true;
            }
            if (type == 0) {
                break;                  /* end tag */
            }
            t += (size + 7u) & ~7u;
        }
        DEBUG_PRINT(DEBUG_BASIC, "Multiboot 2 header at file offset %ld", mb2_off);
    } else {
        out->format = (mb_off >= 0) ? GUEST_MULTIBOOT : GUEST_ELF;
    }

    if (mb_off >= 0) {
        uint32_t flags;
        memcpy(&flags, file + mb_off + 4, 4);
        wants_video = (flags & MB_HEADER_FLAG_VIDEO) != 0;
        if (flags & MB_HEADER_FLAG_AOUT_KLUDGE) {
            /* The kludge supplies its own load addresses for non-ELF images.
             * Refusing is better than loading it to the wrong place. */
            fprintf(stderr, "Error: this kernel uses the Multiboot a.out kludge, "
                            "which Mini-KVM does not implement yet.\n");
            free(file);
            return -1;
        }
        DEBUG_PRINT(DEBUG_BASIC, "Multiboot header at file offset %ld", mb_off);
    }

    if (file[EI_CLASS] == ELFCLASS32) {
        rc = load_elf32(mem, mem_size, file, file_size, out);
    } else if (file[EI_CLASS] == ELFCLASS64) {
        rc = load_elf64(mem, mem_size, file, file_size, out);
    } else {
        fprintf(stderr, "Error: unrecognised ELF class %u\n", file[EI_CLASS]);
        rc = -1;
    }

    free(file);
    if (rc < 0) {
        return -1;
    }

    if (out->load_low == UINT32_MAX) {
        fprintf(stderr, "Error: ELF image contains no loadable segments\n");
        return -1;
    }

    if (out->format == GUEST_MULTIBOOT || out->format == GUEST_MULTIBOOT2) {
        int loaded = 0;
        if (num_modules > 0) {
            uint32_t modules_end = 0;
            loaded = load_modules(mem, mem_size, modules, num_modules,
                                  out->load_high, &modules_end);
            if (loaded < 0) {
                return -1;
            }
            out->load_high = modules_end;
        }
        if (out->format == GUEST_MULTIBOOT2) {
            const struct multiboot_mod_list *mods =
                (const struct multiboot_mod_list *)((char *)mem + MB_MODLIST_ADDR);
            build_multiboot2_info(mem, mem_size, cmdline, loaded, mods,
                                  wants_video, out);
        } else {
            build_multiboot_info(mem, mem_size, cmdline, loaded, wants_video, out);
        }
    } else if (num_modules > 0) {
        fprintf(stderr, "Warning: --module is only meaningful for Multiboot "
                        "kernels; ignored.\n");
    }

    if (verbose_enabled()) {
        printf("Loaded %s: entry 0x%08x, physical 0x%08x-0x%08x\n",
               loader_format_name(out->format), out->entry,
               out->load_low, out->load_high);
    }

    return 0;
}
