/* VGA 80x25 text-mode console driver (memory-mapped at physical 0xB8000). */
#ifndef DRAKE_VGA_H
#define DRAKE_VGA_H

#include <stddef.h>
#include <stdint.h>

#define VGA_COLS 80
#define VGA_ROWS 25

enum vga_color {
    VGA_BLACK, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN, VGA_LIGHT_RED,
    VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE,
};

#define VGA_ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s, size_t len);
void vga_set_attr(uint8_t attr);
uint8_t vga_get_attr(void);
void vga_recolor(uint8_t attr);          /* change the colour of everything on screen */
void vga_set_enabled(int enabled);       /* off while a graphics mode owns the screen */
void vga_sync_cursor(void);

#endif
