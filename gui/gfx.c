#include "gui.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/mouse.h"

static uint32_t *g_front_shadow;
static uint32_t g_shadow_pixels;

static uint8_t gui_channel(uint32_t color, uint8_t shift) {
    return (uint8_t)((color >> shift) & 0xFF);
}

static uint32_t gui_rgb_to_vga(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb & 0xFF);
    uint8_t color = 0;
    if (r > 85) color |= 4;
    if (g > 85) color |= 2;
    if (b > 85) color |= 1;
    if (r > 170 || g > 170 || b > 170) color |= 8;
    return color;
}

static bool gui_gfx_allocate(gui_surface_t *surface) {
    const gfx_info_t *info;
    size_t bytes;

    if (!surface) return false;
    info = gfx_get_info();
    if (!info) return false;

    bytes = (size_t)info->width * info->height * sizeof(uint32_t);
    surface->pixels = (uint32_t *)kmalloc(bytes);
    if (!surface->pixels) return false;
    g_front_shadow = (uint32_t *)kmalloc(bytes);
    if (!g_front_shadow) {
        kfree(surface->pixels);
        surface->pixels = NULL;
        return false;
    }
    g_shadow_pixels = (uint32_t)info->width * info->height;
    for (uint32_t i = 0; i < g_shadow_pixels; i++)
        g_front_shadow[i] = 0xFFFFFFFF;

    surface->width = info->width;
    surface->height = info->height;
    surface->pitch = info->width;
    mouse_set_bounds(surface->width, surface->height);
    return true;
}

bool gui_gfx_init(gui_surface_t *surface) {
    if (!surface) return false;
    surface->pixels = NULL;
    surface->width = 0;
    surface->height = 0;
    surface->pitch = 0;

    if (gfx_get_info()->mode != GFX_MODE_VESA_LFB) {
        if (!gfx_set_mode13h()) return false;
    }
    return gui_gfx_allocate(surface);
}

bool gui_gfx_reconfigure(gui_surface_t *surface) {
    if (!surface) return false;
    if (surface->pixels) {
        kfree(surface->pixels);
        surface->pixels = NULL;
    }
    if (g_front_shadow) {
        kfree(g_front_shadow);
        g_front_shadow = NULL;
    }
    g_shadow_pixels = 0;
    return gui_gfx_allocate(surface);
}

void gui_gfx_shutdown(gui_surface_t *surface) {
    if (!surface) return;
    if (surface->pixels) kfree(surface->pixels);
    if (g_front_shadow) kfree(g_front_shadow);
    surface->pixels = NULL;
    g_front_shadow = NULL;
    g_shadow_pixels = 0;
}

void gui_gfx_present(const gui_surface_t *surface) {
    const gfx_info_t *info = gfx_get_info();
    if (!surface || !surface->pixels) return;

    if (info->mode == GFX_MODE_VGA_13H || info->mode == GFX_MODE_VGA_12H) {
        for (uint16_t y = 0; y < surface->height; y++) {
            for (uint16_t x = 0; x < surface->width; x++) {
                gfx_putpixel(x, y, (uint8_t)gui_rgb_to_vga(surface->pixels[y * surface->pitch + x]));
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 16) {
        for (uint16_t y = 0; y < surface->height; y++) {
            volatile uint16_t *dst = (volatile uint16_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = 0; x < surface->width; x++) {
                uint32_t rgb = src[x];
                uint32_t index = (uint32_t)y * surface->pitch + x;
                if (g_front_shadow[index] == rgb) continue;
                dst[x] = (uint16_t)(((rgb >> 8) & 0xF800) |
                                    ((rgb >> 5) & 0x07E0) |
                                    ((rgb >> 3) & 0x001F));
                g_front_shadow[index] = rgb;
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 32) {
        for (uint16_t y = 0; y < surface->height; y++) {
            volatile uint32_t *dst = (volatile uint32_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = 0; x < surface->width; x++) {
                uint32_t index = (uint32_t)y * surface->pitch + x;
                if (g_front_shadow[index] == src[x]) continue;
                dst[x] = src[x];
                g_front_shadow[index] = src[x];
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 24) {
        for (uint16_t y = 0; y < surface->height; y++) {
            volatile uint8_t *dst = (volatile uint8_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = 0; x < surface->width; x++) {
                uint32_t rgb = src[x];
                uint32_t index = (uint32_t)y * surface->pitch + x;
                if (g_front_shadow[index] == rgb) continue;
                dst[x * 3] = (uint8_t)rgb;
                dst[x * 3 + 1] = (uint8_t)(rgb >> 8);
                dst[x * 3 + 2] = (uint8_t)(rgb >> 16);
                g_front_shadow[index] = rgb;
            }
        }
        return;
    }
}

void gui_gfx_clear(gui_surface_t *surface, uint32_t color) {
    if (!surface || !surface->pixels) return;
    for (uint32_t i = 0; i < (uint32_t)surface->width * surface->height; i++) {
        surface->pixels[i] = color;
    }
}

void gui_gfx_putpixel(gui_surface_t *surface, int x, int y, uint32_t color) {
    if (!surface || !surface->pixels) return;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height) return;
    surface->pixels[(uint32_t)y * surface->pitch + (uint32_t)x] = color;
}

void gui_gfx_fill_rect(gui_surface_t *surface, gui_rect_t rect, uint32_t color) {
    int x2;
    int y2;

    if (!surface || !surface->pixels || rect.w <= 0 || rect.h <= 0) return;
    x2 = rect.x + rect.w;
    y2 = rect.y + rect.h;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    if (x2 > surface->width) x2 = surface->width;
    if (y2 > surface->height) y2 = surface->height;
    if (rect.x >= x2 || rect.y >= y2) return;

    for (int y = rect.y; y < y2; y++) {
        uint32_t *row = &surface->pixels[(uint32_t)y * surface->pitch + (uint32_t)rect.x];
        for (int x = rect.x; x < x2; x++) {
            *row++ = color;
        }
    }
}

void gui_gfx_draw_rect(gui_surface_t *surface, gui_rect_t rect, uint32_t color) {
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, rect.w, 1}, color);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y + rect.h - 1, rect.w, 1}, color);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, 1, rect.h}, color);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 1, rect.y, 1, rect.h}, color);
}

uint32_t gui_color_blend(uint32_t base, uint32_t top, uint8_t alpha) {
    uint32_t inv = (uint32_t)(255 - alpha);
    uint8_t r = (uint8_t)(((uint32_t)gui_channel(base, 16) * inv + (uint32_t)gui_channel(top, 16) * alpha) / 255);
    uint8_t g = (uint8_t)(((uint32_t)gui_channel(base, 8) * inv + (uint32_t)gui_channel(top, 8) * alpha) / 255);
    uint8_t b = (uint8_t)(((uint32_t)gui_channel(base, 0) * inv + (uint32_t)gui_channel(top, 0) * alpha) / 255);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t gui_color_lerp(uint32_t a, uint32_t b, uint8_t t) {
    return gui_color_blend(a, b, t);
}

void gui_gfx_fill_gradient(gui_surface_t *surface, gui_rect_t rect, uint32_t top, uint32_t bottom) {
    int span;

    if (!surface || !surface->pixels || rect.w <= 0 || rect.h <= 0) return;
    if (rect.h == 1) {
        gui_gfx_fill_rect(surface, rect, top);
        return;
    }

    span = rect.h - 1;
    for (int row = 0; row < rect.h; row++) {
        uint8_t t = (uint8_t)((row * 255) / span);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y + row, rect.w, 1}, gui_color_lerp(top, bottom, t));
    }
}

static int gui_rounded_inset(int radius, int y) {
    int dy = radius - 1 - y;
    int inset = 0;

    while (inset < radius) {
        int dx = radius - 1 - inset;
        if ((dx * dx) + (dy * dy) < (radius * radius)) break;
        inset++;
    }

    return inset;
}

void gui_gfx_fill_rounded_rect(gui_surface_t *surface, gui_rect_t rect, int radius, uint32_t color) {
    if (!surface || !surface->pixels || rect.w <= 0 || rect.h <= 0) return;
    if (radius <= 0) {
        gui_gfx_fill_rect(surface, rect, color);
        return;
    }

    if (radius * 2 > rect.w) radius = rect.w / 2;
    if (radius * 2 > rect.h) radius = rect.h / 2;
    if (radius <= 0) {
        gui_gfx_fill_rect(surface, rect, color);
        return;
    }

    for (int row = 0; row < rect.h; row++) {
        int inset = 0;
        if (row < radius) inset = gui_rounded_inset(radius, row);
        else if (row >= rect.h - radius) inset = gui_rounded_inset(radius, rect.h - 1 - row);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + inset, rect.y + row, rect.w - (inset * 2), 1}, color);
    }
}

void gui_gfx_draw_line(gui_surface_t *surface, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        int e2;
        gui_gfx_putpixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

bool gui_rect_intersect(gui_rect_t a, gui_rect_t b, gui_rect_t *out) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x2 <= x1 || y2 <= y1) return false;
    if (out) *out = (gui_rect_t){x1, y1, x2 - x1, y2 - y1};
    return true;
}

bool gui_rect_contains(gui_rect_t rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}
