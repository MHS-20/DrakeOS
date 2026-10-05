/* Inline wrappers for privileged x86 instructions (port I/O, interrupts, control registers). */
#ifndef DRAKE_CPU_H
#define DRAKE_CPU_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)  { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline void outw(uint16_t port, uint16_t val) { asm volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void insw(uint16_t port, void *buf, uint32_t count)
{
    asm volatile("cld; rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *buf, uint32_t count)
{
    asm volatile("cld; rep outsw" : "+S"(buf), "+c"(count) : "d"(port));
}

/* A write to an unused port takes about 1 microsecond: enough delay for old devices like the PIC. */
static inline void io_wait(void) { outb(0x80, 0); }

static inline void cli(void) { asm volatile("cli" ::: "memory"); }
static inline void sti(void) { asm volatile("sti" ::: "memory"); }
static inline void hlt(void) { asm volatile("hlt"); }

/* Disable interrupts and return the previous EFLAGS, for single-CPU critical sections. */
static inline uint32_t irq_save(void)
{
    uint32_t flags;
    asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags)
{
    if (flags & (1u << 9))
        sti();
}

static inline uint32_t read_cr0(void) { uint32_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline uint32_t read_cr2(void) { uint32_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline uint32_t read_cr3(void) { uint32_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void write_cr3(uint32_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
static inline void invlpg(uint32_t addr) { asm volatile("invlpg (%0)" : : "r"(addr) : "memory"); }

static inline __attribute__((noreturn)) void halt_forever(void)
{
    for (;;)
        asm volatile("cli; hlt");
}

#endif
