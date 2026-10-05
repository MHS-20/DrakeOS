/* Bitmap page-frame allocator: one bit per 4 KB frame of the first PHYS_MAP_LIMIT bytes. */
#include <console.h>
#include <cpu.h>
#include <layout.h>
#include <mm.h>
#include <string.h>

#define MAX_FRAMES (PHYS_MAP_LIMIT / PAGE_SIZE)

static uint32_t bitmap[MAX_FRAMES / 32];   /* 1 = used */
static uint32_t total, free_count;
static uint32_t next_hint;

static void mark(uint32_t frame, int used)
{
    uint32_t bit = 1u << (frame % 32);
    int was_used = (bitmap[frame / 32] & bit) != 0;
    if (used && !was_used) {
        bitmap[frame / 32] |= bit;
        free_count--;
    } else if (!used && was_used) {
        bitmap[frame / 32] &= ~bit;
        free_count++;
    }
}

static void mark_range(uint64_t start, uint64_t end, int used)
{
    if (start >= PHYS_MAP_LIMIT)
        return;
    if (end > PHYS_MAP_LIMIT)
        end = PHYS_MAP_LIMIT;
    /* Free only whole frames inside the range; reserve every frame the range touches. */
    uint32_t first = used ? (uint32_t)(start / PAGE_SIZE) : (uint32_t)((start + PAGE_SIZE - 1) / PAGE_SIZE);
    uint32_t last = used ? (uint32_t)((end + PAGE_SIZE - 1) / PAGE_SIZE) : (uint32_t)(end / PAGE_SIZE);
    for (uint32_t f = first; f < last && f < MAX_FRAMES; f++)
        mark(f, used);
}

void pmm_init(const multiboot_info_t *mbi)
{
    memset(bitmap, 0xFF, sizeof bitmap);
    free_count = 0;

    if (mbi->flags & MB_INFO_MEM_MAP) {
        uint32_t addr = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
        while (addr < end) {
            const multiboot_mmap_entry_t *e = P2V(addr);
            klog(LOG_INFO, "e820: %08x%08x +%08x%08x type %u", (uint32_t)(e->addr >> 32),
                 (uint32_t)e->addr, (uint32_t)(e->len >> 32), (uint32_t)e->len, e->type);
            if (e->type == 1)
                mark_range(e->addr, e->addr + e->len, 0);
            addr += e->size + sizeof e->size;
        }
    } else if (mbi->flags & MB_INFO_MEMORY) {
        mark_range(0x100000, 0x100000 + (uint64_t)mbi->mem_upper * 1024, 0);
    } else {
        panic("bootloader provided no memory information");
    }

    /* Never hand out the first megabyte (BIOS data, VGA memory, loader data) or the kernel. */
    mark_range(0, 0x100000, 1);
    mark_range((uint32_t)kernel_phys_start, (uint32_t)kernel_phys_end, 1);
    total = free_count;
    klog(LOG_INFO, "pmm: %u frames free (%u KB)", free_count, free_count * 4);
}

uint32_t pmm_alloc(void)
{
    uint32_t flags = irq_save();
    for (uint32_t n = 0; n < MAX_FRAMES / 32; n++) {
        uint32_t i = (next_hint + n) % (MAX_FRAMES / 32);
        if (bitmap[i] == 0xFFFFFFFF)
            continue;
        for (uint32_t b = 0; b < 32; b++) {
            if (!(bitmap[i] & (1u << b))) {
                uint32_t frame = i * 32 + b;
                mark(frame, 1);
                next_hint = i;
                irq_restore(flags);
                memset(P2V(frame * PAGE_SIZE), 0, PAGE_SIZE);
                return frame * PAGE_SIZE;
            }
        }
    }
    irq_restore(flags);
    return 0;
}

void pmm_free(uint32_t phys)
{
    KASSERT(phys % PAGE_SIZE == 0 && phys < PHYS_MAP_LIMIT);
    uint32_t flags = irq_save();
    KASSERT(bitmap[phys / PAGE_SIZE / 32] & (1u << (phys / PAGE_SIZE % 32)));
    mark(phys / PAGE_SIZE, 0);
    irq_restore(flags);
}

uint32_t pmm_free_frames(void) { return free_count; }
uint32_t pmm_total_frames(void) { return total; }
