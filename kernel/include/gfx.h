#ifndef GFX_H
#define GFX_H

#include "types.h"

typedef enum {
    VIDEO_TYPE_NONE = 0x00,
    VIDEO_TYPE_COLOUR = 0x20,
    VIDEO_TYPE_MONOCHROME = 0x30,
} video_type_t;

typedef enum {
    GFX_MODE_TEXT = 0,
    GFX_MODE_VGA_13H = 1,
    GFX_MODE_VGA_12H = 2,
    GFX_MODE_VESA_LFB = 3,
} gfx_mode_t;

typedef struct {
    gfx_mode_t mode;
    uint32_t framebuffer;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t bpp;
} gfx_info_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} gfx_display_mode_t;

void gfx_init(void);
video_type_t gfx_detect_video_type(void);
const char *gfx_video_type_name(video_type_t type);
const gfx_info_t *gfx_get_info(void);
bool gfx_set_text_mode(void);
bool gfx_set_mode13h(void);
bool gfx_set_mode12h(void);
bool gfx_attach_vesa_lfb(uint32_t framebuffer, uint16_t width, uint16_t height, uint16_t pitch, uint8_t bpp);
bool gfx_has_vesa_lfb(void);
bool gfx_can_change_mode(void);
bool gfx_list_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                            uint32_t *count);
bool gfx_set_display_mode(uint16_t width, uint16_t height, uint8_t bpp);
void gfx_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void gfx_set_default_palette(void);
void gfx_clear(uint8_t color);
void gfx_clear_rgb(uint32_t rgb);
void gfx_putpixel(int x, int y, uint8_t color);
void gfx_putpixel_rgb(int x, int y, uint32_t rgb);
uint32_t gfx_getpixel_rgb(int x, int y);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fill_rect_rgb(int x, int y, int w, int h, uint32_t rgb);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg, bool fill_bg);
void gfx_draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg, bool fill_bg);
void gfx_demo(void);

#endif
