#include "programs.h"
#include "../kernel/include/memory.h"   // Para kzalloc, kfree, kstrncpy
#include "../kernel/include/pit.h"      // Para pit_get_ticks
#include "../kernel/include/task.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/block.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/iso9660.h"

/* ══════════════════════════════════════════════════════════════════════════
 *  Iconos de escritorio
 *
 *  Cada icono de escritorio tiene:
 *    - Posición y tamaño (bounds)
 *    - Etiqueta visible
 *    - Estado hover / press
 *    - Registro de doble clic
 *    - Callback que se dispara al hacer doble clic
 * ══════════════════════════════════════════════════════════════════════════ */

#define DESK_ICON_W          80
#define DESK_ICON_H          94   /* 64px imagen + etiqueta de hasta dos lineas */
#define DESK_ICON_IMG_W      64
#define DESK_ICON_IMG_H      64
#define DESK_DBLCLICK_TICKS  500  /* ~500 ms con PIT a 1000 Hz */
#define DESK_MAX_ICONS      12
#define RESIZE_MARGIN         5
#define RESIZE_LEFT           0x01
#define RESIZE_RIGHT          0x02
#define RESIZE_TOP            0x04
#define RESIZE_BOTTOM         0x08

typedef void (*desk_icon_open_fn)(gui_desktop_t *desktop);

typedef struct {
    int      x, y;
    char     label[24];
    bool     hovered;
    bool     pressed;
    uint32_t last_click_tick;
    int      last_click_x;
    int      last_click_y;
    desk_icon_open_fn open;
    uint32_t *pixels;
} desk_icon_t;

typedef struct {
    desk_icon_t icons[DESK_MAX_ICONS];
    int         icon_count;
    int         base_icon_count;
    bool        context_open;
    int         context_x;
    int         context_y;
    uint32_t    last_drive_poll;
    uint16_t    last_surface_width;
    bool        show_cdrom;
    bool        show_usb;
    bool        show_floppy;
} deskmanager_state_t;

static uint32_t g_desktop_background = 0x00204070;
static uint32_t *g_desktop_wallpaper;
static uint16_t g_wallpaper_w;
static uint16_t g_wallpaper_h;
static gui_desktop_t *g_desk_desktop;
static deskmanager_state_t *g_desk_state;
static char g_wallpaper_path[VFS_MAX_PATH];

void deskmanager_set_background(uint32_t color) {
    g_desktop_background = color & 0x00FFFFFF;
    if (g_desktop_wallpaper) {
        kfree(g_desktop_wallpaper);
        g_desktop_wallpaper = NULL;
    }
    g_wallpaper_path[0] = '\0';
}

uint32_t deskmanager_get_background(void) {
    return g_desktop_background;
}

bool deskmanager_set_wallpaper(const char *path) {
    uint32_t *pixels;
    if (!g_desk_desktop || !path) return false;
    pixels = program_load_bmp_wallpaper_scaled(path,
        g_desk_desktop->surface.width, g_desk_desktop->surface.height);
    if (!pixels) return false;
    if (g_desktop_wallpaper) kfree(g_desktop_wallpaper);
    g_desktop_wallpaper = pixels;
    g_wallpaper_w = g_desk_desktop->surface.width;
    g_wallpaper_h = g_desk_desktop->surface.height;
    kstrncpy(g_wallpaper_path, path, sizeof(g_wallpaper_path) - 1);
    g_wallpaper_path[sizeof(g_wallpaper_path) - 1] = '\0';
    return true;
}

/* ── utilidad: leer ticks PIT (declarada en pit.h, incluida via programs.h→gui.h) ── */
extern uint32_t pit_get_ticks(void);

/* ──────────────────────────────────────────────────────────────────────────
 *  Callbacks de apertura de cada icono
 * ────────────────────────────────────────────────────────────────────────── */

/* Forward-declare las funciones de apertura que viven en sus módulos */
extern void filebrowser_open_from_desktop(gui_desktop_t *desktop);
extern void shelllauncher_open_from_desktop(gui_desktop_t *desktop);
extern void texteditor_open_from_desktop(gui_desktop_t *desktop);
extern void calculator_open_from_desktop(gui_desktop_t *desktop);
extern void processmanager_open_from_desktop(gui_desktop_t *desktop);
extern void midamp_open_from_desktop(gui_desktop_t *desktop);
extern void imageviewer_open_from_desktop(gui_desktop_t *desktop);
extern void games_open_from_desktop(gui_desktop_t *desktop);
extern void settings_open_from_desktop(gui_desktop_t *desktop);

static void cdrom_open_from_desktop(gui_desktop_t *desktop) {
    filebrowser_open_path(desktop, "/CDROM");
}

static void floppy_open_from_desktop(gui_desktop_t *desktop) {
    if (vfs_mount("fd0")) filebrowser_open_path(desktop, "/");
}

static void usb_open_from_desktop(gui_desktop_t *desktop) {
    if (vfs_mount("usb0")) filebrowser_open_path(desktop, "/");
}

static uint16_t desk_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t desk_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool desk_color_matches_key(uint32_t rgb, uint32_t key) {
    int red = (int)((rgb >> 16) & 0xFF) - (int)((key >> 16) & 0xFF);
    int green = (int)((rgb >> 8) & 0xFF) - (int)((key >> 8) & 0xFF);
    int blue = (int)(rgb & 0xFF) - (int)(key & 0xFF);
    if (red < 0) red = -red;
    if (green < 0) green = -green;
    if (blue < 0) blue = -blue;
    return red <= 8 && green <= 8 && blue <= 8;
}

static uint32_t *desk_load_bmp_scaled(const char *path,
                                      uint16_t output_width,
                                      uint16_t output_height,
                                      bool transparent) {
    void *file = NULL;
    uint32_t size = 0;
    if (output_width == 0 || output_height == 0) return NULL;
    if (!vfs_read_all(path, &file, &size) || !file || size < 54) return NULL;
    uint8_t *data = (uint8_t *)file;
    uint32_t offset = desk_le32(data + 10);
    int width = (int)desk_le32(data + 18);
    int raw_height = (int)desk_le32(data + 22);
    int height = raw_height < 0 ? -raw_height : raw_height;
    uint16_t bpp = desk_le16(data + 28);
    uint32_t compression = desk_le32(data + 30);
    if (data[0] != 'B' || data[1] != 'M' || width <= 0 || height <= 0 ||
        width > 1024 || height > 1024 || (bpp != 24 && bpp != 32) ||
        compression != 0 || offset >= size) {
        kfree(file);
        return NULL;
    }

    uint32_t stride = ((uint32_t)width * (bpp / 8) + 3) & ~3U;
    if (offset + stride * (uint32_t)height > size) {
        kfree(file);
        return NULL;
    }
    uint32_t pixel_count = (uint32_t)output_width * output_height;
    if (pixel_count > 1024U * 1024U) {
        kfree(file);
        return NULL;
    }
    uint32_t *pixels = (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t));
    if (!pixels) {
        kfree(file);
        return NULL;
    }
    int key_y = raw_height > 0 ? height - 1 : 0;
    uint8_t *key_src = data + offset + (uint32_t)key_y * stride;
    uint32_t key_rgb = ((uint32_t)key_src[2] << 16) |
                       ((uint32_t)key_src[1] << 8) | key_src[0];

    for (uint16_t y = 0; y < output_height; y++) {
        int sy = ((int)y * height) / output_height;
        if (raw_height > 0) sy = height - 1 - sy;
        for (uint16_t x = 0; x < output_width; x++) {
            int sx = ((int)x * width) / output_width;
            uint8_t *src = data + offset + (uint32_t)sy * stride +
                           (uint32_t)sx * (bpp / 8);
            uint32_t rgb = ((uint32_t)src[2] << 16) |
                           ((uint32_t)src[1] << 8) | src[0];
            uint8_t alpha = bpp == 32 ? src[3] : 0xFF;
            if (!transparent) alpha = 0xFF;
            /*
             * BMP de 32 bits: el alfa es autoritativo. No aplicar color-key
             * porque los píxeles transparentes suelen guardarse con RGB negro
             * y eso borraría también contornos negros opacos.
             */
            if ((transparent && alpha == 0) ||
                (transparent && bpp == 24 &&
                 (desk_color_matches_key(rgb, key_rgb) ||
                  rgb == 0x00FF00FF))) {
                pixels[(uint32_t)y * output_width + x] = 0;
            } else {
                pixels[(uint32_t)y * output_width + x] =
                    ((uint32_t)alpha << 24) | rgb;
            }
        }
    }
    kfree(file);
    return pixels;
}

uint32_t *program_load_bmp_icon_scaled(const char *path,
                                       uint16_t output_width,
                                       uint16_t output_height) {
    return desk_load_bmp_scaled(path, output_width, output_height, true);
}

uint32_t *program_load_bmp_wallpaper_scaled(const char *path,
                                            uint16_t output_width,
                                            uint16_t output_height) {
    return desk_load_bmp_scaled(path, output_width, output_height, false);
}

uint32_t *program_load_bmp_icon(const char *path) {
    return program_load_bmp_icon_scaled(path, 28, 28);
}

void program_draw_icon_pixels(gui_surface_t *surface, int x, int y,
                              const uint32_t *pixels,
                              uint16_t width, uint16_t height) {
    if (!surface || !surface->pixels || !pixels) return;
    for (uint16_t py = 0; py < height; py++) {
        for (uint16_t px = 0; px < width; px++) {
            uint32_t color = pixels[(uint32_t)py * width + px];
            uint8_t alpha = (uint8_t)(color >> 24);
            uint32_t rgb = color & 0x00FFFFFF;
            int dx = x + px;
            int dy = y + py;
            uint32_t *dst;

            if (alpha == 0) continue;
            if (dx < 0 || dy < 0 || dx >= surface->width ||
                dy >= surface->height) continue;
            if (alpha == 0xFF) {
                gui_gfx_putpixel(surface, dx, dy, rgb);
                continue;
            }

            dst = &surface->pixels[(uint32_t)dy * surface->pitch +
                                   (uint32_t)dx];
            *dst = gui_color_blend(*dst, rgb, alpha);
        }
    }
}

static void desk_clear_icon(desk_icon_t *icon) {
    if (!icon) return;
    if (icon->pixels) kfree(icon->pixels);
    kmemset(icon, 0, sizeof(*icon));
}

static void desk_remove_icons_from(deskmanager_state_t *st, int first) {
    if (!st) return;
    if (first < 0) first = 0;
    if (first > st->icon_count) first = st->icon_count;
    for (int i = first; i < st->icon_count; i++) {
        desk_clear_icon(&st->icons[i]);
    }
    st->icon_count = first;
}

static bool desk_devices_share_boot_sector(const char *lhs,
                                           const char *rhs) {
    block_device_t *left = block_get(lhs);
    block_device_t *right = block_get(rhs);
    uint8_t left_sector[BLOCK_SECTOR_SIZE];
    uint8_t right_sector[BLOCK_SECTOR_SIZE];

    if (!left || !right) return false;
    if (left->sector_size != BLOCK_SECTOR_SIZE ||
        right->sector_size != BLOCK_SECTOR_SIZE)
        return false;
    if (!block_read(left, 0, 1, left_sector) ||
        !block_read(right, 0, 1, right_sector))
        return false;
    return kmemcmp(left_sector, right_sector, BLOCK_SECTOR_SIZE) == 0;
}

static bool desk_cdrom_available(void) {
    if (vfs_has_cdrom()) return true;
    if (block_get("cd0")) (void)iso9660_mount_default();
    return vfs_has_cdrom();
}

static bool desk_usb_available(void) {
    return block_get("usb0") != NULL;
}

static bool desk_floppy_available(void) {
    const char *mount = vfs_get_mount_name();

    if (!block_get("fd0")) return false;
    if (mount && kstrcmp(mount, "fd0") == 0) return false;
    if (mount && kstrncmp(mount, "ata", 3) == 0 &&
        desk_devices_share_boot_sector("fd0", mount))
        return false;
    return true;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Dibujo de iconos
 * ────────────────────────────────────────────────────────────────────────── */

/*
 * Iconos integrados eliminados.
 * El escritorio ahora dibuja solo BMP externos cargados desde /ICONS.
 */

static void desk_draw_icon(gui_surface_t *surface, const desk_icon_t *icon) {
    int x = icon->x;
    int y = icon->y;

    /* Selección / hover: fondo resaltado */
    if (icon->hovered || icon->pressed) {
        gui_gfx_fill_rect(surface,
            (gui_rect_t){x - 2, y - 2, DESK_ICON_W + 4, DESK_ICON_H + 4},
            icon->pressed ? 0x00204060 : 0x002A5080);
    }

    /* Imagen del icono (centrada horizontalmente en DESK_ICON_W) */
    int img_x = x + (DESK_ICON_W - DESK_ICON_IMG_W) / 2;
    int img_y = y + 2;

    if (icon->pixels) {
        program_draw_icon_pixels(surface, img_x, img_y,
                                 icon->pixels,
                                 DESK_ICON_IMG_W, DESK_ICON_IMG_H);
    }

    /* Etiqueta debajo */
    uint32_t fg = (icon->hovered || icon->pressed) ? 0x00FFFFFF : 0x00EEEEFF;
    int label_w = (int)gui_font_text_width(icon->label);
    gui_rect_t label_clip = {x - 2, y + DESK_ICON_IMG_H + 5,
                             DESK_ICON_W + 4, 24};
    if (label_w <= DESK_ICON_W) {
        int label_x = x + (DESK_ICON_W - label_w) / 2;
        gui_font_draw_string_clipped(surface, label_x,
                                     y + DESK_ICON_IMG_H + 8,
                                     icon->label, fg, label_clip);
    } else {
        char first[10];
        char second[16];
        int split = 0;
        while (icon->label[split] && split < 9) {
            first[split] = icon->label[split];
            split++;
        }
        first[split] = '\0';
        kstrncpy(second, icon->label + split, sizeof(second) - 1);
        second[sizeof(second) - 1] = '\0';
        int first_x = x + (DESK_ICON_W - (int)gui_font_text_width(first)) / 2;
        int second_x = x + (DESK_ICON_W - (int)gui_font_text_width(second)) / 2;
        gui_font_draw_string_clipped(surface, first_x,
                                     y + DESK_ICON_IMG_H + 5,
                                     first, fg, label_clip);
        gui_font_draw_string_clipped(surface, second_x,
                                     y + DESK_ICON_IMG_H + 15,
                                     second, fg, label_clip);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Registro de iconos
 * ────────────────────────────────────────────────────────────────────────── */

static void desk_add_icon(deskmanager_state_t *st,
                           int x, int y,
                           const char *label,
                           desk_icon_open_fn open_fn) {
    if (st->icon_count >= DESK_MAX_ICONS) return;
    desk_icon_t *ic = &st->icons[st->icon_count++];
    ic->x           = x;
    ic->y           = y;
    ic->hovered     = false;
    ic->pressed     = false;
    ic->last_click_tick = 0;
    ic->last_click_x    = -999;
    ic->last_click_y    = -999;
    ic->open        = open_fn;
    const char *icon_path = NULL;
    if (open_fn == filebrowser_open_from_desktop) icon_path = "/ICONS/FILES.BMP";
    else if (open_fn == shelllauncher_open_from_desktop) icon_path = "/ICONS/SHELL.BMP";
    else if (open_fn == texteditor_open_from_desktop) icon_path = "/ICONS/EDITOR.BMP";
    else if (open_fn == calculator_open_from_desktop) icon_path = "/ICONS/CALC.BMP";
    else if (open_fn == processmanager_open_from_desktop) icon_path = "/ICONS/PROCESOS.BMP";
    else if (open_fn == midamp_open_from_desktop) icon_path = "/ICONS/MIDAMP.BMP";
    else if (open_fn == imageviewer_open_from_desktop) icon_path = "/ICONS/IMAGE.BMP";
    else if (open_fn == settings_open_from_desktop) icon_path = "/ICONS/CONFIG.BMP";
    else if (open_fn == cdrom_open_from_desktop) icon_path = "/ICONS/CDROM.BMP";
    else if (open_fn == floppy_open_from_desktop) icon_path = "/ICONS/FLOPPY.BMP";
    else if (open_fn == usb_open_from_desktop) icon_path = "/ICONS/USB.BMP";
    ic->pixels = icon_path
        ? program_load_bmp_icon_scaled(icon_path,
                                       DESK_ICON_IMG_W, DESK_ICON_IMG_H)
        : NULL;
    kstrncpy(ic->label, label ? label : "", sizeof(ic->label) - 1);
    ic->label[sizeof(ic->label) - 1] = '\0';
}

static void desk_sync_drive_icons(deskmanager_state_t *st,
                                  gui_desktop_t *desktop,
                                  bool force) {
    bool show_cdrom;
    bool show_usb;
    bool show_floppy;
    uint32_t now;
    int drive_x;
    int drive_y;

    if (!st || !desktop) return;

    now = pit_get_ticks();
    if (!force && now - st->last_drive_poll < 1000U) return;
    st->last_drive_poll = now;

    show_cdrom = desk_cdrom_available();
    show_usb = desk_usb_available();
    show_floppy = desk_floppy_available();
    if (!force &&
        st->last_surface_width == desktop->surface.width &&
        st->show_cdrom == show_cdrom &&
        st->show_usb == show_usb &&
        st->show_floppy == show_floppy)
        return;

    desk_remove_icons_from(st, st->base_icon_count);
    st->show_cdrom = show_cdrom;
    st->show_usb = show_usb;
    st->show_floppy = show_floppy;
    st->last_surface_width = desktop->surface.width;

    drive_x = desktop->surface.width - DESK_ICON_W - 16;
    drive_y = 16;
    if (show_cdrom && st->icon_count < DESK_MAX_ICONS) {
        desk_add_icon(st, drive_x, drive_y, "CD-ROM",
                      cdrom_open_from_desktop);
        drive_y += 100;
    }
    if (show_usb && st->icon_count < DESK_MAX_ICONS) {
        desk_add_icon(st, drive_x, drive_y, "USB",
                      usb_open_from_desktop);
        drive_y += 100;
    }
    if (show_floppy && st->icon_count < DESK_MAX_ICONS) {
        desk_add_icon(st, drive_x, drive_y, "Disquete",
                      floppy_open_from_desktop);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Hit-test
 * ────────────────────────────────────────────────────────────────────────── */

static int desk_hit_icon(const deskmanager_state_t *st, int mx, int my) {
    for (int i = 0; i < st->icon_count; i++) {
        gui_rect_t r = {st->icons[i].x - 2, st->icons[i].y - 2,
                        DESK_ICON_W + 4, DESK_ICON_H + 4};
        if (gui_rect_contains(r, mx, my)) return i;
    }
    return -1;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Pintar ventanas (igual que antes)
 * ────────────────────────────────────────────────────────────────────────── */

static void deskmanager_paint_windows(gui_desktop_t *desktop,
                                       gui_surface_t *surface,
                                       gui_rect_t screen) {
    task_preempt_disable();
    gui_window_t *window = desktop->first_window;
    while (window) {
        gui_window_paint(surface, window, screen);
        if (window->visible && window->content_paint)
            window->content_paint(window, surface, window->content_context);
        gui_window_paint_menus(surface, window);
        window->dirty = false;
        window = window->next;
    }
    task_preempt_enable();
}

static bool deskmanager_dispatch_widgets(gui_window_t *window,
                                          const gui_event_t *event) {
    gui_widget_t *widget;
    bool handled = false;

    if (!window) return false;
    widget = window->widgets;
    while (widget) {
        if (gui_widget_handle_event(window, widget, event)) {
            window->dirty = true;
            handled = true;
        }
        widget = widget->next;
    }
    return handled;
}

static uint8_t desk_resize_edges(gui_window_t *window, int x, int y) {
    uint8_t edges = 0;
    if (!window || !gui_window_contains(window, x, y)) return 0;
    if (x < window->bounds.x + RESIZE_MARGIN) edges |= RESIZE_LEFT;
    if (x >= window->bounds.x + window->bounds.w - RESIZE_MARGIN) edges |= RESIZE_RIGHT;
    if (y < window->bounds.y + RESIZE_MARGIN) edges |= RESIZE_TOP;
    if (y >= window->bounds.y + window->bounds.h - RESIZE_MARGIN) edges |= RESIZE_BOTTOM;
    return edges;
}

static void desk_resize_window(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window = desktop->resize_window;
    gui_rect_t bounds = desktop->resize_start_bounds;
    int dx = x - desktop->resize_start_x;
    int dy = y - desktop->resize_start_y;
    if (!window) return;

    if (desktop->resize_edges & RESIZE_LEFT) {
        bounds.x += dx;
        bounds.w -= dx;
    }
    if (desktop->resize_edges & RESIZE_RIGHT) bounds.w += dx;
    if (desktop->resize_edges & RESIZE_TOP) {
        bounds.y += dy;
        bounds.h -= dy;
    }
    if (desktop->resize_edges & RESIZE_BOTTOM) bounds.h += dy;

    int min_w = window->min_w > 0 ? window->min_w : 160;
    int min_h = window->min_h > 0 ? window->min_h : 90;
    if (bounds.w < min_w) {
        if (desktop->resize_edges & RESIZE_LEFT) bounds.x -= min_w - bounds.w;
        bounds.w = min_w;
    }
    if (bounds.h < min_h) {
        if (desktop->resize_edges & RESIZE_TOP) bounds.y -= min_h - bounds.h;
        bounds.h = min_h;
    }
    if (bounds.x < 0) {
        bounds.w += bounds.x;
        bounds.x = 0;
    }
    if (bounds.y < 0) {
        bounds.h += bounds.y;
        bounds.y = 0;
    }
    if (bounds.x + bounds.w > desktop->surface.width)
        bounds.w = desktop->surface.width - bounds.x;
    if (bounds.y + bounds.h > desktop->surface.height - 28)
        bounds.h = desktop->surface.height - 28 - bounds.y;
    window->bounds = bounds;
    window->dirty = true;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Paint principal
 * ────────────────────────────────────────────────────────────────────────── */

static void deskmanager_paint(gui_program_t *program,
                               gui_desktop_t *desktop,
                               gui_surface_t *surface) {
    deskmanager_state_t *st = (deskmanager_state_t *)program->state;
    gui_rect_t screen;

    if (!desktop || !surface) return;
    if (st) desk_sync_drive_icons(st, desktop, false);

    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    gui_gfx_fill_rect(surface, screen, g_desktop_background);
    if (g_desktop_wallpaper)
        program_draw_icon_pixels(surface, 0, 0, g_desktop_wallpaper,
                                 g_wallpaper_w, g_wallpaper_h);

    /* Iconos de escritorio */
    if (st) {
        for (int i = 0; i < st->icon_count; i++) {
            desk_draw_icon(surface, &st->icons[i]);
        }
    }

    deskmanager_paint_windows(desktop, surface, screen);
    if (st && st->context_open) {
        gui_rect_t menu = {st->context_x, st->context_y, 160, 62};
        gui_gfx_fill_rect(surface, menu, 0x00404040);
        gui_gfx_fill_rect(surface, (gui_rect_t){menu.x + 1, menu.y + 1,
                                                menu.w - 2, menu.h - 2},
                          0x00D0D0C8);
        gui_font_draw_string_clipped(surface, menu.x + 10, menu.y + 11,
                                     "Nueva carpeta", 0x00101010,
                                     (gui_rect_t){menu.x + 3, menu.y + 3,
                                                  menu.w - 6, 27});
        gui_font_draw_string_clipped(surface, menu.x + 10, menu.y + 39,
                                     "Nuevo TXT", 0x00101010,
                                     (gui_rect_t){menu.x + 3, menu.y + 31,
                                                  menu.w - 6, 27});
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Handle events
 * ────────────────────────────────────────────────────────────────────────── */

static bool deskmanager_handle_event(gui_program_t *program,
                                      gui_desktop_t *desktop,
                                      const gui_event_t *event) {
    deskmanager_state_t *st = (deskmanager_state_t *)program->state;
    gui_window_t *hit;
    bool handled = false;
    bool left_click;
    bool right_click;

    if (!desktop || !event) return false;
    left_click = event->button == MOUSE_LEFT_BUTTON;
    right_click = event->button == MOUSE_RIGHT_BUTTON;

    /* ── Primero probar si el clic cae sobre una ventana existente ── */
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (st && st->context_open) {
            gui_rect_t folder_item = {st->context_x + 2, st->context_y + 2,
                                      156, 28};
            gui_rect_t text_item = {st->context_x + 2, st->context_y + 30,
                                    156, 28};
            if (gui_rect_contains(folder_item, event->x, event->y)) {
                (void)vfs_mkdir("/NEWFOLDER");
                st->context_open = false;
                return true;
            }
            if (gui_rect_contains(text_item, event->x, event->y)) {
                (void)vfs_write_all("/NEWTEXT.TXT", NULL, 0);
                texteditor_open(desktop, "/NEWTEXT.TXT");
                st->context_open = false;
                return true;
            }
            st->context_open = false;
        }
        hit = gui_desktop_window_at(desktop, event->x, event->y);
        if (hit) {
            gui_desktop_raise_window(desktop, hit);
            gui_desktop_focus_window(desktop, hit);
            if (!left_click) {
                handled = gui_window_dispatch_event(hit, event) || handled;
                return handled || right_click;
            }
            if (gui_window_handle_menu_event(hit, event)) return true;
            gui_window_button_t button =
                gui_window_titlebar_button_at(hit, event->x, event->y);
            if (button == GUI_WINDOW_BUTTON_CLOSE) {
                gui_window_close(hit);
                gui_desktop_focus_window(desktop, NULL);
                return true;
            }
            if (button == GUI_WINDOW_BUTTON_MINIMIZE) {
                gui_window_minimize(hit);
                gui_desktop_focus_window(desktop, NULL);
                return true;
            }
            uint8_t edges = desk_resize_edges(hit, event->x, event->y);
            if (edges) {
                desktop->resize_window = hit;
                desktop->resize_edges = edges;
                desktop->resize_start_bounds = hit->bounds;
                desktop->resize_start_x = event->x;
                desktop->resize_start_y = event->y;
                return true;
            }
            if (gui_window_titlebar_contains(hit, event->x, event->y)) {
                desktop->drag_window  = hit;
                desktop->drag_off_x   = event->x - hit->bounds.x;
                desktop->drag_off_y   = event->y - hit->bounds.y;
            }
            handled = deskmanager_dispatch_widgets(hit, event) || handled;
            handled = gui_window_dispatch_event(hit, event) || handled;

            /* Quitar hover/press de todos los iconos */
            if (st) {
                for (int i = 0; i < st->icon_count; i++) {
                    st->icons[i].pressed = false;
                }
            }
            return true;
        }

        /* No hay ventana → probar icono */
        gui_desktop_focus_window(desktop, NULL);

        if (st) {
            int idx = desk_hit_icon(st, event->x, event->y);
            if (idx >= 0) {
                if (left_click) {
                    st->icons[idx].pressed = true;
                    return true;
                }
                return right_click;
            }
            if (!right_click) return false;
            st->context_x = event->x;
            st->context_y = event->y;
            if (st->context_x + 160 > desktop->surface.width)
                st->context_x = desktop->surface.width - 160;
            if (st->context_y + 62 > desktop->surface.height - 24)
                st->context_y = desktop->surface.height - 24 - 62;
            st->context_open = true;
            return true;
        }
        return false;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        if (desktop->focused_window) {
            if (gui_window_handle_menu_event(desktop->focused_window, event))
                return true;
            handled = deskmanager_dispatch_widgets(desktop->focused_window, event);
            handled = gui_window_dispatch_event(desktop->focused_window, event) || handled;
        }
        if (desktop->drag_window) handled = true;
        desktop->drag_window = NULL;
        if (desktop->resize_window) handled = true;
        desktop->resize_window = NULL;
        desktop->resize_edges = 0;

        /* Soltar icono → detectar doble clic */
        if (st) {
            for (int i = 0; i < st->icon_count; i++) {
                desk_icon_t *ic = &st->icons[i];
                if (!ic->pressed) continue;
                ic->pressed = false;

                gui_rect_t r = {ic->x - 2, ic->y - 2,
                                DESK_ICON_W + 4, DESK_ICON_H + 4};
                if (!gui_rect_contains(r, event->x, event->y)) continue;

                uint32_t now   = pit_get_ticks();
                uint32_t delta = now - ic->last_click_tick;
                bool same = (event->x >= ic->last_click_x - 4 &&
                             event->x <= ic->last_click_x + 4 &&
                             event->y >= ic->last_click_y - 4 &&
                             event->y <= ic->last_click_y + 4);

                if (delta < DESK_DBLCLICK_TICKS && same && ic->last_click_tick != 0) {
                    /* ¡Doble clic! */
                    if (ic->open) ic->open(desktop);
                    ic->last_click_tick = 0;
                } else {
                    ic->last_click_tick = now;
                    ic->last_click_x    = event->x;
                    ic->last_click_y    = event->y;
                }
                handled = true;
            }
        }
        return handled;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        if (desktop->resize_window) {
            desk_resize_window(desktop, event->x, event->y);
            return true;
        }
        if (desktop->drag_window) {
            desktop->drag_window->bounds.x = event->x - desktop->drag_off_x;
            desktop->drag_window->bounds.y = event->y - desktop->drag_off_y;
            desktop->drag_window->dirty    = true;
            handled = true;
        }
        if (desktop->focused_window &&
            deskmanager_dispatch_widgets(desktop->focused_window, event)) {
            handled = true;
        }
        if (desktop->focused_window &&
            gui_window_dispatch_event(desktop->focused_window, event)) {
            handled = true;
        }

        /* Actualizar hover de iconos */
        if (st) {
            for (int i = 0; i < st->icon_count; i++) {
                gui_rect_t r = {st->icons[i].x - 2, st->icons[i].y - 2,
                                DESK_ICON_W + 4, DESK_ICON_H + 4};
                st->icons[i].hovered = gui_rect_contains(r, event->x, event->y);
            }
        }
        return handled;
    }

    if (event->type == GUI_EVENT_KEY && desktop->focused_window) {
        return gui_window_dispatch_event(desktop->focused_window, event);
    }

    return false;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Destructor
 * ────────────────────────────────────────────────────────────────────────── */

static void deskmanager_destroy(gui_program_t *program) {
    if (!program || !program->state) return;
    deskmanager_state_t *st = (deskmanager_state_t *)program->state;
    for (int i = 0; i < st->icon_count; i++)
        desk_clear_icon(&st->icons[i]);
    if (g_desk_state == st) g_desk_state = NULL;
    if (g_desk_desktop && g_desk_state == NULL) g_desk_desktop = NULL;
    kfree(program->state);
    program->state = NULL;
}

void deskmanager_refresh_layout(void) {
    if (g_wallpaper_path[0]) (void)deskmanager_set_wallpaper(g_wallpaper_path);
    if (g_desk_state && g_desk_desktop)
        desk_sync_drive_icons(g_desk_state, g_desk_desktop, true);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Instalación
 * ────────────────────────────────────────────────────────────────────────── */

void deskmanager_install(gui_desktop_t *desktop) {
    deskmanager_state_t *st;
    gui_program_t *prog;

    if (!desktop) return;

    st = (deskmanager_state_t *)kzalloc(sizeof(deskmanager_state_t));
    if (!st) return;

    st->icon_count = 0;
    st->base_icon_count = 0;
    g_desk_desktop = desktop;
    g_desk_state = st;

    void *config = NULL;
    uint32_t config_size = 0;
    if (vfs_read_all("/Desktop.INI", &config, &config_size) && config) {
        char *line = (char *)config;
        while (*line) {
            char *end = line;
            char *eq = NULL;
            while (*end && *end != '\r' && *end != '\n') {
                if (*end == '=' && !eq) eq = end;
                end++;
            }
            char saved = *end;
            *end = '\0';
            if (eq) {
                *eq++ = '\0';
                desk_icon_open_fn open_fn = NULL;
                if (kstrcmp(line, "files") == 0) {
                    open_fn = filebrowser_open_from_desktop;
                } else if (kstrcmp(line, "shell") == 0) {
                    open_fn = shelllauncher_open_from_desktop;
                } else if (kstrcmp(line, "editor") == 0) {
                    open_fn = texteditor_open_from_desktop;
                } else if (kstrcmp(line, "calculator") == 0) {
                    open_fn = calculator_open_from_desktop;
                } else if (kstrcmp(line, "processes") == 0) {
                    open_fn = processmanager_open_from_desktop;
                } else if (kstrcmp(line, "midamp") == 0) {
                    open_fn = midamp_open_from_desktop;
                } else if (kstrcmp(line, "viewer") == 0) {
                    open_fn = imageviewer_open_from_desktop;
                } else if (kstrcmp(line, "games") == 0) {
                    open_fn = games_open_from_desktop;
                } else if (kstrcmp(line, "settings") == 0) {
                    open_fn = settings_open_from_desktop;
                }
                if (open_fn) {
                    char *comma1 = eq;
                    char *comma2 = NULL;
                    while (*comma1 && *comma1 != ',') comma1++;
                    if (*comma1) {
                        *comma1++ = '\0';
                        comma2 = comma1;
                        while (*comma2 && *comma2 != ',') comma2++;
                    }
                    int x = 16;
                    int y = 16 + st->icon_count * 100;
                    if (comma2 && *comma2) {
                        *comma2++ = '\0';
                        x = 0;
                        y = 0;
                        while (*comma1 >= '0' && *comma1 <= '9')
                            x = x * 10 + (*comma1++ - '0');
                        while (*comma2 >= '0' && *comma2 <= '9')
                            y = y * 10 + (*comma2++ - '0');
                    }
                    desk_add_icon(st, x, y, eq, open_fn);
                }
            }
            *end = saved;
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
        kfree(config);
    }

    st->base_icon_count = st->icon_count;
    desk_sync_drive_icons(st, desktop, true);

    prog = gui_desktop_register_program(desktop, "deskmanager", st,
                                        deskmanager_paint,
                                        deskmanager_handle_event,
                                        deskmanager_destroy);
    if (!prog) {
        if (g_desk_state == st) g_desk_state = NULL;
        desk_remove_icons_from(st, 0);
        kfree(st);
    }
}
