/* DrakeOS user library: system call wrappers, strings, printf. */
#ifndef DRAKE_USER_H
#define DRAKE_USER_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define O_READ   1
#define O_WRITE  2
#define O_CREATE 4
#define O_TRUNC  8

#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* system calls (int 0x80) */
__attribute__((noreturn)) void exit(int status);
int write(int fd, const void *buf, size_t len);
int read(int fd, void *buf, size_t len);
void yield(void);
int getpid(void);
void sleep_ms(unsigned ms);
int open(const char *path, int flags);
int close(int fd);
int exec(const char *path, char *const argv[]);   /* returns the child pid */
int wait(int pid);                                 /* returns the child's exit status */

/* strings */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int atoi(const char *s);

/* output */
int puts(const char *s);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif
