/* Smaller device drivers: PC speaker, PS/2 mouse, VGA graphics, ATA disk. */
#ifndef DRAKE_DRIVERS_H
#define DRAKE_DRIVERS_H

#include <stdint.h>

/* PC speaker (PIT channel 2) */
void speaker_tone(uint32_t hz);
void speaker_off(void);
void speaker_beep(uint32_t hz, uint32_t ms);

/* PS/2 mouse */
struct mouse_state {
    int x, y;                  /* clamped to the bounds set by mouse_set_bounds */
    int buttons;               /* bit 0 left, bit 1 right, bit 2 middle */
    uint32_t events;           /* incremented on every packet */
};
int mouse_init(void);
void mouse_set_bounds(int width, int height);
struct mouse_state mouse_get(void);

/* VGA graphics: 320x200 with 256 colours (mode 13h), switched by programming the registers */
#define GFX_WIDTH  320
#define GFX_HEIGHT 200
void gfx_init(void);           /* saves the text-mode font and palette */
void gfx_enter(void);
void gfx_leave(void);          /* back to 80x25 text with font and palette restored */
int gfx_active(void);
void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);   /* 6-bit components */
void gfx_put_pixel(int x, int y, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void gfx_draw_char(int x, int y, char c, uint8_t color);   /* 8x16, using the saved VGA font */
void gfx_draw_text(int x, int y, const char *s, uint8_t color);
void gfx_present(void);        /* copies the back buffer to video memory */
void gfx_wait_vsync(void);

/* ATA PIO, primary bus */
#define ATA_SECTOR_SIZE 512
int ata_init(void);                                      /* detects the primary slave data disk */
uint32_t ata_sector_count(void);
int ata_read(uint32_t lba, uint32_t count, void *buf);
int ata_write(uint32_t lba, uint32_t count, const void *buf);

#endif
