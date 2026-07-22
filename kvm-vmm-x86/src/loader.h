/*
 * loader.h - guest image loading and format detection
 *
 * Mini-KVM decides how to start a guest from the image itself rather than
 * from a command line switch, because the format already says what the guest
 * expects:
 *
 *   flat binary   loaded verbatim; entered in real mode, or in protected mode
 *                 with paging when --paging is given
 *   ELF           program headers loaded at their physical addresses; entered
 *                 in 32-bit protected mode with paging off, at e_entry
 *   Multiboot     an ELF carrying a Multiboot 1 header. Same as ELF, plus the
 *                 boot information structure and the EAX/EBX handshake the
 *                 specification requires
 *   Multiboot 2   likewise, with the newer tag-based information block. An
 *                 image carrying both headers is treated as Multiboot 2,
 *                 which is what GRUB does
 *
 * Multiboot is what makes existing hobby kernels run unmodified: it is the
 * protocol GRUB implements and the one most of them are written against.
 */

#ifndef LOADER_H
#define LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GUEST_FLAT,         /* raw binary, no recognised container */
    GUEST_ELF,          /* ELF32/64 without a Multiboot header */
    GUEST_MULTIBOOT,    /* ELF with a Multiboot 1 header */
    GUEST_MULTIBOOT2,   /* ELF with a Multiboot 2 header */
} guest_format_t;

typedef struct {
    guest_format_t format;
    uint32_t entry;         /* guest physical entry point */
    uint32_t boot_eax;      /* EAX at entry (Multiboot bootloader magic) */
    uint32_t boot_ebx;      /* EBX at entry (boot information structure) */
    uint32_t load_low;      /* lowest physical address written */
    uint32_t load_high;     /* one past the highest */
} guest_image_t;

/* Human-readable format name, for diagnostics. */
const char *loader_format_name(guest_format_t format);

/*
 * Identify the image without loading it. Reads only as much as it takes to
 * recognise the container, so this can run before guest memory is allocated
 * and thus decide how much to allocate.
 */
int loader_probe(const char *path, guest_format_t *format);

/*
 * Load the image into an allocated guest memory mapping and fill in *out.
 * cmdline may be NULL. Fails rather than writing outside the mapping.
 */
int loader_load(const char *path, void *mem, size_t mem_size,
                const char *cmdline, const char *const *modules, int num_modules,
                guest_image_t *out);

#endif /* LOADER_H */
