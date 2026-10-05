/* 8259A PIC pair, remapped so IRQ 0-15 arrive on vectors 32-47 instead of colliding with exceptions. */
#include <arch.h>
#include <cpu.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_init(void)
{
    outb(PIC1_CMD, 0x11);  io_wait();   /* ICW1: initialise, ICW4 follows */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, IRQ_BASE);      io_wait();   /* ICW2: vector offsets */
    outb(PIC2_DATA, IRQ_BASE + 8);  io_wait();
    outb(PIC1_DATA, 0x04); io_wait();   /* ICW3: slave on IRQ 2 */
    outb(PIC2_DATA, 0x02); io_wait();   /*        slave cascade identity */
    outb(PIC1_DATA, 0x01); io_wait();   /* ICW4: 8086 mode */
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFF);              /* everything masked until a driver unmasks its line */
    outb(PIC2_DATA, 0xFF);
    pic_unmask(IRQ_CASCADE);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

void pic_mask(uint8_t irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    outb(port, inb(port) | (1 << (irq & 7)));
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
