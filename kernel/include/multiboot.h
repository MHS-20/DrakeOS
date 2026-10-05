/* Multiboot (version 1) information structure, as passed in EBX by GRUB and by the DrakeOS loader. */
#ifndef DRAKE_MULTIBOOT_H
#define DRAKE_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MB_INFO_MEMORY      (1u << 0)
#define MB_INFO_CMDLINE     (1u << 2)
#define MB_INFO_MODS        (1u << 3)
#define MB_INFO_MEM_MAP     (1u << 6)
#define MB_INFO_LOADER_NAME (1u << 9)

typedef struct {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;          /* KB below 1 MB / above 1 MB */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
} __attribute__((packed)) multiboot_info_t;

typedef struct {
    uint32_t size;                          /* size of the rest of the entry (not counting this field) */
    uint64_t addr;
    uint64_t len;
    uint32_t type;                          /* 1 = available RAM */
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct {
    uint32_t mod_start, mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

#endif
