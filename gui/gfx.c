#include "gui.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/pic.h"
#include "../kernel/include/pit.h"

#define VGA_INPUT_STATUS_1 0x3DA
#define VGA_VERTICAL_RETRACE 0x08
#define GUI_GFX_BULK_PRESENT_PIXELS 16384U

static uint32_t *g_front_shadow;
static uint32_t g_shadow_pixels;
static int8_t g_retrace_state;
static bool g_page_flip;

static void gui_gfx_copy_dwords(volatile uint32_t *destination,
                                const uint32_t *source, uint32_t count) {
    uint32_t *dst = (uint32_t *)(uintptr_t)destination;
    const uint32_t *src = source;

    __asm__ volatile ("cld; rep movsl"
                      : "+D"(dst), "+S"(src), "+c"(count)
                      : : "memory");
}

static void gui_gfx_copy_rect32(uint32_t framebuffer,
                                const gfx_info_t *info,
                                const gui_surface_t *surface,
                                gui_rect_t clip) {
    for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
        volatile uint32_t *dst = (volatile uint32_t *)
            (framebuffer + (uint32_t)y * info->pitch) + clip.x;
        const uint32_t *src = &surface->pixels[(uint32_t)y *
            surface->pitch + (uint32_t)clip.x];
        gui_gfx_copy_dwords(dst, src, (uint32_t)clip.w);
    }
}

/*
 * El LFB no ofrece page flipping en todos los modos VBE que soportamos. Si la
 * copia empieza a mitad del barrido, las filas inferiores se ven en el cuadro
 * actual y las superiores recién en el siguiente, produciendo una diferencia
 * de fluidez según la altura. Esperar el inicio del retrazado reduce ese corte.
 * Los límites por ticks evitan bloquear el GUI en adaptadores que no reflejen
 * el estado VGA clásico por 0x3DA.
 */
static void gui_gfx_wait_vertical_retrace(void) {
    uint32_t start = pit_get_ticks();
    uint32_t timeout = pit_get_frequency_hz() / 30U;

    if (g_retrace_state < 0) return;
    if (!timeout) timeout = 1U;
    while ((inb(VGA_INPUT_STATUS_1) & VGA_VERTICAL_RETRACE) &&
           pit_get_ticks() - start < timeout) {
        __asm__ volatile ("pause");
    }
    if (pit_get_ticks() - start >= timeout) {
        g_retrace_state = -1;
        return;
    }

    start = pit_get_ticks();
    while (!(inb(VGA_INPUT_STATUS_1) & VGA_VERTICAL_RETRACE) &&
           pit_get_ticks() - start < timeout) {
        __asm__ volatile ("pause");
    }
    if (pit_get_ticks() - start >= timeout) {
        g_retrace_state = -1;
        return;
    }
    g_retrace_state = 1;
}

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

static uint8_t gui_rgb_to_332(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xE0);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xE0);
    uint8_t b = (uint8_t)(rgb & 0xC0);
    return (uint8_t)(r | (g >> 3) | (b >> 6));
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
    g_retrace_state = 0;
    g_page_flip = info->mode == GFX_MODE_VESA_LFB && info->bpp == 32 &&
                  gfx_enable_page_flip();
    for (uint32_t i = 0; i < g_shadow_pixels; i++)
        g_front_shadow[i] = 0xFFFFFFFF;

    surface->width = info->width;
    surface->height = info->height;
    surface->pitch = info->width;
    surface->clip = (gui_rect_t){0, 0, surface->width, surface->height};
    mouse_set_bounds(surface->width, surface->height);
    return true;
}

bool gui_gfx_init(gui_surface_t *surface) {
    const gfx_info_t *info;

    if (!surface) return false;
    surface->pixels = NULL;
    surface->width = 0;
    surface->height = 0;
    surface->pitch = 0;
    surface->clip = (gui_rect_t){0, 0, 0, 0};

    info = gfx_get_info();
    if (!info) return false;

    /*
     * Respetar el modo que eligio Stage2/gfx_init().
     *
     * Antes, cualquier modo que no fuera VESA terminaba forzado a VGA 13h.
     * Por eso VGA 12h y VGA texto caian en 320x200x8.
     */
    if (info->mode == GFX_MODE_TEXT) {
        return false; /* No iniciar GUI en modo texto. */
    }

    if (info->mode != GFX_MODE_VESA_LFB &&
        info->mode != GFX_MODE_VGA_13H &&
        info->mode != GFX_MODE_VGA_12H) {
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
    g_page_flip = false;
    return gui_gfx_allocate(surface);
}

void gui_gfx_shutdown(gui_surface_t *surface) {
    if (!surface) return;
    if (surface->pixels) kfree(surface->pixels);
    if (g_front_shadow) kfree(g_front_shadow);
    surface->pixels = NULL;
    g_front_shadow = NULL;
    g_shadow_pixels = 0;
    g_page_flip = false;
}

void gui_gfx_invalidate_front(void) {
    if (!g_front_shadow) return;
    for (uint32_t i = 0; i < g_shadow_pixels; i++)
        g_front_shadow[i] = 0xFFFFFFFF;
}

void gui_gfx_present(const gui_surface_t *surface) {
    if (!surface) return;
    gui_gfx_present_rect(surface, (gui_rect_t){0, 0,
                                               surface->width,
                                               surface->height});
}

void gui_gfx_present_rect(const gui_surface_t *surface, gui_rect_t rect) {
    const gfx_info_t *info = gfx_get_info();
    gui_rect_t screen;
    gui_rect_t clip;
    if (!surface || !surface->pixels || !info) return;
    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    if (!gui_rect_intersect(screen, rect, &clip)) return;

    /*
     * No invalidar g_front_shadow en cada frame.
     *
     * El shadow existe para evitar copiar al framebuffer los píxeles que no
     * cambiaron. Si se invalida acá, gui_gfx_present() termina reescribiendo
     * casi toda la pantalla en cada repaint, justo lo que mata rendimiento al
     * mover el mouse o subir resolución.
     *
     * Invalidate solo debe llamarse cuando cambia el modo/resolución, al
     * arrancar, o si se necesita forzar un repaint completo.
     */
    if (info->mode == GFX_MODE_VGA_13H || info->mode == GFX_MODE_VGA_12H) {
        for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
            for (uint16_t x = (uint16_t)clip.x; x < clip.x + clip.w; x++) {
                gfx_putpixel(x, y, (uint8_t)gui_rgb_to_vga(surface->pixels[y * surface->pitch + x]));
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 16) {
        gui_gfx_wait_vertical_retrace();
        for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
            volatile uint16_t *dst = (volatile uint16_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = (uint16_t)clip.x; x < clip.x + clip.w; x++) {
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

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 8) {
        gui_gfx_wait_vertical_retrace();
        for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
            volatile uint8_t *dst = (volatile uint8_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = (uint16_t)clip.x; x < clip.x + clip.w; x++) {
                uint32_t index = (uint32_t)y * surface->pitch + x;
                if (g_front_shadow[index] == src[x]) continue;
                dst[x] = gui_rgb_to_332(src[x]);
                g_front_shadow[index] = src[x];
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 32) {
        uint32_t area = (uint32_t)clip.w * (uint32_t)clip.h;

        if (g_page_flip) {
            uint32_t draw_buffer = gfx_page_flip_draw_buffer();

            if (!draw_buffer) {
                g_page_flip = false;
            } else {
                /* El adaptador ve sólo la página anterior mientras se arma
                 * ésta. El cambio de Y offset hace visible el cuadro completo
                 * de forma atómica, eliminando la diferencia arriba/abajo. */
                gui_gfx_copy_rect32(draw_buffer, info, surface, clip);
                gui_gfx_wait_vertical_retrace();
                if (!gfx_page_flip_commit()) {
                    g_page_flip = false;
                } else {
                    /* Sincronice también la página que acaba de quedar oculta.
                     * Así el próximo dirty rect puede ser parcial sin arrastrar
                     * contenido de dos cuadros atrás. */
                    draw_buffer = gfx_page_flip_draw_buffer();
                    if (draw_buffer)
                        gui_gfx_copy_rect32(draw_buffer, info, surface, clip);
                    for (uint16_t y = (uint16_t)clip.y;
                         y < clip.y + clip.h; y++) {
                        uint32_t index = (uint32_t)y * surface->pitch +
                                         (uint32_t)clip.x;
                        kmemcpy(&g_front_shadow[index],
                                &surface->pixels[index],
                                (size_t)clip.w * sizeof(uint32_t));
                    }
                    return;
                }
            }
        }

        if (area >= GUI_GFX_BULK_PRESENT_PIXELS) {
            /* Prepare el shadow antes de VSync: ninguna comparación ni copia
             * RAM debe consumir la ventana de retrazado. Después se envía
             * cada fila con REP MOVSD, una ráfaga de DWORDs contiguos. */
            for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
                uint32_t index = (uint32_t)y * surface->pitch +
                                 (uint32_t)clip.x;
                const uint32_t *src = &surface->pixels[index];
                kmemcpy(&g_front_shadow[index], src,
                        (size_t)clip.w * sizeof(uint32_t));
            }
            gui_gfx_wait_vertical_retrace();
            gui_gfx_copy_rect32(info->framebuffer, info, surface, clip);
            return;
        }

        gui_gfx_wait_vertical_retrace();
        for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
            volatile uint32_t *dst = (volatile uint32_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            uint16_t end = (uint16_t)(clip.x + clip.w);

            /* Un dirty rect es la union de las posiciones anterior y nueva.
             * En movimientos de cursor o ventana gran parte de esa union no
             * cambia. Además, kmemcpy es byte a byte en el runtime del kernel:
             * escribir aquí DWORDs alineados reduce de cuatro a una las
             * transacciones de VRAM por píxel modificado. */
            for (uint16_t x = (uint16_t)clip.x; x < end; x++) {
                uint32_t index = (uint32_t)y * surface->pitch + x;
                uint32_t rgb = src[x];
                if (g_front_shadow[index] == rgb) continue;
                dst[x] = rgb;
                g_front_shadow[index] = rgb;
            }
        }
        return;
    }

    if (info->mode == GFX_MODE_VESA_LFB && info->bpp == 24) {
        gui_gfx_wait_vertical_retrace();
        for (uint16_t y = (uint16_t)clip.y; y < clip.y + clip.h; y++) {
            volatile uint8_t *dst = (volatile uint8_t *)
                (info->framebuffer + (uint32_t)y * info->pitch);
            const uint32_t *src = &surface->pixels[(uint32_t)y *
                                                  surface->pitch];
            for (uint16_t x = (uint16_t)clip.x; x < clip.x + clip.w; x++) {
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
    gui_gfx_fill_rect(surface, (gui_rect_t){0, 0, surface->width,
                                            surface->height}, color);
}

void gui_gfx_set_clip(gui_surface_t *surface, gui_rect_t clip) {
    gui_rect_t screen;

    if (!surface) return;
    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    if (!gui_rect_intersect(screen, clip, &surface->clip))
        surface->clip = (gui_rect_t){0, 0, 0, 0};
}

void gui_gfx_reset_clip(gui_surface_t *surface) {
    if (!surface) return;
    surface->clip = (gui_rect_t){0, 0, surface->width, surface->height};
}

gui_rect_t gui_gfx_get_clip(const gui_surface_t *surface) {
    if (!surface) return (gui_rect_t){0, 0, 0, 0};
    return surface->clip;
}

bool gui_gfx_point_visible(const gui_surface_t *surface, int x, int y) {
    if (!surface || !surface->pixels) return false;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height)
        return false;
    return gui_rect_contains(surface->clip, x, y);
}

void gui_gfx_fill_rect(gui_surface_t *surface, gui_rect_t rect, uint32_t color) {
    int x2;
    int y2;
    gui_rect_t clipped;

    if (!surface || !surface->pixels || rect.w <= 0 || rect.h <= 0) return;
    if (!gui_rect_intersect(rect, surface->clip, &clipped)) return;
    rect = clipped;
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

void gui_gfx_putpixel(gui_surface_t *surface, int x, int y, uint32_t color) {
    if (!surface || !surface->pixels) return;
    if (!gui_gfx_point_visible(surface, x, y)) return;
    surface->pixels[(uint32_t)y * surface->pitch + (uint32_t)x] = color;
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

gui_rect_t gui_rect_union(gui_rect_t a, gui_rect_t b) {
    int x1;
    int y1;
    int x2;
    int y2;

    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    x1 = a.x < b.x ? a.x : b.x;
    y1 = a.y < b.y ? a.y : b.y;
    x2 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    y2 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return (gui_rect_t){x1, y1, x2 - x1, y2 - y1};
}

bool gui_rect_contains(gui_rect_t rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

void gui_scrollbar_init_vertical(gui_scrollbar_t *bar, gui_rect_t bounds,
                                 uint32_t value, uint32_t visible,
                                 uint32_t total) {
    uint32_t max_value;

    if (!bar) return;
    if (visible > total) visible = total;
    max_value = total > visible ? total - visible : 0;
    if (value > max_value) value = max_value;
    bar->bounds = bounds;
    bar->value = value;
    bar->visible = visible;
    bar->total = total;
}

gui_rect_t gui_scrollbar_thumb_rect(const gui_scrollbar_t *bar) {
    gui_rect_t b;
    int track_y;
    int track_h;
    int thumb_h;
    int thumb_y;
    uint32_t max_value;

    if (!bar) return (gui_rect_t){0, 0, 0, 0};
    b = bar->bounds;
    if (b.w <= 0 || b.h <= GUI_SCROLLBAR_SIZE * 2)
        return (gui_rect_t){b.x, b.y, b.w, b.h};
    track_y = b.y + GUI_SCROLLBAR_SIZE;
    track_h = b.h - GUI_SCROLLBAR_SIZE * 2;
    if (!bar->total || bar->visible >= bar->total) {
        return (gui_rect_t){b.x + 2, track_y + 2, b.w - 4, track_h - 4};
    }
    thumb_h = (int)(((uint64_t)track_h * bar->visible) / bar->total);
    if (thumb_h < 12) thumb_h = 12;
    if (thumb_h > track_h) thumb_h = track_h;
    max_value = bar->total - bar->visible;
    thumb_y = track_y + (int)(((uint64_t)(track_h - thumb_h) * bar->value) /
                              (max_value ? max_value : 1U));
    return (gui_rect_t){b.x + 2, thumb_y, b.w - 4, thumb_h};
}

static void gui_scrollbar_draw_arrow(gui_surface_t *surface, gui_rect_t r,
                                     bool down) {
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;

    gui_gfx_fill_rect(surface, r, 0x00C0C0C0);
    gui_gfx_draw_line(surface, r.x, r.y, r.x + r.w - 1, r.y, 0x00FFFFFF);
    gui_gfx_draw_line(surface, r.x, r.y, r.x, r.y + r.h - 1, 0x00FFFFFF);
    gui_gfx_draw_line(surface, r.x, r.y + r.h - 1, r.x + r.w - 1,
                      r.y + r.h - 1, 0x00404040);
    gui_gfx_draw_line(surface, r.x + r.w - 1, r.y, r.x + r.w - 1,
                      r.y + r.h - 1, 0x00404040);
    if (down) {
        gui_gfx_draw_line(surface, cx - 4, cy - 2, cx + 4, cy - 2, 0x00101010);
        gui_gfx_draw_line(surface, cx - 3, cy - 1, cx + 3, cy - 1, 0x00101010);
        gui_gfx_draw_line(surface, cx - 2, cy, cx + 2, cy, 0x00101010);
        gui_gfx_draw_line(surface, cx - 1, cy + 1, cx + 1, cy + 1, 0x00101010);
    } else {
        gui_gfx_draw_line(surface, cx - 1, cy - 2, cx + 1, cy - 2, 0x00101010);
        gui_gfx_draw_line(surface, cx - 2, cy - 1, cx + 2, cy - 1, 0x00101010);
        gui_gfx_draw_line(surface, cx - 3, cy, cx + 3, cy, 0x00101010);
        gui_gfx_draw_line(surface, cx - 4, cy + 1, cx + 4, cy + 1, 0x00101010);
    }
}

void gui_scrollbar_paint_vertical(gui_surface_t *surface,
                                  const gui_scrollbar_t *bar) {
    gui_rect_t b;
    gui_rect_t thumb;

    if (!surface || !bar) return;
    b = bar->bounds;
    if (b.w <= 0 || b.h <= 0) return;
    gui_gfx_fill_rect(surface, b, 0x00D8D8D8);
    gui_gfx_draw_rect(surface, b, 0x00808080);
    if (b.h >= GUI_SCROLLBAR_SIZE * 2) {
        gui_scrollbar_draw_arrow(surface,
            (gui_rect_t){b.x, b.y, b.w, GUI_SCROLLBAR_SIZE}, false);
        gui_scrollbar_draw_arrow(surface,
            (gui_rect_t){b.x, b.y + b.h - GUI_SCROLLBAR_SIZE,
                         b.w, GUI_SCROLLBAR_SIZE}, true);
    }
    thumb = gui_scrollbar_thumb_rect(bar);
    gui_gfx_fill_rect(surface, thumb, 0x00C0C0C0);
    gui_gfx_draw_line(surface, thumb.x, thumb.y,
                      thumb.x + thumb.w - 1, thumb.y, 0x00FFFFFF);
    gui_gfx_draw_line(surface, thumb.x, thumb.y,
                      thumb.x, thumb.y + thumb.h - 1, 0x00FFFFFF);
    gui_gfx_draw_line(surface, thumb.x, thumb.y + thumb.h - 1,
                      thumb.x + thumb.w - 1, thumb.y + thumb.h - 1,
                      0x00404040);
    gui_gfx_draw_line(surface, thumb.x + thumb.w - 1, thumb.y,
                      thumb.x + thumb.w - 1, thumb.y + thumb.h - 1,
                      0x00404040);
}

bool gui_scrollbar_handle_click_vertical(const gui_scrollbar_t *bar,
                                         int x, int y,
                                         uint32_t *new_value) {
    gui_rect_t b;
    gui_rect_t thumb;
    uint32_t value;
    uint32_t max_value;

    if (!bar || !new_value || !gui_rect_contains(bar->bounds, x, y))
        return false;
    if (bar->visible >= bar->total) {
        *new_value = 0;
        return true;
    }
    b = bar->bounds;
    value = bar->value;
    max_value = bar->total - bar->visible;
    thumb = gui_scrollbar_thumb_rect(bar);
    if (y < b.y + GUI_SCROLLBAR_SIZE) {
        if (value) value--;
    } else if (y >= b.y + b.h - GUI_SCROLLBAR_SIZE) {
        if (value < max_value) value++;
    } else if (y < thumb.y) {
        value = value > bar->visible ? value - bar->visible : 0;
    } else if (y >= thumb.y + thumb.h) {
        value += bar->visible;
        if (value > max_value) value = max_value;
    }
    *new_value = value;
    return true;
}

bool gui_scrollbar_handle_event_vertical(const gui_scrollbar_t *bar,
                                         gui_scrollbar_drag_t *drag,
                                         const gui_event_t *event,
                                         uint32_t wheel_step,
                                         uint32_t *new_value) {
    gui_rect_t thumb;
    uint32_t max_value;
    int track_y;
    int track_h;
    int thumb_top;

    if (!bar || !drag || !event || !new_value) return false;
    max_value = bar->total > bar->visible ? bar->total - bar->visible : 0;

    if (event->type == GUI_EVENT_MOUSE_WHEEL) {
        int64_t value;
        if (!wheel_step) wheel_step = 1;
        value = (int64_t)bar->value - (int64_t)event->dy * wheel_step;
        if (value < 0) value = 0;
        if ((uint64_t)value > max_value) value = max_value;
        *new_value = (uint32_t)value;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN &&
        event->button == 1 && gui_rect_contains(bar->bounds, event->x, event->y)) {
        thumb = gui_scrollbar_thumb_rect(bar);
        if (gui_rect_contains(thumb, event->x, event->y) && max_value) {
            drag->active = true;
            drag->grab_offset = event->y - thumb.y;
            *new_value = bar->value;
            return true;
        }
        return gui_scrollbar_handle_click_vertical(bar, event->x, event->y,
                                                   new_value);
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE && drag->active) {
        if (!(event->buttons & 1)) {
            drag->active = false;
            return false;
        }
        thumb = gui_scrollbar_thumb_rect(bar);
        track_y = bar->bounds.y + GUI_SCROLLBAR_SIZE;
        track_h = bar->bounds.h - GUI_SCROLLBAR_SIZE * 2 - thumb.h;
        if (track_h <= 0) {
            *new_value = 0;
            return true;
        }
        thumb_top = event->y - drag->grab_offset - track_y;
        if (thumb_top < 0) thumb_top = 0;
        if (thumb_top > track_h) thumb_top = track_h;
        *new_value = track_h > 0
            ? (uint32_t)(((uint64_t)thumb_top * max_value) / (uint32_t)track_h)
            : 0;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP && event->button == 1 &&
        drag->active) {
        drag->active = false;
        *new_value = bar->value;
        return true;
    }
    return false;
}
