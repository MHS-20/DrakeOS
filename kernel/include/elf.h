/* Loader for static 32-bit ELF executables (user programs). */
#ifndef DRAKE_ELF_H
#define DRAKE_ELF_H

#include <stdint.h>

int elf_load(uint32_t pd, const void *image, uint32_t size, uint32_t *entry);

#endif
