/* i386 descriptor tables, interrupt dispatch, PIC and PIT. */
#ifndef DRAKE_ARCH_H
#define DRAKE_ARCH_H

#include <stdint.h>

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   (0x18 | 3)
#define USER_DS   (0x20 | 3)
#define TSS_SEL   0x28

#define IRQ_BASE  32
#define IRQ_TIMER 0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE 2
#define IRQ_MOUSE 12
#define IRQ_ATA_PRIMARY 14
#define SYSCALL_VECTOR 0x80

/* Register frame built by the interrupt stubs (see interrupts.asm), lowest address first. */
typedef struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, useless_esp, ebx, edx, ecx, eax;   /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;                                  /* pushed by the CPU */
    uint32_t user_esp, user_ss;                                /* only when coming from ring 3 */
} registers_t;

typedef void (*interrupt_handler_t)(registers_t *r);

void gdt_init(void);
void tss_set_kernel_stack(uint32_t esp0);

void idt_init(void);
void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler);

void pic_init(void);
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_eoi(uint8_t irq);

#define TIMER_HZ 100
void pit_init(uint32_t hz);
uint32_t timer_ticks(void);
void timer_sleep_busy(uint32_t ms);   /* hlt-wait without the scheduler (early boot, drivers) */

#endif
