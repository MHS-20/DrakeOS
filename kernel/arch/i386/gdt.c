/* GDT: null, kernel code/data (ring 0), user code/data (ring 3), and the TSS. */
#include <arch.h>
#include <string.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit_high;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Only esp0/ss0 are used: the stack the CPU switches to on a ring 3 -> ring 0 interrupt. */
struct tss {
    uint32_t prev_tss, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct tss tss;

extern void gdt_flush(const struct gdt_ptr *ptr);   /* gdt.asm */
extern void tss_flush(uint16_t selector);

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    gdt[i] = (struct gdt_entry){
        .limit_low = limit & 0xFFFF,
        .base_low = base & 0xFFFF,
        .base_mid = (base >> 16) & 0xFF,
        .access = access,
        .flags_limit_high = (uint8_t)((flags << 4) | ((limit >> 16) & 0x0F)),
        .base_high = base >> 24,
    };
}

void gdt_init(void)
{
    set_entry(0, 0, 0, 0, 0);
    set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);   /* kernel code: present, ring 0, exec/read; 4K gran, 32-bit */
    set_entry(2, 0, 0xFFFFF, 0x92, 0xC);   /* kernel data */
    set_entry(3, 0, 0xFFFFF, 0xFA, 0xC);   /* user code: DPL 3 */
    set_entry(4, 0, 0xFFFFF, 0xF2, 0xC);   /* user data: DPL 3 */

    memset(&tss, 0, sizeof tss);
    tss.ss0 = KERNEL_DS;
    tss.iomap_base = sizeof tss;           /* no I/O bitmap: ring 3 port access always faults */
    set_entry(5, (uint32_t)&tss, sizeof tss - 1, 0x89, 0x0);   /* available 32-bit TSS */

    struct gdt_ptr ptr = { sizeof gdt - 1, (uint32_t)gdt };
    gdt_flush(&ptr);
    tss_flush(TSS_SEL);
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}
