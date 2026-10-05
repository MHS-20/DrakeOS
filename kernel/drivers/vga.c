/* Text-mode console: writes character/attribute cells, scrolls, and drives the hardware cursor. */
#include <cpu.h>
#include <layout.h>
#include <string.h>
#include <vga.h>

#define VGA_CTRL 0x3D4
#define VGA_DATA 0x3D5

static uint16_t *const cells = (uint16_t *)P2V(0xB8000);
static int row, col;
static uint8_t attr = VGA_ATTR(VGA_LIGHT_GREY, VGA_BLACK);
static int enabled = 1;

static inline uint16_t cell(char c, uint8_t a) { return (uint16_t)(uint8_t)c | (uint16_t)a << 8; }

void vga_sync_cursor(void)
{
    if (!enabled)
        return;
    uint16_t pos = (uint16_t)(row * VGA_COLS + col);
    outb(VGA_CTRL, 14);
    outb(VGA_DATA, pos >> 8);
    outb(VGA_CTRL, 15);
    outb(VGA_DATA, pos & 0xFF);
}

static void scroll(void)
{
    memmove(cells, cells + VGA_COLS, (VGA_ROWS - 1) * VGA_COLS * sizeof *cells);
    for (int i = 0; i < VGA_COLS; i++)
        cells[(VGA_ROWS - 1) * VGA_COLS + i] = cell(' ', attr);
    row = VGA_ROWS - 1;
}

void vga_clear(void)
{
    if (enabled)
        for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
            cells[i] = cell(' ', attr);
    row = col = 0;
    vga_sync_cursor();
}

void vga_init(void)
{
    /* Cursor shape: scan lines 14-15 of the 16-line character cell. */
    outb(VGA_CTRL, 0x0A);
    outb(VGA_DATA, (inb(VGA_DATA) & 0xC0) | 14);
    outb(VGA_CTRL, 0x0B);
    outb(VGA_DATA, (inb(VGA_DATA) & 0xE0) | 15);
    vga_clear();
}

static void put_raw(char c)
{
    switch (c) {
    case '\n':
        col = 0;
        row++;
        break;
    case '\r':
        col = 0;
        break;
    case '\b':
        if (col > 0)
            col--;
        else if (row > 0)
            row--, col = VGA_COLS - 1;
        cells[row * VGA_COLS + col] = cell(' ', attr);
        break;
    case '\t':
        col = (col + 8) & ~7;
        break;
    default:
        cells[row * VGA_COLS + col] = cell(c, attr);
        col++;
    }
    if (col >= VGA_COLS) {
        col = 0;
        row++;
    }
    if (row >= VGA_ROWS)
        scroll();
}

void vga_putc(char c)
{
    if (!enabled)
        return;
    put_raw(c);
    vga_sync_cursor();
}

void vga_write(const char *s, size_t len)
{
    if (!enabled)
        return;
    while (len--)
        put_raw(*s++);
    vga_sync_cursor();
}

void vga_set_attr(uint8_t a) { attr = a; }
uint8_t vga_get_attr(void) { return attr; }

void vga_recolor(uint8_t a)
{
    attr = a;
    if (enabled)
        for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
            cells[i] = cell((char)(cells[i] & 0xFF), a);
}

void vga_set_enabled(int on)
{
    enabled = on;
    if (on)
        vga_sync_cursor();
}
