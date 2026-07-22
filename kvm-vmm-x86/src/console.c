/*
 * console.c - host terminal, keyboard input, and vCPU-tagged output
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <pthread.h>
#include <sys/select.h>

#include "console.h"

#define KEYBOARD_BUFFER_SIZE 256
#define MAX_COLORS 8

/* --- Terminal state --------------------------------------------------- */

static struct termios orig_termios;
static bool termios_saved = false;
static volatile sig_atomic_t shutdown_flag = 0;

/* --- Keyboard ring ---------------------------------------------------- */

/*
 * Guarded by kbd_lock. kbd_cond is signalled whenever a character arrives or
 * shutdown is requested, so console_wait_char() can block instead of spin.
 */
static struct {
    char buffer[KEYBOARD_BUFFER_SIZE];
    int head;               /* write position */
    int tail;               /* read position */
    bool stdin_eof;         /* no more input will ever arrive */
    pthread_mutex_t lock;
    pthread_cond_t cond;
} kbd = {
    .head = 0,
    .tail = 0,
    .stdin_eof = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
};

static pthread_t stdin_thread;
static bool stdin_thread_running = false;
static void (*input_wakeup)(void) = NULL;

/* --- Output state ----------------------------------------------------- */

static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;
static int vcpu_colors[MAX_COLORS];
static int color_count = 0;

/* ====================================================================== */
/* Terminal                                                               */
/* ====================================================================== */

void console_enable_raw_mode(void)
{
    if (!isatty(STDIN_FILENO) || termios_saved) {
        /* Piped input: leave the tty alone so scripted runs stay reproducible. */
        return;
    }

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        return;
    }
    termios_saved = true;

    struct termios raw = orig_termios;

    /* ICANON off: deliver keys immediately. ECHO off: the guest echoes.
     * ISIG off: the guest sees Ctrl-C as a byte -- which is why
     * console_install_signal_handlers() exists. */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);

    /* OPOST stays ON deliberately: it supplies the \n -> \r\n translation
     * that guest output depends on. Clearing it breaks every line break. */

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        termios_saved = false;
    }
}

void console_restore(void)
{
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_saved = false;
    }
}

bool console_shutdown_requested(void)
{
    return shutdown_flag != 0;
}

void console_request_shutdown(void)
{
    shutdown_flag = 1;
    pthread_mutex_lock(&kbd.lock);
    pthread_cond_broadcast(&kbd.cond);
    pthread_mutex_unlock(&kbd.lock);
}

/*
 * Async-signal-safe: only sets a flag and restores termios. The condvar
 * broadcast that unblocks readers is not signal-safe, so instead
 * console_wait_char() polls the flag on a bounded timed wait.
 */
static void on_terminate_signal(int signo)
{
    (void)signo;
    shutdown_flag = 1;
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        termios_saved = false;
    }
}

void console_install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_terminate_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;            /* no SA_RESTART: let blocking reads return EINTR */

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

/* ====================================================================== */
/* Keyboard input                                                         */
/* ====================================================================== */

static void keyboard_push(char ch)
{
    pthread_mutex_lock(&kbd.lock);
    int next_head = (kbd.head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_head != kbd.tail) {
        kbd.buffer[kbd.head] = ch;
        kbd.head = next_head;
    }
    /* Ring full: drop the character, same as a real UART overrun. */
    pthread_cond_signal(&kbd.cond);
    pthread_mutex_unlock(&kbd.lock);
}

/* Caller must hold kbd.lock. */
static int keyboard_pop_locked(void)
{
    if (kbd.head == kbd.tail) {
        return -1;
    }
    char ch = kbd.buffer[kbd.tail];
    kbd.tail = (kbd.tail + 1) % KEYBOARD_BUFFER_SIZE;
    return (unsigned char)ch;
}

int console_poll_char(void)
{
    pthread_mutex_lock(&kbd.lock);
    int ch = keyboard_pop_locked();
    pthread_mutex_unlock(&kbd.lock);
    return ch;
}

int console_wait_char(void)
{
    int ch = -1;

    pthread_mutex_lock(&kbd.lock);
    while (!shutdown_flag) {
        ch = keyboard_pop_locked();
        if (ch >= 0) {
            break;
        }
        if (kbd.stdin_eof) {
            /* Nothing buffered and nothing more coming. Returning -1 lets the
             * guest see EOF rather than spinning forever, which is what used
             * to hang scripted runs that did not end in an exit command. */
            break;
        }
        pthread_cond_wait(&kbd.cond, &kbd.lock);
    }
    pthread_mutex_unlock(&kbd.lock);

    return shutdown_flag ? -1 : ch;
}

bool console_has_input(void)
{
    pthread_mutex_lock(&kbd.lock);
    bool has = (kbd.head != kbd.tail);
    pthread_mutex_unlock(&kbd.lock);
    return has;
}

static void *stdin_monitor_thread_func(void *arg)
{
    (void)arg;

    while (stdin_thread_running && !shutdown_flag) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;    /* 100ms, so we notice stdin_thread_running */

        int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ret == 0 || !FD_ISSET(STDIN_FILENO, &readfds)) {
            continue;
        }

        char buf[256];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                keyboard_push(buf[i]);
            }
            if (input_wakeup) {
                input_wakeup();
            }
        } else if (n == 0) {
            /* EOF. Wake any blocked reader so it can return -1. */
            pthread_mutex_lock(&kbd.lock);
            kbd.stdin_eof = true;
            pthread_cond_broadcast(&kbd.cond);
            pthread_mutex_unlock(&kbd.lock);
            break;
        } else if (errno != EINTR && errno != EAGAIN) {
            break;
        }
    }

    return NULL;
}

int console_start_input_thread(void (*wake_irq4)(void))
{
    if (stdin_thread_running) {
        return 0;
    }
    input_wakeup = wake_irq4;
    stdin_thread_running = true;

    if (pthread_create(&stdin_thread, NULL, stdin_monitor_thread_func, NULL) != 0) {
        fprintf(stderr, "Warning: failed to start stdin thread; keyboard input disabled.\n");
        stdin_thread_running = false;
        input_wakeup = NULL;
        return -1;
    }
    return 0;
}

void console_stop_input_thread(void)
{
    if (!stdin_thread_running) {
        return;
    }
    stdin_thread_running = false;
    pthread_join(stdin_thread, NULL);
    input_wakeup = NULL;
}

/* ====================================================================== */
/* Output                                                                 */
/* ====================================================================== */

/*
 * Map a hue (0-360) onto the ANSI 256-color cube (codes 16-231).
 */
static int hue_to_ansi256(int hue)
{
    hue = hue % 360;
    if (hue < 0) {
        hue += 360;
    }

    int r, g, b;
    int sector = hue / 60;
    int offset = hue % 60;

    switch (sector) {
    case 0:  r = 5;                  g = offset * 5 / 60;     b = 0;                  break;
    case 1:  r = 5 - offset * 5 / 60; g = 5;                  b = 0;                  break;
    case 2:  r = 0;                  g = 5;                   b = offset * 5 / 60;    break;
    case 3:  r = 0;                  g = 5 - offset * 5 / 60; b = 5;                  break;
    case 4:  r = offset * 5 / 60;    g = 0;                   b = 5;                  break;
    default: r = 5;                  g = 0;                   b = 5 - offset * 5 / 60; break;
    }

    return 16 + 36 * r + 6 * g + b;
}

void console_init_colors(int n_vcpus)
{
    /* Span 300 degrees starting at green so no vCPU is red -- red output in a
     * demo reads as an error. */
    const int start_hue = 120;
    const int span = 300;

    color_count = (n_vcpus < MAX_COLORS) ? n_vcpus : MAX_COLORS;
    for (int i = 0; i < color_count; i++) {
        vcpu_colors[i] = hue_to_ansi256(start_hue + (i * span) / color_count);
    }
}

static int color_for(int vcpu_id)
{
    if (vcpu_id < 0 || vcpu_id >= color_count) {
        return 7;
    }
    return vcpu_colors[vcpu_id];
}

/* True when output should be color-tagged, i.e. more than one vCPU. */
static bool tagging_enabled(void)
{
    return color_count > 1;
}

void console_print_legend(int n_vcpus, const char *const *names)
{
    if (n_vcpus <= 1) {
        return;
    }
    pthread_mutex_lock(&stdout_mutex);
    printf("Legend: ");
    for (int i = 0; i < n_vcpus; i++) {
        printf("\033[38;5;%dm[%s]\033[0m ", color_for(i), names[i]);
    }
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&stdout_mutex);
}

void console_vcpu_printf(int vcpu_id, const char *name, const char *fmt, ...)
{
    pthread_mutex_lock(&stdout_mutex);

    if (tagging_enabled()) {
        printf("\033[38;5;%dm[vCPU %d:%s]\033[0m ", color_for(vcpu_id), vcpu_id, name);
    } else {
        printf("[%s] ", name);
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
    pthread_mutex_unlock(&stdout_mutex);
}

void console_vcpu_putchar(int vcpu_id, const char *name, char ch)
{
    (void)name;

    pthread_mutex_lock(&stdout_mutex);
    if (tagging_enabled()) {
        printf("\033[38;5;%dm%c\033[0m", color_for(vcpu_id), ch);
    } else {
        putchar(ch);
    }
    fflush(stdout);
    pthread_mutex_unlock(&stdout_mutex);
}

void console_putchar_raw(char ch)
{
    pthread_mutex_lock(&stdout_mutex);
    putchar(ch);
    fflush(stdout);
    pthread_mutex_unlock(&stdout_mutex);
}
