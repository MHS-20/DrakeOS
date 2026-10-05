/* Maps every PT_LOAD segment of an ELF image into a user address space. */
#include <elf.h>
#include <layout.h>
#include <mm.h>
#include <string.h>

typedef struct {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed)) elf_header_t;

typedef struct {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __attribute__((packed)) elf_phdr_t;

#define PT_LOAD 1
#define ET_EXEC 2
#define EM_386  3

static int load_segment(uint32_t pd, const uint8_t *image, const elf_phdr_t *ph)
{
    if (ph->memsz < ph->filesz || ph->vaddr < 0x1000 || ph->vaddr + ph->memsz > KERNEL_VMA ||
        ph->vaddr + ph->memsz < ph->vaddr)
        return -1;
    for (uint32_t va = ALIGN_DOWN(ph->vaddr, PAGE_SIZE); va < ph->vaddr + ph->memsz; va += PAGE_SIZE) {
        if (vmm_translate(pd, va))
            continue;                               /* page shared with the previous segment */
        uint32_t frame = pmm_alloc();               /* zeroed, which also clears .bss */
        if (!frame || vmm_map(pd, va, frame, PTE_USER | PTE_WRITE) < 0)
            return -1;
    }
    /* Copy file bytes page by page through the kernel's view of each frame. */
    for (uint32_t done = 0; done < ph->filesz;) {
        uint32_t va = ph->vaddr + done;
        uint32_t chunk = PAGE_SIZE - (va % PAGE_SIZE);
        if (chunk > ph->filesz - done)
            chunk = ph->filesz - done;
        memcpy(P2V(vmm_translate(pd, va)), image + ph->offset + done, chunk);
        done += chunk;
    }
    return 0;
}

int elf_load(uint32_t pd, const void *image, uint32_t size, uint32_t *entry)
{
    const elf_header_t *eh = image;
    if (size < sizeof *eh || memcmp(eh->ident, "\x7f" "ELF", 4) || eh->ident[4] != 1 ||
        eh->type != ET_EXEC || eh->machine != EM_386 || eh->phentsize != sizeof(elf_phdr_t) ||
        eh->phoff + (uint32_t)eh->phnum * sizeof(elf_phdr_t) > size)
        return -1;
    const elf_phdr_t *ph = (const elf_phdr_t *)((const uint8_t *)image + eh->phoff);
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD)
            continue;
        if (ph[i].offset + ph[i].filesz > size || load_segment(pd, image, &ph[i]) < 0)
            return -1;
    }
    *entry = eh->entry;
    return eh->entry < KERNEL_VMA ? 0 : -1;
}
