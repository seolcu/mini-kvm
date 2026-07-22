/*
 * console.h - host terminal, keyboard input, and vCPU-tagged output
 *
 * Owns everything that touches the controlling terminal:
 *   - raw mode (enabled only for interactive guests; see AGENTS.md)
 *   - the keyboard ring buffer filled by the stdin monitor thread
 *   - thread-safe, color-tagged output on behalf of each vCPU
 *
 * All state is private to console.c. Callers identify a vCPU by id and name
 * rather than passing a vcpu_context_t, so this module stays independent of
 * the VM core.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

/* --- Terminal -------------------------------------------------------- */

/*
 * Switch the terminal to raw mode so guests receive keystrokes immediately.
 * No-op when stdin is not a tty (piped input), which is what makes the
 * scripted 1K OS tests deterministic. Safe to call when already raw.
 */
void console_enable_raw_mode(void);

/* Restore the terminal. Idempotent; also runs from the signal handler. */
void console_restore(void);

/*
 * Install SIGINT/SIGTERM handlers that restore the terminal and unblock any
 * vCPU waiting in console_wait_char(). Without this, raw mode (which clears
 * ISIG) makes an interactive guest impossible to interrupt.
 */
void console_install_signal_handlers(void);

/* True once a termination signal has been received. */
bool console_shutdown_requested(void);

/* Ask any blocked reader to give up, as if a signal had arrived. */
void console_request_shutdown(void);

/* --- Keyboard input -------------------------------------------------- */

/*
 * Start the stdin monitor thread. It reads stdin and feeds the keyboard
 * ring. If wake_irq4 is non-NULL it is invoked after each batch of input,
 * which the Linux serial console uses to raise COM1 IRQ4.
 */
int console_start_input_thread(void (*wake_irq4)(void));
void console_stop_input_thread(void);

/* Pop one character, or -1 if none buffered. Never blocks. */
int console_poll_char(void);

/*
 * Block until a character is available, then return it. Returns -1 if
 * shutdown was requested or stdin reached EOF with nothing buffered.
 * This replaces the guest-side busy-wait loops.
 */
int console_wait_char(void);

/* True if at least one character is buffered. */
bool console_has_input(void);

/* --- Output ---------------------------------------------------------- */

/*
 * Assign display colors for n vCPUs. A single vCPU prints an uncolored
 * "[name]" prefix; multiple vCPUs get distinct hues so interleaved output is
 * readable in demos.
 */
void console_init_colors(int n_vcpus);

/* Print the "Legend: [a] [b] ..." banner. No-op for a single vCPU. */
void console_print_legend(int n_vcpus, const char *const *names);

/* Printf on behalf of a vCPU, with an identifying prefix. Thread-safe. */
void console_vcpu_printf(int vcpu_id, const char *name, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/*
 * Emit one guest character. Unbuffered and character-at-a-time on purpose:
 * it is what makes concurrent vCPU output visibly interleave.
 */
void console_vcpu_putchar(int vcpu_id, const char *name, char ch);

/* Emit one character with no vCPU tagging (UART pass-through). */
void console_putchar_raw(char ch);

#endif /* CONSOLE_H */
