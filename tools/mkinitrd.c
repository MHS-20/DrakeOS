/* Host tool: packs files into a DrakeOS initrd image.  Usage: mkinitrd OUT FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/fs/initrd_format.h"

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s OUT [FILE...]\n", argv[0]);
        return 1;
    }
    int count = argc - 2;
    struct initrd_header hdr = { INITRD_MAGIC, (uint32_t)count };
    struct initrd_entry *entries = calloc(count ? count : 1, sizeof *entries);
    unsigned char **data = calloc(count ? count : 1, sizeof *data);
    uint32_t offset = sizeof hdr + count * sizeof *entries;

    for (int i = 0; i < count; i++) {
        const char *path = argv[i + 2];
        FILE *f = fopen(path, "rb");
        if (!f) {
            perror(path);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        data[i] = malloc(size ? size : 1);
        if (fread(data[i], 1, size, f) != (size_t)size) {
            perror(path);
            return 1;
        }
        fclose(f);
        if (strlen(basename_of(path)) >= INITRD_NAME_MAX) {
            fprintf(stderr, "%s: name too long\n", path);
            return 1;
        }
        strncpy(entries[i].name, basename_of(path), INITRD_NAME_MAX - 1);
        entries[i].offset = offset;
        entries[i].size = (uint32_t)size;
        offset += (uint32_t)((size + 3) & ~3L);
    }

    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        perror(argv[1]);
        return 1;
    }
    fwrite(&hdr, sizeof hdr, 1, out);
    fwrite(entries, sizeof *entries, count, out);
    static const char pad[4];
    for (int i = 0; i < count; i++) {
        fwrite(data[i], 1, entries[i].size, out);
        fwrite(pad, 1, ((entries[i].size + 3) & ~3u) - entries[i].size, out);
    }
    fclose(out);
    printf("initrd: %d files, %u bytes\n", count, offset);
    return 0;
}
