#pragma once
#include "common.h"

/* Process management */
#define PROCS_MAX 8
#define PROC_UNUSED   0
#define PROC_RUNNABLE 1
#define PROC_EXITED   2

/* x86 32-bit paging flags */
#define PAGE_P    (1 << 0)  // Present
#define PAGE_RW   (1 << 1)  // Read/Write
#define PAGE_U    (1 << 2)  // User/Supervisor
#define PAGE_PS   (1 << 7)  // Page Size (4MB)

/* User space base address */
#define USER_BASE 0x01000000

/* Filesystem */
#define FILES_MAX   2
#define SECTOR_SIZE       512
#define DISK_MAX_SIZE     (((sizeof(struct file) * FILES_MAX) + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1))

struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[];
} __attribute__((packed));

struct file {
    bool in_use;
    char name[100];
    char data[1024];
    size_t size;
};

/* Trap frame for syscalls (pushed by INT 0x80 handler) */
struct trap_frame {
    uint32_t eax;   // Syscall number / return value
    uint32_t ebx;   // Arg 0
    uint32_t ecx;   // Arg 1
    uint32_t edx;   // Arg 2
} __attribute__((packed));

/* Interrupt frame (passed by CPU to interrupt handlers) */
struct interrupt_frame {
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} __attribute__((packed));

/* I/O port functions for keyboard and PIC */
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile(
        "inb %1, %0"
        : "=a"(result)
        : "Nd"(port)
    );
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) { __asm__ volatile("hlt"); }                                 \
    } while (0)
