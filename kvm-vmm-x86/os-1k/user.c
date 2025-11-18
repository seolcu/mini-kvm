/*
 * user.c - User space library for syscalls
 *
 * x86 32-bit version using hypercall interface (port 0x500)
 */

#include "user.h"

extern char __stack_top[];

/*
 * Syscall interface via hypercall
 * Uses port 0x500 with syscall number in EAX
 * Arguments in EBX, ECX, EDX
 * Return value in EAX
 */
int syscall(int sysno, int arg0, int arg1, int arg2) {
    int ret;
    __asm__ volatile(
        "pushl %%ebx\n\t"         // Save EBX (callee-saved)
        "movl %1, %%eax\n\t"      // Syscall number
        "movl %2, %%ebx\n\t"      // Arg 0
        "movl %3, %%ecx\n\t"      // Arg 1
        "movl %4, %%edx\n\t"      // Arg 2
        "movw $0x500, %%dx\n\t"   // Port 0x500
        "outb %%al, %%dx\n\t"     // Trigger hypercall
        "movl %%eax, %0\n\t"      // Return value
        "popl %%ebx"              // Restore EBX
        : "=r"(ret)
        : "r"(sysno), "r"(arg0), "r"(arg1), "r"(arg2)
        : "eax", "ecx", "edx", "memory"
    );

    return ret;
}

void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}

int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}

int readfile(const char *filename, char *buf, int len) {
    return syscall(SYS_READFILE, (int) filename, (int) buf, len);
}

int writefile(const char *filename, const char *buf, int len) {
    return syscall(SYS_WRITEFILE, (int) filename, (int) buf, len);
}

__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;);
}

/*
 * User program entry point
 * Sets up stack and calls main()
 */
__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ volatile(
        "movl %[stack_top], %%esp\n\t"
        "call main\n\t"
        "call exit\n\t"
        :
        : [stack_top] "r"(__stack_top)
        : "memory"
    );
}
