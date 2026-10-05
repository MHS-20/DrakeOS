/* Kernel console: every message goes to the VGA screen and the serial log. */
#include <console.h>
#include <cpu.h>
#include <serial.h>
#include <vga.h>

void console_write(const char *s, size_t len)
{
    uint32_t flags = irq_save();
    vga_write(s, len);
    serial_write(s, len);
    irq_restore(flags);
}

void console_putc(char c)
{
    console_write(&c, 1);
}

int kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    console_write(buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
    return n;
}

/* Log messages go to the serial port only, so they never clutter the screen. */
void klog(enum log_level level, const char *fmt, ...)
{
    static const char *const names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    char buf[512];
    int n = snprintf(buf, sizeof buf, "[%s] ", names[level]);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    uint32_t flags = irq_save();
    serial_write(buf, n);
    irq_restore(flags);
}

void panic(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    cli();
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    vga_set_enabled(1);
    vga_set_attr(VGA_ATTR(VGA_WHITE, VGA_RED));
    kprintf("\nKERNEL PANIC: %s\nSystem halted.\n", buf);
    halt_forever();
}
