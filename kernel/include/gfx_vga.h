#ifndef GFX_VGA_H
#define GFX_VGA_H

#include "gfx.h"

bool gfx_vga_set_text_mode(gfx_info_t *info);
bool gfx_vga_set_mode13h(gfx_info_t *info);
bool gfx_vga_set_mode12h(gfx_info_t *info);
void gfx_vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void gfx_vga_set_default_palette(void);
void gfx_vga_clear(const gfx_info_t *info, uint8_t color);
void gfx_vga_putpixel(const gfx_info_t *info, int x, int y, uint8_t color);
void gfx_vga_fill_rect(const gfx_info_t *info, int x, int y, int w, int h, uint8_t color);
uint8_t gfx_vga_rgb_to_color(uint32_t rgb);

#endif
