#include "gui.h"
#include "../kernel/include/language.h"

#define GUI_FONT_ADVANCE 7
#define GUI_FONT_LARGE_THRESHOLD 16
#define GUI_FONT_LARGE_SOURCE_HEIGHT 16

static const uint8_t font_digits[10][8] = {
    {0x00,0x38,0x44,0x4C,0x54,0x64,0x38,0x00},
    {0x00,0x10,0x30,0x10,0x10,0x10,0x38,0x00},
    {0x00,0x38,0x44,0x08,0x10,0x20,0x7C,0x00},
    {0x00,0x78,0x04,0x18,0x04,0x44,0x38,0x00},
    {0x00,0x08,0x18,0x28,0x48,0x7C,0x08,0x00},
    {0x00,0x7C,0x40,0x78,0x04,0x44,0x38,0x00},
    {0x00,0x18,0x20,0x40,0x78,0x44,0x38,0x00},
    {0x00,0x7C,0x04,0x08,0x10,0x20,0x20,0x00},
    {0x00,0x38,0x44,0x38,0x44,0x44,0x38,0x00},
    {0x00,0x38,0x44,0x3C,0x04,0x08,0x30,0x00},
};

static const uint8_t font_upper[26][8] = {
    {0x00,0x38,0x44,0x44,0x7C,0x44,0x44,0x00},
    {0x00,0x78,0x44,0x78,0x44,0x44,0x78,0x00},
    {0x00,0x38,0x44,0x40,0x40,0x44,0x38,0x00},
    {0x00,0x70,0x48,0x44,0x44,0x48,0x70,0x00},
    {0x00,0x7C,0x40,0x78,0x40,0x40,0x7C,0x00},
    {0x00,0x7C,0x40,0x78,0x40,0x40,0x40,0x00},
    {0x00,0x38,0x44,0x40,0x5C,0x44,0x38,0x00},
    {0x00,0x44,0x44,0x7C,0x44,0x44,0x44,0x00},
    {0x00,0x38,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x00,0x1C,0x08,0x08,0x08,0x48,0x30,0x00},
    {0x00,0x44,0x48,0x70,0x50,0x48,0x44,0x00},
    {0x00,0x40,0x40,0x40,0x40,0x40,0x7C,0x00},
    {0x00,0x44,0x6C,0x54,0x54,0x44,0x44,0x00},
    {0x00,0x44,0x64,0x54,0x4C,0x44,0x44,0x00},
    {0x00,0x38,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x00,0x78,0x44,0x44,0x78,0x40,0x40,0x00},
    {0x00,0x38,0x44,0x44,0x54,0x48,0x34,0x00},
    {0x00,0x78,0x44,0x44,0x78,0x48,0x44,0x00},
    {0x00,0x38,0x40,0x38,0x04,0x44,0x38,0x00},
    {0x00,0x7C,0x10,0x10,0x10,0x10,0x10,0x00},
    {0x00,0x44,0x44,0x44,0x44,0x44,0x38,0x00},
    {0x00,0x44,0x44,0x44,0x44,0x28,0x10,0x00},
    {0x00,0x44,0x44,0x54,0x54,0x6C,0x44,0x00},
    {0x00,0x44,0x28,0x10,0x10,0x28,0x44,0x00},
    {0x00,0x44,0x28,0x10,0x10,0x10,0x10,0x00},
    {0x00,0x7C,0x08,0x10,0x20,0x40,0x7C,0x00},
};

static const uint8_t font_lower[26][8] = {
    {0x00,0x00,0x38,0x04,0x3C,0x44,0x3C,0x00},
    {0x00,0x40,0x58,0x64,0x44,0x64,0x58,0x00},
    {0x00,0x00,0x38,0x40,0x40,0x44,0x38,0x00},
    {0x00,0x04,0x34,0x4C,0x44,0x4C,0x34,0x00},
    {0x00,0x00,0x38,0x44,0x78,0x40,0x38,0x00},
    {0x00,0x18,0x20,0x78,0x20,0x20,0x20,0x00},
    {0x00,0x00,0x3C,0x44,0x3C,0x04,0x38,0x00},
    {0x00,0x40,0x58,0x64,0x44,0x44,0x44,0x00},
    {0x00,0x10,0x00,0x30,0x10,0x10,0x38,0x00},
    {0x00,0x08,0x00,0x18,0x08,0x48,0x30,0x00},
    {0x00,0x40,0x48,0x70,0x50,0x48,0x44,0x00},
    {0x00,0x30,0x10,0x10,0x10,0x10,0x38,0x00},
    {0x00,0x00,0x68,0x54,0x54,0x54,0x54,0x00},
    {0x00,0x00,0x58,0x64,0x44,0x44,0x44,0x00},
    {0x00,0x00,0x38,0x44,0x44,0x44,0x38,0x00},
    {0x00,0x00,0x78,0x44,0x78,0x40,0x40,0x00},
    {0x00,0x00,0x34,0x4C,0x3C,0x04,0x04,0x00},
    {0x00,0x00,0x58,0x64,0x40,0x40,0x40,0x00},
    {0x00,0x00,0x3C,0x40,0x38,0x04,0x78,0x00},
    {0x00,0x20,0x78,0x20,0x20,0x24,0x18,0x00},
    {0x00,0x00,0x44,0x44,0x44,0x4C,0x34,0x00},
    {0x00,0x00,0x44,0x44,0x28,0x28,0x10,0x00},
    {0x00,0x00,0x44,0x44,0x54,0x6C,0x44,0x00},
    {0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x00},
    {0x00,0x00,0x44,0x44,0x3C,0x04,0x38,0x00},
    {0x00,0x00,0x7C,0x08,0x10,0x20,0x7C,0x00},
};

static const uint8_t *glyph_for(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return font_upper[c - 'A'];
    if (c >= 'a' && c <= 'z') return font_lower[c - 'a'];
    if (c >= '0' && c <= '9') return font_digits[c - '0'];

    switch (c) {
        case ' ': { static const uint8_t g[8] = {0,0,0,0,0,0,0,0}; return g; }
        case 0xA0: { static const uint8_t g[8] = {0,0,0,0,0,0,0,0}; return g; }
        case '.': { static const uint8_t g[8] = {0,0,0,0,0,0,0x10,0}; return g; }
        case ',': { static const uint8_t g[8] = {0,0,0,0,0,0x10,0x10,0x20}; return g; }
        case ':': { static const uint8_t g[8] = {0,0x10,0x10,0,0,0x10,0x10,0}; return g; }
        case '/': { static const uint8_t g[8] = {0x04,0x08,0x08,0x10,0x20,0x20,0x40,0}; return g; }
        case '-': { static const uint8_t g[8] = {0,0,0,0x38,0,0,0,0}; return g; }
        case '_': { static const uint8_t g[8] = {0,0,0,0,0,0,0x7C,0}; return g; }
        case '[': { static const uint8_t g[8] = {0x30,0x20,0x20,0x20,0x20,0x20,0x30,0}; return g; }
        case ']': { static const uint8_t g[8] = {0x30,0x10,0x10,0x10,0x10,0x10,0x30,0}; return g; }
        case '+': { static const uint8_t g[8] = {0,0x10,0x10,0x7C,0x10,0x10,0,0}; return g; }
        case '*': { static const uint8_t g[8] = {0,0x44,0x28,0x7C,0x28,0x44,0,0}; return g; }
        case '=': { static const uint8_t g[8] = {0,0,0x7C,0,0x7C,0,0,0}; return g; }
        case '%': { static const uint8_t g[8] = {0,0x62,0x64,0x08,0x10,0x26,0x46,0}; return g; }
        case '(': { static const uint8_t g[8] = {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0}; return g; }
        case ')': { static const uint8_t g[8] = {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0}; return g; }
        case '<': { static const uint8_t g[8] = {0,0x08,0x10,0x20,0x10,0x08,0,0}; return g; }
        case '>': { static const uint8_t g[8] = {0,0x20,0x10,0x08,0x10,0x20,0,0}; return g; }
        case '\\': { static const uint8_t g[8] = {0x40,0x20,0x20,0x10,0x08,0x08,0x04,0}; return g; }
        case ';': { static const uint8_t g[8] = {0,0x10,0x10,0,0,0x10,0x10,0x20}; return g; }
        case '!': { static const uint8_t g[8] = {0x10,0x10,0x10,0x10,0x10,0,0x10,0}; return g; }
        case '?': { static const uint8_t g[8] = {0x38,0x44,0x08,0x10,0x10,0,0x10,0}; return g; }
        case '\'': { static const uint8_t g[8] = {0x10,0x10,0x20,0,0,0,0,0}; return g; }
        case '"': { static const uint8_t g[8] = {0x28,0x28,0x50,0,0,0,0,0}; return g; }
        case '#': { static const uint8_t g[8] = {0x28,0x7C,0x28,0x28,0x7C,0x28,0,0}; return g; }
        case '@': { static const uint8_t g[8] = {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0}; return g; }
        case '&': { static const uint8_t g[8] = {0x30,0x48,0x30,0x50,0x48,0x48,0x34,0}; return g; }
        case '|': { static const uint8_t g[8] = {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0}; return g; }
        case 0xA1: { static const uint8_t g[8] = {0x10,0,0x10,0x10,0x10,0x10,0x10,0}; return g; }
        case 0xBF: { static const uint8_t g[8] = {0x10,0,0x10,0x10,0x20,0x44,0x38,0}; return g; }
        case 0xA9: { static const uint8_t g[8] = {0x38,0x44,0x5A,0x52,0x52,0x5A,0x44,0x38}; return g; }
        case 0xC1: { static const uint8_t g[8] = {0x10,0x20,0x38,0x44,0x7C,0x44,0x44,0}; return g; }
        case 0xE1: { static const uint8_t g[8] = {0x10,0x20,0x38,0x04,0x3C,0x44,0x3C,0}; return g; }
        case 0xC9: { static const uint8_t g[8] = {0x10,0x20,0x7C,0x40,0x78,0x40,0x7C,0}; return g; }
        case 0xE9: { static const uint8_t g[8] = {0x10,0x20,0x38,0x44,0x78,0x40,0x38,0}; return g; }
        case 0xCD: { static const uint8_t g[8] = {0x10,0x20,0x38,0x10,0x10,0x10,0x38,0}; return g; }
        case 0xED: { static const uint8_t g[8] = {0x08,0x10,0,0x30,0x10,0x10,0x38,0}; return g; }
        case 0xD3: { static const uint8_t g[8] = {0x10,0x20,0x38,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xF3: { static const uint8_t g[8] = {0x10,0x20,0x38,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xDA: { static const uint8_t g[8] = {0x10,0x20,0x44,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xFA: { static const uint8_t g[8] = {0x10,0x20,0,0x44,0x44,0x4C,0x34,0}; return g; }
        case 0xDC: { static const uint8_t g[8] = {0x28,0,0x44,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xFC: { static const uint8_t g[8] = {0x28,0,0x44,0x44,0x44,0x4C,0x34,0}; return g; }
        case 0xD1: { static const uint8_t g[8] = {0x34,0x48,0x44,0x64,0x54,0x4C,0x44,0}; return g; }
        case 0xF1: { static const uint8_t g[8] = {0x34,0x48,0,0x58,0x64,0x44,0x44,0}; return g; }
        case 0xC7: { static const uint8_t g[8] = {0x38,0x44,0x40,0x40,0x44,0x38,0x10,0x20}; return g; }
        case 0xE7: { static const uint8_t g[8] = {0,0x38,0x40,0x40,0x44,0x38,0x10,0x20}; return g; }
        case 0xC0: { static const uint8_t g[8] = {0x20,0x10,0x38,0x44,0x7C,0x44,0x44,0}; return g; }
        case 0xE0: { static const uint8_t g[8] = {0x20,0x10,0x38,0x04,0x3C,0x44,0x3C,0}; return g; }
        case 0xC8: { static const uint8_t g[8] = {0x20,0x10,0x7C,0x40,0x78,0x40,0x7C,0}; return g; }
        case 0xE8: { static const uint8_t g[8] = {0x20,0x10,0x38,0x44,0x78,0x40,0x38,0}; return g; }
        case 0xCC: { static const uint8_t g[8] = {0x20,0x10,0x38,0x10,0x10,0x10,0x38,0}; return g; }
        case 0xEC: { static const uint8_t g[8] = {0x20,0x10,0,0x30,0x10,0x10,0x38,0}; return g; }
        case 0xD2: { static const uint8_t g[8] = {0x20,0x10,0x38,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xF2: { static const uint8_t g[8] = {0x20,0x10,0x38,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xD9: { static const uint8_t g[8] = {0x20,0x10,0x44,0x44,0x44,0x44,0x38,0}; return g; }
        case 0xF9: { static const uint8_t g[8] = {0x20,0x10,0,0x44,0x44,0x4C,0x34,0}; return g; }
        default: { static const uint8_t g[8] = {0x38,0x44,0x08,0x10,0x10,0,0x10,0}; return g; }
    }
}

bool gui_font_get_glyph8(uint8_t c, uint8_t rows[8]) {
    const uint8_t *glyph;
    if (!rows) return false;
    glyph = glyph_for(c);
    for (int i = 0; i < 8; i++) rows[i] = glyph[i];
    return true;
}

void gui_font_draw_char(gui_surface_t *surface, int x, int y, char c, uint32_t fg, uint32_t bg, bool fill_bg) {
    const uint8_t *glyph = glyph_for((unsigned char)c);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool on = (glyph[row] & (0x80 >> col)) != 0;
            if (on) gui_gfx_putpixel(surface, x + col, y + row, fg);
            else if (fill_bg) gui_gfx_putpixel(surface, x + col, y + row, bg);
        }
    }
}

void gui_font_draw_string(gui_surface_t *surface, int x, int y, const char *text, uint32_t fg, uint32_t bg, bool fill_bg) {
    char translated[384];
    text = language_expand(text, translated, sizeof(translated));
    while (text && *text) {
        gui_font_draw_char(surface, x, y, *text++, fg, bg, fill_bg);
        x += GUI_FONT_ADVANCE;
    }
}

void gui_font_draw_string_scaled(gui_surface_t *surface, int x, int y,
                                 const char *text, uint32_t fg, int scale) {
    char translated[384];
    if (scale < 1) scale = 1;
    text = language_expand(text, translated, sizeof(translated));
    while (text && *text) {
        const uint8_t *glyph = glyph_for((unsigned char)*text++);
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (!(glyph[row] & (0x80 >> col))) continue;
                gui_gfx_fill_rect(surface,
                    (gui_rect_t){x + col * scale, y + row * scale,
                                 scale, scale}, fg);
            }
        }
        x += GUI_FONT_ADVANCE * scale;
    }
}

void gui_font_draw_string_scaled_clipped(gui_surface_t *surface, int x, int y,
                                         const char *text, uint32_t fg,
                                         int scale, gui_rect_t clip) {
    char translated[384];
    if (scale < 1) scale = 1;
    text = language_expand(text, translated, sizeof(translated));
    while (text && *text) {
        const uint8_t *glyph = glyph_for((unsigned char)*text++);
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (!(glyph[row] & (0x80 >> col))) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (gui_rect_contains(clip, px, py))
                            gui_gfx_putpixel(surface, px, py, fg);
                    }
            }
        }
        x += GUI_FONT_ADVANCE * scale;
        if (x >= clip.x + clip.w) break;
    }
}

void gui_font_draw_string_clipped(gui_surface_t *surface, int x, int y,
                                  const char *text, uint32_t fg,
                                  gui_rect_t clip) {
    gui_font_draw_string_scaled_clipped(surface, x, y, text, fg, 1, clip);
}


static void gui_font_glyph_bounds(unsigned char c, int *first, int *last) {
    const uint8_t *glyph = glyph_for(c);
    int lo = 8, hi = -1;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (0x80U >> col)) {
                if (col < lo) lo = col;
                if (col > hi) hi = col;
            }
        }
    }
    if (hi < lo) { lo = 0; hi = 2; }
    *first = lo;
    *last = hi;
}

static int gui_font_advance_px(unsigned char c, int pixel_height,
                               bool monospace, bool bold) {
    int first, last, width;
    int source_height = pixel_height >= GUI_FONT_LARGE_THRESHOLD
        ? GUI_FONT_LARGE_SOURCE_HEIGHT : 8;
    int spacing = pixel_height >= 24 ? 2 : 1;
    if (monospace) {
        width = (pixel_height * 8 + source_height - 1) / source_height;
        if (bold) width++;
        return width + spacing;
    }
    gui_font_glyph_bounds(c, &first, &last);
    width = ((last - first + 1) * pixel_height + source_height - 1) /
            source_height;
    if (c == ' ' || c == 0xA0)
        width = (pixel_height * 4 + source_height - 1) / source_height;
    if (bold) width++;
    return width + spacing;
}

/*
 * Native large bitmap face.
 *
 * The original GUI face is 8x8. Scaling that face with bilinear filtering made
 * 17-32 px headings look soft. For large text we treat every source row as two
 * crisp rows, yielding an 8x16 VGA-style face, and scale it with nearest-neighbor
 * sampling. It stays sharp at 16, 20, 24 and 32 px and keeps the 90s bitmap look.
 */
static uint8_t gui_font_sample_large(const uint8_t *glyph,
                                     int source_x, int source_y) {
    int row = source_y >> 1;
    if (!glyph || source_x < 0 || source_x > 7 ||
        row < 0 || row > 7) return 0U;
    return (glyph[row] & (0x80U >> source_x)) ? 255U : 0U;
}

static void gui_font_blend_pixel(gui_surface_t *surface, int x, int y,
                                 uint32_t foreground, uint8_t alpha,
                                 gui_rect_t clip) {
    uint32_t background, inverse, red, green, blue;
    if (!surface || !surface->pixels || alpha == 0U ||
        x < surface->origin_x || y < surface->origin_y ||
        x >= surface->origin_x + (surface->storage_width
            ? surface->storage_width : surface->width) ||
        y >= surface->origin_y + (surface->storage_height
            ? surface->storage_height : surface->height) ||
        !gui_rect_contains(clip, x, y)) return;
    if (alpha == 255U) {
        surface->pixels[(uint32_t)(y - surface->origin_y) * surface->pitch +
                        (uint32_t)(x - surface->origin_x)] =
            foreground & 0x00ffffffU;
        return;
    }
    background = surface->pixels[
        (uint32_t)(y - surface->origin_y) * surface->pitch +
        (uint32_t)(x - surface->origin_x)];
    inverse = 255U - alpha;
    red = (((foreground >> 16) & 0xffU) * alpha +
           ((background >> 16) & 0xffU) * inverse) / 255U;
    green = (((foreground >> 8) & 0xffU) * alpha +
             ((background >> 8) & 0xffU) * inverse) / 255U;
    blue = ((foreground & 0xffU) * alpha +
            (background & 0xffU) * inverse) / 255U;
    surface->pixels[(uint32_t)(y - surface->origin_y) * surface->pitch +
                    (uint32_t)(x - surface->origin_x)] =
        (red << 16) | (green << 8) | blue;
}

uint16_t gui_font_text_width_px(const char *text, uint32_t length,
                                int pixel_height, bool monospace, bool bold) {
    uint32_t i;
    uint32_t width = 0;
    if (!text) return 0;
    for (i = 0; i < length && text[i]; i++)
        width += (uint32_t)gui_font_advance_px((unsigned char)text[i],
                                               pixel_height, monospace, bold);
    return width > 65535U ? 65535U : (uint16_t)width;
}

void gui_font_draw_string_px_clipped(gui_surface_t *surface, int x, int y,
                                     const char *text, uint32_t length,
                                     uint32_t fg, int pixel_height,
                                     bool bold, bool italic, bool monospace,
                                     gui_rect_t clip) {
    uint32_t i;
    bool large_face;
    if (!surface || !text) return;
    if (pixel_height < 8) pixel_height = 8;
    if (pixel_height > 32) pixel_height = 32;
    large_face = pixel_height >= GUI_FONT_LARGE_THRESHOLD;
    for (i = 0; i < length && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        const uint8_t *glyph = glyph_for(c);
        int first, last;
        int advance = gui_font_advance_px(c, pixel_height, monospace, bold);
        int source_height = large_face ? GUI_FONT_LARGE_SOURCE_HEIGHT : 8;
        int glyph_width;
        gui_font_glyph_bounds(c, &first, &last);
        if (monospace) { first = 0; last = 7; }
        glyph_width = ((last - first + 1) * pixel_height +
                       source_height - 1) / source_height;
        if (glyph_width < 1) glyph_width = 1;
        for (int dy = 0; dy < pixel_height; dy++) {
            int slant = italic ? (pixel_height - 1 - dy) / 7 : 0;
            if (large_face) {
                int source_y = (dy * GUI_FONT_LARGE_SOURCE_HEIGHT) /
                               pixel_height;
                for (int dx = 0; dx < glyph_width; dx++) {
                    int source_x = first +
                        (dx * (last - first + 1)) / glyph_width;
                    uint8_t alpha = gui_font_sample_large(glyph,
                                                          source_x, source_y);
                    int px = x + dx + slant;
                    gui_font_blend_pixel(surface, px, y + dy, fg, alpha, clip);
                    if (bold && alpha)
                        gui_font_blend_pixel(surface, px + 1, y + dy,
                                             fg, alpha, clip);
                }
            } else {
                /*
                 * Classic Win9x dialog faces were hinted bitmap fonts.
                 * Bilinear resampling an 8x8 glyph at the common 10-13 px
                 * dialog sizes produces the grey/blurred edges seen in PE
                 * applications.  Snap both axes to the source grid so the
                 * built-in System, MS Sans Serif, Tahoma, Arial, Courier and
                 * Times substitutions stay crisp.
                 */
                int source_y = (dy * 8) / pixel_height;
                for (int dx = 0; dx < glyph_width; dx++) {
                    int source_x = first +
                        (dx * (last - first + 1)) / glyph_width;
                    uint8_t alpha =
                        (glyph[source_y] & (0x80U >> source_x)) ? 255U : 0U;
                    int px = x + dx + slant;
                    gui_font_blend_pixel(surface, px, y + dy, fg, alpha, clip);
                    if (bold)
                        gui_font_blend_pixel(surface, px + 1, y + dy,
                                             fg, alpha, clip);
                }
            }
        }
        x += advance;
        if (x >= clip.x + clip.w) break;
    }
}

uint16_t gui_font_text_width(const char *text) {
    char translated[384];
    uint16_t width = 0;
    text = language_expand(text, translated, sizeof(translated));
    while (text && *text++) width += GUI_FONT_ADVANCE;
    return width;
}
