/* System call wrappers: number in eax, arguments in ebx, ecx, edx, result in eax. */
#include <drake.h>

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_YIELD  3
#define SYS_GETPID 4
#define SYS_SLEEP  5
#define SYS_OPEN   6
#define SYS_CLOSE  7
#define SYS_EXEC   8
#define SYS_WAIT   9

static inline int syscall3(int num, int a, int b, int c)
{
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

void exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;)
        ;
}

int write(int fd, const void *buf, size_t len) { return syscall3(SYS_WRITE, fd, (int)buf, (int)len); }
int read(int fd, void *buf, size_t len) { return syscall3(SYS_READ, fd, (int)buf, (int)len); }
void yield(void) { syscall3(SYS_YIELD, 0, 0, 0); }
int getpid(void) { return syscall3(SYS_GETPID, 0, 0, 0); }
void sleep_ms(unsigned ms) { syscall3(SYS_SLEEP, (int)ms, 0, 0); }
int open(const char *path, int flags) { return syscall3(SYS_OPEN, (int)path, flags, 0); }
int close(int fd) { return syscall3(SYS_CLOSE, fd, 0, 0); }
int exec(const char *path, char *const argv[]) { return syscall3(SYS_EXEC, (int)path, (int)argv, 0); }
int wait(int pid) { return syscall3(SYS_WAIT, pid, 0, 0); }
