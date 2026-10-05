/*
 * PS/2 keyboard driver. The IRQ handler only decodes scan codes and queues characters;
 * readers block on the buffer and are woken by the interrupt.
 */
#include <arch.h>
#include <cpu.h>
#include <keyboard.h>
#include <proc.h>

#define KBD_DATA   0x60
#define BUF_SIZE   256

#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36
#define SC_CTRL    0x1D
#define SC_ALT     0x38
#define SC_CAPS    0x3A
#define SC_EXTENDED 0xE0

static const char normal_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ',
};

static const char shift_map[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ',
};

static volatile unsigned char buffer[BUF_SIZE];
static volatile unsigned head, tail;
static int shift, ctrl, caps, extended;

static void enqueue(unsigned char c)
{
    if (head - tail < BUF_SIZE) {
        buffer[head % BUF_SIZE] = c;
        head++;
    }
    process_wakeup((const void *)buffer);
}

static void extended_key(uint8_t code)
{
    switch (code) {
    case 0x48: enqueue(KEY_UP); break;
    case 0x50: enqueue(KEY_DOWN); break;
    case 0x4B: enqueue(KEY_LEFT); break;
    case 0x4D: enqueue(KEY_RIGHT); break;
    case 0x47: enqueue(KEY_HOME); break;
    case 0x4F: enqueue(KEY_END); break;
    case 0x53: enqueue(KEY_DELETE); break;
    case 0x1C: enqueue('\n'); break;           /* keypad Enter */
    }
}

static void keyboard_irq(registers_t *r)
{
    (void)r;
    uint8_t sc = inb(KBD_DATA);
    if (sc == SC_EXTENDED) {
        extended = 1;
        return;
    }
    int released = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (extended) {
        extended = 0;
        if (code == SC_CTRL)
            ctrl = !released;                  /* right Ctrl */
        else if (!released)
            extended_key(code);
        return;
    }
    switch (code) {
    case SC_LSHIFT:
    case SC_RSHIFT:
        shift = !released;
        return;
    case SC_CTRL:
        ctrl = !released;
        return;
    case SC_ALT:
        return;
    case SC_CAPS:
        if (!released)
            caps = !caps;
        return;
    }
    if (released)
        return;

    char c = shift ? shift_map[code] : normal_map[code];
    if (!c)
        return;
    if (caps && c >= 'a' && c <= 'z')
        c -= 32;
    else if (caps && c >= 'A' && c <= 'Z')
        c += 32;
    if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c &= 0x1F;                             /* Ctrl+C = 3, Ctrl+L = 12, ... */
    enqueue((unsigned char)c);
}

void keyboard_init(void)
{
    while (inb(0x64) & 1)                      /* drop bytes left over from the BIOS */
        inb(KBD_DATA);
    register_interrupt_handler(IRQ_BASE + IRQ_KEYBOARD, keyboard_irq);
    pic_unmask(IRQ_KEYBOARD);
}

int kbd_trygetchar(void)
{
    uint32_t flags = irq_save();
    int c = -1;
    if (head != tail)
        c = buffer[tail++ % BUF_SIZE];
    irq_restore(flags);
    return c;
}

int kbd_getchar(void)
{
    uint32_t flags = irq_save();
    while (head == tail)
        process_block((const void *)buffer);
    int c = buffer[tail++ % BUF_SIZE];
    irq_restore(flags);
    return c;
}
