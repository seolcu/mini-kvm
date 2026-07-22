/*
 * vga.h - VGA text mode (80x25) display
 *
 * Almost every hobby kernel's first output is a store to the text buffer at
 * physical 0xB8000. Mini-KVM maps that address as ordinary guest RAM rather
 * than trapping it as MMIO, so the guest writes at full speed and the host
 * simply reads the same pages through its own mapping. A render thread polls
 * the buffer and redraws when it changes.
 *
 * Each cell is two bytes: character, then an attribute byte holding a 4-bit
 * foreground, a 3-bit background, and a blink bit.
 *
 * Rendering is opt-in (--vga) because it takes over the terminal, which would
 * collide with the line-oriented output that UART and hypercall guests
 * produce.
 */

#ifndef VGA_H
#define VGA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VGA_TEXT_BASE   0xB8000
#define VGA_COLS        80
#define VGA_ROWS        25
#define VGA_BYTES       (VGA_COLS * VGA_ROWS * 2)

/*
 * Begin rendering the text buffer inside this guest's memory mapping.
 *
 * Returns -1 if the mapping is too small to contain the buffer, which is the
 * case for the 256KB real-mode layout. On a terminal this switches to the
 * alternate screen and redraws continuously; when stdout is redirected it
 * renders nothing until vga_stop(), so scripted runs stay diffable.
 */
int vga_start(void *guest_mem, size_t mem_size);

/*
 * Stop rendering and restore the terminal. When stdout is not a terminal,
 * this prints the final screen contents as plain text, with trailing blanks
 * trimmed — which is what makes a VGA guest testable in CI.
 * Safe to call when vga_start() failed or was never called.
 */
void vga_stop(void);

/* True while the render thread is active. */
bool vga_active(void);

#endif /* VGA_H */
