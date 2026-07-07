#ifndef VESA_H
#define VESA_H

#include "gfx.h"

#define VESA_BOOTINFO_ADDR  0x00000700
#define VESA_BOOTINFO_MAGIC 0x31495547 /* 'GUI1' */

#define VGA_BOOTINFO_MAGIC  0x31414756 /* 'VGA1' */
#define VGA_BOOTINFO_TEXT   0
#define VGA_BOOTINFO_13H    1
#define VGA_BOOTINFO_12H    2

bool vesa_init_from_bootinfo(gfx_info_t *info);
bool vesa_attach_lfb(gfx_info_t *info, uint32_t framebuffer, uint16_t width, uint16_t height, uint16_t pitch, uint8_t bpp);
bool vesa_has_lfb(void);
bool vesa_can_change_mode(void);
bool vesa_list_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                     uint32_t *count, uint8_t preferred_bpp);
bool vesa_set_mode(gfx_info_t *info, uint16_t width, uint16_t height,
                   uint8_t bpp);
void vesa_clear_rgb(const gfx_info_t *info, uint32_t rgb);
void vesa_putpixel_rgb(const gfx_info_t *info, int x, int y, uint32_t rgb);
uint32_t vesa_getpixel_rgb(const gfx_info_t *info, int x, int y);
void vesa_fill_rect_rgb(const gfx_info_t *info, int x, int y, int w, int h, uint32_t rgb);

#endif
