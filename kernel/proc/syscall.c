/* The int 0x80 system call interface. Every user pointer is validated before use. */
#include <arch.h>
#include <console.h>
#include <fs.h>
#include <mm.h>
#include <proc.h>
#include <string.h>
#include <syscall.h>

#define PATH_MAX_LEN 128
#define MAX_ARGS 15

static int user_ok(const void *p, uint32_t len)
{
    return vmm_user_range_ok(current_process()->pd, (uint32_t)p, len);
}

/* Copies a NUL-terminated user string into buf; -1 if it is unmapped or too long. */
static int copy_user_string(char *buf, const char *user, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (!user_ok(user + i, 1))
            return -1;
        buf[i] = user[i];
        if (!buf[i])
            return 0;
    }
    return -1;
}

static struct file *fd_get(int fd)
{
    return fd >= 0 && fd < MAX_FDS ? current_process()->fds[fd] : NULL;
}

static int sys_write(int fd, const void *buf, uint32_t len)
{
    struct file *f = fd_get(fd);
    if (!f || !user_ok(buf, len))
        return -1;
    return vfs_write(f, buf, len);
}

static int sys_read(int fd, void *buf, uint32_t len)
{
    struct file *f = fd_get(fd);
    if (!f || !user_ok(buf, len))
        return -1;
    return vfs_read(f, buf, len);
}

static int sys_open(const char *upath, int flags)
{
    char path[PATH_MAX_LEN];
    if (copy_user_string(path, upath, sizeof path) < 0)
        return -1;
    struct process *p = current_process();
    for (int fd = 0; fd < MAX_FDS; fd++) {
        if (!p->fds[fd]) {
            p->fds[fd] = vfs_open(path, flags);
            return p->fds[fd] ? fd : -1;
        }
    }
    return -1;
}

static int sys_close(int fd)
{
    struct file *f = fd_get(fd);
    if (!f)
        return -1;
    vfs_close(f);
    current_process()->fds[fd] = NULL;
    return 0;
}

static int sys_exec(const char *upath, char *const *uargv)
{
    char path[PATH_MAX_LEN], strings[MAX_ARGS][PATH_MAX_LEN];
    char *argv[MAX_ARGS];
    int argc = 0;
    if (copy_user_string(path, upath, sizeof path) < 0)
        return -1;
    if (uargv) {
        for (; argc < MAX_ARGS; argc++) {
            if (!user_ok(&uargv[argc], sizeof *uargv))
                return -1;
            if (!uargv[argc])
                break;
            if (copy_user_string(strings[argc], uargv[argc], PATH_MAX_LEN) < 0)
                return -1;
            argv[argc] = strings[argc];
        }
    }
    return process_spawn(path, argc, argv, 0);
}

static void syscall_handler(registers_t *r)
{
    int32_t ret;
    switch (r->eax) {
    case SYS_EXIT:   process_exit((int)r->ebx);
    case SYS_WRITE:  ret = sys_write((int)r->ebx, (const void *)r->ecx, r->edx); break;
    case SYS_READ:   ret = sys_read((int)r->ebx, (void *)r->ecx, r->edx); break;
    case SYS_YIELD:  sched_yield(); ret = 0; break;
    case SYS_GETPID: ret = current_process()->pid; break;
    case SYS_SLEEP:  process_sleep(r->ebx); ret = 0; break;
    case SYS_OPEN:   ret = sys_open((const char *)r->ebx, (int)r->ecx); break;
    case SYS_CLOSE:  ret = sys_close((int)r->ebx); break;
    case SYS_EXEC:   ret = sys_exec((const char *)r->ebx, (char *const *)r->ecx); break;
    case SYS_WAIT:   ret = process_wait((int)r->ebx); break;
    default:         ret = -1;
    }
    r->eax = (uint32_t)ret;
}

void syscall_init(void)
{
    register_interrupt_handler(SYSCALL_VECTOR, syscall_handler);
}
