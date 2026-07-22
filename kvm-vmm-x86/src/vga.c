/*
 * vga.c - VGA text mode (80x25) display
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "vga.h"
#include "console.h"

/* ~30 fps. Fast enough to look live, slow enough to cost nothing. */
#define REFRESH_NS (33 * 1000 * 1000L)

static const uint8_t *vga_mem = NULL;    /* host view of guest 0xB8000 */
static uint8_t shadow[VGA_BYTES];        /* last frame we drew */
static bool shadow_valid = false;

static pthread_t render_thread;
static volatile bool running = false;
static bool to_terminal = false;

/*
 * VGA's 16 colors are not in the same order as ANSI's. VGA counts
 * black, blue, green, cyan, red, magenta, brown, light grey, then the bright
 * half; ANSI counts black, red, green, yellow, blue, magenta, cyan, white.
 */
static const uint8_t vga_to_ansi[16] = {
    0, 4, 2, 6, 1, 5, 3, 7,
    8, 12, 10, 14, 9, 13, 11, 15,
};

bool vga_active(void)
{
    return running;
}

/*
 * Render one frame into buf as an ANSI escape sequence that repaints the
 * whole grid. Colour codes are emitted only when the attribute changes, which
 * keeps a typical frame to a few kilobytes. Returns the length written.
 */
static size_t build_frame(char *buf, size_t cap)
{
    size_t n = 0;
    int last_attr = -1;

    /* Home the cursor rather than clearing: repainting every cell avoids the
     * flicker a clear-then-draw cycle produces. */
    n += (size_t)snprintf(buf + n, cap - n, "\033[H");

    for (int row = 0; row < VGA_ROWS && n + 64 < cap; row++) {
        for (int col = 0; col < VGA_COLS && n + 64 < cap; col++) {
            size_t off = (size_t)(row * VGA_COLS + col) * 2;
            unsigned char ch = vga_mem[off];
            unsigned char attr = vga_mem[off + 1];

            if (attr != last_attr) {
                n += (size_t)snprintf(buf + n, cap - n, "\033[38;5;%um\033[48;5;%um",
                                      vga_to_ansi[attr & 0x0F],
                                      vga_to_ansi[(attr >> 4) & 0x07]);
                last_attr = attr;
            }

            /* Control characters would move the cursor and corrupt the grid;
             * a NUL cell is just an unwritten one. */
            if (ch < 0x20 || ch == 0x7F) {
                ch = ' ';
            }
            buf[n++] = (char)ch;
        }
        if (row < VGA_ROWS - 1 && n + 2 < cap) {
            buf[n++] = '\r';
            buf[n++] = '\n';
        }
    }

    n += (size_t)snprintf(buf + n, cap - n, "\033[0m");
    return n;
}

static void *render_loop(void *arg)
{
    (void)arg;

    /* Generous: worst case is a colour change on every one of 2000 cells. */
    static char frame[VGA_BYTES * 24 + 256];

    while (running) {
        /* Redirected output gets one plain-text dump at the end instead of a
         * stream of escape sequences, so scripted runs stay diffable. */
        if (to_terminal &&
            (!shadow_valid || memcmp(shadow, vga_mem, VGA_BYTES) != 0)) {
            memcpy(shadow, vga_mem, VGA_BYTES);
            shadow_valid = true;
            size_t len = build_frame(frame, sizeof(frame));
            console_write_raw(frame, len);
        }

        struct timespec ts = { .tv_sec = 0, .tv_nsec = REFRESH_NS };
        nanosleep(&ts, NULL);
    }

    return NULL;
}

/*
 * Print the final screen as plain text: no escapes, trailing blanks trimmed,
 * fully blank trailing rows dropped. This is the form a test can diff.
 */
static void dump_plain(void)
{
    if (!vga_mem) {
        return;
    }

    /* Find the last row with any visible content. */
    int last_row = -1;
    for (int row = 0; row < VGA_ROWS; row++) {
        for (int col = 0; col < VGA_COLS; col++) {
            unsigned char ch = vga_mem[(size_t)(row * VGA_COLS + col) * 2];
            if (ch != 0 && ch != ' ') {
                last_row = row;
                break;
            }
        }
    }
    if (last_row < 0) {
        return;             /* screen never written */
    }

    char line[VGA_COLS + 2];
    for (int row = 0; row <= last_row; row++) {
        int len = 0;
        for (int col = 0; col < VGA_COLS; col++) {
            unsigned char ch = vga_mem[(size_t)(row * VGA_COLS + col) * 2];
            line[len++] = (ch < 0x20 || ch == 0x7F) ? ' ' : (char)ch;
        }
        while (len > 0 && line[len - 1] == ' ') {
            len--;          /* trim trailing blanks */
        }
        line[len++] = '\n';
        console_write_raw(line, (size_t)len);
    }
}

int vga_start(void *guest_mem, size_t mem_size)
{
    if (running) {
        return 0;
    }
    if (guest_mem == NULL || mem_size < VGA_TEXT_BASE + VGA_BYTES) {
        fprintf(stderr,
                "Error: --vga needs the text buffer at 0x%x to be inside guest "
                "memory, but this guest has only %zu KB.\n"
                "       Real-mode guests cannot use --vga; try --paging.\n",
                VGA_TEXT_BASE, mem_size / 1024);
        return -1;
    }

    vga_mem = (const uint8_t *)guest_mem + VGA_TEXT_BASE;
    shadow_valid = false;
    to_terminal = isatty(STDOUT_FILENO);

    if (to_terminal) {
        /* Alternate screen buffer, cursor hidden: leaves the user's scrollback
         * untouched when we exit. */
        console_write_raw("\033[?1049h\033[?25l\033[2J", 14);
    }

    running = true;
    if (pthread_create(&render_thread, NULL, render_loop, NULL) != 0) {
        fprintf(stderr, "Warning: failed to start VGA render thread.\n");
        running = false;
        if (to_terminal) {
            console_write_raw("\033[?1049l\033[?25h", 14);
        }
        return -1;
    }

    return 0;
}

void vga_stop(void)
{
    if (!running) {
        return;
    }
    running = false;
    pthread_join(render_thread, NULL);

    if (to_terminal) {
        /* Draw once more before tearing down. A guest that paints the screen
         * and halts inside one refresh interval would otherwise never have
         * had a frame rendered at all. */
        static char frame[VGA_BYTES * 24 + 256];
        size_t len = build_frame(frame, sizeof(frame));
        console_write_raw(frame, len);

        /* Leave the alternate screen, then reprint the final contents so the
         * result survives in the user's scrollback. */
        console_write_raw("\033[?1049l\033[?25h", 14);
    }
    dump_plain();

    vga_mem = NULL;
}
