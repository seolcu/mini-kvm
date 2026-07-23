/*
 * ps2.h - i8042 keyboard controller
 *
 * A kernel that wants keyboard input reads scancodes from port 0x60 after
 * IRQ1, which is how every hobby kernel's keyboard driver is written. The
 * host only has a stream of characters from stdin, so this translates them
 * back into the set 1 make/break sequences a real keyboard would have
 * produced, including the shift presses needed for capitals and symbols.
 *
 * Only what a keyboard driver actually reads is modelled: there is no
 * translation-mode switching, no LED state, and no mouse.
 */

#ifndef PS2_H
#define PS2_H

#include <stdbool.h>
#include <stdint.h>

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64

/* Enable scancode generation. Off for guests that use hypercalls or serial. */
void ps2_enable(bool enable);
bool ps2_enabled(void);

/*
 * Convert one host character into scancodes and queue them.
 * Returns true if anything was queued, i.e. if IRQ1 should be raised.
 */
bool ps2_push_char(char ch);

/* True while scancodes are waiting to be read. */
bool ps2_has_data(void);

/* Port 0x60: next scancode, or 0 when the queue is empty. */
uint8_t ps2_read_data(void);

/* Port 0x64: controller status byte. */
uint8_t ps2_read_status(void);

/* Port 0x60/0x64 writes: commands. Mostly acknowledged and ignored. */
void ps2_write_data(uint8_t value);
void ps2_write_command(uint8_t value);

#endif /* PS2_H */
