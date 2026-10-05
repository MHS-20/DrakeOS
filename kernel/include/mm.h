/* Physical frames, page tables and the kernel heap. */
#ifndef DRAKE_MM_H
#define DRAKE_MM_H

#include <stddef.h>
#include <stdint.h>
#include <multiboot.h>

/* Page table entry flags */
#define PTE_PRESENT  0x001
#define PTE_WRITE    0x002
#define PTE_USER     0x004
#define PDE_4MB      0x080

void pmm_init(const multiboot_info_t *mbi);
uint32_t pmm_alloc(void);                  /* zeroed 4 KB frame (physical address), 0 if none left */
void pmm_free(uint32_t phys);
uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);

void vmm_init(void);
uint32_t vmm_kernel_directory(void);       /* physical address of the kernel page directory */
int vmm_map(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t vmm_translate(uint32_t pd_phys, uint32_t virt);   /* physical address or 0 */
uint32_t vmm_create_address_space(void);   /* new directory sharing the kernel half */
void vmm_destroy_address_space(uint32_t pd_phys);
void vmm_switch(uint32_t pd_phys);
int vmm_user_range_ok(uint32_t pd_phys, uint32_t addr, uint32_t len);

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
size_t heap_used(void);

#endif
