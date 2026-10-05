/*
 * Paging. Every address space shares the kernel half (PDE 768-1023):
 *   0xC0000000-0xCFFFFFFF  physical 0-256 MB, 4 MB pages (so page tables are reachable via P2V)
 *   0xD0000000-0xD3FFFFFF  kernel heap; its page tables exist from boot so no PDE ever changes
 * User mappings live below 0xC0000000 and use 4 KB pages.
 */
#include <console.h>
#include <cpu.h>
#include <layout.h>
#include <mm.h>
#include <string.h>

static uint32_t kernel_pd[1024] __attribute__((aligned(PAGE_SIZE)));

#define PD_INDEX(v) ((v) >> 22)
#define PT_INDEX(v) (((v) >> 12) & 0x3FF)

uint32_t vmm_kernel_directory(void)
{
    return V2P(kernel_pd);
}

void vmm_init(void)
{
    for (uint32_t i = 0; i < PHYS_MAP_LIMIT >> 22; i++)
        kernel_pd[PD_INDEX(KERNEL_VMA) + i] = (i << 22) | PDE_4MB | PTE_WRITE | PTE_PRESENT;
    for (uint32_t v = HEAP_START; v < HEAP_MAX; v += 0x400000) {
        uint32_t pt = pmm_alloc();
        if (!pt)
            panic("vmm: out of memory for heap page tables");
        kernel_pd[PD_INDEX(v)] = pt | PTE_WRITE | PTE_PRESENT;
    }
    write_cr3(V2P(kernel_pd));     /* drops the boot identity map of the first 16 MB */
    klog(LOG_INFO, "vmm: kernel page directory at %08x", V2P(kernel_pd));
}

static uint32_t *pte_for(uint32_t pd_phys, uint32_t virt, int create, uint32_t flags)
{
    uint32_t *pd = P2V(pd_phys);
    uint32_t pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) {
        if (!create)
            return NULL;
        uint32_t pt = pmm_alloc();
        if (!pt)
            return NULL;
        pde = pt | PTE_PRESENT | PTE_WRITE | (flags & PTE_USER);
        pd[PD_INDEX(virt)] = pde;
    }
    if (pde & PDE_4MB)
        return NULL;
    return (uint32_t *)P2V(pde & ~0xFFFu) + PT_INDEX(virt);
}

int vmm_map(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t irq = irq_save();
    uint32_t *pte = pte_for(pd_phys, virt, 1, flags);
    if (pte)
        *pte = (phys & ~0xFFFu) | (flags & 0xFFF) | PTE_PRESENT;
    if (pte && read_cr3() == pd_phys)
        invlpg(virt);
    irq_restore(irq);
    return pte ? 0 : -1;
}

uint32_t vmm_translate(uint32_t pd_phys, uint32_t virt)
{
    uint32_t *pd = P2V(pd_phys);
    uint32_t pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT))
        return 0;
    if (pde & PDE_4MB)
        return (pde & 0xFFC00000u) | (virt & 0x3FFFFF);
    uint32_t pte = ((uint32_t *)P2V(pde & ~0xFFFu))[PT_INDEX(virt)];
    return (pte & PTE_PRESENT) ? (pte & ~0xFFFu) | (virt & 0xFFF) : 0;
}

/* True if [addr, addr+len) is user-accessible memory in this address space. */
int vmm_user_range_ok(uint32_t pd_phys, uint32_t addr, uint32_t len)
{
    if (addr + len < addr || addr + len > KERNEL_VMA)
        return 0;
    for (uint32_t page = ALIGN_DOWN(addr, PAGE_SIZE); page < addr + len; page += PAGE_SIZE) {
        uint32_t *pte = pte_for(pd_phys, page, 0, 0);
        if (!pte || (*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
            return 0;
    }
    return 1;
}

uint32_t vmm_create_address_space(void)
{
    uint32_t pd_phys = pmm_alloc();
    if (!pd_phys)
        return 0;
    uint32_t *pd = P2V(pd_phys);
    memcpy(pd + PD_INDEX(KERNEL_VMA), kernel_pd + PD_INDEX(KERNEL_VMA),
           (1024 - PD_INDEX(KERNEL_VMA)) * sizeof(uint32_t));
    return pd_phys;
}

void vmm_destroy_address_space(uint32_t pd_phys)
{
    KASSERT(pd_phys != V2P(kernel_pd) && pd_phys != read_cr3());
    uint32_t *pd = P2V(pd_phys);
    for (uint32_t i = 0; i < PD_INDEX(KERNEL_VMA); i++) {
        if (!(pd[i] & PTE_PRESENT))
            continue;
        uint32_t *pt = P2V(pd[i] & ~0xFFFu);
        for (int j = 0; j < 1024; j++)
            if (pt[j] & PTE_PRESENT)
                pmm_free(pt[j] & ~0xFFFu);
        pmm_free(pd[i] & ~0xFFFu);
    }
    pmm_free(pd_phys);
}

void vmm_switch(uint32_t pd_phys)
{
    if (read_cr3() != pd_phys)
        write_cr3(pd_phys);
}
