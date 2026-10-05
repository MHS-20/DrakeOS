/* Virtual memory layout shared by the kernel. See docs/architecture.md. */
#ifndef DRAKE_LAYOUT_H
#define DRAKE_LAYOUT_H

#include <stdint.h>

#define KERNEL_VMA        0xC0000000u            /* physical 0 is mapped here */
#define PHYS_MAP_LIMIT    0x10000000u            /* 256 MB of RAM linearly mapped */
#define HEAP_START        0xD0000000u
#define HEAP_MAX          0xD4000000u            /* 64 MB, page tables preallocated */
#define USER_CODE_BASE    0x08048000u
#define USER_STACK_TOP    0xBFFFF000u
#define USER_STACK_PAGES  4
#define PAGE_SIZE         4096u

#define P2V(p) ((void *)((uint32_t)(p) + KERNEL_VMA))
#define V2P(v) ((uint32_t)(v) - KERNEL_VMA)

#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

/* Symbols exported by kernel/linker.ld */
extern char kernel_phys_start[], kernel_phys_end[];

#endif
