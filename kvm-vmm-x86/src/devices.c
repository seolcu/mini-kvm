/*
 * devices.c - emulated legacy I/O devices (16550 UART + PC port stubs)
 */

#include <string.h>

#include "devices.h"
#include "console.h"

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

static void (*raise_irq4_hook)(void) = NULL;

void devices_set_irq_hook(void (*raise_irq4)(void))
{
    raise_irq4_hook = raise_irq4;
}

static void raise_irq4(void)
{
    if (raise_irq4_hook) {
        raise_irq4_hook();
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
    /* Accepted and discarded: guests program these during early boot and
     * only need the writes not to fault. */
    case 0x20:  /* PIC1 command */
    case 0x21:  /* PIC1 data */
    case 0xA0:  /* PIC2 command */
    case 0xA1:  /* PIC2 data */
    case 0x80:  /* POST / delay port */
    case 0x60:  /* 8042 data */
    case 0x64:  /* 8042 command */
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
    case 0x64:
        /* 8042 status: both input and output buffers empty. */
        data[0] = 0x00;
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
