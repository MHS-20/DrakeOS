/* On-disk layout of the initrd, shared with tools/mkinitrd.c. All fields little-endian. */
#ifndef DRAKE_INITRD_FORMAT_H
#define DRAKE_INITRD_FORMAT_H

#include <stdint.h>

#define INITRD_MAGIC 0x494B5244u      /* "DRKI" */
#define INITRD_NAME_MAX 48

struct initrd_header {
    uint32_t magic;
    uint32_t count;
};

struct initrd_entry {
    char name[INITRD_NAME_MAX];
    uint32_t offset;                   /* from the start of the image */
    uint32_t size;
};

#endif
