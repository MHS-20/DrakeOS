/* 16550 UART on COM1, used for logging (QEMU: -serial stdio). */
#ifndef DRAKE_SERIAL_H
#define DRAKE_SERIAL_H

#include <stddef.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t len);

#endif
