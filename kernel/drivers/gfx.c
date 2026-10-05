/*
 * VGA 320x200x256 (mode 13h) without the BIOS: the mode is set by writing the VGA register
 * values directly, so the kernel can switch at any time from protected mode.
 *
 * Mode 13h overwrites planes 0-2, which in text mode hold the characters, attributes and the
 * font. gfx_init() saves the font and palette once; gfx_enter() saves the text screen;
 * gfx_leave() restores all three.
 */
#include <cpu.h>
#include <drivers.h>
#include <layout.h>
#include <string.h>
#include <vga.h>

#define MISC_WRITE  0x3C2
#define SEQ_INDEX   0x3C4
#define SEQ_DATA    0x3C5
#define DAC_READ    0x3C7
#define DAC_WRITE   0x3C8
#define DAC_DATA    0x3C9
#define GC_INDEX    0x3CE
#define GC_DATA     0x3CF
#define CRTC_INDEX  0x3D4
#define CRTC_DATA   0x3D5
#define INPUT_STAT  0x3DA
#define AC_INDEX    0x3C0

struct vga_regs {
    uint8_t misc;
    uint8_t seq[5];
    uint8_t crtc[25];
    uint8_t gc[9];
    uint8_t ac[21];
};

/* Register dumps for the two standard modes (OSDev wiki "VGA Hardware", modes.c by C. Giese). */
static const struct vga_regs mode_text_80x25 = {
    0x67,
    { 0x03, 0x00, 0x03, 0x00, 0x02 },
    { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00,
      0x50, 0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
      0x3F, 0x0C, 0x00, 0x0F, 0x08, 0x00 },
};

static const struct vga_regs mode_320x200x256 = {
    0x63,
    { 0x03, 0x01, 0x0F, 0x00, 0x0E },
    { 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
      0x0F, 0x41, 0x00, 0x0F, 0x00, 0x00 },
};

#define FONT_BYTES (256 * 32)          /* 256 glyphs, 32-byte slots, 16 lines used */

static uint8_t saved_font[FONT_BYTES];
static uint8_t saved_palette[768];
static uint16_t saved_text[VGA_COLS * VGA_ROWS];
static uint8_t backbuf[GFX_WIDTH * GFX_HEIGHT];
static int active;

static uint8_t *const vram = (uint8_t *)P2V(0xA0000);

static void write_regs(const struct vga_regs *m)
{
    outb(MISC_WRITE, m->misc);
    for (int i = 0; i < 5; i++) {
        outb(SEQ_INDEX, i);
        outb(SEQ_DATA, m->seq[i]);
    }
    /* Unlock CRTC registers 0-7 (bit 7 of register 0x11) before writing them. */
    outb(CRTC_INDEX, 0x03);
    outb(CRTC_DATA, inb(CRTC_DATA) | 0x80);
    outb(CRTC_INDEX, 0x11);
    outb(CRTC_DATA, inb(CRTC_DATA) & ~0x80);
    for (int i = 0; i < 25; i++) {
        uint8_t v = m->crtc[i];
        if (i == 0x03)
            v |= 0x80;
        if (i == 0x11)
            v &= ~0x80;
        outb(CRTC_INDEX, i);
        outb(CRTC_DATA, v);
    }
    for (int i = 0; i < 9; i++) {
        outb(GC_INDEX, i);
        outb(GC_DATA, m->gc[i]);
    }
    for (int i = 0; i < 21; i++) {
        inb(INPUT_STAT);                 /* resets the attribute controller's index/data flip-flop */
        outb(AC_INDEX, i);
        outb(AC_INDEX, m->ac[i]);
    }
    inb(INPUT_STAT);
    outb(AC_INDEX, 0x20);                /* re-enable the display */
}

/* Gives linear access to plane 2 (the font plane) at 0xA0000, as in the text-mode font load. */
static void select_font_plane(int writing)
{
    outb(SEQ_INDEX, 2);  outb(SEQ_DATA, writing ? 0x04 : 0x03);   /* map mask: plane 2 */
    outb(SEQ_INDEX, 4);  outb(SEQ_DATA, 0x06);                    /* sequential, no odd/even */
    outb(GC_INDEX, 4);   outb(GC_DATA, 0x02);                     /* read map: plane 2 */
    outb(GC_INDEX, 5);   outb(GC_DATA, 0x00);                     /* read/write mode 0 */
    outb(GC_INDEX, 6);   outb(GC_DATA, 0x04);                     /* map A0000-AFFFF, no odd/even */
}

static void restore_text_planes(void)
{
    outb(SEQ_INDEX, 2);  outb(SEQ_DATA, mode_text_80x25.seq[2]);
    outb(SEQ_INDEX, 4);  outb(SEQ_DATA, mode_text_80x25.seq[4]);
    outb(GC_INDEX, 4);   outb(GC_DATA, mode_text_80x25.gc[4]);
    outb(GC_INDEX, 5);   outb(GC_DATA, mode_text_80x25.gc[5]);
    outb(GC_INDEX, 6);   outb(GC_DATA, mode_text_80x25.gc[6]);
}

static void read_palette(uint8_t *pal)
{
    outb(DAC_READ, 0);
    for (int i = 0; i < 768; i++)
        pal[i] = inb(DAC_DATA);
}

static void write_palette(const uint8_t *pal)
{
    outb(DAC_WRITE, 0);
    for (int i = 0; i < 768; i++)
        outb(DAC_DATA, pal[i]);
}

void gfx_init(void)
{
    uint32_t flags = irq_save();
    select_font_plane(0);
    memcpy(saved_font, vram, FONT_BYTES);
    restore_text_planes();
    read_palette(saved_palette);
    irq_restore(flags);
}

void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    outb(DAC_WRITE, index);
    outb(DAC_DATA, r & 0x3F);
    outb(DAC_DATA, g & 0x3F);
    outb(DAC_DATA, b & 0x3F);
}

/* 16 text colours, a 6x6x6 colour cube (16-231) and a grey ramp (232-255). */
static void default_palette(void)
{
    /* The text-mode EGA colours live at DAC indexes 0-5, 20, 7, 56-63 (see the AC table). */
    static const uint8_t ega_dac[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };
    for (int i = 0; i < 16; i++) {
        const uint8_t *c = &saved_palette[ega_dac[i] * 3];
        gfx_set_palette(i, c[0], c[1], c[2]);
    }
    for (int i = 0; i < 216; i++)
        gfx_set_palette(16 + i, (i / 36) * 63 / 5, (i / 6 % 6) * 63 / 5, (i % 6) * 63 / 5);
    for (int i = 0; i < 24; i++)
        gfx_set_palette(232 + i, i * 63 / 23, i * 63 / 23, i * 63 / 23);
}

void gfx_enter(void)
{
    if (active)
        return;
    uint32_t flags = irq_save();
    memcpy(saved_text, P2V(0xB8000), sizeof saved_text);
    vga_set_enabled(0);
    write_regs(&mode_320x200x256);
    default_palette();
    memset(backbuf, 0, sizeof backbuf);
    memset(vram, 0, sizeof backbuf);
    active = 1;
    irq_restore(flags);
}

void gfx_leave(void)
{
    if (!active)
        return;
    uint32_t flags = irq_save();
    write_regs(&mode_text_80x25);
    select_font_plane(1);
    memcpy(vram, saved_font, FONT_BYTES);
    restore_text_planes();
    write_palette(saved_palette);
    memcpy(P2V(0xB8000), saved_text, sizeof saved_text);
    active = 0;
    vga_set_enabled(1);
    irq_restore(flags);
}

int gfx_active(void) { return active; }

void gfx_put_pixel(int x, int y, uint8_t color)
{
    if (x >= 0 && x < GFX_WIDTH && y >= 0 && y < GFX_HEIGHT)
        backbuf[y * GFX_WIDTH + x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            gfx_put_pixel(i, j, color);
}

/* Bresenham's line algorithm. */
void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

void gfx_draw_char(int x, int y, char c, uint8_t color)
{
    const uint8_t *glyph = &saved_font[(uint8_t)c * 32];
    for (int row = 0; row < 16; row++)
        for (int bit = 0; bit < 8; bit++)
            if (glyph[row] & (0x80 >> bit))
                gfx_put_pixel(x + bit, y + row, color);
}

void gfx_draw_text(int x, int y, const char *s, uint8_t color)
{
    for (; *s; s++, x += 8)
        gfx_draw_char(x, y, *s, color);
}

void gfx_wait_vsync(void)
{
    /* Bounded loops: an emulator without retrace emulation must not hang the caller. */
    for (int i = 0; i < 1000000 && (inb(INPUT_STAT) & 0x08); i++)   /* finish the current retrace */
        ;
    for (int i = 0; i < 1000000 && !(inb(INPUT_STAT) & 0x08); i++)  /* wait for the next one */
        ;
}

void gfx_present(void)
{
    if (active)
        memcpy(vram, backbuf, sizeof backbuf);
}
