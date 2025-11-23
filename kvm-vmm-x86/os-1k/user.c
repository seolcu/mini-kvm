/*
 * user.c - User space library for syscalls
 *
 * x86 32-bit version using hypercall interface (port 0x500)
 */

#include "user.h"

extern char __stack_top[];

/*
 * Syscall interface via hypercall (port 0x500)
 * Direct hypercall to VMM - IOPL=3 allows user space to use OUT instruction
 * Arguments: EAX=syscall number, EBX=arg0, ECX=arg1, EDX=arg2
 * Return value in EAX (set by VMM via KVM_SET_REGS after hypercall)
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
        "outb %%al, %%dx\n\t"     // Trigger hypercall (VMM handles & sets RAX)
        "movl %%eax, %0\n\t"      // Return value (RAX modified by VMM)
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
    int ch;
    // Blocking getchar: retry until we get a valid character
    while (1) {
        __asm__ volatile(
            "movb $2, %%al\n\t"         // SYS_GETCHAR
            "movw $0x500, %%dx\n\t"     // Port 0x500
            "outb %%al, %%dx\n\t"       // Signal GETCHAR request to VMM
            "inb %%dx, %%al\n\t"        // Read character from VMM
            "movsbl %%al, %0"           // Sign-extend AL to int
            : "=r"(ch)
            :
            : "eax", "edx"
        );
        
        if (ch != -1) {
            return ch;
        }
        
        // No input yet, brief delay and retry
        for (volatile int i = 0; i < 1000; i++);
    }
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
 * Read a line of input with echo and backspace support
 * Supports ASCII backspace (0x08) and DEL (0x7F)
 * Returns length of input (excluding null terminator)
 */
int readline(char *buf, int bufsz) {
    int pos = 0;
    
    while (pos < bufsz - 1) {  // Reserve space for null terminator
        int ch = getchar();
        
        if (ch == '\n' || ch == '\r') {
            putchar('\n');
            buf[pos] = '\0';
            return pos;
        } 
        else if (ch == 0x08 || ch == 0x7F) {  // Backspace or DEL
            if (pos > 0) {
                pos--;
                // Erase character: backspace, space, backspace
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        }
        else if (ch >= 0x20 && ch < 0x7F) {  // Printable ASCII
            putchar((char)ch);  // Echo character
            buf[pos++] = (char)ch;
        }
        // Ignore other control characters
    }
    
    // Buffer full - force termination
    buf[bufsz - 1] = '\0';
    
    // Consume remaining input until newline
    int ch;
    while ((ch = getchar()) != '\n' && ch != '\r' && ch != -1) {
        // Discard
    }
    putchar('\n');
    
    return bufsz - 1;
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
