/* COM1 output driver: 38400 baud, 8N1, FIFOs enabled. Output only. */
#include <cpu.h>
#include <serial.h>

#define COM1        0x3F8
#define DATA        (COM1 + 0)
#define IER         (COM1 + 1)
#define DLL         (COM1 + 0)     /* divisor latch low/high, visible while DLAB = 1 */
#define DLH         (COM1 + 1)
#define FCR         (COM1 + 2)
#define LCR         (COM1 + 3)
#define MCR         (COM1 + 4)
#define LSR         (COM1 + 5)
#define LSR_THRE    0x20           /* transmit holding register empty */

static int present;

void serial_init(void)
{
    outb(IER, 0x00);               /* no interrupts: we only poll */
    outb(LCR, 0x80);               /* DLAB on */
    outb(DLL, 3);                  /* 115200 / 3 = 38400 baud */
    outb(DLH, 0);
    outb(LCR, 0x03);               /* DLAB off, 8 data bits, no parity, 1 stop bit */
    outb(FCR, 0xC7);               /* enable + clear FIFOs, 14-byte threshold */
    outb(MCR, 0x03);               /* DTR + RTS */
    present = inb(LSR) != 0xFF;    /* a missing UART reads back as all ones */
}

void serial_putc(char c)
{
    if (!present)
        return;
    if (c == '\n')
        serial_putc('\r');
    for (int spins = 100000; !(inb(LSR) & LSR_THRE) && spins; spins--)
        ;
    outb(DATA, (uint8_t)c);
}

void serial_write(const char *s, size_t len)
{
    while (len--)
        serial_putc(*s++);
}
