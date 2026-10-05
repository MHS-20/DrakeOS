/* 8253/8254 PIT channel 0: the system tick that drives timekeeping and preemption. */
#include <arch.h>
#include <cpu.h>
#include <proc.h>

#define PIT_FREQ    1193182u
#define PIT_CH0     0x40
#define PIT_CMD     0x43

static volatile uint32_t ticks;

static void timer_irq(registers_t *r)
{
    (void)r;
    ticks++;
    sched_tick();
}

void pit_init(uint32_t hz)
{
    uint32_t divisor = PIT_FREQ / hz;
    outb(PIT_CMD, 0x36);                 /* channel 0, lo/hi byte, mode 3 (square wave), binary */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
    register_interrupt_handler(IRQ_BASE + IRQ_TIMER, timer_irq);
    pic_unmask(IRQ_TIMER);
}

uint32_t timer_ticks(void)
{
    return ticks;
}

void timer_sleep_busy(uint32_t ms)
{
    uint32_t end = ticks + (ms * TIMER_HZ + 999) / 1000;
    uint32_t flags = irq_save();
    sti();
    while ((int32_t)(end - ticks) > 0)
        hlt();
    irq_restore(flags);
    if (!(flags & (1u << 9)))
        cli();
}
