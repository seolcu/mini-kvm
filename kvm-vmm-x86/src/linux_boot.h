/*
 * Linux Boot Protocol structures and constants
 *
 * Based on Linux Documentation/x86/boot.rst
 * Supports bzImage format (protected mode kernel)
 */

#ifndef LINUX_BOOT_H
#define LINUX_BOOT_H

#include <stdint.h>
#include <stddef.h>

// Linux kernel boot signature
#define LINUX_BOOT_SIGNATURE    0x53726448  // "HdrS"

// Boot protocol versions
#define BOOT_PROTOCOL_2_00      0x0200
#define BOOT_PROTOCOL_2_02      0x0202
#define BOOT_PROTOCOL_2_10      0x0210

// Loader type IDs (we use 0xFF for "undefined")
#define LOADER_TYPE_UNDEFINED   0xFF

// Kernel load addresses (for bzImage)
#define KERNEL_LOAD_ADDR        0x100000    // 1MB (protected mode kernel)
#define REAL_MODE_KERNEL_ADDR   0x90000     // Real-mode kernel setup
#define COMMAND_LINE_ADDR       0x20000     // Command line location
#define INITRD_ADDR_MAX         0x37FFFFFF  // Max initrd address (<896MB)

// Memory layout
#define E820_MAP_ADDR           0x2d0       // E820 memory map location in real-mode data

/*
 * Linux kernel setup header (at offset 0x01f1 in bzImage)
 * This is the "real-mode kernel header" that the bootloader reads
 */
struct linux_setup_header {
    uint8_t  setup_sects;       // 0x01f1: Size of setup in 512-byte sectors
    uint16_t root_flags;        // 0x01f2: Root readonly flag
    uint32_t syssize;           // 0x01f4: Size of protected-mode code (16-byte paras)
    uint16_t ram_size;          // 0x01f8: Obsolete
    uint16_t vid_mode;          // 0x01fa: Video mode
    uint16_t root_dev;          // 0x01fc: Root device number
    uint16_t boot_flag;         // 0x01fe: 0xAA55 boot signature
    
    // Protocol 2.00+
    uint16_t jump;              // 0x0200: Jump instruction
    uint32_t header;            // 0x0202: Magic "HdrS"
    uint16_t version;           // 0x0206: Boot protocol version
    uint32_t realmode_swtch;    // 0x0208: Real-mode switch routine
    uint16_t start_sys_seg;     // 0x020c: Obsolete
    uint16_t kernel_version;    // 0x020e: Pointer to kernel version string
    uint8_t  type_of_loader;    // 0x0210: Loader ID
    uint8_t  loadflags;         // 0x0211: Load flags
    uint16_t setup_move_size;   // 0x0212: Move size
    uint32_t code32_start;      // 0x0214: 32-bit entry point
    uint32_t ramdisk_image;     // 0x0218: initrd load address
    uint32_t ramdisk_size;      // 0x021c: initrd size
    uint32_t bootsect_kludge;   // 0x0220: Obsolete
    
    // Protocol 2.01+
    uint16_t heap_end_ptr;      // 0x0224: Heap end pointer
    uint8_t  ext_loader_ver;    // 0x0226: Extended loader version
    uint8_t  ext_loader_type;   // 0x0227: Extended loader type
    uint32_t cmd_line_ptr;      // 0x0228: Command line pointer
    
    // Protocol 2.02+
    uint32_t initrd_addr_max;   // 0x022c: Max initrd address
    
    // Protocol 2.03+
    uint32_t kernel_alignment;  // 0x0230: Kernel alignment
    uint8_t  relocatable_kernel;// 0x0234: Kernel is relocatable
    uint8_t  min_alignment;     // 0x0235: Minimum alignment (power of 2)
    uint16_t xloadflags;        // 0x0236: Extended load flags
    
    // Protocol 2.04+
    uint32_t cmdline_size;      // 0x0238: Max command line size
    
    // Protocol 2.05+
    uint32_t hardware_subarch;  // 0x023c: Hardware subarchitecture
    uint64_t hardware_subarch_data; // 0x0240: Subarch-specific data
    
    // Protocol 2.06+
    uint32_t payload_offset;    // 0x0248: Offset to compressed payload
    uint32_t payload_length;    // 0x024c: Length of compressed payload
    
    // Protocol 2.07+
    uint64_t setup_data;        // 0x0250: Pointer to setup_data chain
    uint64_t pref_address;      // 0x0258: Preferred load address
    uint32_t init_size;         // 0x0260: Kernel initialization size
    
    // Protocol 2.08+
    uint32_t handover_offset;   // 0x0264: EFI handover protocol offset
} __attribute__((packed));

// Load flags (setup_header.loadflags)
#define LOADED_HIGH             (1 << 0)    // Loaded at 0x100000 (bzImage)
#define KASLR_FLAG              (1 << 1)    // Kernel supports KASLR
#define QUIET_FLAG              (1 << 5)    // Suppress early messages
#define KEEP_SEGMENTS           (1 << 6)    // Don't reload segment registers
#define CAN_USE_HEAP            (1 << 7)    // Heap end pointer is valid

// Extended load flags (setup_header.xloadflags)
#define XLF_KERNEL_64           (1 << 0)    // 64-bit kernel
#define XLF_CAN_BE_LOADED_ABOVE_4G (1 << 1) // Can load above 4GB
#define XLF_EFI_HANDOVER_32     (1 << 2)    // 32-bit EFI handover
#define XLF_EFI_HANDOVER_64     (1 << 3)    // 64-bit EFI handover
#define XLF_EFI_KEXEC           (1 << 4)    // kexec EFI runtime support

/*
 * Real-mode kernel parameters (zero page)
 * The bootloader fills this in and passes it to the kernel
 */
struct boot_params {
    // Screen info (0x000 - 0x03f)
    uint8_t  screen_info[0x40];
    
    // APM BIOS info (0x040 - 0x053)
    uint8_t  apm_bios_info[0x14];
    
    uint8_t  _pad1[4];          // 0x054
    
    // Drive info (0x058 - 0x07f)
    uint8_t  tboot_addr[8];     // 0x058
    uint8_t  ist_info[16];      // 0x060
    uint8_t  _pad2[16];         // 0x070
    
    // System descriptor table (0x080 - 0x0af)
    uint8_t  hd0_info[16];      // 0x080
    uint8_t  hd1_info[16];      // 0x090
    uint8_t  sys_desc_table[16];// 0x0a0
    
    // Old loader parameter area (0x0b0 - 0x1ef)
    uint8_t  olpc_ofw_header[16]; // 0x0b0
    uint8_t  _pad3[0x140];        // 0x0c0
    
    // E820 memory map (0x1e8 - 0x1ef)
    uint8_t  e820_entries;      // 0x1e8: Number of E820 entries
    uint8_t  eddbuf_entries;    // 0x1e9: Number of EDD entries
    uint8_t  edd_mbr_sig_buf_entries; // 0x1ea
    uint8_t  kbd_status;        // 0x1eb
    uint8_t  _pad4[4];          // 0x1ec
    
    // Setup header starts at 0x1f1
    struct linux_setup_header hdr; // 0x1f1
    
    uint8_t  _pad5[0x290 - 0x1f1 - sizeof(struct linux_setup_header)];
    
    // EDD info (0x290 - 0x2cf)
    uint32_t edd_mbr_sig_buffer[16]; // 0x290
    
    // E820 memory map entries (0x2d0 - 0x????)
    // Each entry is 20 bytes: addr (8), size (8), type (4)
    uint8_t  e820_map[20 * 128]; // 0x2d0: Up to 128 entries
} __attribute__((packed));

// E820 memory types
#define E820_RAM        1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_UNUSABLE   5

// Function declarations
int load_linux_kernel(const char *bzimage_path, void *guest_mem, size_t mem_size,
                      struct boot_params *boot_params);
void setup_linux_boot_params(struct boot_params *boot_params, size_t mem_size,
                              const char *cmdline);
void add_e820_entry(struct boot_params *boot_params, uint64_t addr,
                    uint64_t size, uint32_t type);

#endif // LINUX_BOOT_H
