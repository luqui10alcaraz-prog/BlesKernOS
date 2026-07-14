#include "../include/gfx.h"
#include "../include/gfx_vga.h"
#include "../include/vesa.h"

static gfx_info_t g_gfx;

static uint32_t gfx_bootinfo_read32(uint32_t offset) {
    volatile uint32_t *p = (volatile uint32_t *)(VESA_BOOTINFO_ADDR + offset);
    return *p;
}

static uint32_t gfx_palette_rgb(uint8_t color) {
    static const uint32_t palette[16] = {
        0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
        0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
        0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
        0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF
    };
    return palette[color & 0x0F];
}

static const uint8_t font8x8_digits[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
};

static const uint8_t font8x8_upper[26][8] = {
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
};

static const uint8_t *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return font8x8_upper[c - 'A'];
    if (c >= '0' && c <= '9') return font8x8_digits[c - '0'];
    switch (c) {
        case ' ': { static const uint8_t g[8] = {0,0,0,0,0,0,0,0}; return g; }
        case '.': { static const uint8_t g[8] = {0,0,0,0,0,0x18,0x18,0}; return g; }
        case ':': { static const uint8_t g[8] = {0,0x18,0x18,0,0,0x18,0x18,0}; return g; }
        case '/': { static const uint8_t g[8] = {0x06,0x0C,0x18,0x30,0x60,0,0,0}; return g; }
        case '-': { static const uint8_t g[8] = {0,0,0,0x7E,0,0,0,0}; return g; }
        default: { static const uint8_t g[8] = {0x7E,0x42,0x0C,0x18,0x18,0,0x18,0}; return g; }
    }
}

static void gfx_clip_rect(int *x, int *y, int *w, int *h) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > g_gfx.width) *w = g_gfx.width - *x;
    if (*y + *h > g_gfx.height) *h = g_gfx.height - *y;
}

void gfx_init(void) {
    uint32_t magic;

    g_gfx.mode = GFX_MODE_TEXT;
    g_gfx.framebuffer = 0x000B8000;
    g_gfx.width = 80;
    g_gfx.height = 25;
    g_gfx.pitch = 160;
    g_gfx.bpp = 16;

    magic = gfx_bootinfo_read32(0);

    if (magic == VGA_BOOTINFO_MAGIC) {
        uint32_t requested = gfx_bootinfo_read32(4);

        if (requested == VGA_BOOTINFO_13H) {
            if (!gfx_vga_set_mode13h(&g_gfx)) gfx_vga_set_text_mode(&g_gfx);
            return;
        }

        if (requested == VGA_BOOTINFO_12H) {
            if (!gfx_vga_set_mode12h(&g_gfx)) gfx_vga_set_text_mode(&g_gfx);
            return;
        }

        gfx_vga_set_text_mode(&g_gfx);
        return;
    }

    (void)vesa_init_from_bootinfo(&g_gfx);
}

video_type_t gfx_detect_video_type(void) {
    uint16_t detected;
    __asm__ volatile ("movw 0x410, %0" : "=r"(detected));
    return (video_type_t)(detected & 0x30);
}

const char *gfx_video_type_name(video_type_t type) {
    switch (type) {
        case VIDEO_TYPE_COLOUR: return "colour";
        case VIDEO_TYPE_MONOCHROME: return "monochrome";
        case VIDEO_TYPE_NONE: return "none";
        default: return "unknown";
    }
}

const gfx_info_t *gfx_get_info(void) {
    return &g_gfx;
}

bool gfx_set_text_mode(void) {
    return gfx_vga_set_text_mode(&g_gfx);
}

bool gfx_set_mode13h(void) {
    return gfx_vga_set_mode13h(&g_gfx);
}

bool gfx_set_mode12h(void) {
    return gfx_vga_set_mode12h(&g_gfx);
}

bool gfx_attach_vesa_lfb(uint32_t framebuffer, uint16_t width, uint16_t height, uint16_t pitch, uint8_t bpp) {
    return vesa_attach_lfb(&g_gfx, framebuffer, width, height, pitch, bpp);
}

bool gfx_has_vesa_lfb(void) {
    return vesa_has_lfb();
}

bool gfx_can_change_mode(void) {
    return vesa_can_change_mode();
}

bool gfx_list_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                            uint32_t *count) {
    uint8_t current_bpp = g_gfx.bpp ? g_gfx.bpp : 16;
    return vesa_list_modes(modes, max_modes, count, current_bpp);
}

bool gfx_list_all_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                                uint32_t *count) {
    return vesa_list_all_modes(modes, max_modes, count);
}

bool gfx_set_display_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    if (g_gfx.mode != GFX_MODE_VESA_LFB) return false;
    return vesa_set_mode(&g_gfx, width, height, bpp);
}

bool gfx_enable_page_flip(void) {
    return g_gfx.mode == GFX_MODE_VESA_LFB &&
           vesa_enable_page_flip(&g_gfx);
}

uint32_t gfx_page_flip_draw_buffer(void) {
    if (g_gfx.mode != GFX_MODE_VESA_LFB) return 0U;
    return vesa_page_flip_draw_buffer(&g_gfx);
}

bool gfx_page_flip_commit(void) {
    return g_gfx.mode == GFX_MODE_VESA_LFB &&
           vesa_page_flip_commit(&g_gfx);
}

void gfx_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    gfx_vga_set_palette_color(index, r, g, b);
}

void gfx_set_default_palette(void) {
    gfx_vga_set_default_palette();
}

void gfx_clear(uint8_t color) {
    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_clear_rgb(&g_gfx, color);
    } else {
        gfx_vga_clear(&g_gfx, color);
    }
}

void gfx_clear_rgb(uint32_t rgb) {
    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_clear_rgb(&g_gfx, rgb);
    } else {
        gfx_vga_clear(&g_gfx, gfx_vga_rgb_to_color(rgb));
    }
}

void gfx_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) return;
    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_putpixel_rgb(&g_gfx, x, y, gfx_palette_rgb(color));
    } else {
        gfx_vga_putpixel(&g_gfx, x, y, color);
    }
}

void gfx_putpixel_rgb(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) return;
    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_putpixel_rgb(&g_gfx, x, y, rgb);
    } else {
        gfx_vga_putpixel(&g_gfx, x, y, gfx_vga_rgb_to_color(rgb));
    }
}

uint32_t gfx_getpixel_rgb(int x, int y) {
    if (x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) return 0;
    if (g_gfx.mode != GFX_MODE_VESA_LFB) return 0;
    return vesa_getpixel_rgb(&g_gfx, x, y);
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    gfx_clip_rect(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_fill_rect_rgb(&g_gfx, x, y, w, h, color);
    } else {
        gfx_vga_fill_rect(&g_gfx, x, y, w, h, color);
    }
}

void gfx_fill_rect_rgb(int x, int y, int w, int h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    gfx_clip_rect(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_fill_rect_rgb(&g_gfx, x, y, w, h, rgb);
    } else {
        gfx_vga_fill_rect(&g_gfx, x, y, w, h, gfx_vga_rgb_to_color(rgb));
    }
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        int e2;
        gfx_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg, bool fill_bg) {
    const uint8_t *glyph = glyph_for(c);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool on = (glyph[row] & (0x80 >> col)) != 0;
            if (on) gfx_putpixel(x + col, y + row, fg);
            else if (fill_bg) gfx_putpixel(x + col, y + row, bg);
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg, bool fill_bg) {
    while (s && *s) {
        gfx_draw_char(x, y, *s++, fg, bg, fill_bg);
        x += 8;
    }
}


void gfx_demo(void) {
    gfx_clear(1);
    gfx_fill_rect(20, 20, 280, 160, 3);
    gfx_fill_rect(28, 28, 264, 144, 0);
    gfx_draw_line(0, 0, 319, 199, 12);
    gfx_draw_line(319, 0, 0, 199, 10);
    gfx_fill_rect(48, 64, 224, 56, 9);
    gfx_draw_string(64, 80, "BLESKERNOS VGA", 15, 9, false);
    gfx_draw_string(64, 96, "MODE 13H 320X200", 14, 9, false);
}
