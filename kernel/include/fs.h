/*
 * Virtual file system. The namespace is fixed:
 *   /            initrd (read-only, flat, embedded in the kernel image)
 *   /disk/       DrakeFS on the ATA primary-slave disk (read-write, flat)
 *   /dev/console the screen (write) and keyboard (read)
 */
#ifndef DRAKE_FS_H
#define DRAKE_FS_H

#include <stdint.h>

#define O_READ   1
#define O_WRITE  2
#define O_CREATE 4
#define O_TRUNC  8

enum vfs_type { VFS_FILE = 1, VFS_DIR, VFS_DEVICE };

#define VFS_NAME_MAX 48

struct dirent {
    char name[VFS_NAME_MAX];
    uint32_t size;
    enum vfs_type type;
};

struct file;

struct file_ops {
    int (*read)(struct file *f, void *buf, uint32_t len);
    int (*write)(struct file *f, const void *buf, uint32_t len);
    void (*close)(struct file *f);
};

struct file {
    const struct file_ops *ops;
    void *priv;
    uint32_t offset;
    uint32_t size;
    int flags;
    int refs;
};

void vfs_init(void);
struct file *vfs_open(const char *path, int flags);
int vfs_read(struct file *f, void *buf, uint32_t len);
int vfs_write(struct file *f, const void *buf, uint32_t len);
void vfs_close(struct file *f);
struct file *vfs_dup(struct file *f);
int vfs_readdir(const char *path, int index, struct dirent *out);
int vfs_stat(const char *path, struct dirent *out);
int vfs_unlink(const char *path);
int vfs_read_all(const char *path, void **data, uint32_t *size);   /* kmalloc'd copy */

/* initrd */
void initrd_init(const void *image, uint32_t size);
int initrd_lookup(const char *name, const void **data, uint32_t *size);
int initrd_entry(int index, struct dirent *out);

/* DrakeFS */
int drakefs_mount(void);             /* 0 if a formatted disk is present */
int drakefs_format(void);
int drakefs_is_mounted(void);
int drakefs_entry(int index, struct dirent *out);
int drakefs_lookup(const char *name, struct dirent *out);
int drakefs_read(const char *name, uint32_t offset, void *buf, uint32_t len);
int drakefs_write_file(const char *name, const void *data, uint32_t len);
int drakefs_unlink(const char *name);
uint32_t drakefs_free_sectors(void);

#endif
