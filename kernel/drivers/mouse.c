/* PS/2 mouse on the 8042's auxiliary port (IRQ 12): 3-byte packets with relative motion. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>

#define DATA    0x60
#define STATUS  0x64
#define COMMAND 0x64

static volatile struct mouse_state state;
static int width = 320, height = 200;
static uint8_t packet[3];
static int cycle;

static int wait_output(void)       /* data waiting to be read */
{
    for (int i = 0; i < 100000; i++)
        if (inb(STATUS) & 1)
            return 0;
    return -1;
}

static int wait_input(void)        /* controller ready to accept a byte */
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(STATUS) & 2))
            return 0;
    return -1;
}

static int mouse_command(uint8_t cmd)
{
    wait_input();
    outb(COMMAND, 0xD4);           /* next data byte goes to the mouse */
    wait_input();
    outb(DATA, cmd);
    if (wait_output() < 0)
        return -1;
    return inb(DATA) == 0xFA ? 0 : -1;   /* ACK */
}

static void mouse_irq(registers_t *r)
{
    (void)r;
    uint8_t b = inb(DATA);
    if (cycle == 0 && !(b & 0x08))
        return;                    /* bit 3 of byte 0 is always set: resynchronise */
    packet[cycle++] = b;
    if (cycle < 3)
        return;
    cycle = 0;
    if (packet[0] & 0xC0)
        return;                    /* overflow: discard */
    int dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);
    int x = state.x + dx, y = state.y - dy;   /* PS/2 y grows upwards */
    state.x = x < 0 ? 0 : x >= width ? width - 1 : x;
    state.y = y < 0 ? 0 : y >= height ? height - 1 : y;
    state.buttons = packet[0] & 0x07;
    state.events++;
}

int mouse_init(void)
{
    wait_input();
    outb(COMMAND, 0xA8);           /* enable the auxiliary device */
    wait_input();
    outb(COMMAND, 0x20);           /* read the controller configuration byte */
    if (wait_output() < 0)
        return -1;
    uint8_t config = (inb(DATA) | 0x02) & ~0x20;   /* IRQ 12 on, mouse clock on */
    wait_input();
    outb(COMMAND, 0x60);
    wait_input();
    outb(DATA, config);
    if (mouse_command(0xF6) < 0 || mouse_command(0xF4) < 0) {   /* defaults, enable reporting */
        klog(LOG_WARN, "mouse: no PS/2 mouse answered");
        return -1;
    }
    state.x = width / 2;
    state.y = height / 2;
    register_interrupt_handler(IRQ_BASE + IRQ_MOUSE, mouse_irq);
    pic_unmask(IRQ_MOUSE);
    klog(LOG_INFO, "mouse: PS/2 mouse enabled");
    return 0;
}

void mouse_set_bounds(int w, int h)
{
    uint32_t flags = irq_save();
    width = w;
    height = h;
    state.x = w / 2;
    state.y = h / 2;
    irq_restore(flags);
}

struct mouse_state mouse_get(void)
{
    uint32_t flags = irq_save();
    struct mouse_state s = state;
    irq_restore(flags);
    return s;
}
