/*
 * devices.h - emulated legacy I/O devices
 *
 * Mini-KVM emulates only what guests actually touch:
 *   - a 16550 UART on COM1 (0x3f8-0x3ff), forwarded to host stdout
 *   - enough stubs for the legacy PC ports (PIC, CMOS, 8042, A20) that a
 *     booting kernel pokes, so polling loops make progress instead of
 *     wedging on an unhandled port
 *
 * The hypercall port (0x500) is NOT handled here; see hypercall.h.
 */

#ifndef DEVICES_H
#define DEVICES_H

#include <stdbool.h>
#include <stdint.h>

#define UART_COM1_BASE 0x3f8
#define UART_COM1_LAST 0x3ff

/*
 * Register the callback used to raise a guest IRQ line. Only meaningful once
 * an IRQCHIP exists; pass NULL to disable, which is what real-mode guests
 * want -- they run without an interrupt controller.
 */
void devices_set_irq_hook(void (*set_irq)(uint32_t irq, int level));

/* Route host input to the PS/2 keyboard (kernel guests) or to COM1 (Linux). */
void devices_enable_keyboard(bool enable);
void devices_enable_serial_irq(bool enable);

/*
 * Called when a character arrives from the host. Converts it for whichever
 * input device is enabled and raises the matching interrupt.
 */
void devices_notify_input(char ch);

/*
 * Called periodically by the vCPU watchdog. Re-asserts a pending keyboard
 * interrupt that the guest was not yet able to receive.
 */
void devices_tick(void);

/* VGA CRTC index/data registers (0x3D4/0x3D5), used for the text cursor. */
bool devices_is_vga_crtc_port(uint16_t port);
void devices_vga_crtc_write(uint16_t port, uint8_t value);
uint8_t devices_vga_crtc_read(uint16_t port);

/*
 * Current hardware cursor position as a linear cell offset, or -1 if the
 * guest has not placed it. The VGA renderer draws the cursor there.
 */
int devices_vga_cursor(void);

/* True if port belongs to the emulated COM1 UART. */
bool devices_is_uart_port(uint16_t port);

/* Handle one byte of an OUT to a UART register. */
void devices_uart_write(uint16_t port, uint8_t value);

/* Produce one byte for an IN from a UART register. */
uint8_t devices_uart_read(uint16_t port);

/* Handle an OUT to a non-UART, non-hypercall legacy port. */
void devices_misc_out(uint16_t port, const uint8_t *data, int size);

/* Produce data for an IN from a non-UART, non-hypercall legacy port. */
void devices_misc_in(uint16_t port, uint8_t *data, int size);

#endif /* DEVICES_H */
