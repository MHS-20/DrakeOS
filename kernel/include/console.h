/* Kernel output: formatted printing to screen and serial, log levels, panic. */
#ifndef DRAKE_CONSOLE_H
#define DRAKE_CONSOLE_H

#include <stdarg.h>
#include <stddef.h>

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

void console_write(const char *s, size_t len);   /* screen + serial */
void console_putc(char c);
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void klog(enum log_level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
__attribute__((noreturn)) void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define KASSERT(cond) do { if (!(cond)) panic("assertion failed: %s (%s:%d)", #cond, __FILE__, __LINE__); } while (0)

#endif
