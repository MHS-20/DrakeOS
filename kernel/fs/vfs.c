/*
 * VFS: resolves a path to one of three backends and hands out reference-counted files.
 *   /dev/console  keyboard (canonical line input with echo) and screen
 *   /disk/NAME    DrakeFS; writes are buffered and committed to disk on close
 *   /NAME         initrd (read-only)
 */
#include <console.h>
#include <fs.h>
#include <keyboard.h>
#include <mm.h>
#include <string.h>

extern const uint8_t initrd_start[], initrd_end[];   /* initrd_blob.asm */

#define DISK_PREFIX "/disk/"

/* ------------------------------------------------------------------ console */

static int console_read(struct file *f, void *buf, uint32_t len)
{
    (void)f;
    char *out = buf;
    uint32_t n = 0;
    while (n < len) {
        int c = kbd_getchar();
        if (c == '\b') {
            if (n > 0) {
                n--;
                console_write("\b", 1);
            }
            continue;
        }
        if (c == 4 && n == 0)                  /* Ctrl+D on an empty line: end of input */
            return 0;
        if (c >= 0x80 || (c < ' ' && c != '\n' && c != '\t'))
            continue;
        out[n++] = (char)c;
        console_putc((char)c);
        if (c == '\n')
            break;
    }
    return (int)n;
}

static int console_write_op(struct file *f, const void *buf, uint32_t len)
{
    (void)f;
    console_write(buf, len);
    return (int)len;
}

static const struct file_ops console_ops = { console_read, console_write_op, NULL };

/* ------------------------------------------------------------------ initrd files */

static int initrd_read(struct file *f, void *buf, uint32_t len)
{
    if (f->offset >= f->size)
        return 0;
    if (len > f->size - f->offset)
        len = f->size - f->offset;
    memcpy(buf, (const uint8_t *)f->priv + f->offset, len);
    f->offset += len;
    return (int)len;
}

static const struct file_ops initrd_ops = { initrd_read, NULL, NULL };

/* ------------------------------------------------------------------ DrakeFS files */

struct disk_file {
    char name[VFS_NAME_MAX];
    uint8_t *wbuf;                             /* pending contents when opened for writing */
    uint32_t wcap;
    int dirty;
};

static int disk_read(struct file *f, void *buf, uint32_t len)
{
    struct disk_file *d = f->priv;
    int n = drakefs_read(d->name, f->offset, buf, len);
    if (n > 0)
        f->offset += n;
    return n;
}

static int disk_write(struct file *f, const void *buf, uint32_t len)
{
    struct disk_file *d = f->priv;
    uint32_t end = f->offset + len;
    if (end > d->wcap) {
        uint32_t cap = d->wcap ? d->wcap : 512;
        while (cap < end)
            cap *= 2;
        uint8_t *grown = kzalloc(cap);
        if (!grown)
            return -1;
        if (d->wbuf) {
            memcpy(grown, d->wbuf, f->size);
            kfree(d->wbuf);
        }
        d->wbuf = grown;
        d->wcap = cap;
    }
    memcpy(d->wbuf + f->offset, buf, len);
    f->offset = end;
    if (end > f->size)
        f->size = end;
    d->dirty = 1;
    return (int)len;
}

static void disk_close(struct file *f)
{
    struct disk_file *d = f->priv;
    if (d->dirty && drakefs_write_file(d->name, d->wbuf, f->size) < 0)
        klog(LOG_ERROR, "vfs: writing /disk/%s failed", d->name);
    kfree(d->wbuf);
    kfree(d);
}

static const struct file_ops disk_ops = { disk_read, disk_write, disk_close };

/* ------------------------------------------------------------------ generic layer */

void vfs_init(void)
{
    initrd_init(initrd_start, initrd_end - initrd_start);
}

static struct file *new_file(const struct file_ops *ops, void *priv, uint32_t size, int flags)
{
    struct file *f = kzalloc(sizeof *f);
    if (!f)
        return NULL;
    f->ops = ops;
    f->priv = priv;
    f->size = size;
    f->flags = flags;
    f->refs = 1;
    return f;
}

static struct file *open_disk(const char *name, int flags)
{
    struct dirent de;
    int exists = drakefs_lookup(name, &de) == 0;
    if (!drakefs_is_mounted() || (!exists && !(flags & O_CREATE)))
        return NULL;
    struct disk_file *d = kzalloc(sizeof *d);
    if (!d)
        return NULL;
    strlcpy(d->name, name, sizeof d->name);
    struct file *f = new_file(&disk_ops, d, 0, flags);
    if (!f) {
        kfree(d);
        return NULL;
    }
    if (exists && !(flags & O_TRUNC)) {
        f->size = de.size;
        if (flags & O_WRITE) {                 /* keep existing contents for appending writes */
            d->wcap = de.size ? de.size : 512;
            d->wbuf = kzalloc(d->wcap);
            if (!d->wbuf || drakefs_read(name, 0, d->wbuf, de.size) < 0) {
                vfs_close(f);
                return NULL;
            }
        }
    } else if (flags & O_WRITE) {
        d->dirty = 1;                          /* creating or truncating writes even if empty */
    }
    return f;
}

struct file *vfs_open(const char *path, int flags)
{
    if (!strcmp(path, "/dev/console"))
        return new_file(&console_ops, NULL, 0, flags);
    if (!strncmp(path, DISK_PREFIX, strlen(DISK_PREFIX)))
        return open_disk(path + strlen(DISK_PREFIX), flags);
    if (path[0] != '/' || (flags & O_WRITE))
        return NULL;
    const void *data;
    uint32_t size;
    if (initrd_lookup(path + 1, &data, &size) < 0)
        return NULL;
    return new_file(&initrd_ops, (void *)data, size, flags);
}

int vfs_read(struct file *f, void *buf, uint32_t len)
{
    if (!(f->flags & O_READ) || !f->ops->read)
        return -1;
    return f->ops->read(f, buf, len);
}

int vfs_write(struct file *f, const void *buf, uint32_t len)
{
    if (!(f->flags & O_WRITE) || !f->ops->write)
        return -1;
    return f->ops->write(f, buf, len);
}

struct file *vfs_dup(struct file *f)
{
    f->refs++;
    return f;
}

void vfs_close(struct file *f)
{
    if (--f->refs > 0)
        return;
    if (f->ops->close)
        f->ops->close(f);
    kfree(f);
}

int vfs_readdir(const char *path, int index, struct dirent *out)
{
    if (!strcmp(path, "/disk") || !strcmp(path, DISK_PREFIX))
        return drakefs_entry(index, out);
    if (!strcmp(path, "/dev") || !strcmp(path, "/dev/")) {
        if (index != 0)
            return -1;
        strlcpy(out->name, "console", sizeof out->name);
        out->size = 0;
        out->type = VFS_DEVICE;
        return 0;
    }
    if (strcmp(path, "/"))
        return -1;
    /* The root lists the two mount points, then the initrd files. */
    if (index < 2) {
        strlcpy(out->name, index == 0 ? "dev" : "disk", sizeof out->name);
        out->size = 0;
        out->type = VFS_DIR;
        return 0;
    }
    return initrd_entry(index - 2, out);
}

int vfs_stat(const char *path, struct dirent *out)
{
    if (!strncmp(path, DISK_PREFIX, strlen(DISK_PREFIX)))
        return drakefs_lookup(path + strlen(DISK_PREFIX), out);
    const void *data;
    if (path[0] == '/' && initrd_lookup(path + 1, &data, &out->size) == 0) {
        strlcpy(out->name, path + 1, sizeof out->name);
        out->type = VFS_FILE;
        return 0;
    }
    return -1;
}

int vfs_unlink(const char *path)
{
    if (strncmp(path, DISK_PREFIX, strlen(DISK_PREFIX)))
        return -1;                             /* the initrd is read-only */
    return drakefs_unlink(path + strlen(DISK_PREFIX));
}

int vfs_read_all(const char *path, void **data, uint32_t *size)
{
    struct file *f = vfs_open(path, O_READ);
    if (!f)
        return -1;
    uint8_t *buf = kmalloc(f->size ? f->size : 1);
    int n = buf ? vfs_read(f, buf, f->size) : -1;
    if (n < 0 || (uint32_t)n != f->size) {
        kfree(buf);
        vfs_close(f);
        return -1;
    }
    *data = buf;
    *size = f->size;
    vfs_close(f);
    return 0;
}
