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

struct process {
    int pid;
    int state;
    vaddr_t sp;
    uint32_t *page_table;
    uint8_t stack[8192];
};

/*
 * There is deliberately no trap frame and no syscall gate here. User code
 * runs with IOPL=3 and issues syscalls as a direct OUT to port 0x500, which
 * exits straight to the VMM -- the VMM *is* the syscall handler. The kernel
 * is never involved in a syscall.
 */

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
