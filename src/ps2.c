/*
 * ps2.c - i8042 keyboard controller
 */

#include <string.h>
#include <pthread.h>

#include "ps2.h"

#define QUEUE_SIZE 64

/* Set 1 scancodes for the keys this translation needs. */
#define SC_LSHIFT 0x2A
#define SC_BREAK  0x80      /* OR'd into a make code to release the key */

/* Status register bits a driver polls. */
#define STATUS_OUTPUT_FULL 0x01
#define STATUS_INPUT_FULL  0x02
#define STATUS_SYSTEM      0x04

static bool enabled = false;

/*
 * Three threads reach this queue: the stdin reader pushes scancodes, the vCPU
 * pops them when the guest reads port 0x60, and the watchdog checks whether
 * any are pending. It needs a lock.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct {
    uint8_t data[QUEUE_SIZE];
    int head;
    int tail;
} queue;

/* Last command byte written to 0x64, for the few that expect a data byte. */
static uint8_t pending_command = 0;

/*
 * ASCII to set 1 make code. Entry 0 means "no key produces this".
 * Characters that need Shift are handled by the second table.
 */
static const uint8_t unshifted[128] = {
    ['\033'] = 0x01,
    ['1'] = 0x02, ['2'] = 0x03, ['3'] = 0x04, ['4'] = 0x05, ['5'] = 0x06,
    ['6'] = 0x07, ['7'] = 0x08, ['8'] = 0x09, ['9'] = 0x0A, ['0'] = 0x0B,
    ['-'] = 0x0C, ['='] = 0x0D, ['\b'] = 0x0E, ['\t'] = 0x0F,
    ['q'] = 0x10, ['w'] = 0x11, ['e'] = 0x12, ['r'] = 0x13, ['t'] = 0x14,
    ['y'] = 0x15, ['u'] = 0x16, ['i'] = 0x17, ['o'] = 0x18, ['p'] = 0x19,
    ['['] = 0x1A, [']'] = 0x1B, ['\n'] = 0x1C, ['\r'] = 0x1C,
    ['a'] = 0x1E, ['s'] = 0x1F, ['d'] = 0x20, ['f'] = 0x21, ['g'] = 0x22,
    ['h'] = 0x23, ['j'] = 0x24, ['k'] = 0x25, ['l'] = 0x26, [';'] = 0x27,
    ['\''] = 0x28, ['`'] = 0x29, ['\\'] = 0x2B,
    ['z'] = 0x2C, ['x'] = 0x2D, ['c'] = 0x2E, ['v'] = 0x2F, ['b'] = 0x30,
    ['n'] = 0x31, ['m'] = 0x32, [','] = 0x33, ['.'] = 0x34, ['/'] = 0x35,
    [' '] = 0x39,
};

/*
 * Characters produced by holding Shift. The value is the make code of the
 * unshifted key on the same physical switch.
 */
static const uint8_t shifted[128] = {
    ['!'] = 0x02, ['@'] = 0x03, ['#'] = 0x04, ['$'] = 0x05, ['%'] = 0x06,
    ['^'] = 0x07, ['&'] = 0x08, ['*'] = 0x09, ['('] = 0x0A, [')'] = 0x0B,
    ['_'] = 0x0C, ['+'] = 0x0D,
    ['Q'] = 0x10, ['W'] = 0x11, ['E'] = 0x12, ['R'] = 0x13, ['T'] = 0x14,
    ['Y'] = 0x15, ['U'] = 0x16, ['I'] = 0x17, ['O'] = 0x18, ['P'] = 0x19,
    ['{'] = 0x1A, ['}'] = 0x1B,
    ['A'] = 0x1E, ['S'] = 0x1F, ['D'] = 0x20, ['F'] = 0x21, ['G'] = 0x22,
    ['H'] = 0x23, ['J'] = 0x24, ['K'] = 0x25, ['L'] = 0x26, [':'] = 0x27,
    ['"'] = 0x28, ['~'] = 0x29, ['|'] = 0x2B,
    ['Z'] = 0x2C, ['X'] = 0x2D, ['C'] = 0x2E, ['V'] = 0x2F, ['B'] = 0x30,
    ['N'] = 0x31, ['M'] = 0x32, ['<'] = 0x33, ['>'] = 0x34, ['?'] = 0x35,
};

void ps2_enable(bool on)
{
    pthread_mutex_lock(&lock);
    enabled = on;
    if (!on) {
        queue.head = queue.tail = 0;
    }
    pthread_mutex_unlock(&lock);
}

bool ps2_enabled(void)
{
    return enabled;
}

/* Caller must hold the lock. */
static void push_locked(uint8_t code)
{
    int next = (queue.head + 1) % QUEUE_SIZE;
    if (next == queue.tail) {
        return;             /* full: drop, as a real controller would */
    }
    queue.data[queue.head] = code;
    queue.head = next;
}

bool ps2_push_char(char ch)
{
    if (!enabled) {
        return false;
    }

    unsigned char c = (unsigned char)ch;
    if (c >= 128) {
        return false;       /* not representable on a US keyboard */
    }

    uint8_t code = unshifted[c];
    bool needs_shift = false;

    if (code == 0) {
        code = shifted[c];
        needs_shift = (code != 0);
    }
    if (code == 0) {
        return false;
    }

    /* A real keyboard reports the shift press, the key, then both releases.
     * Drivers that track modifier state depend on seeing all four. */
    pthread_mutex_lock(&lock);
    if (needs_shift) {
        push_locked(SC_LSHIFT);
    }
    push_locked(code);
    push_locked(code | SC_BREAK);
    if (needs_shift) {
        push_locked(SC_LSHIFT | SC_BREAK);
    }
    pthread_mutex_unlock(&lock);

    return true;
}

bool ps2_has_data(void)
{
    pthread_mutex_lock(&lock);
    bool has = (queue.head != queue.tail);
    pthread_mutex_unlock(&lock);
    return has;
}

uint8_t ps2_read_data(void)
{
    pthread_mutex_lock(&lock);
    uint8_t code = 0;
    if (queue.head != queue.tail) {
        code = queue.data[queue.tail];
        queue.tail = (queue.tail + 1) % QUEUE_SIZE;
    }
    pthread_mutex_unlock(&lock);
    return code;
}

uint8_t ps2_read_status(void)
{
    uint8_t status = STATUS_SYSTEM;     /* POST completed */
    if (ps2_has_data()) {
        status |= STATUS_OUTPUT_FULL;
    }
    /* The input buffer is never full: we consume commands immediately, so a
     * driver polling for "ready to write" always succeeds. */
    return status;
}

void ps2_write_data(uint8_t value)
{
    pthread_mutex_lock(&lock);
    if (pending_command != 0) {
        /* Data byte for the preceding command; none we honour need it. */
        pending_command = 0;
        pthread_mutex_unlock(&lock);
        return;
    }

    switch (value) {
    case 0xFF:      /* reset */
        queue.head = queue.tail = 0;
        push_locked(0xFA);             /* ACK */
        push_locked(0xAA);             /* self-test passed */
        break;
    case 0xF4:      /* enable scanning */
    case 0xF5:      /* disable scanning */
    case 0xF3:      /* set typematic rate (a data byte follows) */
    case 0xED:      /* set LEDs (a data byte follows) */
        push_locked(0xFA);
        if (value == 0xF3 || value == 0xED) {
            pending_command = value;
        }
        break;
    case 0xF2:      /* identify */
        push_locked(0xFA);
        push_locked(0xAB);
        push_locked(0x83);             /* MF2 keyboard */
        break;
    default:
        push_locked(0xFA);             /* acknowledge anything else */
        break;
    }
    pthread_mutex_unlock(&lock);
}

void ps2_write_command(uint8_t value)
{
    pthread_mutex_lock(&lock);
    switch (value) {
    case 0xAA:      /* controller self-test */
        push_locked(0x55);             /* passed */
        break;
    case 0xAB:      /* interface test */
        push_locked(0x00);             /* no error */
        break;
    case 0x60:      /* write command byte; a data byte follows */
    case 0xD1:      /* write output port; a data byte follows */
        pending_command = value;
        break;
    case 0xAD:      /* disable first port */
    case 0xAE:      /* enable first port */
    case 0xA7:      /* disable second port */
    case 0xA8:      /* enable second port */
        break;
    case 0xFE:      /* pulse reset line - a guest asking to reboot */
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&lock);
}
