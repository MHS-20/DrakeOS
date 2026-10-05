/*
 * DrakeFS: a flat file system for the data disk.
 *
 *   sector 0        superblock
 *   sectors 1-8     directory: 64 entries of 64 bytes
 *   sectors 9-...   file data, each file one contiguous extent (first-fit allocation)
 *
 * The directory is cached in memory and written back after every change.
 */
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <fs.h>
#include <mm.h>
#include <string.h>

#define MAGIC        "DRAKEFS1"
#define DIR_START    1
#define DIR_SECTORS  8
#define DATA_START   (DIR_START + DIR_SECTORS)
#define MAX_FILES    (DIR_SECTORS * ATA_SECTOR_SIZE / sizeof(struct dir_entry))

struct superblock {
    char magic[8];
    uint32_t total_sectors;
    uint32_t dir_start, dir_sectors, data_start;
    uint8_t pad[ATA_SECTOR_SIZE - 24];
} __attribute__((packed));

struct dir_entry {
    char name[VFS_NAME_MAX];
    uint32_t start;                    /* first sector of the extent */
    uint32_t size;                     /* bytes */
    uint32_t used;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct dir_entry) == 64, "directory entry must be 64 bytes");

static struct dir_entry dir[MAX_FILES];
static uint32_t total_sectors;
static int mounted;

static uint32_t sectors_for(uint32_t bytes)
{
    return (bytes + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
}

static int flush_directory(void)
{
    return ata_write(DIR_START, DIR_SECTORS, dir);
}

int drakefs_mount(void)
{
    struct superblock sb;
    mounted = 0;
    if (!ata_sector_count() || ata_read(0, 1, &sb) < 0 || memcmp(sb.magic, MAGIC, 8))
        return -1;
    if (ata_read(DIR_START, DIR_SECTORS, dir) < 0)
        return -1;
    total_sectors = sb.total_sectors;
    mounted = 1;
    klog(LOG_INFO, "drakefs: mounted, %u sectors", total_sectors);
    return 0;
}

int drakefs_format(void)
{
    if (!ata_sector_count())
        return -1;
    struct superblock sb;
    memset(&sb, 0, sizeof sb);
    memcpy(sb.magic, MAGIC, 8);
    sb.total_sectors = ata_sector_count();
    sb.dir_start = DIR_START;
    sb.dir_sectors = DIR_SECTORS;
    sb.data_start = DATA_START;
    memset(dir, 0, sizeof dir);
    if (ata_write(0, 1, &sb) < 0 || flush_directory() < 0)
        return -1;
    return drakefs_mount();
}

int drakefs_is_mounted(void) { return mounted; }

static struct dir_entry *find(const char *name)
{
    for (uint32_t i = 0; i < MAX_FILES; i++)
        if (dir[i].used && !strncmp(dir[i].name, name, VFS_NAME_MAX))
            return &dir[i];
    return NULL;
}

int drakefs_entry(int index, struct dirent *out)
{
    if (!mounted)
        return -1;
    for (uint32_t i = 0; i < MAX_FILES; i++) {
        if (dir[i].used && index-- == 0) {
            strlcpy(out->name, dir[i].name, sizeof out->name);
            out->size = dir[i].size;
            out->type = VFS_FILE;
            return 0;
        }
    }
    return -1;
}

int drakefs_lookup(const char *name, struct dirent *out)
{
    struct dir_entry *e = mounted ? find(name) : NULL;
    if (!e)
        return -1;
    strlcpy(out->name, e->name, sizeof out->name);
    out->size = e->size;
    out->type = VFS_FILE;
    return 0;
}

int drakefs_read(const char *name, uint32_t offset, void *buf, uint32_t len)
{
    struct dir_entry *e = mounted ? find(name) : NULL;
    if (!e)
        return -1;
    if (offset >= e->size)
        return 0;
    if (len > e->size - offset)
        len = e->size - offset;
    uint8_t sector[ATA_SECTOR_SIZE];
    uint32_t done = 0;
    while (done < len) {
        uint32_t pos = offset + done;
        if (ata_read(e->start + pos / ATA_SECTOR_SIZE, 1, sector) < 0)
            return -1;
        uint32_t chunk = ATA_SECTOR_SIZE - pos % ATA_SECTOR_SIZE;
        if (chunk > len - done)
            chunk = len - done;
        memcpy((uint8_t *)buf + done, sector + pos % ATA_SECTOR_SIZE, chunk);
        done += chunk;
    }
    return (int)done;
}

/* First-fit search for `count` free sectors, ignoring the extent of `skip`. */
static uint32_t find_space(uint32_t count, const struct dir_entry *skip)
{
    uint32_t candidate = DATA_START;
    for (;;) {
        int moved = 0;
        for (uint32_t i = 0; i < MAX_FILES; i++) {
            const struct dir_entry *e = &dir[i];
            if (!e->used || e == skip || !e->size)
                continue;
            uint32_t end = e->start + sectors_for(e->size);
            if (candidate < end && e->start < candidate + count) {
                candidate = end;               /* overlap: try just after this extent */
                moved = 1;
            }
        }
        if (!moved)
            break;
    }
    return candidate + count <= total_sectors ? candidate : 0;
}

int drakefs_write_file(const char *name, const void *data, uint32_t len)
{
    if (!mounted || !*name || strlen(name) >= VFS_NAME_MAX || strchr(name, '/'))
        return -1;
    uint32_t flags = irq_save();
    struct dir_entry *e = find(name);
    if (!e) {
        for (uint32_t i = 0; i < MAX_FILES && !e; i++)
            if (!dir[i].used)
                e = &dir[i];
        if (!e) {
            irq_restore(flags);
            return -1;
        }
        memset(e, 0, sizeof *e);
        strlcpy(e->name, name, VFS_NAME_MAX);
    }
    uint32_t count = sectors_for(len);
    uint32_t start = count <= sectors_for(e->size) && e->used ? e->start : find_space(count, e);
    if (count && !start) {
        irq_restore(flags);
        return -1;                             /* disk full */
    }
    int rc = 0;
    if (count) {
        /* Whole sectors from the caller's buffer, then the zero-padded tail. */
        uint32_t full = len / ATA_SECTOR_SIZE;
        if (full)
            rc = ata_write(start, full, data);
        if (!rc && len % ATA_SECTOR_SIZE) {
            uint8_t tail[ATA_SECTOR_SIZE] = { 0 };
            memcpy(tail, (const uint8_t *)data + full * ATA_SECTOR_SIZE, len % ATA_SECTOR_SIZE);
            rc = ata_write(start + full, 1, tail);
        }
    }
    if (!rc) {
        e->start = start;
        e->size = len;
        e->used = 1;
        rc = flush_directory();
    }
    irq_restore(flags);
    return rc;
}

int drakefs_unlink(const char *name)
{
    struct dir_entry *e = mounted ? find(name) : NULL;
    if (!e)
        return -1;
    e->used = 0;
    return flush_directory();
}

uint32_t drakefs_free_sectors(void)
{
    if (!mounted)
        return 0;
    uint32_t used = DATA_START;
    for (uint32_t i = 0; i < MAX_FILES; i++)
        if (dir[i].used)
            used += sectors_for(dir[i].size);
    return total_sectors > used ? total_sectors - used : 0;
}
