/*
 * Linux Boot Protocol implementation for Mini-KVM
 *
 * Implements bzImage loading and boot parameter setup
 */

#include "linux_boot.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// E820 entry structure
struct e820_entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed));

/*
 * Add an E820 memory map entry
 */
void add_e820_entry(struct boot_params *boot_params, uint64_t addr,
                    uint64_t size, uint32_t type)
{
    if (boot_params->e820_entries >= 128) {
        fprintf(stderr, "Warning: E820 table full, entry not added\n");
        return;
    }
    
    struct e820_entry *entry = (struct e820_entry *)
        &boot_params->e820_map[boot_params->e820_entries * 20];
    
    entry->addr = addr;
    entry->size = size;
    entry->type = type;
    
    boot_params->e820_entries++;
    
    DEBUG_PRINT(DEBUG_DETAILED, "E820[%d]: 0x%016lx - 0x%016lx (type %d)",
                boot_params->e820_entries - 1, (unsigned long)addr, (unsigned long)(addr + size - 1), type);
}

/*
 * Setup Linux boot parameters (zero page)
 */
void setup_linux_boot_params(struct boot_params *boot_params, size_t mem_size,
                              const char *cmdline)
{
    // Preserve setup header parsed from bzImage
    struct linux_setup_header saved_hdr = boot_params->hdr;

    // Clear boot_params
    memset(boot_params, 0, sizeof(*boot_params));
    boot_params->hdr = saved_hdr;
    
    // Setup E820 memory map
    // Entry 0: Low memory (0 - 640KB)
    add_e820_entry(boot_params, 0, 640 * 1024, E820_RAM);
    
    // Entry 1: Reserved (640KB - 1MB) for BIOS/video
    add_e820_entry(boot_params, 640 * 1024, 384 * 1024, E820_RESERVED);
    
    // Entry 2: High memory (1MB - mem_size)
    if (mem_size > 1024 * 1024) {
        add_e820_entry(boot_params, 1024 * 1024, mem_size - 1024 * 1024, E820_RAM);
    }
    
    // Copy command line if provided
    if (cmdline) {
        boot_params->hdr.cmd_line_ptr = COMMAND_LINE_ADDR;
        DEBUG_PRINT(DEBUG_BASIC, "Command line: %s", cmdline);
    }
    
    // Set loader type
    boot_params->hdr.type_of_loader = LOADER_TYPE_UNDEFINED;
    boot_params->hdr.initrd_addr_max = INITRD_ADDR_MAX;
    
    DEBUG_PRINT(DEBUG_BASIC, "Boot parameters initialized (E820 entries: %d)",
                boot_params->e820_entries);
}

/*
 * Load Linux bzImage kernel
 * Returns 0 on success, -1 on error
 */
int load_linux_kernel(const char *bzimage_path, void *guest_mem, size_t mem_size,
                      struct boot_params *boot_params)
{
    int fd;
    struct stat st;
    ssize_t bytes_read;
    uint8_t *setup_buf;
    size_t setup_size;
    struct linux_setup_header *hdr;
    
    DEBUG_PRINT(DEBUG_BASIC, "Loading Linux kernel: %s", bzimage_path);
    
    // Open bzImage file
    fd = open(bzimage_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open kernel image '%s': %s\n",
                bzimage_path, strerror(errno));
        return -1;
    }
    
    // Get file size
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return -1;
    }
    
    DEBUG_PRINT(DEBUG_DETAILED, "Kernel image size: %ld bytes", st.st_size);
    
    // Read first 4KB to get full setup header and initial setup code
    setup_buf = malloc(4096);
    if (!setup_buf) {
        perror("malloc");
        close(fd);
        return -1;
    }
    
    bytes_read = read(fd, setup_buf, 4096);
    if (bytes_read < 512) {
        fprintf(stderr, "Failed to read setup sector\n");
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    // Parse setup header (starts at offset 0x1f1)
    hdr = (struct linux_setup_header *)(setup_buf + 0x1f1);
    
    // Verify boot signature
    if (hdr->boot_flag != 0xAA55) {
        fprintf(stderr, "Invalid boot signature: 0x%04x (expected 0xAA55)\n",
                hdr->boot_flag);
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    // Verify kernel magic
    if (hdr->header != LINUX_BOOT_SIGNATURE) {
        fprintf(stderr, "Invalid kernel signature: 0x%08x (expected 0x%08x)\n",
                hdr->header, LINUX_BOOT_SIGNATURE);
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    DEBUG_PRINT(DEBUG_DETAILED, "Boot protocol version: %d.%02d",
                hdr->version >> 8, hdr->version & 0xff);
    
    // Check if this is a bzImage (loaded high)
    if (!(hdr->loadflags & LOADED_HIGH)) {
        fprintf(stderr, "Kernel is not a bzImage (not LOADED_HIGH)\n");
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    // Calculate setup size
    if (hdr->setup_sects == 0) {
        setup_size = 4 * 512; // Default: 4 sectors
    } else {
        setup_size = (hdr->setup_sects + 1) * 512; // +1 for boot sector
    }
    
    DEBUG_PRINT(DEBUG_DETAILED, "Setup size: %zu bytes (%d sectors)",
                setup_size, hdr->setup_sects);
    DEBUG_PRINT(DEBUG_DETAILED, "Protected-mode code size: %u bytes",
                hdr->syssize * 16);
    DEBUG_PRINT(DEBUG_DETAILED, "32-bit entry point: 0x%08x", hdr->code32_start);
    
    // Check if kernel is 64-bit
    if (hdr->xloadflags & XLF_KERNEL_64) {
        DEBUG_PRINT(DEBUG_BASIC, "Kernel is 64-bit (Long Mode)");
    } else {
        DEBUG_PRINT(DEBUG_BASIC, "Kernel is 32-bit (Protected Mode)");
    }
    
    // Copy setup header to boot_params before freeing setup_buf
    memcpy(&boot_params->hdr, hdr, sizeof(struct linux_setup_header));
    
    // Rewind and read full setup
    lseek(fd, 0, SEEK_SET);
    free(setup_buf);
    setup_buf = malloc(setup_size);
    if (!setup_buf) {
        perror("malloc");
        close(fd);
        return -1;
    }
    
    bytes_read = read(fd, setup_buf, setup_size);
    if (bytes_read < (ssize_t)setup_size) {
        fprintf(stderr, "Failed to read setup code\n");
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    // Copy setup code to real-mode address (REAL_MODE_KERNEL_ADDR)
    if (REAL_MODE_KERNEL_ADDR + setup_size > mem_size) {
        fprintf(stderr, "Not enough memory for setup code\n");
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    memcpy((char *)guest_mem + REAL_MODE_KERNEL_ADDR, setup_buf, setup_size);
    DEBUG_PRINT(DEBUG_DETAILED, "Setup code copied to 0x%x", REAL_MODE_KERNEL_ADDR);
    
    // Read protected-mode kernel
    size_t kernel_size = st.st_size - setup_size;
    uint8_t *kernel_buf = malloc(kernel_size);
    if (!kernel_buf) {
        perror("malloc");
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    bytes_read = read(fd, kernel_buf, kernel_size);
    if (bytes_read < (ssize_t)kernel_size) {
        fprintf(stderr, "Failed to read kernel code\n");
        free(kernel_buf);
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    // Copy kernel to 1MB (KERNEL_LOAD_ADDR)
    if (KERNEL_LOAD_ADDR + kernel_size > mem_size) {
        fprintf(stderr, "Not enough memory for kernel (need %zu MB, have %zu MB)\n",
                (KERNEL_LOAD_ADDR + kernel_size) / (1024*1024),
                mem_size / (1024*1024));
        free(kernel_buf);
        free(setup_buf);
        close(fd);
        return -1;
    }
    
    memcpy((char *)guest_mem + KERNEL_LOAD_ADDR, kernel_buf, kernel_size);
    DEBUG_PRINT(DEBUG_DETAILED, "Kernel code copied to 0x%x (%zu bytes)",
                KERNEL_LOAD_ADDR, kernel_size);
    
    // Update entry point
    if (boot_params->hdr.code32_start == 0) {
        boot_params->hdr.code32_start = KERNEL_LOAD_ADDR;
    }
    
    DEBUG_PRINT(DEBUG_BASIC, "Linux kernel loaded successfully");
    DEBUG_PRINT(DEBUG_BASIC, "Entry point: 0x%08x", boot_params->hdr.code32_start);
    
    free(kernel_buf);
    free(setup_buf);
    close(fd);
    
    return 0;
}

/*
 * Load initrd image into guest memory and update boot params
 */
int load_initrd(const char *initrd_path, void *guest_mem, size_t mem_size,
                struct boot_params *boot_params)
{
    int fd = -1;
    struct stat st;
    ssize_t bytes_read;
    uint8_t *buf = NULL;
    uint64_t load_addr = 0;

    if (!initrd_path) {
        return 0;
    }

    fd = open(initrd_path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open initrd '%s': %s\n",
                initrd_path, strerror(errno));
        return -1;
    }

    if (fstat(fd, &st) < 0) {
        perror("fstat initrd");
        close(fd);
        return -1;
    }

    if (st.st_size <= 0) {
        fprintf(stderr, "Initrd is empty\n");
        close(fd);
        return -1;
    }

    if ((uint64_t)st.st_size > mem_size) {
        fprintf(stderr, "Not enough memory for initrd (size %ld bytes)\n", st.st_size);
        close(fd);
        return -1;
    }

    uint64_t max_end = boot_params->hdr.initrd_addr_max;
    if (max_end >= mem_size) {
        max_end = mem_size - 1;
    }

    uint64_t desired_end = max_end;
    if ((uint64_t)st.st_size > desired_end + 1) {
        fprintf(stderr, "Initrd too large for allowed range (size %ld, max_end 0x%llx)\n",
                (long)st.st_size, (unsigned long long)desired_end);
        close(fd);
        return -1;
    }
    uint64_t desired_start = (desired_end + 1) - (uint64_t)st.st_size;
    desired_start &= ~0xfffull; // 4KB align down

    uint64_t kernel_end = (uint64_t)KERNEL_LOAD_ADDR + (uint64_t)boot_params->hdr.init_size;
    if (desired_start < kernel_end) {
        fprintf(stderr,
                "Initrd placement overlaps kernel (kernel_end=0x%llx, initrd_size=%ld, mem=%zu)\n",
                (unsigned long long)kernel_end, (long)st.st_size, mem_size);
        close(fd);
        return -1;
    }

    load_addr = desired_start;

    buf = malloc(st.st_size);
    if (!buf) {
        perror("malloc initrd");
        close(fd);
        return -1;
    }

    bytes_read = read(fd, buf, st.st_size);
    if (bytes_read != st.st_size) {
        fprintf(stderr, "Failed to read initrd fully (read %ld of %ld)\n",
                (long)bytes_read, (long)st.st_size);
        free(buf);
        close(fd);
        return -1;
    }

    memcpy((char *)guest_mem + (size_t)load_addr, buf, st.st_size);

    boot_params->hdr.ramdisk_image = (uint32_t)load_addr;
    boot_params->hdr.ramdisk_size = (uint32_t)st.st_size;
    boot_params->hdr.initrd_addr_max = INITRD_ADDR_MAX;

    DEBUG_PRINT(DEBUG_BASIC, "Initrd loaded at 0x%llx (%ld bytes)", (unsigned long long)load_addr, (long)st.st_size);

    free(buf);
    close(fd);
    return 0;
}
