/* Read-only flat file system embedded in the kernel image. */
#include <console.h>
#include <fs.h>
#include <string.h>
#include "initrd_format.h"

static const uint8_t *image;
static const struct initrd_header *header;
static const struct initrd_entry *entries;

void initrd_init(const void *data, uint32_t size)
{
    header = data;
    if (size < sizeof *header || header->magic != INITRD_MAGIC)
        panic("initrd: bad image");
    image = data;
    entries = (const struct initrd_entry *)(header + 1);
    klog(LOG_INFO, "initrd: %u files", header->count);
}

int initrd_lookup(const char *name, const void **data, uint32_t *size)
{
    for (uint32_t i = 0; i < header->count; i++) {
        if (!strncmp(entries[i].name, name, INITRD_NAME_MAX)) {
            *data = image + entries[i].offset;
            *size = entries[i].size;
            return 0;
        }
    }
    return -1;
}

int initrd_entry(int index, struct dirent *out)
{
    if (index < 0 || (uint32_t)index >= header->count)
        return -1;
    strlcpy(out->name, entries[index].name, sizeof out->name);
    out->size = entries[index].size;
    out->type = VFS_FILE;
    return 0;
}
