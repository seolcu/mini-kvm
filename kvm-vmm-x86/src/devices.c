/*
 * devices.c - emulated legacy I/O devices (16550 UART + PC port stubs)
 */

#include <string.h>

#include "devices.h"
#include "console.h"
#include "ps2.h"

/* --- 16550 UART on COM1 ------------------------------------------------ */

static struct {
    uint8_t ier;    /* interrupt enable */
    uint8_t lcr;    /* line control (bit 7 = DLAB) */
    uint8_t mcr;    /* modem control */
    uint8_t dll;    /* divisor latch low */
    uint8_t dlh;    /* divisor latch high */
} uart0 = {
    .ier = 0x00,
    .lcr = 0x03,    /* 8N1 */
    .mcr = 0x00,
    .dll = 0x01,
    .dlh = 0x00,
};

static void (*set_irq_hook)(uint32_t, int) = NULL;
static bool keyboard_routing = false;
static bool serial_irq_routing = false;

void devices_set_irq_hook(void (*set_irq)(uint32_t, int))
{
    set_irq_hook = set_irq;
}

void devices_enable_keyboard(bool enable)
{
    keyboard_routing = enable;
    ps2_enable(enable);
}

void devices_enable_serial_irq(bool enable)
{
    serial_irq_routing = enable;
}

static void set_irq(uint32_t irq, int level)
{
    if (set_irq_hook) {
        set_irq_hook(irq, level);
    }
}

static void pulse_irq(uint32_t irq)
{
    set_irq(irq, 1);
    set_irq(irq, 0);
}

/*
 * Generate a fresh IRQ1 edge whenever a scancode is waiting.
 *
 * The 8259 in a PC is wired edge-triggered, so simply holding the line high
 * is not enough: an assertion made while the guest still has IRQ1 masked
 * produces no interrupt once it unmasks, and every subsequent scancode would
 * be stranded behind it. Dropping the line and raising it again gives the
 * controller the edge it needs.
 */
static void keyboard_refill(void);

static void refresh_keyboard_irq(void)
{
    if (!keyboard_routing) {
        return;
    }
    set_irq(1, 0);
    if (ps2_has_data()) {
        set_irq(1, 1);
    }
}

/*
 * Called periodically while the guest runs. Piped input is all available
 * before the guest has even installed an IDT, so the edge produced when it
 * arrived is long gone by the time the guest is ready; re-asserting until the
 * queue drains is what actually gets it delivered.
 */
void devices_tick(void)
{
    if (!keyboard_routing) {
        return;
    }
    keyboard_refill();
    if (ps2_has_data()) {
        refresh_keyboard_irq();
    }
}

static void raise_irq4(void)
{
    if (serial_irq_routing) {
        pulse_irq(4);
    }
}

/*
 * Convert one buffered host character into scancodes, but only once the
 * previous key has been fully consumed.
 *
 * Converting eagerly does not work: piped input arrives all at once, and a
 * keyboard queue large enough to hold every scancode of an entire script is
 * both wasteful and still bounded. Leaving the characters in the console ring
 * and pulling one at a time makes the ring the only buffer that has to be
 * big, and models a real keyboard more closely -- one key is in flight at a
 * time.
 */
static void keyboard_refill(void)
{
    if (!keyboard_routing || ps2_has_data()) {
        return;
    }
    int ch = console_poll_char();
    if (ch < 0) {
        return;
    }
    ps2_push_char((char)ch);
}

void devices_notify_input(char ch)
{
    (void)ch;

    /*
     * The host has one input stream but a PC has two input devices, so route
     * it to whichever the guest is actually using. A kernel guest reads
     * scancodes; Linux reads its serial console.
     */
    if (keyboard_routing) {
        keyboard_refill();
        refresh_keyboard_irq();
    }
    if (serial_irq_routing) {
        pulse_irq(4);
    }
}

bool devices_is_uart_port(uint16_t port)
{
    return port >= UART_COM1_BASE && port <= UART_COM1_LAST;
}

void devices_uart_write(uint16_t port, uint8_t value)
{
    uint16_t reg = port - UART_COM1_BASE;
    bool dlab = (uart0.lcr & 0x80) != 0;

    switch (reg) {
    case 0:     /* THR, or DLL when DLAB is set */
        if (dlab) {
            uart0.dll = value;
        } else {
            console_putchar_raw((char)value);
            if (uart0.ier & 0x02) {
                /* THR-empty interrupt, so the guest keeps draining its buffer. */
                raise_irq4();
            }
        }
        break;
    case 1:     /* IER, or DLH when DLAB is set */
        if (dlab) {
            uart0.dlh = value;
        } else {
            uart0.ier = value;
            if (uart0.ier & 0x02) {
                /* On real hardware, enabling THRE with an empty THR fires
                 * immediately. Guests rely on that first edge. */
                raise_irq4();
            }
        }
        break;
    case 3:     /* LCR */
        uart0.lcr = value;
        break;
    case 4:     /* MCR */
        uart0.mcr = value;
        break;
    default:
        break;
    }
}

uint8_t devices_uart_read(uint16_t port)
{
    uint16_t reg = port - UART_COM1_BASE;
    bool dlab = (uart0.lcr & 0x80) != 0;

    switch (reg) {
    case 0:     /* RBR, or DLL when DLAB is set */
        if (dlab) {
            return uart0.dll;
        } else {
            int ch = console_poll_char();
            return (ch < 0) ? 0x00 : (uint8_t)ch;
        }
    case 1:     /* IER, or DLH when DLAB is set */
        return dlab ? uart0.dlh : uart0.ier;
    case 2:     /* IIR - report the highest-priority pending source */
        if (console_has_input() && (uart0.ier & 0x01)) {
            return 0x04;    /* received data available */
        }
        if (uart0.ier & 0x02) {
            return 0x02;    /* THR empty */
        }
        return 0x01;        /* none pending */
    case 3:     /* LCR */
        return uart0.lcr;
    case 4:     /* MCR */
        return uart0.mcr;
    case 5: {   /* LSR */
        uint8_t lsr = 0x60;                 /* THR empty | transmitter empty */
        if (console_has_input()) {
            lsr |= 0x01;                    /* data ready */
        }
        return lsr;
    }
    case 6:     /* MSR */
    case 7:     /* SCR */
    default:
        return 0x00;
    }
}

/* --- VGA CRTC ----------------------------------------------------------- */

/*
 * Only the cursor location registers are modelled. A text-mode driver writes
 * index 0x0E/0x0F to move the hardware cursor after every character, so
 * without these the cursor never moves and the display looks wrong even
 * though the characters are correct.
 */
#define CRTC_INDEX_PORT 0x3D4
#define CRTC_DATA_PORT  0x3D5

#define CRTC_CURSOR_START 0x0A
#define CRTC_CURSOR_END   0x0B
#define CRTC_CURSOR_HIGH  0x0E
#define CRTC_CURSOR_LOW   0x0F

static uint8_t crtc_index = 0;
static uint8_t crtc_regs[256];
static bool crtc_cursor_set = false;

bool devices_is_vga_crtc_port(uint16_t port)
{
    return port == CRTC_INDEX_PORT || port == CRTC_DATA_PORT;
}

void devices_vga_crtc_write(uint16_t port, uint8_t value)
{
    if (port == CRTC_INDEX_PORT) {
        crtc_index = value;
        return;
    }

    crtc_regs[crtc_index] = value;
    if (crtc_index == CRTC_CURSOR_HIGH || crtc_index == CRTC_CURSOR_LOW) {
        crtc_cursor_set = true;
    }
}

uint8_t devices_vga_crtc_read(uint16_t port)
{
    if (port == CRTC_INDEX_PORT) {
        return crtc_index;
    }
    return crtc_regs[crtc_index];
}

int devices_vga_cursor(void)
{
    if (!crtc_cursor_set) {
        return -1;
    }
    /* Cursor start register bit 5 disables the cursor entirely. */
    if (crtc_regs[CRTC_CURSOR_START] & 0x20) {
        return -1;
    }
    return ((int)crtc_regs[CRTC_CURSOR_HIGH] << 8) | crtc_regs[CRTC_CURSOR_LOW];
}

/* --- Legacy PC port stubs ---------------------------------------------- */

static uint8_t cmos_index = 0;
static uint8_t port92 = 0x02;   /* A20 gate reported enabled */

void devices_misc_out(uint16_t port, const uint8_t *data, int size)
{
    (void)size;
    uint8_t value = data[0];

    switch (port) {
    case 0x92:  /* fast A20 gate - bit 1 stays set, we are always in "A20 on" */
        port92 = value | 0x02;
        break;
    case 0x70:  /* CMOS index latch */
        cmos_index = value;
        break;
    case 0x60:  /* 8042 data */
        ps2_write_data(value);
        break;
    case 0x64:  /* 8042 command */
        ps2_write_command(value);
        break;
    /* Accepted and discarded: guests program these during early boot and
     * only need the writes not to fault. */
    case 0x20:  /* PIC1 command */
    case 0x21:  /* PIC1 data */
    case 0xA0:  /* PIC2 command */
    case 0xA1:  /* PIC2 data */
    case 0x80:  /* POST / delay port */
    default:
        break;
    }
}

void devices_misc_in(uint16_t port, uint8_t *data, int size)
{
    /* Default to zero so polling loops terminate rather than spin on garbage. */
    memset(data, 0, (size_t)size);

    switch (port) {
    case 0x92:
        data[0] = port92;
        break;
    case 0x60:
        data[0] = ps2_read_data();
        /* Bring in the next key as soon as this one is done, then re-edge so
         * the guest is told about it. */
        keyboard_refill();
        refresh_keyboard_irq();
        break;
    case 0x64:
        data[0] = ps2_read_status();
        break;
    case 0x71:
        /* CMOS data for whichever index was latched; we model none of it. */
        (void)cmos_index;
        data[0] = 0x00;
        break;
    default:
        break;
    }
}
