/*
 * user.c - User space library for syscalls
 *
 * x86 32-bit version using hypercall interface (port 0x500)
 */

#include "user.h"

extern char __stack_top[];

/*
 * Syscall interface via hypercall (port 0x500)
 *
 * Direct hypercall to the VMM: IOPL=3 lets ring 3 execute OUT, so there is no
 * in-kernel syscall gate -- the VMM is the syscall handler.
 *
 *   EAX = syscall number, EBX/ECX = args
 *   EAX = return value, written back by the VMM before the guest resumes
 *
 * DX must be loaded with the port *last*. The previous version wrote the port
 * with `movw $0x500, %dx` after loading an argument into EDX, silently
 * destroying the low 16 bits of that argument.
 */
int syscall(int sysno, int arg0, int arg1) {
    int ret;
    __asm__ volatile(
        "outb %%al, %%dx\n\t"     // Trigger hypercall; VMM writes result to EAX
        : "=a"(ret)
        : "a"(sysno), "b"(arg0), "c"(arg1), "d"((short)0x500)
        : "memory"
    );

    return ret;
}

void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0);
}

/*
 * Blocking read of one character. The VMM parks this vCPU until a key is
 * available, so there is no retry loop and no spinning: an idle prompt costs
 * no host CPU. Returns -1 at end of input.
 */
int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0);
}

__attribute__((noreturn)) void exit(void) {
    syscall(SYS_EXIT, 0, 0);
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

        if (ch < 0) {
            // End of input. Nothing more will ever arrive, so quit instead of
            // looping forever -- this is what used to hang scripted runs that
            // did not end with an explicit exit command.
            putchar('\n');
            exit();
        }
        else if (ch == '\n' || ch == '\r') {
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
