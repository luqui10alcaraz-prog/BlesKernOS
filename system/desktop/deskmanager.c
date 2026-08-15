#include "programs.h"
#include "kernel/include/memory.h"   // Para kzalloc, kfree, kstrncpy
#include "kernel/include/api.h"
#include "kernel/include/task.h"
#include "kernel/include/pit.h"
#include "kernel/include/vfs.h"
#include "kernel/include/block.h"
#include "kernel/include/mouse.h"
#include "kernel/include/iso9660.h"
#include "kernel/include/bootsplash.h"
#include "kernel/include/compat_mode.h"
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
#define DESK_DBLCLICK_MS     500U
#define DESK_MAX_ICONS      16
#define RESIZE_MARGIN         5
#define RESIZE_LEFT           0x01
#define RESIZE_RIGHT          0x02
#define RESIZE_TOP            0x04
#define RESIZE_BOTTOM         0x08

static uint32_t desk_ticks_from_ms(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks;
    if (!hz) hz = 100U;
    ticks = (hz * milliseconds + 999U) / 1000U;
    return ticks ? ticks : 1U;
}

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
    bool        setup_only;
} deskmanager_state_t;

#define DESK_DEFAULT_BACKGROUND 0x00204070U
#define DESK_DEFAULT_WALLPAPER  "/SYSTEM/WALLPAPERS/CLASSIC.BMP"
#define DESK_BMP_MAX_DIMENSION   4096U
#define DESK_BMP_MAX_PIXELS      (16U * 1024U * 1024U)
static uint32_t g_desktop_background = DESK_DEFAULT_BACKGROUND;
static uint32_t *g_desktop_wallpaper;
static uint16_t g_wallpaper_w;
static uint16_t g_wallpaper_h;
static bool g_wallpaper_tiled_source;
static uint8_t g_wallpaper_mode = DESK_WALLPAPER_TILE;
static gui_desktop_t *g_desk_desktop;
static deskmanager_state_t *g_desk_state;
static char g_wallpaper_path[VFS_MAX_PATH];
static uint16_t desk_le16(const uint8_t *p);
static uint32_t desk_le32(const uint8_t *p);

static void desk_wallpaper_release(void) {
    if (g_desktop_wallpaper) kfree(g_desktop_wallpaper);
    g_desktop_wallpaper = NULL;
    g_wallpaper_w = 0U;
    g_wallpaper_h = 0U;
    g_wallpaper_tiled_source = false;
}

/* Compone una sola vez el fondo completo. El repintado posterior se limita a
 * copiar las filas del dirty rect; no vuelve a escalar, mezclar alfa ni
 * recorrer el BMP entero al mover una ventana. */
static uint32_t *desk_wallpaper_build_cache(const uint32_t *source,
                                             uint16_t source_w,
                                             uint16_t source_h,
                                             uint16_t screen_w,
                                             uint16_t screen_h,
                                             uint8_t mode) {
    uint32_t count;
    uint32_t *cache;
    uint32_t background;
    int origin_x = 0;
    int origin_y = 0;

    if (!source || !source_w || !source_h || !screen_w || !screen_h)
        return NULL;
    count = (uint32_t)screen_w * (uint32_t)screen_h;
    /* Full-screen caches are useful for scaled/centered artwork.  Keep a
       generous ceiling on normal machines, while the tile mode below retains
       the tiny source image and never reaches this allocation at all. */
    if (!count || count >
        (mm_physical_top() > 32U * 1024U * 1024U ? 4U * 1024U * 1024U
                                                 : 1024U * 1024U))
        return NULL;
    cache = (uint32_t *)kmalloc((size_t)count * sizeof(uint32_t));
    if (!cache) return NULL;
    background = g_desktop_background ? g_desktop_background
                                      : DESK_DEFAULT_BACKGROUND;
    for (uint32_t i = 0U; i < count; i++) cache[i] = background;

    if (mode == DESK_WALLPAPER_TILE) {
        for (uint16_t y = 0U; y < screen_h; y++) {
            const uint32_t *src_row = source +
                (uint32_t)(y % source_h) * source_w;
            uint32_t *dst_row = cache + (uint32_t)y * screen_w;
            for (uint16_t x = 0U; x < screen_w; x++)
                dst_row[x] = src_row[x % source_w] & 0x00FFFFFFU;
        }
        return cache;
    }

    if (mode != DESK_WALLPAPER_STRETCH) {
        origin_x = ((int)screen_w - (int)source_w) / 2;
        origin_y = ((int)screen_h - (int)source_h) / 2;
    }
    for (uint16_t sy = 0U; sy < source_h; sy++) {
        int dy = origin_y + (int)sy;
        int sx_first = 0;
        int sx_last = source_w;
        if (dy < 0 || dy >= screen_h) continue;
        if (origin_x < 0) sx_first = -origin_x;
        if (origin_x + sx_last > screen_w) sx_last = screen_w - origin_x;
        if (sx_first < 0) sx_first = 0;
        if (sx_last > source_w) sx_last = source_w;
        if (sx_first >= sx_last) continue;
        for (int sx = sx_first; sx < sx_last; sx++)
            cache[(uint32_t)dy * screen_w + (uint32_t)(origin_x + sx)] =
                source[(uint32_t)sy * source_w + (uint32_t)sx] & 0x00FFFFFFU;
    }
    return cache;
}

static void desk_wallpaper_copy_dirty(gui_surface_t *surface) {
    gui_rect_t visible;
    gui_rect_t screen;
    if (!surface || !surface->pixels || !g_desktop_wallpaper ||
        (!g_wallpaper_tiled_source &&
         (g_wallpaper_w != surface->width || g_wallpaper_h != surface->height)))
        return;
    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    if (!gui_rect_intersect(screen, gui_gfx_get_clip(surface), &visible))
        return;
    if (g_wallpaper_tiled_source) {
        for (int y = visible.y; y < visible.y + visible.h; y++) {
            uint32_t *dst = &surface->pixels[(uint32_t)y * surface->pitch +
                                             (uint32_t)visible.x];
            const uint32_t *src = g_desktop_wallpaper +
                (uint32_t)(y % g_wallpaper_h) * g_wallpaper_w;
            for (int x = 0; x < visible.w; x++)
                dst[x] = src[(visible.x + x) % g_wallpaper_w] & 0x00FFFFFFU;
        }
        return;
    }
    for (int y = visible.y; y < visible.y + visible.h; y++) {
        kmemcpy(&surface->pixels[(uint32_t)y * surface->pitch +
                                 (uint32_t)visible.x],
                &g_desktop_wallpaper[(uint32_t)y * g_wallpaper_w +
                                     (uint32_t)visible.x],
                (size_t)visible.w * sizeof(uint32_t));
    }
}

void deskmanager_set_background(uint32_t color) {
    g_desktop_background = color & 0x00FFFFFF;
    /* Negro no forma parte de la paleta del panel. Un cero puede llegar al
       iniciar el CPL antes de sincronizar su estado: conservar el fondo
       clasico en vez de dejar el escritorio completamente negro. */
    if (!g_desktop_background)
        g_desktop_background = DESK_DEFAULT_BACKGROUND;
    desk_wallpaper_release();
    g_wallpaper_path[0] = '\0';
}

uint32_t deskmanager_get_background(void) {
    return g_desktop_background;
}

const char *deskmanager_get_wallpaper_path(void) {
    return g_wallpaper_path;
}

uint8_t deskmanager_get_wallpaper_mode(void) {
    return g_wallpaper_mode;
}

static bool desk_wallpaper_dimensions(const char *path, uint16_t *width,
                                      uint16_t *height) {
    void *file = NULL;
    uint32_t size = 0;
    uint8_t *data;
    int32_t w, raw_h, h;
    uint16_t planes, bpp;
    uint32_t dib_size, compression;

    if (!path || !width || !height) return false;
    if (!vfs_read_all(path, &file, &size) || !file) {
        kprintf("[WALLPAPER] no se pudo leer %s (%s)\n", path,
                vfs_last_error_text());
        return false;
    }
    if (size < 54U) {
        kprintf("[WALLPAPER] BMP demasiado corto: %s size=%u\n", path, size);
        kfree(file);
        return false;
    }

    data = (uint8_t *)file;
    dib_size = desk_le32(data + 14);
    w = (int32_t)desk_le32(data + 18);
    raw_h = (int32_t)desk_le32(data + 22);
    planes = desk_le16(data + 26);
    bpp = desk_le16(data + 28);
    compression = desk_le32(data + 30);
    if (raw_h == (int32_t)0x80000000U) raw_h = 0;
    h = raw_h < 0 ? -raw_h : raw_h;

    if (data[0] != 'B' || data[1] != 'M' || dib_size < 40U ||
        planes != 1U || w <= 0 || h <= 0 ||
        (uint32_t)w > DESK_BMP_MAX_DIMENSION ||
        (uint32_t)h > DESK_BMP_MAX_DIMENSION ||
        (uint32_t)w > DESK_BMP_MAX_PIXELS / (uint32_t)h ||
        !(((bpp == 24U || bpp == 32U) && compression == 0U) ||
          ((bpp == 1U || bpp == 4U || bpp == 8U) && compression == 0U) ||
          (bpp == 8U && compression == 1U)) ||
        (compression == 1U && raw_h < 0)) {
        kprintf("[WALLPAPER] BMP no compatible: %s %dx%d bpp=%u comp=%u\n",
                path, w, h, bpp, compression);
        kfree(file);
        return false;
    }
    *width = (uint16_t)w;
    *height = (uint16_t)h;
    kfree(file);
    return true;
}

void deskmanager_set_wallpaper_mode(uint8_t mode) {
    char path[VFS_MAX_PATH];
    if (mode > DESK_WALLPAPER_FILL) mode = DESK_WALLPAPER_STRETCH;
    if (g_wallpaper_mode == mode) return;
    g_wallpaper_mode = mode;
    if (!g_wallpaper_path[0]) return;
    kstrncpy(path, g_wallpaper_path, sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    (void)deskmanager_set_wallpaper(path);
}

bool deskmanager_set_wallpaper(const char *path) {
    if (!compat_mode_allow_wallpaper()) {
        desk_wallpaper_release();
        g_wallpaper_path[0] = '\0';
        return false;
    }
    uint32_t *pixels;
    uint32_t *cache;
    uint16_t source_w, source_h, output_w, output_h;
    uint16_t screen_w, screen_h;
    if (!g_desk_desktop || !path) return false;
    screen_w = g_desk_desktop->surface.width;
    screen_h = g_desk_desktop->surface.height;
    output_w = screen_w;
    output_h = screen_h;
    if (g_wallpaper_mode != DESK_WALLPAPER_STRETCH) {
        if (!desk_wallpaper_dimensions(path, &source_w, &source_h)) return false;
        output_w = source_w;
        output_h = source_h;
        if (g_wallpaper_mode == DESK_WALLPAPER_FIT ||
            g_wallpaper_mode == DESK_WALLPAPER_FILL) {
            bool use_width = g_wallpaper_mode == DESK_WALLPAPER_FIT
                ? (uint32_t)screen_w * source_h <= (uint32_t)screen_h * source_w
                : (uint32_t)screen_w * source_h >= (uint32_t)screen_h * source_w;
            if (use_width) {
                output_w = screen_w;
                output_h = (uint16_t)(((uint32_t)source_h * screen_w) / source_w);
            } else {
                output_h = screen_h;
                output_w = (uint16_t)(((uint32_t)source_w * screen_h) / source_h);
            }
            if (!output_w) output_w = 1U;
            if (!output_h) output_h = 1U;
        }
    }
    pixels = program_load_bmp_wallpaper_scaled(path, output_w, output_h);
    if (!pixels) return false;
    if (g_wallpaper_mode == DESK_WALLPAPER_TILE &&
        (uint32_t)output_w * output_h <= 65536U) {
        /* El BMP CLASSIC ocupa apenas 10 KiB decodificado. Repetirlo por el
           rectángulo sucio evita un cache de varios MiB y mantiene el mosaico
           al pasar a resoluciones mayores que el limite del cache completo. */
        cache = pixels;
    } else {
        cache = desk_wallpaper_build_cache(pixels, output_w, output_h,
                                           screen_w, screen_h,
                                           g_wallpaper_mode);
        kfree(pixels);
    }
    if (!cache) return false;
    /* El cache queda instalado globalmente aunque cierre DISPLAY.CPL. */
    if (!mm_set_allocation_owner(cache, 0U)) {
        kfree(cache);
        return false;
    }
    desk_wallpaper_release();
    g_desktop_wallpaper = cache;
    g_wallpaper_tiled_source = g_wallpaper_mode == DESK_WALLPAPER_TILE &&
                              cache == pixels;
    g_wallpaper_w = g_wallpaper_tiled_source ? output_w : screen_w;
    g_wallpaper_h = g_wallpaper_tiled_source ? output_h : screen_h;
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
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/FILE.BEX");
}

static void desk_open_shell_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/SHELL.BEX");
}

static void desk_open_editor_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/TEXTEDITOR.BEX");
}

static void desk_open_netsurf_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/NETSURF.BEX");
}

static void desk_open_calculator_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/CALCULATOR.BEX");
}

static void desk_open_processmanager_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/PROCESSMANAGER.BEX");
}

static void desk_open_performance_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop,
        "/SYSTEM/PROGRAMS/PERFORMANCE_MONITOR.BEX");
}

static void desk_open_midamp_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/MIDAMP.BEX");
}

static void desk_open_viewer_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/IMAGEVIEWER.BEX");
}

static void desk_open_games_app(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/PROGRAMS/GAMES.BEX");
}

static void desk_open_control_panel(gui_desktop_t *desktop) {
    (void)program_execute_path(desktop, "/SYSTEM/CONTROL/CONTROL.BEX");
}

static void desk_open_cdrom(gui_desktop_t *desktop) {
    /* El acceso ATAPI puede ser lento: solo se intenta por accion del usuario. */
    if (vfs_has_cdrom() || iso9660_mount_default())
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.BEX",
                                       "/CDROM");
}

static void desk_open_floppy(gui_desktop_t *desktop) {
    if (vfs_mount("fd0"))
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.BEX",
                                       "/");
}

static void desk_open_usb(gui_desktop_t *desktop) {
    if (vfs_mount("usb0"))
        (void)program_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/FILE.BEX",
                                       "/");
}

static uint16_t desk_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t desk_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* BI_RLE8 (compression=1) stores runs in bottom-up scanline order.
 * Decode once to an indexed, top-down buffer so the scaling loop remains
 * simple and does not need to interpret the variable-length stream per pixel. */
static bool desk_bmp_decode_rle8(const uint8_t *data, uint32_t size,
                                 uint32_t offset, uint32_t image_size,
                                 uint16_t width, uint16_t height,
                                 uint8_t *indices) {
    uint32_t pos = offset;
    uint32_t limit = size;
    uint32_t x = 0U;
    int32_t y = (int32_t)height - 1;

    if (!data || !indices || !width || !height || offset >= size) return false;
    if (image_size && image_size <= size - offset) limit = offset + image_size;
    kmemset(indices, 0, (size_t)width * height);

    while (pos < limit && y >= 0) {
        uint8_t count = data[pos++];
        uint8_t value;
        if (pos >= limit) return false;
        value = data[pos++];

        if (count != 0U) {
            if (x + count > width) return false;
            kmemset(indices + (uint32_t)y * width + x, value, count);
            x += count;
            continue;
        }

        if (value == 0U) {              /* end of line */
            x = 0U;
            y--;
        } else if (value == 1U) {       /* end of bitmap */
            return true;
        } else if (value == 2U) {       /* delta */
            uint8_t dx, dy;
            if (pos + 2U > limit) return false;
            dx = data[pos++];
            dy = data[pos++];
            if (x + dx > width || dy > (uint32_t)y) return false;
            x += dx;
            y -= dy;
        } else {                        /* absolute run */
            uint32_t literal = value;
            if (x + literal > width || pos + literal > limit) return false;
            kmemcpy(indices + (uint32_t)y * width + x, data + pos, literal);
            pos += literal;
            x += literal;
            if (literal & 1U) {
                if (pos >= limit) return false;
                pos++;                  /* word alignment padding */
            }
        }
    }
    /* Some encoders finish the final row with EOL and omit EOB. */
    return y < 0;
}

static uint32_t desk_bmp_palette_rgb(const uint8_t *palette,
                                     uint16_t palette_count,
                                     uint8_t index) {
    const uint8_t *entry;
    if (!palette || index >= palette_count) return 0U;
    entry = palette + (uint32_t)index * 4U;
    return ((uint32_t)entry[2] << 16) |
           ((uint32_t)entry[1] << 8) | entry[0];
}

/* Read one palette index from a BI_RGB row.  BMP stores 4-bpp pixels high
 * nibble first and 1-bpp pixels most-significant bit first. */
static uint8_t desk_bmp_index_at(const uint8_t *row, uint16_t bpp,
                                 uint32_t x) {
    if (!row) return 0U;
    if (bpp == 8U) return row[x];
    if (bpp == 4U) {
        uint8_t packed = row[x >> 1];
        return (x & 1U) ? (uint8_t)(packed & 0x0FU)
                        : (uint8_t)(packed >> 4);
    }
    if (bpp == 1U)
        return (uint8_t)((row[x >> 3] >> (7U - (x & 7U))) & 1U);
    return 0U;
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
    uint8_t *data;
    uint32_t offset, dib_size, compression, image_size;
    uint32_t stride = 0U;
    uint32_t palette_offset = 0U;
    uint32_t colors_used = 0U;
    uint16_t palette_count = 0U;
    const uint8_t *palette = NULL;
    uint8_t *rle_indices = NULL;
    int32_t width, raw_height, height;
    uint16_t planes, bpp;
    uint32_t pixel_count;
    uint32_t *pixels = NULL;
    uint16_t *source_x = NULL;
    uint32_t key_rgb = 0U;
    bool exact_key;

    if (!path || output_width == 0U || output_height == 0U) return NULL;
    if (!vfs_read_all(path, &file, &size) || !file || size < 54U) {
        if (!transparent)
            kprintf("[WALLPAPER] no se pudo leer %s (%s)\n", path,
                    vfs_last_error_text());
        return NULL;
    }
    data = (uint8_t *)file;
    offset = desk_le32(data + 10);
    dib_size = desk_le32(data + 14);
    width = (int32_t)desk_le32(data + 18);
    raw_height = (int32_t)desk_le32(data + 22);
    planes = desk_le16(data + 26);
    bpp = desk_le16(data + 28);
    compression = desk_le32(data + 30);
    image_size = desk_le32(data + 34);
    if (raw_height == (int32_t)0x80000000U) raw_height = 0;
    height = raw_height < 0 ? -raw_height : raw_height;

    if (data[0] != 'B' || data[1] != 'M' || dib_size < 40U ||
        planes != 1U || width <= 0 || height <= 0 ||
        (uint32_t)width > DESK_BMP_MAX_DIMENSION ||
        (uint32_t)height > DESK_BMP_MAX_DIMENSION ||
        (uint32_t)width > DESK_BMP_MAX_PIXELS / (uint32_t)height ||
        !(((bpp == 24U || bpp == 32U) && compression == 0U) ||
          ((bpp == 1U || bpp == 4U || bpp == 8U) && compression == 0U) ||
          (bpp == 8U && compression == 1U)) ||
        (compression == 1U && raw_height < 0) || offset >= size) {
        if (!transparent)
            kprintf("[WALLPAPER] BMP no compatible: %s %dx%d bpp=%u comp=%u\n",
                    path, width, height, bpp, compression);
        kfree(file);
        return NULL;
    }

    if (bpp == 1U || bpp == 4U || bpp == 8U) {
        uint32_t palette_limit = 1U << bpp;
        palette_offset = 14U + dib_size;
        colors_used = desk_le32(data + 46);
        if (!colors_used) colors_used = palette_limit;
        if (colors_used > palette_limit || palette_offset > offset ||
            colors_used > (offset - palette_offset) / 4U ||
            palette_offset + colors_used * 4U > size) {
            if (!transparent)
                kprintf("[WALLPAPER] paleta BMP%u invalida: %s colors=%u\n",
                        bpp, path, colors_used);
            kfree(file);
            return NULL;
        }
        palette = data + palette_offset;
        palette_count = (uint16_t)colors_used;
        if (compression == 0U) {
            uint32_t row_bits = (uint32_t)width * bpp;
            stride = ((row_bits + 31U) / 32U) * 4U;
            if (!stride || stride > size - offset ||
                (uint32_t)height > (size - offset) / stride) {
                if (!transparent)
                    kprintf("[WALLPAPER] pixeles BMP%u truncados: %s\n",
                            bpp, path);
                kfree(file);
                return NULL;
            }
        } else {
            uint32_t source_count = (uint32_t)width * (uint32_t)height;
            rle_indices = (uint8_t *)kmalloc(source_count);
            if (!rle_indices ||
                !desk_bmp_decode_rle8(data, size, offset, image_size,
                                      (uint16_t)width, (uint16_t)height,
                                      rle_indices)) {
                if (!transparent)
                    kprintf("[WALLPAPER] flujo BI_RLE8 invalido: %s\n", path);
                if (rle_indices) kfree(rle_indices);
                kfree(file);
                return NULL;
            }
        }
    } else {
        stride = ((uint32_t)width * (uint32_t)(bpp / 8U) + 3U) & ~3U;
        if (stride > size - offset ||
            (uint32_t)height > (size - offset) / stride) {
            if (!transparent)
                kprintf("[WALLPAPER] pixeles BMP truncados: %s\n", path);
            kfree(file);
            return NULL;
        }
    }

    pixel_count = (uint32_t)output_width * output_height;
    if (!pixel_count || pixel_count > DESK_BMP_MAX_PIXELS) {
        if (rle_indices) kfree(rle_indices);
        kfree(file);
        return NULL;
    }
    pixels = (uint32_t *)kmalloc((size_t)pixel_count * sizeof(uint32_t));
    source_x = (uint16_t *)kmalloc((size_t)output_width * sizeof(uint16_t));
    if (!pixels || !source_x) {
        if (source_x) kfree(source_x);
        if (pixels) kfree(pixels);
        if (rle_indices) kfree(rle_indices);
        kfree(file);
        return NULL;
    }
    exact_key = desk_use_exact_key(path);
    for (uint16_t x = 0U; x < output_width; x++)
        source_x[x] = (uint16_t)(((uint32_t)x * (uint32_t)width) /
                                 output_width);

    if (bpp == 1U || bpp == 4U || bpp == 8U) {
        uint8_t key_index;
        if (compression == 1U) {
            key_index = rle_indices[0];
        } else {
            uint32_t key_row = raw_height > 0 ? (uint32_t)height - 1U : 0U;
            key_index = desk_bmp_index_at(data + offset + key_row * stride,
                                          bpp, 0U);
        }
        key_rgb = desk_bmp_palette_rgb(palette, palette_count, key_index);
    } else {
        uint32_t key_row = raw_height > 0 ? (uint32_t)height - 1U : 0U;
        const uint8_t *key_src = data + offset + key_row * stride;
        key_rgb = ((uint32_t)key_src[2] << 16) |
                  ((uint32_t)key_src[1] << 8) | key_src[0];
    }

    for (uint16_t y = 0U; y < output_height; y++) {
        uint32_t top_y = ((uint32_t)y * (uint32_t)height) / output_height;
        uint32_t file_y = raw_height > 0
            ? (uint32_t)height - 1U - top_y : top_y;
        for (uint16_t x = 0U; x < output_width; x++) {
            uint32_t sx = source_x[x];
            uint32_t rgb;
            uint8_t alpha = 0xFFU;

            if (bpp == 1U || bpp == 4U || bpp == 8U) {
                uint8_t index = compression == 1U
                    ? rle_indices[top_y * (uint32_t)width + sx]
                    : desk_bmp_index_at(data + offset + file_y * stride,
                                        bpp, sx);
                rgb = desk_bmp_palette_rgb(palette, palette_count, index);
            } else {
                const uint8_t *src = data + offset + file_y * stride +
                    sx * (uint32_t)(bpp / 8U);
                rgb = ((uint32_t)src[2] << 16) |
                      ((uint32_t)src[1] << 8) | src[0];
                if (bpp == 32U) alpha = src[3];
            }
            if (!transparent) alpha = 0xFFU;
            if ((transparent && alpha == 0U) ||
                (transparent && bpp != 32U &&
                 ((exact_key ? rgb == key_rgb
                             : desk_color_matches_key(rgb, key_rgb)) ||
                  rgb == 0x00FF00FFU))) {
                pixels[(uint32_t)y * output_width + x] = 0U;
            } else {
                pixels[(uint32_t)y * output_width + x] =
                    ((uint32_t)alpha << 24) | rgb;
            }
        }
    }

    kfree(source_x);
    if (rle_indices) kfree(rle_indices);
    if (!transparent && bpp == 8U && compression == 1U)
        kprintf("[WALLPAPER] BI_RLE8 OK: %s %dx%d -> %ux%u\n",
                path, width, height, output_width, output_height);
    else if (!transparent && (bpp == 1U || bpp == 4U || bpp == 8U))
        kprintf("[WALLPAPER] BMP%u OK: %s %dx%d -> %ux%u modo=%u\n",
                bpp, path, width, height, output_width, output_height,
                g_wallpaper_mode);
    bootsplash_pulse();
    kfree(file);
    return pixels;
}

/* ------------------------------------------------------------------------- */
/* BVI1 scalable icon loader.
 *
 * Applications still request their historical /ICONS/NAME.BMP path.  The
 * central icon loader transparently replaces the extension with .BVI and
 * renders the vector rectangles at the requested size.  This keeps every
 * existing caller compatible and leaves ICONS.PAK/BMP as a safe fallback.
 */

#define BK_BVI_MAX_COLORS 256U
#define BK_BVI_PACK_ENTRY_SIZE 24U

static void *bk_bvi_pack_data;
static uint32_t bk_bvi_pack_size;
static bool bk_bvi_pack_attempted;
static bool bk_bvi_pack_table_only;
static char bk_bvi_pack_path[32];

static bool bk_bvi_read_range(uint32_t offset, void *buffer, uint32_t size) {
    int fd;
    int got;
    if (!bk_bvi_pack_path[0] || !buffer || !size || offset > 0x7FFFFFFFU)
        return false;
    fd = vfs_open(bk_bvi_pack_path, VFS_O_RDONLY);
    if (fd < 0) return false;
    if (vfs_seek(fd, (int32_t)offset, 0U) != (int32_t)offset) {
        vfs_close(fd);
        return false;
    }
    got = vfs_read(fd, buffer, size);
    vfs_close(fd);
    return got >= 0 && (uint32_t)got == size;
}

static uint32_t bk_bvi_rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool bk_bvi_make_name(const char *path, char out[16]) {
    const char *base = path;
    uint32_t used = 0U;
    if (!path || !out) return false;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    while (base[used] && base[used] != '.' && used < 15U) {
        char ch = base[used];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        out[used++] = ch;
    }
    out[used] = '\0';
    return used > 0U;
}

static bool bk_bvi_pack_load_once(void) {
    uint8_t *data;
    vfs_dir_entry_t entry;
    if (bk_bvi_pack_attempted) return bk_bvi_pack_data != NULL;
    bk_bvi_pack_attempted = true;
    if (compat_mode_is_low_memory()) {
        uint8_t header[12];
        uint32_t count;
        uint32_t table_size;
        const char *path = NULL;
        if (vfs_stat("/ICONS/ICONS.PAK", &entry) &&
            entry.type == VFS_NODE_FILE) path = "/ICONS/ICONS.PAK";
        else if (vfs_stat("/ICONS.PAK", &entry) &&
                 entry.type == VFS_NODE_FILE) path = "/ICONS.PAK";
        if (!path) return false;
        kstrncpy(bk_bvi_pack_path, path, sizeof(bk_bvi_pack_path) - 1U);
        if (!bk_bvi_read_range(0U, header, sizeof(header)) ||
            header[0] != 'B' || header[1] != 'K' ||
            header[2] != 'V' || header[3] != 'P' ||
            bk_bvi_rd32(header + 4U) != 1U) return false;
        count = bk_bvi_rd32(header + 8U);
        if (count > (entry.size - 12U) / BK_BVI_PACK_ENTRY_SIZE)
            return false;
        table_size = 12U + count * BK_BVI_PACK_ENTRY_SIZE;
        bk_bvi_pack_data = kmalloc(table_size);
        if (!bk_bvi_pack_data ||
            !bk_bvi_read_range(0U, bk_bvi_pack_data, table_size)) {
            if (bk_bvi_pack_data) kfree(bk_bvi_pack_data);
            bk_bvi_pack_data = NULL;
            return false;
        }
        bk_bvi_pack_size = entry.size;
        bk_bvi_pack_table_only = true;
    } else if (!vfs_read_all("/ICONS/ICONS.PAK", &bk_bvi_pack_data,
                             &bk_bvi_pack_size) &&
               !vfs_read_all("/ICONS.PAK", &bk_bvi_pack_data,
                             &bk_bvi_pack_size)) return false;
    data = (uint8_t *)bk_bvi_pack_data;
    if (bk_bvi_pack_size < 12U || data[0] != 'B' || data[1] != 'K' ||
        data[2] != 'V' || data[3] != 'P' || bk_bvi_rd32(data + 4U) != 1U) {
        kfree(bk_bvi_pack_data);
        bk_bvi_pack_data = NULL;
        bk_bvi_pack_size = 0U;
        return false;
    }
    uint32_t count = bk_bvi_rd32(data + 8U);
    if (count > (bk_bvi_pack_size - 12U) / BK_BVI_PACK_ENTRY_SIZE) {
        kfree(bk_bvi_pack_data);
        bk_bvi_pack_data = NULL;
        bk_bvi_pack_size = 0U;
        return false;
    }
    return true;
}

static bool bk_bvi_pack_find(const char *path, const uint8_t **document,
                             uint32_t *document_size, void **owned_document) {
    char wanted[16];
    uint8_t *data;
    uint32_t count;
    if (owned_document) *owned_document = NULL;
    if (!document || !document_size || !bk_bvi_make_name(path, wanted) ||
        !bk_bvi_pack_load_once()) return false;
    data = (uint8_t *)bk_bvi_pack_data;
    count = bk_bvi_rd32(data + 8U);
    for (uint32_t i = 0U; i < count; i++) {
        uint8_t *entry = data + 12U + i * BK_BVI_PACK_ENTRY_SIZE;
        bool equal_name = true;
        for (uint32_t ch = 0U; ch < 16U; ch++) {
            if ((uint8_t)wanted[ch] != entry[ch]) {
                equal_name = false;
                break;
            }
            if (!wanted[ch]) break;
        }
        if (!equal_name) continue;
        uint32_t offset = bk_bvi_rd32(entry + 16U);
        uint32_t size = bk_bvi_rd32(entry + 20U);
        if (offset > bk_bvi_pack_size || size > bk_bvi_pack_size - offset)
            return false;
        if (bk_bvi_pack_table_only) {
            uint8_t *copy = (uint8_t *)kmalloc(size);
            if (!copy || !bk_bvi_read_range(offset, copy, size)) {
                if (copy) kfree(copy);
                return false;
            }
            *document = copy;
            if (owned_document) *owned_document = copy;
        } else {
            *document = data + offset;
        }
        *document_size = size;
        return true;
    }
    return false;
}

static bool bk_bvi_icon_path(const char *path, char *out, uint32_t capacity) {
    const char *dot = NULL;
    uint32_t length = 0U;
    if (!path || !out || capacity < 6U) return false;
    for (const char *p = path; *p; p++) {
        if (*p == '.') dot = p;
        length++;
    }
    if (!dot || length + 1U > capacity) return false;
    if (!((dot[1] == 'B' || dot[1] == 'b') &&
          (dot[2] == 'M' || dot[2] == 'm') &&
          (dot[3] == 'P' || dot[3] == 'p') && dot[4] == '\0'))
        return false;
    uint32_t prefix = (uint32_t)(dot - path);
    if (prefix + 5U > capacity) return false;
    for (uint32_t i = 0U; i < prefix; i++) out[i] = path[i];
    out[prefix + 0U] = '.';
    out[prefix + 1U] = 'B';
    out[prefix + 2U] = 'V';
    out[prefix + 3U] = 'I';
    out[prefix + 4U] = '\0';
    return true;
}

static const uint8_t *bk_bvi_skip_space(const uint8_t *cursor,
                                         const uint8_t *end) {
    while (cursor < end && (*cursor == ' ' || *cursor == '\t' ||
                            *cursor == '\r' || *cursor == '\n')) cursor++;
    return cursor;
}

static const uint8_t *bk_bvi_next_line(const uint8_t *cursor,
                                        const uint8_t *end) {
    while (cursor < end && *cursor != '\n') cursor++;
    return cursor < end ? cursor + 1U : end;
}

static bool bk_bvi_word(const uint8_t *cursor, const uint8_t *end,
                        const char *word, const uint8_t **after) {
    while (*word && cursor < end && *cursor == (uint8_t)*word) {
        cursor++;
        word++;
    }
    if (*word || (cursor < end && *cursor != ' ' && *cursor != '\t' &&
                  *cursor != '\r' && *cursor != '\n')) return false;
    if (after) *after = cursor;
    return true;
}

static bool bk_bvi_uint(const uint8_t **cursor, const uint8_t *end,
                        uint32_t *value) {
    const uint8_t *p = bk_bvi_skip_space(*cursor, end);
    uint32_t result = 0U;
    bool any = false;
    while (p < end && *p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p - '0');
        if (result > 429496729U ||
            (result == 429496729U && digit > 5U)) return false;
        result = result * 10U + digit;
        p++;
        any = true;
    }
    if (!any) return false;
    *cursor = p;
    *value = result;
    return true;
}

static bool bk_bvi_fringe_color(uint32_t pixel) {
    uint32_t red = (pixel >> 16) & 0xFFU;
    uint32_t green = (pixel >> 8) & 0xFFU;
    uint32_t blue = pixel & 0xFFU;
    uint32_t difference = red > blue ? red - blue : blue - red;
    uint32_t magenta = red < blue ? red : blue;
    return (pixel >> 24) != 0U && magenta >= 238U && difference <= 12U &&
           green + 60U < magenta;
}

static void bk_bvi_cleanup_fringe(uint32_t *pixels, uint32_t width,
                                  uint32_t height) {
    if (!pixels || width < 2U || height < 2U) return;
    /* Una sola capa de magenta fuerte limpia el fondo residual sin comerse
       sombras rosadas ni detalles que pertenecen al dibujo. Alfa=1 se usa
       como marca temporal para no propagar el borrado durante el recorrido. */
    for (uint32_t pass = 0U; pass < 1U; pass++) {
        bool changed = false;
        for (uint32_t y = 0U; y < height; y++) {
            for (uint32_t x = 0U; x < width; x++) {
                uint32_t index = y * width + x;
                if (!bk_bvi_fringe_color(pixels[index])) continue;
                bool touches_clear = false;
                uint32_t y0 = y ? y - 1U : y;
                uint32_t y1 = y + 1U < height ? y + 1U : y;
                uint32_t x0 = x ? x - 1U : x;
                uint32_t x1 = x + 1U < width ? x + 1U : x;
                for (uint32_t ny = y0; ny <= y1 && !touches_clear; ny++)
                    for (uint32_t nx = x0; nx <= x1; nx++)
                        if ((pixels[ny * width + nx] >> 24) == 0U) {
                            touches_clear = true;
                            break;
                        }
                if (touches_clear) {
                    pixels[index] = (pixels[index] & 0x00FFFFFFU) | 0x01000000U;
                    changed = true;
                }
            }
        }
        for (uint32_t i = 0U; i < width * height; i++)
            if ((pixels[i] >> 24) == 1U) pixels[i] = 0U;
        if (!changed) break;
    }
}

static uint32_t *bk_bvi_load_bmp_path(const char *path, int out_w, int out_h) {
    char bvi_path[VFS_MAX_PATH];
    void *owned_raw = NULL;
    const uint8_t *raw = NULL;
    uint32_t raw_size = 0U;
    uint32_t colors[BK_BVI_MAX_COLORS];
    uint8_t color_defined[BK_BVI_MAX_COLORS];
    uint32_t canvas_w = 0U, canvas_h = 0U;
    uint32_t *pixels = NULL;
    bool saw_header = false;

    if (out_w <= 0 || out_h <= 0 || out_w > 1024 || out_h > 1024 ||
        !bk_bvi_icon_path(path, bvi_path, sizeof(bvi_path)))
        return NULL;
    if (!bk_bvi_pack_find(path, &raw, &raw_size, &owned_raw)) {
        if (!vfs_read_all(bvi_path, &owned_raw, &raw_size) || !owned_raw)
            return NULL;
        raw = (const uint8_t *)owned_raw;
    }
    if (raw_size < 5U) goto fail;

    kmemset(colors, 0, sizeof(colors));
    kmemset(color_defined, 0, sizeof(color_defined));
    const uint8_t *cursor = raw;
    const uint8_t *end = cursor + raw_size;

    while (cursor < end) {
        const uint8_t *line = bk_bvi_skip_space(cursor, end);
        const uint8_t *args = NULL;
        if (line >= end) break;
        if (*line == '#') {
            cursor = bk_bvi_next_line(line, end);
            continue;
        }
        if (!saw_header) {
            if ((uint32_t)(end - line) < 4U || line[0] != 'B' ||
                line[1] != 'V' || line[2] != 'I' || line[3] != '1')
                goto fail;
            saw_header = true;
            cursor = bk_bvi_next_line(line, end);
            continue;
        }
        if (bk_bvi_word(line, end, "canvas", &args)) {
            if (!bk_bvi_uint(&args, end, &canvas_w) ||
                !bk_bvi_uint(&args, end, &canvas_h) || !canvas_w || !canvas_h ||
                canvas_w > 4096U || canvas_h > 4096U) goto fail;
            if (!pixels) {
                uint32_t count = (uint32_t)out_w * (uint32_t)out_h;
                pixels = (uint32_t *)kmalloc(count * sizeof(uint32_t));
                if (!pixels) goto fail;
                kmemset(pixels, 0, count * sizeof(uint32_t));
            }
        } else if (bk_bvi_word(line, end, "color", &args)) {
            uint32_t id, red, green, blue, alpha;
            if (!bk_bvi_uint(&args, end, &id) ||
                !bk_bvi_uint(&args, end, &red) ||
                !bk_bvi_uint(&args, end, &green) ||
                !bk_bvi_uint(&args, end, &blue) ||
                !bk_bvi_uint(&args, end, &alpha) ||
                id >= BK_BVI_MAX_COLORS || red > 255U || green > 255U ||
                blue > 255U || alpha > 255U) goto fail;
            colors[id] = (alpha << 24) | (red << 16) | (green << 8) | blue;
            color_defined[id] = 1U;
        } else if (bk_bvi_word(line, end, "rect", &args)) {
            uint32_t x, y, width, height, color;
            if (!pixels || !canvas_w || !canvas_h ||
                !bk_bvi_uint(&args, end, &x) ||
                !bk_bvi_uint(&args, end, &y) ||
                !bk_bvi_uint(&args, end, &width) ||
                !bk_bvi_uint(&args, end, &height) ||
                !bk_bvi_uint(&args, end, &color) ||
                color >= BK_BVI_MAX_COLORS || !color_defined[color] ||
                x > canvas_w || y > canvas_h || width > canvas_w - x ||
                height > canvas_h - y) goto fail;
            uint32_t pixel = colors[color];
            if (pixel >> 24) {
                uint32_t x0 = (x * (uint32_t)out_w) / canvas_w;
                uint32_t y0 = (y * (uint32_t)out_h) / canvas_h;
                uint32_t x1 = ((x + width) * (uint32_t)out_w + canvas_w - 1U) /
                              canvas_w;
                uint32_t y1 = ((y + height) * (uint32_t)out_h + canvas_h - 1U) /
                              canvas_h;
                if (x1 > (uint32_t)out_w) x1 = (uint32_t)out_w;
                if (y1 > (uint32_t)out_h) y1 = (uint32_t)out_h;
                for (uint32_t py = y0; py < y1; py++)
                    for (uint32_t px = x0; px < x1; px++)
                        pixels[py * (uint32_t)out_w + px] = pixel;
            }
        }
        cursor = bk_bvi_next_line(line, end);
    }

    if (owned_raw) kfree(owned_raw);
    if (saw_header && pixels && canvas_w && canvas_h) {
        bk_bvi_cleanup_fringe(pixels, (uint32_t)out_w, (uint32_t)out_h);
        return pixels;
    }
    if (pixels) kfree(pixels);
    return NULL;

fail:
    if (pixels) kfree(pixels);
    if (owned_raw) kfree(owned_raw);
    return NULL;
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

    /* El paquete instalado actualmente es BKVP (vectorial). Intentar abrirlo
       tambien como el antiguo BKIP dejaba sus 278 KiB completos residentes
       aun despues de rechazar la firma. */
    if (compat_mode_is_low_memory()) return false;

    bootsplash_show("@H33FF5DFA", 78);
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
    bootsplash_debug("@H6021B258");
    (void)bk_iconpak_load_once();
    bootsplash_debug("@H8D532FD4");
}



uint32_t *program_load_bmp_icon_scaled(const char *path,
                                       uint16_t output_width,
                                       uint16_t output_height) {
    uint32_t *bvi_icon = bk_bvi_load_bmp_path(
        path, (int)output_width, (int)output_height);
    if (bvi_icon) return bvi_icon;

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
    gui_rect_t visible;
    gui_rect_t image;
    int start_x, start_y, end_x, end_y;

    if (!surface || !surface->pixels || !pixels) return;
    image = (gui_rect_t){x, y, width, height};
    if (!gui_rect_intersect(image, gui_gfx_get_clip(surface), &visible))
        return;
    start_x = visible.x - x;
    start_y = visible.y - y;
    end_x = start_x + visible.w;
    end_y = start_y + visible.h;

    /* El escritorio se repinta con un clip pequeño. Antes se recorría el BMP
     * completo (hasta 800x600) para cada movimiento del cursor aunque casi
     * todos los píxeles fueran descartados por gui_gfx_point_visible(). */
    for (int py = start_y; py < end_y; py++) {
        for (int px = start_x; px < end_x; px++) {
            uint32_t color = pixels[(uint32_t)py * width + px];
            uint8_t alpha = (uint8_t)(color >> 24);
            uint32_t rgb = color & 0x00FFFFFF;
            int dx = x + px;
            int dy = y + py;
            uint32_t *dst;

            if (alpha == 0) continue;
            if (alpha == 0xFF) {
                surface->pixels[(uint32_t)dy * surface->pitch +
                                (uint32_t)dx] = rgb;
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
    else if (open_fn == desk_open_netsurf_app) icon_path = "/ICONS/NETSCAPE.BMP";
    else if (open_fn == desk_open_calculator_app) icon_path = "/ICONS/CALC.BMP";
    else if (open_fn == desk_open_processmanager_app) icon_path = "/ICONS/PROCESOS.BMP";
    else if (open_fn == desk_open_performance_app) icon_path = "/ICONS/PROCESOS.BMP";
    else if (open_fn == desk_open_midamp_app) icon_path = "/ICONS/MIDAMP.BMP";
    else if (open_fn == desk_open_viewer_app) icon_path = "/ICONS/IMAGE.BMP";
    else if (open_fn == desk_open_control_panel) icon_path = "/ICONS/CONFIG.BMP";
    else if (open_fn == desk_open_cdrom) icon_path = "/ICONS/CDROM.BMP";
    else if (open_fn == desk_open_floppy) icon_path = "/ICONS/FLOPPY.BMP";
    else if (open_fn == desk_open_usb) icon_path = "/ICONS/USB.BMP";
    ic->pixels = (compat_mode_allow_icon_images() && icon_path)
        ? program_load_bmp_icon_scaled(icon_path,
                                       DESK_ICON_IMG_W, DESK_ICON_IMG_H)
        : NULL;
    kstrncpy(ic->label, label ? label : "", sizeof(ic->label) - 1);
    ic->label[sizeof(ic->label) - 1] = '\0';
}

static bool desk_has_icon_open_fn(const deskmanager_state_t *st,
                                  desk_icon_open_fn open_fn) {
    int i;
    if (!st || !open_fn) return false;
    for (i = 0; i < st->icon_count; i++) {
        if (st->icons[i].open == open_fn) return true;
    }
    return false;
}

static void desk_find_free_default_slot(const deskmanager_state_t *st,
                                        const gui_desktop_t *desktop,
                                        int *x_out, int *y_out) {
    int usable_h;
    int rows;
    int column;
    int row;

    if (!x_out || !y_out) return;
    usable_h = desktop ? desktop->surface.height - 48 : 552;
    rows = usable_h / 100;
    if (rows < 1) rows = 1;
    if (rows > 7) rows = 7;

    for (column = 0; column < 5; column++) {
        for (row = 0; row < rows; row++) {
            int x = 16 + column * 96;
            int y = 16 + row * 100;
            bool occupied = false;
            int i;
            for (i = 0; st && i < st->icon_count; i++) {
                if (st->icons[i].x == x && st->icons[i].y == y) {
                    occupied = true;
                    break;
                }
            }
            if (!occupied) {
                *x_out = x;
                *y_out = y;
                return;
            }
        }
    }
    *x_out = 16;
    *y_out = 16 + (st ? st->icon_count : 0) * 100;
}

static void desk_add_default_icon_if_missing(deskmanager_state_t *st,
                                             gui_desktop_t *desktop,
                                             const char *label,
                                             desk_icon_open_fn open_fn) {
    int x;
    int y;
    if (!st || !open_fn || desk_has_icon_open_fn(st, open_fn)) return;
    desk_find_free_default_slot(st, desktop, &x, &y);
    desk_add_icon(st, x, y, label, open_fn);
}

static void desk_ensure_default_program_icons(deskmanager_state_t *st,
                                              gui_desktop_t *desktop) {
    desk_add_default_icon_if_missing(st, desktop, "Archivos",
                                     desk_open_files_app);
    desk_add_default_icon_if_missing(st, desktop, "Editor de texto",
                                     desk_open_editor_app);
    desk_add_default_icon_if_missing(st, desktop, "NetSurf",
                                     desk_open_netsurf_app);
    desk_add_default_icon_if_missing(st, desktop, "Rendimiento",
                                     desk_open_performance_app);
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
        desk_add_icon(st, drive_x, drive_y, "@H3737CE2B",
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
                    "/SYSTEM/PROGRAMS/TEXTEDITOR.BEX", path);
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
            DESK_CONTEXT_OPEN, "@H5A216C13", true, desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_FILES, "@H3D400190", true, desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_NEW_FOLDER, "@HAAAADA68", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_NEW_TEXT, "@HB9A3132A", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_CONTROL, "@H59EF0B79", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_DISPLAY, "@H898CDA5D", true,
        desk_context_callback, st);
    (void)gui_context_menu_add_item(&st->context_menu,
        DESK_CONTEXT_REFRESH, "@H7CC1E2B3", true, desk_context_callback, st);
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

    /* El dominio GUI se libera temporalmente al cambiar de tarea. Mientras
       recorremos la lista, no permita que CPU0 ejecute un callback Ring 3 que
       pueda cerrar o modificar esas mismas ventanas. Es una barrera local;
       los demás núcleos siguen ejecutando trabajo no GUI. */
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
                    if (staging && task_queue_window_upcall(window,
                            window->content_pid,
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
        /* BLESKERNOS_SVGA3D_WINDOW_SURFACE_BEGIN */
        if (intersects)
            gui_gpu_compositor_capture_window(window, surface);
        /* BLESKERNOS_SVGA3D_WINDOW_SURFACE_END */
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

static gui_cursor_style_t desk_resize_cursor(uint8_t edges) {
    if ((edges & (RESIZE_LEFT | RESIZE_RIGHT)) &&
        (edges & (RESIZE_TOP | RESIZE_BOTTOM)))
        return ((edges & RESIZE_LEFT && edges & RESIZE_TOP) ||
                (edges & RESIZE_RIGHT && edges & RESIZE_BOTTOM))
             ? GUI_CURSOR_SIZE_NWSE : GUI_CURSOR_SIZE_NESW;
    if (edges & (RESIZE_LEFT | RESIZE_RIGHT)) return GUI_CURSOR_SIZE_WE;
    if (edges & (RESIZE_TOP | RESIZE_BOTTOM)) return GUI_CURSOR_SIZE_NS;
    return GUI_CURSOR_ARROW;
}

static gui_rect_t desk_resize_bounds(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window = desktop ? desktop->resize_window : NULL;
    gui_rect_t bounds = desktop ? desktop->resize_start_bounds
                                : (gui_rect_t){0, 0, 0, 0};
    int dx;
    int dy;
    int min_w;
    int min_h;
    if (!desktop || !window) return bounds;
    dx = x - desktop->resize_start_x;
    dy = y - desktop->resize_start_y;

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

    min_w = window->min_w > 0 ? window->min_w : 160;
    min_h = window->min_h > 0 ? window->min_h : 90;
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
    if (bounds.y + bounds.h > desktop->surface.height - 30)
        bounds.h = desktop->surface.height - 30 - bounds.y;
    return bounds;
}

static void desk_resize_window(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window = desktop ? desktop->resize_window : NULL;
    gui_rect_t bounds;
    if (!desktop || !window) return;
    bounds = desk_resize_bounds(desktop, x, y);
    if (desktop->drag_outline_enabled) {
        gui_rect_t old_outline = desktop->drag_outline_bounds;
        if (old_outline.x != bounds.x || old_outline.y != bounds.y ||
            old_outline.w != bounds.w || old_outline.h != bounds.h) {
            desktop->drag_outline_bounds = bounds;
            desktop->drag_outline_visible = true;
            gui_desktop_invalidate_rect(desktop, old_outline);
            gui_desktop_invalidate_rect(desktop, bounds);
        }
        return;
    }
    if (window->bounds.x != bounds.x || window->bounds.y != bounds.y ||
        window->bounds.w != bounds.w || window->bounds.h != bounds.h) {
        gui_rect_t old_bounds = window->bounds;
        window->bounds = bounds;
        window->mode_restore_valid = false;
        window->dirty = true;
        gui_desktop_invalidate_rect(desktop, old_bounds);
        gui_desktop_invalidate_rect(desktop, bounds);
    }
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
    /* BLESKERNOS_SVGA3D_FRAME_BEGIN */
    gui_gpu_compositor_begin_frame(desktop, surface);
    /* BLESKERNOS_SVGA3D_FRAME_END */
    if (st && !st->setup_only) desk_sync_drive_icons(st, desktop, false);

    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    if (g_desktop_wallpaper && (g_wallpaper_tiled_source ||
        (g_wallpaper_w == surface->width && g_wallpaper_h == surface->height))) {
        desk_wallpaper_copy_dirty(surface);
    } else {
        gui_gfx_fill_rect(surface, screen,
            g_desktop_background ? g_desktop_background
                                 : DESK_DEFAULT_BACKGROUND);
    }

    /* Iconos de escritorio */
    if (st && !st->setup_only) {
        for (int i = 0; i < st->icon_count; i++) {
            desk_draw_icon(surface, &st->icons[i]);
        }
    }

    /* BLESKERNOS_SVGA3D_BACKGROUND_BEGIN */
    gui_gpu_compositor_capture_background(surface);
    /* BLESKERNOS_SVGA3D_BACKGROUND_END */
    deskmanager_paint_windows(desktop, surface, screen);
    if (desktop->drag_outline_visible) {
        gui_rect_t outline = desktop->drag_outline_bounds;
        gui_gfx_draw_rect(surface, outline, 0x00000000);
        if (outline.w > 2 && outline.h > 2) {
            outline.x++;
            outline.y++;
            outline.w -= 2;
            outline.h -= 2;
            gui_gfx_draw_rect(surface, outline, 0x00FFFFFF);
        }
    }
    if (st && !st->setup_only)
        gui_context_menu_paint(surface, &st->context_menu);
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Handle events
 * ────────────────────────────────────────────────────────────────────────── */

static gui_window_t *desk_focus_fallback(gui_desktop_t *desktop,
                                         gui_window_t *excluded) {
    gui_window_t *candidate;
    if (!desktop) return NULL;
    candidate = desktop->last_window;
    while (candidate) {
        if (candidate != excluded && candidate->visible && candidate->listed &&
            candidate->input_enabled && candidate->destroy_state == 0U)
            return candidate;
        candidate = candidate->prev;
    }
    return NULL;
}

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

    if (st && !st->setup_only && st->context_menu.open &&
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
            /*
             * Win32 keeps a disabled owner below its active modal child.
             * Raising it here made nested WinZip dialogs cover the license
             * window and left every visible button inert.
             */
            if (!hit->input_enabled) {
                gui_window_t *modal = desktop->last_window;
                while (modal && (!modal->visible || !modal->input_enabled ||
                                 modal->owner_pid != hit->owner_pid))
                    modal = modal->prev;
                if (modal) {
                    gui_desktop_raise_window(desktop, modal);
                    gui_desktop_focus_window(desktop, modal);
                }
                return true;
            }
            gui_desktop_raise_window(desktop, hit);
            gui_desktop_focus_window(desktop, hit);
            if (!left_click) {
                if (right_click &&
                    gui_window_open_text_context_at(hit, event->x, event->y)) {
                    hit->dirty = true;
                    gui_request_paint();
                    return true;
                }
                handled = gui_window_dispatch_event(hit, event) || handled;
                return handled || right_click;
            }
            if (gui_window_handle_menu_event(hit, event)) {
                hit->dirty = true;
                return true;
            }
            gui_window_button_t button =
                gui_window_titlebar_button_at(hit, event->x, event->y);
            if (!st || !st->setup_only) {
                if (button == GUI_WINDOW_BUTTON_CLOSE) {
                    gui_window_t *fallback = desk_focus_fallback(desktop, hit);
                    gui_window_close(hit);
                    gui_desktop_focus_window(desktop, fallback);
                    return true;
                }
                if (button == GUI_WINDOW_BUTTON_MINIMIZE) {
                    gui_window_t *fallback = desk_focus_fallback(desktop, hit);
                    gui_window_minimize(hit);
                    gui_desktop_focus_window(desktop, fallback);
                    return true;
                }
                uint8_t edges = desk_resize_edges(hit, event->x, event->y);
                if (edges) {
                    desktop->resize_window = hit;
                    desktop->resize_edges = edges;
                    desktop->resize_start_bounds = hit->bounds;
                    desktop->resize_start_x = event->x;
                    desktop->resize_start_y = event->y;
                    if (desktop->drag_outline_enabled) {
                        desktop->drag_outline_bounds = hit->bounds;
                        desktop->drag_outline_visible = true;
                        gui_desktop_invalidate_rect(desktop, hit->bounds);
                    }
                    return true;
                }
                if (gui_window_titlebar_contains(hit, event->x, event->y)) {
                    desktop->drag_window  = hit;
                    desktop->drag_off_x   = event->x - hit->bounds.x;
                    desktop->drag_off_y   = event->y - hit->bounds.y;
                    if (desktop->drag_outline_enabled) {
                        desktop->drag_outline_bounds = hit->bounds;
                        desktop->drag_outline_visible = true;
                        gui_desktop_invalidate_rect(desktop,
                                                    hit->bounds);
                    }
                }
            } else if (button != GUI_WINDOW_BUTTON_NONE ||
                       gui_window_titlebar_contains(hit, event->x, event->y)) {
                /* SETUP.BEX es modal durante el primer arranque: su ventana no
                   puede cerrarse, minimizarse, moverse ni redimensionarse. */
                return true;
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

        /* En modo Setup el fondo es decorativo: todo el input permanece
           capturado por la única ventana del asistente. */
        if (st && st->setup_only) {
            if (desktop->focused_window)
                return gui_window_dispatch_event(desktop->focused_window, event);
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
        if (desktop->drag_window) {
            if (desktop->drag_outline_enabled &&
                desktop->drag_outline_visible) {
                gui_window_t *dragged = desktop->drag_window;
                gui_rect_t old_bounds = dragged->bounds;
                gui_rect_t new_bounds = desktop->drag_outline_bounds;

                desktop->drag_outline_visible = false;
                dragged->bounds = new_bounds;
                dragged->mode_restore_valid = false;
                dragged->dirty = true;
                gui_desktop_invalidate_rect(desktop, old_bounds);
                gui_desktop_invalidate_rect(desktop, new_bounds);
            }
            handled = true;
        }
        desktop->drag_window = NULL;
        if (desktop->resize_window) {
            gui_window_t *resized = desktop->resize_window;
            handled = true;
            if (desktop->drag_outline_enabled &&
                desktop->drag_outline_visible) {
                gui_rect_t old_bounds = resized->bounds;
                gui_rect_t new_bounds = desktop->drag_outline_bounds;
                desktop->drag_outline_visible = false;
                resized->bounds = new_bounds;
                resized->mode_restore_valid = false;
                resized->dirty = true;
                gui_desktop_invalidate_rect(desktop, old_bounds);
                gui_desktop_invalidate_rect(desktop, new_bounds);
            }
        }
        desktop->resize_window = NULL;
        desktop->resize_edges = 0;

        /* Soltar icono → detectar doble clic */
        if (st && !st->setup_only) {
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

                if (delta < desk_ticks_from_ms(DESK_DBLCLICK_MS) && same && ic->last_click_tick != 0) {
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
        if (!desktop->cursor_custom) {
            uint8_t edges = desktop->resize_window ? desktop->resize_edges :
                desk_resize_edges(gui_desktop_window_at(desktop, event->x, event->y),
                                  event->x, event->y);
            gui_desktop_set_cursor_style(desktop, desk_resize_cursor(edges));
        }
        if (desktop->resize_window) {
            desk_resize_window(desktop, event->x, event->y);
            return true;
        }
        if (desktop->drag_window) {
            if (desktop->drag_outline_enabled) {
                gui_rect_t old_outline = desktop->drag_outline_bounds;
                gui_rect_t new_outline = old_outline;
                new_outline.x = event->x - desktop->drag_off_x;
                new_outline.y = event->y - desktop->drag_off_y;
                if (new_outline.x != old_outline.x ||
                    new_outline.y != old_outline.y) {
                    desktop->drag_outline_bounds = new_outline;
                    desktop->drag_outline_visible = true;
                    gui_desktop_invalidate_rect(desktop, old_outline);
                    gui_desktop_invalidate_rect(desktop, new_outline);
                }
            } else {
                desktop->drag_window->bounds.x =
                    event->x - desktop->drag_off_x;
                desktop->drag_window->bounds.y =
                    event->y - desktop->drag_off_y;
                desktop->drag_window->mode_restore_valid = false;
                desktop->drag_window->dirty = true;
            }
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
        if (st && !st->setup_only) {
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

    if (event->type == GUI_EVENT_KEY && desktop->focused_window) {
        if (deskmanager_dispatch_widgets(desktop->focused_window, event))
            return true;
        return gui_window_dispatch_event(desktop->focused_window, event);
    }

    if (event->type == GUI_EVENT_MOUSE_WHEEL && desktop->focused_window)
        return gui_window_dispatch_event(desktop->focused_window, event);

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
    bootsplash_show("@HD53E5F05", 78);
    if (compat_mode_allow_icon_images()) bk_iconpak_preload_on_gui_start();
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

    /* El escritorio normal arranca con el fondo clásico de BlesKernOS. */
    if (!g_wallpaper_path[0] && compat_mode_allow_wallpaper()) {
        g_wallpaper_mode = DESK_WALLPAPER_TILE;
        if (!deskmanager_set_wallpaper(DESK_DEFAULT_WALLPAPER))
            g_desktop_background = DESK_DEFAULT_BACKGROUND;
    }

    void *config = NULL;
    uint32_t config_size = 0;
    bootsplash_pulse();
    bootsplash_debug("@H4F9706BD");
    if (bk_user_config_read_all(BK_DESKTOP_CONFIG_PATH,
                                BK_DESKTOP_CONFIG_LEGACY_PATH,
                                &config, &config_size) && config) {
        bootsplash_debug("@H8790A8E4");
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
                } else if (kstrcmp(line, "netsurf") == 0 ||
                           kstrcmp(line, "browser") == 0) {
                    open_fn = desk_open_netsurf_app;
                } else if (kstrcmp(line, "calculator") == 0) {
                    open_fn = desk_open_calculator_app;
                } else if (kstrcmp(line, "processes") == 0) {
                    open_fn = desk_open_processmanager_app;
                } else if (kstrcmp(line, "performance") == 0) {
                    open_fn = desk_open_performance_app;
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

    /* Los perfiles viejos guardaban sólo Archivos y Rendimiento. Se conservan
     * sus posiciones y se completan los accesos de trabajo sin duplicarlos. */
    desk_ensure_default_program_icons(st, desktop);
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

void deskmanager_install_setup(gui_desktop_t *desktop,
                               const char *wallpaper_path) {
    deskmanager_state_t *st;
    gui_program_t *prog;

    if (!desktop) return;
    st = (deskmanager_state_t *)kzalloc(sizeof(deskmanager_state_t));
    if (!st) return;

    st->setup_only = true;
    st->icon_count = 0;
    st->base_icon_count = 0;
    g_desk_desktop = desktop;
    g_desk_state = st;

    /* Noche queda en el escritorio, no dentro del cliente de SETUP.BEX. */
    deskmanager_set_wallpaper_mode(DESK_WALLPAPER_STRETCH);
    if (!wallpaper_path || !deskmanager_set_wallpaper(wallpaper_path))
        deskmanager_set_background(0x00101830U);

    prog = gui_desktop_register_program(desktop, "setup-shell", st,
                                        deskmanager_paint,
                                        deskmanager_handle_event,
                                        deskmanager_destroy);
    if (!prog) {
        if (g_desk_state == st) g_desk_state = NULL;
        kfree(st);
    }
}
