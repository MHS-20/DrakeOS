/*
 * Kernel heap: a first-fit allocator with an address-ordered circular free list and
 * coalescing on free. More memory comes from page frames mapped at the end of the heap.
 */
#include <console.h>
#include <cpu.h>
#include <layout.h>
#include <mm.h>
#include <string.h>

typedef struct header {
    struct header *next;        /* next free block (free list only) */
    size_t units;               /* block size in header-sized units, header included */
} header_t;

static header_t base;
static header_t *freep;
static uint32_t heap_end = HEAP_START;
static size_t used_units;

static header_t *morecore(size_t units)
{
    size_t bytes = ALIGN_UP(units * sizeof(header_t), PAGE_SIZE);
    if (heap_end + bytes > HEAP_MAX)
        return NULL;
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE) {
        uint32_t frame = pmm_alloc();
        if (!frame || vmm_map(vmm_kernel_directory(), heap_end + off, frame, PTE_WRITE) < 0)
            panic("heap: out of physical memory");
    }
    header_t *h = (header_t *)heap_end;
    h->units = bytes / sizeof(header_t);
    heap_end += bytes;
    used_units += h->units;      /* kfree() below subtracts it again */
    kfree(h + 1);
    return freep;
}

void heap_init(void)
{
    base.next = freep = &base;
    base.units = 0;
}

void *kmalloc(size_t size)
{
    if (!size)
        return NULL;
    size_t units = (size + sizeof(header_t) - 1) / sizeof(header_t) + 1;
    uint32_t flags = irq_save();
    header_t *prev = freep;
    for (header_t *p = prev->next;; prev = p, p = p->next) {
        if (p->units >= units) {
            if (p->units == units) {
                prev->next = p->next;
            } else {
                p->units -= units;        /* hand out the tail of the block */
                p += p->units;
                p->units = units;
            }
            freep = prev;
            used_units += units;
            irq_restore(flags);
            return p + 1;
        }
        if (p == freep && !(p = morecore(units))) {
            irq_restore(flags);
            return NULL;
        }
    }
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    uint32_t flags = irq_save();
    header_t *b = (header_t *)ptr - 1, *p;
    KASSERT((uint32_t)b >= HEAP_START && (uint32_t)b < heap_end);
    used_units -= b->units;
    for (p = freep; !(b > p && b < p->next); p = p->next)
        if (p >= p->next && (b > p || b < p->next))
            break;                        /* b goes at one end of the arena */
    if (b + b->units == p->next) {        /* merge with the upper neighbour */
        b->units += p->next->units;
        b->next = p->next->next;
    } else {
        b->next = p->next;
    }
    if (p + p->units == b) {              /* merge with the lower neighbour */
        p->units += b->units;
        p->next = b->next;
    } else {
        p->next = b;
    }
    freep = p;
    irq_restore(flags);
}

size_t heap_used(void)
{
    return used_units * sizeof(header_t);
}
