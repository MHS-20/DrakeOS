/* IDT setup and the C side of interrupt dispatch: exceptions, IRQs and the system call gate. */
#include <arch.h>
#include <console.h>
#include <cpu.h>
#include <proc.h>
#include <string.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;              /* P | DPL | type (0xE = 32-bit interrupt gate) */
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GATE_KERNEL 0x8E        /* present, DPL 0, interrupt gate: IF cleared on entry */
#define GATE_USER   0xEE        /* DPL 3: reachable with `int` from user mode */

static struct idt_gate idt[256];
static interrupt_handler_t handlers[256];

extern const uint32_t interrupt_stub_table[48];   /* interrupts.asm */
extern const uint32_t syscall_stub;
extern void idt_flush(const struct idt_ptr *ptr);

static const char *const exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor segment overrun",
    "Invalid TSS", "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating-point error", "Alignment check", "Machine check",
    "SIMD floating-point error", "Virtualization exception", "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security exception", "Reserved",
};

static void set_gate(int vector, uint32_t handler, uint8_t flags)
{
    idt[vector] = (struct idt_gate){
        .offset_low = handler & 0xFFFF,
        .selector = KERNEL_CS,
        .flags = flags,
        .offset_high = handler >> 16,
    };
}

void idt_init(void)
{
    for (int v = 0; v < 48; v++)
        set_gate(v, interrupt_stub_table[v], GATE_KERNEL);
    set_gate(SYSCALL_VECTOR, syscall_stub, GATE_USER);
    struct idt_ptr ptr = { sizeof idt - 1, (uint32_t)idt };
    idt_flush(&ptr);
}

void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler)
{
    handlers[vector] = handler;
}

static void dump_registers(const registers_t *r)
{
    kprintf("  eax=%08x ebx=%08x ecx=%08x edx=%08x\n", r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  esi=%08x edi=%08x ebp=%08x eip=%08x\n", r->esi, r->edi, r->ebp, r->eip);
    kprintf("  cs=%04x ds=%04x eflags=%08x err=%08x\n", r->cs, r->ds, r->eflags, r->err_code);
}

static void handle_exception(registers_t *r)
{
    if (handlers[r->int_no]) {
        handlers[r->int_no](r);
        return;
    }
    kprintf("\n%s (vector %u)", exception_names[r->int_no], r->int_no);
    if (r->int_no == 14)
        kprintf(" at address %08x", read_cr2());
    kprintf("\n");
    dump_registers(r);
    if ((r->cs & 3) == 3) {
        process_kill_current(r->int_no);      /* user fault: only the process dies */
        return;
    }
    panic("unhandled exception in kernel mode");
}

static int spurious(uint8_t irq)
{
    /* IRQ 7/15 with the in-service bit clear is a spurious interrupt: no EOI to that PIC. */
    if (irq != 7 && irq != 15)
        return 0;
    uint16_t port = irq == 7 ? 0x20 : 0xA0;
    outb(port, 0x0B);
    if (inb(port) & 0x80)
        return 0;
    if (irq == 15)
        pic_eoi(2);                           /* the master did see the cascade line */
    return 1;
}

void interrupt_dispatch(registers_t *r)
{
    if (r->int_no < 32) {
        handle_exception(r);
    } else if (r->int_no < 48) {
        uint8_t irq = r->int_no - IRQ_BASE;
        if (spurious(irq))
            return;
        pic_eoi(irq);                         /* before the handler: it may switch tasks */
        if (handlers[r->int_no])
            handlers[r->int_no](r);
    } else if (handlers[r->int_no]) {
        handlers[r->int_no](r);
    }
    process_check_signals(r);                 /* a killed process never returns to user mode */
}
