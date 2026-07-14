#include "programs.h"
#include "kernel/include/memory.h"   // Para kzalloc, kfree, kstrncpy
#include "kernel/include/api.h"
#include "kernel/include/task.h"
#include "kernel/include/vfs.h"
#include "kernel/include/block.h"
#include "kernel/include/mouse.h"
#include "kernel/include/iso9660.h"
#include "kernel/include/bootsplash.h"
#include "kernel/include/user_config.h"
#include "kernel/stdio.h"

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
    gui_context_menu_t context_menu;
    int         context_icon;
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

const char *deskmanager_get_wallpaper_path(void) {
    return g_wallpaper_path;
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

/* ──────────────────────────────────────────────────────────────────────────
 *  Callbacks de apertura de cada icono
 * ────────────────────────────────────────────────────────────────────────── */

/* Forward-declare las funciones de apertura que viven en sus módulos */
static void desk_open_files_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/FILE.O");
}

static void desk_open_shell_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/SHELL.O");
}

static void desk_open_editor_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/TEXTEDITOR.O");
}

static void desk_open_calculator_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/CALCULATOR.O");
}

static void desk_open_processmanager_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/PROCESSMANAGER.O");
}

static void desk_open_midamp_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/MIDAMP.O");
}

static void desk_open_viewer_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/IMAGEVIEWER.O");
}

static void desk_open_games_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/GAMES.O");
}

static void desk_open_control_panel(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/CONTROL/CONTROL.O");
}

static void desk_open_cdrom(gui_desktop_t *desktop) {
    /* El acceso ATAPI puede ser lento: solo se intenta por accion del usuario. */
    if (vfs_has_cdrom() || iso9660_mount_default())
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.O",
                                       "/CDROM");
}

static void desk_open_floppy(gui_desktop_t *desktop) {
    if (vfs_mount("fd0"))
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.O",
                                       "/");
}

static void desk_open_usb(gui_desktop_t *desktop) {
    if (vfs_mount("usb0"))
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.O",
                                       "/");
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

static bool desk_use_exact_key(const char *path) {
    return path && kstrcmp(path, "/ICONS/MONITOR.BMP") == 0;
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
    bool exact_key = desk_use_exact_key(path);
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
                 ((exact_key ? rgb == key_rgb
                             : desk_color_matches_key(rgb, key_rgb)) ||
                  rgb == 0x00FF00FF))) {
                pixels[(uint32_t)y * output_width + x] = 0;
            } else {
                pixels[(uint32_t)y * output_width + x] =
                    ((uint32_t)alpha << 24) | rgb;
            }
        }
        if ((y & 3U) == 0) bootsplash_pulse();
    }
    kfree(file);
    return pixels;
}

#ifndef BK_ICONPAK_CENTRAL_LOADER
#define BK_ICONPAK_CENTRAL_LOADER 1

static void *bk_iconpak_data = NULL;
static uint32_t bk_iconpak_size = 0;

static uint32_t bk_iconpak_rd32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool bk_iconpak_load_once(void) {
    if (bk_iconpak_data) return true;

    bootsplash_show("LOADING ICON CACHE", 78);
    bootsplash_pulse();

    if (vfs_read_all("/ICONS/ICONS.PAK", &bk_iconpak_data, &bk_iconpak_size)) {
        bootsplash_pulse();
        return true;
    }

    bootsplash_pulse();
    if (vfs_read_all("/ICONS.PAK", &bk_iconpak_data, &bk_iconpak_size)) {
        bootsplash_pulse();
        return true;
    }

    return false;
}

static bool bk_iconpak_make_name_from_bmp_path(const char *path, char out[16]) {
    const char *base;
    uint32_t i = 0;

    if (!path || !out) return false;

    base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    while (base[i] && base[i] != '.' && i < 15) {
        char c = base[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i++] = c;
    }

    out[i] = '\0';
    return i > 0;
}

static bool bk_iconpak_name16_eq(const uint8_t *name16, const char *name) {
    uint32_t i = 0;

    if (!name16 || !name) return false;

    while (i < 16 && name[i]) {
        if ((char)name16[i] != name[i]) return false;
        i++;
    }

    return i < 16 && name16[i] == '\0';
}

static bool bk_iconpak_path_is_icon_bmp(const char *path) {
    const char *dot = NULL;
    const char *base;

    if (!path) return false;

    base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
        if (*p == '.') dot = p;
    }

    if (!dot) return false;

    return (dot[0] == '.' &&
           (dot[1] == 'B' || dot[1] == 'b') &&
           (dot[2] == 'M' || dot[2] == 'm') &&
           (dot[3] == 'P' || dot[3] == 'p') &&
            dot[4] == '\0' &&
            base[0] != '\0');
}

static uint32_t *bk_iconpak_load_bmp_path(const char *path,
                                          int out_w,
                                          int out_h) {
    char wanted[16];
    uint8_t *data;
    uint32_t count;

    if (!bk_iconpak_path_is_icon_bmp(path)) return NULL;
    if (out_w <= 0 || out_h <= 0) return NULL;
    if (!bk_iconpak_make_name_from_bmp_path(path, wanted)) return NULL;
    if (!bk_iconpak_load_once()) return NULL;

    data = (uint8_t *)bk_iconpak_data;

    if (!data || bk_iconpak_size < 12) return NULL;
    if (data[0] != 'B' || data[1] != 'K' ||
        data[2] != 'I' || data[3] != 'P') return NULL;
    if (bk_iconpak_rd32(data + 4) != 1) return NULL;

    count = bk_iconpak_rd32(data + 8);
    if (12U + count * 32U > bk_iconpak_size) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        if ((i & 3U) == 0) bootsplash_pulse();
        uint8_t *e = data + 12U + i * 32U;
        uint32_t w = bk_iconpak_rd32(e + 16);
        uint32_t h = bk_iconpak_rd32(e + 20);
        uint32_t off = bk_iconpak_rd32(e + 24);
        uint32_t size = bk_iconpak_rd32(e + 28);
        uint32_t need;
        uint32_t *src;
        uint32_t *out;

        if (!bk_iconpak_name16_eq(e, wanted)) continue;
        if (!w || !h) return NULL;

        need = w * h * sizeof(uint32_t);
        if (size < need) return NULL;
        if (off > bk_iconpak_size || off + need > bk_iconpak_size)
            return NULL;

        src = (uint32_t *)(data + off);
        out = (uint32_t *)kmalloc((uint32_t)out_w *
                                  (uint32_t)out_h *
                                  sizeof(uint32_t));
        if (!out) return NULL;

        for (int y = 0; y < out_h; y++) {
            uint32_t sy = ((uint32_t)y * h) / (uint32_t)out_h;
            if ((y & 3) == 0) bootsplash_pulse();
            for (int x = 0; x < out_w; x++) {
                uint32_t sx = ((uint32_t)x * w) / (uint32_t)out_w;
                out[y * out_w + x] = src[sy * w + sx];
            }
        }

        return out;
    }

    return NULL;
}

#endif /* BK_ICONPAK_CENTRAL_LOADER */


static void bk_iconpak_preload_on_gui_start(void) {
    /*
     * Precarga temprana. Si VFS todavía no está listo, falla barato;
     * la primera carga real volverá a intentar.
     */
    bootsplash_debug("ICONPAK preload begin");
    (void)bk_iconpak_load_once();
    bootsplash_debug("ICONPAK preload end");
}



uint32_t *program_load_bmp_icon_scaled(const char *path,
                                       uint16_t output_width,
                                       uint16_t output_height) {
    uint32_t *bk_pak_icon = bk_iconpak_load_bmp_path(path, (int)output_width, (int)output_height);
    if (bk_pak_icon) return bk_pak_icon;

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
            if (!gui_gfx_point_visible(surface, dx, dy)) continue;
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

static bool desk_cdrom_available(void) {
    /*
     * Esta funcion corre desde el repintado del escritorio. No debe emitir
     * comandos ATAPI ni tratar de montar una unidad vacia: en el PIT de
     * 300 Hz el primer sondeo ocurre cerca de los 3,33 segundos y bloqueaba
     * la GUI en algunas unidades reales.
     */
    return vfs_has_cdrom();
}

static bool desk_usb_available(void) {
    const char *mount = vfs_get_mount_name();

    if (mount && kstrcmp(mount, "usb0") == 0) return false;
    return block_get("usb0") != NULL;
}

static bool desk_floppy_available(void) {
    const char *mount = vfs_get_mount_name();

    if (!block_get("fd0")) return false;
    return mount && kstrcmp(mount, "fd0") == 0;
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
    if (open_fn == desk_open_files_app) icon_path = "/ICONS/FILES.BMP";
    else if (open_fn == desk_open_shell_app) icon_path = "/ICONS/SHELL.BMP";
    else if (open_fn == desk_open_editor_app) icon_path = "/ICONS/EDITOR.BMP";
    else if (open_fn == desk_open_calculator_app) icon_path = "/ICONS/CALC.BMP";
    else if (open_fn == desk_open_processmanager_app) icon_path = "/ICONS/PROCESOS.BMP";
    else if (open_fn == desk_open_midamp_app) icon_path = "/ICONS/MIDAMP.BMP";
    else if (open_fn == desk_open_viewer_app) icon_path = "/ICONS/IMAGE.BMP";
    else if (open_fn == desk_open_control_panel) icon_path = "/ICONS/CONFIG.BMP";
    else if (open_fn == desk_open_cdrom) icon_path = "/ICONS/CDROM.BMP";
    else if (open_fn == desk_open_floppy) icon_path = "/ICONS/FLOPPY.BMP";
    else if (open_fn == desk_open_usb) icon_path = "/ICONS/USB.BMP";
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

    now = bk_sys_ticks();
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
                      desk_open_cdrom);
        drive_y += 100;
    }
    if (show_usb && st->icon_count < DESK_MAX_ICONS) {
        desk_add_icon(st, drive_x, drive_y, "USB",
                      desk_open_usb);
        drive_y += 100;
    }
    if (show_floppy && st->icon_count < DESK_MAX_ICONS) {
        desk_add_icon(st, drive_x, drive_y, "Disquete",
                      desk_open_floppy);
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

static gui_rect_t desk_icon_rect(const desk_icon_t *icon) {
    if (!icon) return (gui_rect_t){0, 0, 0, 0};
    return (gui_rect_t){icon->x - 2, icon->y - 2,
                        DESK_ICON_W + 4, DESK_ICON_H + 4};
}

enum {
    DESK_CONTEXT_OPEN = 1,
    DESK_CONTEXT_FILES,
    DESK_CONTEXT_NEW_FOLDER,
    DESK_CONTEXT_NEW_TEXT,
    DESK_CONTEXT_CONTROL,
    DESK_CONTEXT_DISPLAY,
    DESK_CONTEXT_REFRESH,
};

static void desk_context_callback(gui_window_t *window UNUSED,
                                  uint32_t item_id, void *context) {
    deskmanager_state_t *st = (deskmanager_state_t *)context;
    gui_desktop_t *desktop = g_desk_desktop;
    char path[VFS_MAX_PATH];
    if (!st || !desktop) return;
    if (item_id == DESK_CONTEXT_OPEN && st->context_icon >= 0 &&
        st->context_icon < st->icon_count) {
        if (st->icons[st->context_icon].open)
            st->icons[st->context_icon].open(desktop);
    } else if (item_id == DESK_CONTEXT_FILES) {
        desk_open_files_app(desktop);
    } else if (item_id == DESK_CONTEXT_NEW_FOLDER) {
        for (int n = 1; n < 100; n++) {
            snprintf(path, sizeof(path), "/Nueva carpeta %u", (uint32_t)n);
            if (vfs_mkdir(path)) break;
        }
    } else if (item_id == DESK_CONTEXT_NEW_TEXT) {
        for (int n = 1; n < 100; n++) {
            snprintf(path, sizeof(path), "/Nuevo documento %u.txt", (uint32_t)n);
            if (vfs_write_all(path, NULL, 0)) {
                (void)program_execute_path_arg(desktop,
                    "/SYSTEM/PROGRAMS/TEXTEDITOR.O", path);
                break;
            }
        }
    } else if (item_id == DESK_CONTEXT_CONTROL) {
        desk_open_control_panel(desktop);
    } else if (item_id == DESK_CONTEXT_DISPLAY) {
        (void)program_execute_path(desktop, "/SYSTEM/CONTROL/DISPLAY.CPL");
    } else if (item_id == DESK_CONTEXT_REFRESH) {
        desk_sync_drive_icons(st, desktop, true);
    }
    gui_desktop_invalidate_all(desktop);
}

static void desk_open_context(deskmanager_state_t *st, gui_desktop_t *desktop,
                              int x, int y, int icon) {
    gui_rect_t limits;
    if (!st || !desktop) return;
    st->context_icon = icon;
    gui_context_menu_clear(&st->context_menu);
    if (icon >= 0)
        (void)gui_context_menu_add_item(&st->context_menu,
            DESK_CONTEXT_OPEN, "Abrir", true, desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_FILES, "Abrir Archivos", true, desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_NEW_FOLDER, "Nueva carpeta", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_NEW_TEXT, "Nuevo documento", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_CONTROL, "Panel de control", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_DISPLAY, "Propiedades de pantalla", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_REFRESH, "Actualizar", true, desk_context_callback, st);
    limits = (gui_rect_t){0, 0, desktop->surface.width,
                          desktop->surface.height - 24};
    gui_context_menu_open(&st->context_menu, x, y, limits);
    gui_desktop_invalidate_all(desktop);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Pintar ventanas (igual que antes)
 * ────────────────────────────────────────────────────────────────────────── */

static void deskmanager_paint_windows(gui_desktop_t *desktop,
                                       gui_surface_t *surface,
                                       gui_rect_t screen) {
    gui_rect_t win_clip;
    gui_rect_t content_clip;
    gui_rect_t saved_clip;

    task_preempt_disable();
    gui_window_t *window = desktop->first_window;
    while (window) {
        bool intersects = window->visible &&
                          gui_rect_intersect(window->bounds, screen,
                                             &win_clip);
        if (intersects)
            gui_window_paint(surface, window, screen);
        if (intersects && window->content_paint) {
            if (window->content_pid) {
                bool dragging = desktop->drag_window == window;
                /* Los callbacks Ring 3 terminan despues de este frame. Se
                 * compone siempre la ultima imagen completa para no alternar
                 * el interior entre fondo vacio y contenido nuevo. */
                gui_window_paint_cached_content(surface, window, screen);
                if (window->dirty && window->content_pending) {
                    if (!dragging) window->content_repaint = true;
                } else if (window->dirty && !dragging) {
                    gui_surface_t *staging = NULL;
                    uint32_t arguments[3] = {
                        (uint32_t)(uintptr_t)window,
                        0U,
                        (uint32_t)(uintptr_t)window->content_context};
                    if (gui_window_begin_content_paint(window, surface,
                            &staging)) {
                        arguments[1] = (uint32_t)(uintptr_t)staging;
                    }
                    if (staging && task_queue_user_upcall(window->content_pid,
                            (uint32_t)(uintptr_t)window->content_paint,
                            arguments, 3, NULL, 0, -2)) {
                        window->content_pending = true;
                        window->content_repaint = false;
                    } else {
                        if (staging)
                            gui_window_end_content_paint(window);
                        window->content_repaint = true;
                    }
                }
                /* Durante un arrastre el cache terminado se traslada junto
                 * con el marco. Pedir además un callback Ring 3 por cada
                 * paquete PS/2 sólo crea una cola de cuadros atrasados. */
            } else {
                saved_clip = gui_gfx_get_clip(surface);
                if (gui_rect_intersect(gui_window_content_rect(window),
                                       saved_clip, &content_clip)) {
                    gui_gfx_set_clip(surface, content_clip);
                    window->content_paint(window, surface,
                                          window->content_context);
                }
                gui_gfx_set_clip(surface, saved_clip);
            }
        }
        if (intersects)
            gui_window_paint_widgets(surface, window, screen);
        if (intersects)
            gui_window_paint_menus(surface, window);
        window = window->next;
    }
    task_preempt_enable();
}

static bool deskmanager_dispatch_widgets(gui_window_t *window,
                                          const gui_event_t *event) {
    gui_widget_t *widget;
    bool handled = false;
    bool expanded_dropdown = false;

    if (!window) return false;
    widget = window->widgets;
    while (widget) {
        if (gui_widget_is_dropdown_expanded(widget)) {
            expanded_dropdown = true;
            break;
        }
        widget = widget->next;
    }

    widget = window->widgets;
    while (widget) {
        if (expanded_dropdown &&
            widget->style != GUI_WIDGET_STYLE_DROPDOWN) {
            widget = widget->next;
            continue;
        }
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
    if (!window->resizable) return 0;
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
    if (st) gui_context_menu_paint(surface, &st->context_menu);
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

    if (st && st->context_menu.open &&
        (event->type == GUI_EVENT_MOUSE_DOWN ||
         event->type == GUI_EVENT_MOUSE_UP)) {
        bool menu_handled = gui_context_menu_handle_event(&st->context_menu,
                                                           NULL, event);
        gui_desktop_invalidate_all(desktop);
        if (menu_handled) return true;
    }

    /* ── Primero probar si el clic cae sobre una ventana existente ── */
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        hit = gui_desktop_window_at(desktop, event->x, event->y);
        if (hit) {
            gui_desktop_raise_window(desktop, hit);
            gui_desktop_focus_window(desktop, hit);
            if (!left_click) {
                handled = gui_window_dispatch_event(hit, event) || handled;
                return handled || right_click;
            }
            if (gui_window_handle_menu_event(hit, event)) {
                hit->dirty = true;
                return true;
            }
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
                    gui_desktop_invalidate_rect(desktop,
                                                desk_icon_rect(&st->icons[idx]));
                    return true;
                }
                if (right_click) {
                    desk_open_context(st, desktop, event->x, event->y, idx);
                    return true;
                }
                return false;
            }
            if (!right_click) return false;
            desk_open_context(st, desktop, event->x, event->y, -1);
            return true;
        }
        return false;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        if (desktop->focused_window) {
            if (gui_window_handle_menu_event(desktop->focused_window, event)) {
                desktop->focused_window->dirty = true;
                return true;
            }
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
                gui_desktop_invalidate_rect(desktop, desk_icon_rect(ic));

                gui_rect_t r = {ic->x - 2, ic->y - 2,
                                DESK_ICON_W + 4, DESK_ICON_H + 4};
                if (!gui_rect_contains(r, event->x, event->y)) continue;

                uint32_t now   = bk_sys_ticks();
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
                bool hovered = gui_rect_contains(r, event->x, event->y);
                if (st->icons[i].hovered != hovered) {
                    st->icons[i].hovered = hovered;
                    gui_desktop_invalidate_rect(desktop,
                                                desk_icon_rect(&st->icons[i]));
                    handled = true;
                }
            }
        }
        return handled;
    }

    if ((event->type == GUI_EVENT_KEY ||
         event->type == GUI_EVENT_MOUSE_WHEEL) && desktop->focused_window) {
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
    bootsplash_show("LOADING DESKTOP ICONS", 78);
    bk_iconpak_preload_on_gui_start();
    bootsplash_pulse();

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
    bootsplash_pulse();
    bootsplash_debug("DESKMANAGER read desktop config");
    if (bk_user_config_read_all(BK_DESKTOP_CONFIG_PATH,
                                BK_DESKTOP_CONFIG_LEGACY_PATH,
                                &config, &config_size) && config) {
        bootsplash_debug("DESKMANAGER desktop config loaded");
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
                    open_fn = desk_open_files_app;
                } else if (kstrcmp(line, "shell") == 0) {
                    open_fn = desk_open_shell_app;
                } else if (kstrcmp(line, "editor") == 0) {
                    open_fn = desk_open_editor_app;
                } else if (kstrcmp(line, "calculator") == 0) {
                    open_fn = desk_open_calculator_app;
                } else if (kstrcmp(line, "processes") == 0) {
                    open_fn = desk_open_processmanager_app;
                } else if (kstrcmp(line, "midamp") == 0) {
                    open_fn = desk_open_midamp_app;
                } else if (kstrcmp(line, "viewer") == 0) {
                    open_fn = desk_open_viewer_app;
                } else if (kstrcmp(line, "games") == 0) {
                    open_fn = desk_open_games_app;
                } else if (kstrcmp(line, "control") == 0 ||
                           kstrcmp(line, "settings") == 0) {
                    open_fn = desk_open_control_panel;
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
