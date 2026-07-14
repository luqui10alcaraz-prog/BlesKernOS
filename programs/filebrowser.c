/*
 * filebrowser.c — Navegador de archivos para BlesKernOS GUI
 *
 * Integración:
 *   1. Añadir al Makefile (ya está en KERNEL_SOURCES como programs/filebrowser.c)
 *   2. Declarar en programs.h:
 *        void filebrowser_install(gui_desktop_t *desktop);
 *        void filebrowser_open_from_desktop(gui_desktop_t *desktop);
 *   3. Llamar filebrowser_install(desktop) desde el arranque GUI.
 *
 *  deskmanager.c llama filebrowser_open_from_desktop() al doble clic en el icono.
 */

#include "../kernel/include/api.h"
#include <stdio.h>

/* ── Ventana ── */
#define FB_WIN_X    60
#define FB_WIN_Y    40
#define FB_WIN_W   560
#define FB_WIN_H   330

/* ── Lista interna ── */
#define FB_LIST_X       6
#define FB_LIST_Y       2
#define FB_TILE_W     112
#define FB_TILE_H      84
#define FB_ICON_SIZE   48

/* ── Colores ── */
#define FB_COLOR_FOLDER  0x00E8B840
#define FB_COLOR_SEL     0x001060A0
#define FB_COLOR_TEXT    0x00101010
#define FB_COLOR_TEXTSEL 0x00FFFFFF
#define FB_COLOR_BG      0x00F4F4F0

/* ── Doble clic dentro de la lista ── */
#define FB_DBLCLICK_TICKS 500
#define FB_ICON_BYTES (FB_ICON_SIZE * FB_ICON_SIZE * sizeof(uint32_t))
#define FB_LIST_ROW_H 24

enum { FB_DIALOG_NONE = 0, FB_DIALOG_NEW_FOLDER, FB_DIALOG_RENAME };

enum {
    FB_DRIVE_NONE = 0,
    FB_DRIVE_HDD,
    FB_DRIVE_CD,
    FB_DRIVE_USB,
    FB_DRIVE_FLOPPY
};

enum {
    FB_MENU_OPEN = 100, FB_MENU_NEW_FOLDER, FB_MENU_RENAME, FB_MENU_DELETE,
    FB_MENU_PROPERTIES, FB_MENU_COPY, FB_MENU_CUT, FB_MENU_PASTE,
    FB_MENU_VIEW_ICONS, FB_MENU_VIEW_LIST, FB_MENU_REFRESH, FB_MENU_ABOUT
};

/* ──────────────────────────────────────────────── */

typedef struct {
    gui_desktop_t   *desktop;
    gui_window_t   *window;
    char            cwd[VFS_MAX_PATH];
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint8_t         entry_drive[VFS_MAX_DIR_ENTRIES];
    uint32_t        entry_count;
    bool            drive_view;
    int             selected;
    int             scroll;
    gui_scrollbar_drag_t scrollbar_drag;
    uint32_t        btn_up_id;
    uint32_t        btn_copy_id;
    uint32_t        btn_paste_id;
    uint32_t        btn_new_id;
    uint32_t        btn_refresh_id;
    void           *associations;
    uint32_t        associations_size;
    char            address[VFS_MAX_PATH];
    char            status[48];
    bool            editing_address;
    bool            list_view;
    uint8_t         dialog;
    char            dialog_input[VFS_MAX_NAME];
    bool            delete_dialog_open;
    uint32_t        delete_dialog_token;
    uint32_t       *folder_icon;
    uint32_t       *file_icon;
    uint32_t       *text_icon;
    uint32_t       *config_icon;
    uint32_t       *image_icon;
    uint32_t       *object_icon;
    uint32_t       *midi_icon;
    uint32_t       *hdd_icon;
    uint32_t       *cd_icon;
    uint32_t       *usb_icon;
    uint32_t       *floppy_icon;
    uint32_t       *shell_icon;
    uint32_t       *editor_icon;
    uint32_t       *calc_icon;
    uint32_t       *files_icon;
    uint32_t       *midamp_icon;
    /* doble clic en lista */
    uint32_t        last_list_click_tick;
    int             last_list_click_idx;
} fb_state_t;

typedef struct {
    vfs_dir_entry_t entry;
    char path[VFS_MAX_PATH];
    char mount[16];
    uint8_t drive;
    int active_tab;
} fb_properties_t;

typedef struct {
    fb_state_t *owner;
    gui_desktop_t *desktop;
    gui_window_t *window;
    char path[VFS_MAX_PATH];
    char name[VFS_MAX_NAME];
    bool directory;
    uint8_t action;
    uint32_t delete_button_id;
    uint32_t cancel_button_id;
    uint32_t owner_token;
    uint32_t *warning_icon;
} fb_delete_dialog_t;

enum {
    FB_CONTEXT_OPEN = 1,
    FB_CONTEXT_COPY,
    FB_CONTEXT_CUT,
    FB_CONTEXT_PASTE,
    FB_CONTEXT_RENAME,
    FB_CONTEXT_DELETE,
    FB_CONTEXT_PROPERTIES,
    FB_CONTEXT_NEW_FOLDER,
    FB_CONTEXT_REFRESH,
};

static fb_state_t *g_fb = NULL;
static char g_fb_clipboard[VFS_MAX_PATH];
static bool g_fb_clipboard_cut;
static bool g_fb_clipboard_directory;
static uint32_t g_fb_delete_token;
static void fb_main(void *argument);

static gui_rect_t fb_list_rect(const fb_state_t *st) {
    gui_rect_t content;
    int list_top;
    int list_h;

    if (!st || !st->window) return (gui_rect_t){0, 0, 0, 0};
    content = bk_gui_window_content_rect_raw(st->window);
    list_top = content.y + FB_LIST_Y + 48;
    list_h = content.h - 70;
    return (gui_rect_t){content.x + FB_LIST_X, list_top,
                        content.w - FB_LIST_X * 2, list_h};
}

static int fb_cols_for_rect(gui_rect_t list) {
    if (g_fb && g_fb->list_view) return 1;
    int cols = (list.w - GUI_SCROLLBAR_SIZE) / FB_TILE_W;
    return cols < 1 ? 1 : cols;
}

static int fb_visible_slots(const fb_state_t *st) {
    gui_rect_t list = fb_list_rect(st);
    int cols = fb_cols_for_rect(list);
    int rows = list.h / (st && st->list_view ? FB_LIST_ROW_H : FB_TILE_H);
    if (list.w <= 0 || list.h <= 0) return 1;
    if (rows < 1) rows = 1;
    return rows * cols;
}

static void fb_clamp_scroll(fb_state_t *st) {
    int visible;
    int max_scroll;

    if (!st) return;
    visible = fb_visible_slots(st);
    max_scroll = (int)st->entry_count > visible
        ? (int)st->entry_count - visible
        : 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
    if (st->scroll < 0) st->scroll = 0;
}

/* ICONS.PAK cache.
 *
 * Formato:
 *   magic    "BKIP"
 *   u32      version = 1
 *   u32      count
 *   entries[count]:
 *      char name[16]   // sin .BMP, ej: FOLDER
 *      u32  width
 *      u32  height
 *      u32  offset
 *      u32  size
 *   payload:
 *      uint32_t pixels[width * height] en RGB32/ARGB32
 */
typedef struct {
    bool loaded;
    void *pak_data;
    uint32_t pak_size;

    uint32_t *folder_icon;
    uint32_t *file_icon;
    uint32_t *text_icon;
    uint32_t *config_icon;
    uint32_t *image_icon;
    uint32_t *object_icon;
    uint32_t *midi_icon;
    uint32_t *hdd_icon;
    uint32_t *cd_icon;
    uint32_t *usb_icon;
    uint32_t *floppy_icon;
    uint32_t *shell_icon;
    uint32_t *editor_icon;
    uint32_t *calc_icon;
    uint32_t *files_icon;
    uint32_t *midamp_icon;

    void *associations;
    uint32_t associations_size;
} fb_icon_cache_t;

static fb_icon_cache_t g_fb_icon_cache;

static uint32_t fb_rd32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool fb_name16_eq(const uint8_t *name16, const char *name) {
    uint32_t i = 0;
    if (!name) return false;
    while (i < 16 && name[i]) {
        if ((char)name16[i] != name[i]) return false;
        i++;
    }
    return i < 16 && name16[i] == '\0';
}

static uint32_t *fb_pak_find_icon(const char *name) {
    uint8_t *data = (uint8_t *)g_fb_icon_cache.pak_data;
    uint32_t count;

    if (!data || g_fb_icon_cache.pak_size < 12) return NULL;
    if (data[0] != 'B' || data[1] != 'K' ||
        data[2] != 'I' || data[3] != 'P') return NULL;
    if (fb_rd32(data + 4) != 1) return NULL;

    count = fb_rd32(data + 8);
    if (12U + count * 32U > g_fb_icon_cache.pak_size) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t *e = data + 12U + i * 32U;
        uint32_t w = fb_rd32(e + 16);
        uint32_t h = fb_rd32(e + 20);
        uint32_t off = fb_rd32(e + 24);
        uint32_t size = fb_rd32(e + 28);
        uint32_t need = w * h * sizeof(uint32_t);
        uint32_t *src;
        uint32_t *out;

        if (!fb_name16_eq(e, name)) continue;
        if (!w || !h) return NULL;
        if (size < need) return NULL;
        if (off > g_fb_icon_cache.pak_size ||
            off + need > g_fb_icon_cache.pak_size) return NULL;

        src = (uint32_t *)(data + off);
        out = (uint32_t *)bk_sys_alloc(FB_ICON_BYTES);
        if (!out) return NULL;

        for (uint32_t y = 0; y < FB_ICON_SIZE; y++) {
            uint32_t sy = (y * h) / FB_ICON_SIZE;
            for (uint32_t x = 0; x < FB_ICON_SIZE; x++) {
                uint32_t sx = (x * w) / FB_ICON_SIZE;
                out[y * FB_ICON_SIZE + x] = src[sy * w + sx];
            }
        }

        return out;
    }

    return NULL;
}

static bool fb_load_icon_pak(void) {
    if (g_fb_icon_cache.pak_data) return true;

    if (bk_file_read_all("/ICONS/ICONS.PAK",
                     &g_fb_icon_cache.pak_data,
                     &g_fb_icon_cache.pak_size)) {
        return true;
    }

    if (bk_file_read_all("/ICONS.PAK",
                     &g_fb_icon_cache.pak_data,
                     &g_fb_icon_cache.pak_size)) {
        return true;
    }
/*
     * fb_load_icon_pak disabled: central loader.
     *
     * Antes el filebrowser leía /ICONS/ICONS.PAK otra vez con su propia cache.
     * Eso duplicaba la lectura del PAK desde disquete.
     *
     * Ahora dejamos que el fallback use bk_app_load_icon().
     * Esa función ya resuelve los BMP de /ICONS desde el PAK central cacheado.
     */
    return false;
}

static void fb_cache_load_resources(void) {
    if (g_fb_icon_cache.loaded) return;

    if (fb_load_icon_pak()) {
        g_fb_icon_cache.folder_icon = fb_pak_find_icon("FOLDER");
        g_fb_icon_cache.file_icon = fb_pak_find_icon("FILE");
        g_fb_icon_cache.text_icon = fb_pak_find_icon("TEXT");
        g_fb_icon_cache.config_icon = fb_pak_find_icon("CONFIG");
        g_fb_icon_cache.image_icon = fb_pak_find_icon("IMAGE");
        g_fb_icon_cache.object_icon = fb_pak_find_icon("OBJECT");
        g_fb_icon_cache.midi_icon = fb_pak_find_icon("MIDI");
        g_fb_icon_cache.hdd_icon = fb_pak_find_icon("HDD");
        g_fb_icon_cache.cd_icon = fb_pak_find_icon("CD");
        g_fb_icon_cache.usb_icon = fb_pak_find_icon("USB");
        g_fb_icon_cache.floppy_icon = fb_pak_find_icon("FLOPPY");
        g_fb_icon_cache.shell_icon = fb_pak_find_icon("SHELL");
        g_fb_icon_cache.editor_icon = fb_pak_find_icon("EDITOR");
        g_fb_icon_cache.calc_icon = fb_pak_find_icon("CALC");
        g_fb_icon_cache.files_icon = fb_pak_find_icon("FILES");
        g_fb_icon_cache.midamp_icon = fb_pak_find_icon("MIDAMP");
    }

#define FB_CACHE_LOAD_ICON(field, path) do { \
    if (!g_fb_icon_cache.field) { \
        g_fb_icon_cache.field = bk_app_load_icon(path, 48, 48); \
        bk_sys_yield(); \
    } \
} while (0)

    /*
     * Fallback compatible: si ICONS.PAK no existe o falta algún icono,
     * cargamos el BMP viejo. Si BK_ICON_PAK_ONLY=1 y no hay BMPs, simplemente
     * quedarán NULL; fb_draw_bmp_icon() lo tolera.
     */
    FB_CACHE_LOAD_ICON(folder_icon, "/ICONS/FOLDER.BMP");
    FB_CACHE_LOAD_ICON(file_icon, "/ICONS/FILE.BMP");
    FB_CACHE_LOAD_ICON(text_icon, "/ICONS/TEXT.BMP");
    FB_CACHE_LOAD_ICON(config_icon, "/ICONS/CONFIG.BMP");
    FB_CACHE_LOAD_ICON(image_icon, "/ICONS/IMAGE.BMP");
    FB_CACHE_LOAD_ICON(object_icon, "/ICONS/OBJECT.BMP");
    FB_CACHE_LOAD_ICON(midi_icon, "/ICONS/MIDI.BMP");
    FB_CACHE_LOAD_ICON(hdd_icon, "/ICONS/HDD.BMP");
    FB_CACHE_LOAD_ICON(cd_icon, "/ICONS/CD.BMP");
    FB_CACHE_LOAD_ICON(usb_icon, "/ICONS/USB.BMP");
    FB_CACHE_LOAD_ICON(floppy_icon, "/ICONS/FLOPPY.BMP");
    FB_CACHE_LOAD_ICON(shell_icon, "/ICONS/SHELL.BMP");
    FB_CACHE_LOAD_ICON(editor_icon, "/ICONS/EDITOR.BMP");
    FB_CACHE_LOAD_ICON(calc_icon, "/ICONS/CALC.BMP");
    FB_CACHE_LOAD_ICON(files_icon, "/ICONS/FILES.BMP");
    FB_CACHE_LOAD_ICON(midamp_icon, "/ICONS/MIDAMP.BMP");

#undef FB_CACHE_LOAD_ICON

    if (!bk_file_read_all("/ASSOC.INI",
                      &g_fb_icon_cache.associations,
                      &g_fb_icon_cache.associations_size)) {
        (void)bk_file_read_all("/Associations.INI",
                           &g_fb_icon_cache.associations,
                           &g_fb_icon_cache.associations_size);
    }

    g_fb_icon_cache.loaded = true;
}

static void fb_load_resources(fb_state_t *st) {
    if (!st) return;

    fb_cache_load_resources();

    st->folder_icon = g_fb_icon_cache.folder_icon;
    st->file_icon = g_fb_icon_cache.file_icon;
    st->text_icon = g_fb_icon_cache.text_icon;
    st->config_icon = g_fb_icon_cache.config_icon;
    st->image_icon = g_fb_icon_cache.image_icon;
    st->object_icon = g_fb_icon_cache.object_icon;
    st->midi_icon = g_fb_icon_cache.midi_icon;
    st->hdd_icon = g_fb_icon_cache.hdd_icon;
    st->cd_icon = g_fb_icon_cache.cd_icon;
    st->usb_icon = g_fb_icon_cache.usb_icon;
    st->floppy_icon = g_fb_icon_cache.floppy_icon;
    st->shell_icon = g_fb_icon_cache.shell_icon;
    st->editor_icon = g_fb_icon_cache.editor_icon;
    st->calc_icon = g_fb_icon_cache.calc_icon;
    st->files_icon = g_fb_icon_cache.files_icon;
    st->midamp_icon = g_fb_icon_cache.midamp_icon;

    st->associations = g_fb_icon_cache.associations;
    st->associations_size = g_fb_icon_cache.associations_size;
}

static bool fb_cdrom_available(void) {
    /* Nunca ejecutar I/O ATAPI mientras se construye o pinta la ventana. */
    return bk_device_has_cdrom();
}

static bool fb_usb_available(void) {
    const char *mount = bk_device_mount_name();

    if (mount && bk_runtime_strncmp(mount, "usb", 3) == 0) return false;
    return bk_device_block_get("usb0") != NULL;
}

static bool fb_hdd_available(void) {
    const char *mount = bk_device_mount_name();

    if (mount && bk_runtime_strncmp(mount, "ata", 3) == 0) return false;
    return bk_device_block_get("ata0") != NULL;
}

static bool fb_floppy_available(void) {
    /*
     * Un controlador FDC registrado no demuestra que haya medio insertado.
     * Si fd0 ya es el montaje activo, fb_load_drives lo agrega por su cuenta.
     */
    return false;
}

static void fb_join_path(char *out, uint32_t capacity,
                         const char *dir, const char *name) {
    size_t len;
    if (!out || !capacity) return;
    bk_runtime_strncpy(out, dir ? dir : "/", capacity - 1);
    out[capacity - 1] = '\0';
    len = bk_runtime_strlen(out);
    if (len > 1 && out[len - 1] != '/' && len + 1 < capacity) {
        out[len++] = '/';
        out[len] = '\0';
    }
    bk_runtime_strncpy(out + len, name ? name : "", capacity - len - 1);
}

static void fb_draw_bmp_icon(gui_surface_t *s, int x, int y,
                             const uint32_t *pixels) {
    if (!pixels) return;
    bk_app_draw_icon(s, x, y, pixels, FB_ICON_SIZE, FB_ICON_SIZE);
}

static void fb_draw_small_icon(gui_surface_t *surface, int x, int y,
                               const uint32_t *pixels) {
    if (!surface || !pixels) return;
    for (int dy = 0; dy < 20; dy++) {
        for (int dx = 0; dx < 20; dx++) {
            uint32_t color = pixels[(dy * FB_ICON_SIZE / 20) * FB_ICON_SIZE +
                                    (dx * FB_ICON_SIZE / 20)];
            if ((color >> 24) && x + dx >= 0 && y + dy >= 0 &&
                x + dx < surface->width && y + dy < surface->height &&
                bk_gui_rect_contains(surface->clip, x + dx, y + dy))
                bk_gui_gfx_putpixel(surface, x + dx, y + dy,
                                 color & 0x00FFFFFF);
        }
    }
}

static const char *fb_extension(const char *name) {
    const char *dot = NULL;
    while (name && *name) {
        if (*name == '.') dot = name;
        name++;
    }
    return dot ? dot : "";
}

static bool fb_ext_is(const char *ext, const char *upper, const char *lower) {
    return bk_runtime_strcmp(ext, upper) == 0 || bk_runtime_strcmp(ext, lower) == 0;
}

static bool fb_text_equal_nocase(const char *left, const char *right,
                                 size_t length) {
    for (size_t i = 0; i < length; i++) {
        char a = left[i], b = right[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return true;
}

static bool fb_assoc_value(const fb_state_t *st, const char *name,
                           const char **value_out, size_t *length_out) {
    const char *ext = fb_extension(name);
    const char *line;
    if (!st || !st->associations || !name || !value_out || !length_out)
        return false;
    line = (const char *)st->associations;
    while (*line) {
        const char *end = line, *eq = NULL;
        while (*end && *end != '\r' && *end != '\n') {
            if (*end == '=' && !eq) eq = end;
            end++;
        }
        if (eq) {
            size_t key_len = (size_t)(eq - line);
            const char *extension = *ext == '.' ? ext + 1 : ext;
            if ((key_len == bk_runtime_strlen(name) &&
                 fb_text_equal_nocase(line, name, key_len)) ||
                (key_len == bk_runtime_strlen(extension) &&
                 fb_text_equal_nocase(line, extension, key_len))) {
                *value_out = eq + 1;
                *length_out = (size_t)(end - (eq + 1));
                return true;
            }
        }
        line = end;
        while (*line == '\r' || *line == '\n') line++;
    }
    return false;
}

static const uint32_t *fb_icon_for_entry(const fb_state_t *st,
                                         const vfs_dir_entry_t *entry,
                                         uint32_t index) {
    const char *ext;
    if (st->drive_view && index < st->entry_count) {
        switch (st->entry_drive[index]) {
            case FB_DRIVE_HDD: return st->hdd_icon;
            case FB_DRIVE_CD: return st->cd_icon;
            case FB_DRIVE_USB: return st->usb_icon;
            case FB_DRIVE_FLOPPY: return st->floppy_icon;
            default: break;
        }
    }
    if (entry->type == VFS_NODE_DIR) return st->folder_icon;

    ext = fb_extension(entry->name);
    if (st->associations) {
        const char *value;
        size_t value_len;
        if (fb_assoc_value(st, entry->name, &value, &value_len)) {
            const char *separator = value;
            while (separator < value + value_len && *separator != '|') separator++;
            value_len = (size_t)(separator - value);
#define FB_ASSOC(path, icon) \
                        if (value_len == sizeof(path) - 1 && \
                            bk_runtime_strncmp(value, path, value_len) == 0) return icon
                        FB_ASSOC("/ICONS/SHELL.BMP", st->shell_icon);
                        FB_ASSOC("/ICONS/FILES.BMP", st->files_icon);
                        FB_ASSOC("/ICONS/EDITOR.BMP", st->editor_icon);
                        FB_ASSOC("/ICONS/CALC.BMP", st->calc_icon);
                        FB_ASSOC("/ICONS/MIDAMP.BMP", st->midamp_icon);
                        FB_ASSOC("/ICONS/IMAGE.BMP", st->image_icon);
                        FB_ASSOC("/ICONS/OBJECT.BMP", st->object_icon);
                        FB_ASSOC("/ICONS/CONFIG.BMP", st->config_icon);
                        FB_ASSOC("/ICONS/TEXT.BMP", st->text_icon);
                        FB_ASSOC("/ICONS/MIDI.BMP", st->midi_icon);
#undef FB_ASSOC
        }
    }
    if (fb_ext_is(ext, ".TXT", ".txt")) return st->text_icon;
    if (fb_ext_is(ext, ".INI", ".ini")) return st->config_icon;
    if (fb_ext_is(ext, ".BMP", ".bmp") ||
        fb_ext_is(ext, ".GIF", ".gif")) return st->image_icon;
    if (fb_ext_is(ext, ".DVR", ".dvr")) return st->config_icon;
    if (fb_ext_is(ext, ".CPL", ".cpl")) return st->config_icon;
    if (fb_ext_is(ext, ".WAD", ".wad")) return st->midi_icon;
    if (fb_ext_is(ext, ".O", ".o") ||
        fb_ext_is(ext, ".ELF", ".elf") ||
        fb_ext_is(ext, ".EXE", ".exe")) return st->object_icon;
    if (fb_ext_is(ext, ".MID", ".mid") ||
        fb_ext_is(ext, ".KAR", ".kar")) return st->midi_icon;
    return st->file_icon;
}

static void fb_draw_tile_name(gui_surface_t *surface, int x, int y,
                              const char *name, uint32_t color,
                              gui_rect_t clip) {
    char first[18];
    char second[18];
    size_t length = bk_runtime_strlen(name);
    size_t first_count = length > 16 ? 16 : length;
    size_t second_count = length > first_count ? length - first_count : 0;
    if (second_count > 16) second_count = 16;
    bk_runtime_strncpy(first, name, first_count);
    first[first_count] = '\0';
    bk_gui_font_draw_string_clipped(surface, x, y, first, color, clip);
    if (second_count) {
        bk_runtime_strncpy(second, name + first_count, second_count);
        second[second_count] = '\0';
        if (length > first_count + second_count && second_count >= 3) {
            second[second_count - 3] = '.';
            second[second_count - 2] = '.';
            second[second_count - 1] = '.';
        }
        bk_gui_font_draw_string_clipped(surface, x, y + 12, second, color, clip);
    }
}

static void fb_add_drive(fb_state_t *st, const char *name, uint8_t drive) {
    vfs_dir_entry_t *entry;
    if (st->entry_count >= VFS_MAX_DIR_ENTRIES) return;
    entry = &st->entries[st->entry_count];
    bk_runtime_strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->type = VFS_NODE_DIR;
    entry->size = 0;
    entry->attributes = 0;
    st->entry_drive[st->entry_count++] = drive;
}

static void fb_load_drives(fb_state_t *st) {
    const char *mount = bk_device_mount_name();
    st->entry_count = 0;
    st->selected = 0;
    st->scroll = 0;

    /*
     * La primera unidad debe representar el volumen que ya esta montado.
     * Antes todo volumen que no fuera fd0 se etiquetaba como disco local y,
     * al abrirlo, bk_device_mount_default() cambiaba un boot desde usb0 al ATA de
     * la Dell.
     */
    if (mount && bk_runtime_strncmp(mount, "usb", 3) == 0)
        fb_add_drive(st, "USB", FB_DRIVE_USB);
    else if (mount && bk_runtime_strcmp(mount, "fd0") == 0)
        fb_add_drive(st, "Disquete", FB_DRIVE_FLOPPY);
    else if (mount && bk_runtime_strncmp(mount, "ata", 3) == 0)
        fb_add_drive(st, "Disco local", FB_DRIVE_HDD);
    else if (bk_device_block_get("ata0"))
        fb_add_drive(st, "Disco local", FB_DRIVE_HDD);

    /* Mostrar el ATA interno aunque el sistema haya arrancado desde USB. */
    if (mount && bk_runtime_strncmp(mount, "ata", 3) != 0 && fb_hdd_available())
        fb_add_drive(st, "Disco local", FB_DRIVE_HDD);
    if (fb_cdrom_available()) fb_add_drive(st, "CD-ROM", FB_DRIVE_CD);
    if (fb_usb_available()) fb_add_drive(st, "USB", FB_DRIVE_USB);
    if (fb_floppy_available()) fb_add_drive(st, "Disquete", FB_DRIVE_FLOPPY);
}

/* ══════════════════════════════════════════════════
 *  Cargar directorio
 * ══════════════════════════════════════════════════ */

static void fb_load_dir(fb_state_t *st) {
    if (st->drive_view) {
        fb_load_drives(st);
        return;
    }
    st->entry_count = 0;
    st->selected    = 0;
    st->scroll      = 0;

    /* Entrada ".." manual si no estamos en raíz */
    if (st->cwd[0] == '/' && st->cwd[1] != '\0') {
        bk_runtime_strncpy(st->entries[0].name, "..", sizeof(st->entries[0].name) - 1);
        st->entries[0].type = VFS_NODE_DIR;
        st->entries[0].size = 0;
        st->entries[0].attributes = 0;
        st->entry_count = 1;
    }

    vfs_dir_entry_t tmp[VFS_MAX_DIR_ENTRIES];
    uint32_t found = 0;
    if (bk_file_list_dir(st->cwd, tmp, VFS_MAX_DIR_ENTRIES, &found)) {
        for (uint32_t i = 0; i < found && st->entry_count < VFS_MAX_DIR_ENTRIES; i++) {
            if (tmp[i].name[0] == '.' &&
                (tmp[i].name[1] == '\0' ||
                 (tmp[i].name[1] == '.' && tmp[i].name[2] == '\0'))) continue;
            st->entries[st->entry_count++] = tmp[i];
        }
    }
    {
        uint64_t total = 0, free = 0;
        const char *mount = bk_device_mount_name();
        if (bk_file_space(&total, &free))
            snprintf(st->status, sizeof(st->status), "%s: %u MB libres / %u MB",
                     mount ? mount : "unidad",
                     (uint32_t)(free >> 20), (uint32_t)(total >> 20));
    }
}

/* ══════════════════════════════════════════════════
 *  Actualizar título de ventana
 * ══════════════════════════════════════════════════ */

static void fb_update_title(fb_state_t *st) {
    if (!st->window) return;
    char title[48];
    if (st->drive_view) {
        bk_runtime_strncpy(st->window->title, "Este equipo",
                 sizeof(st->window->title) - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        st->window->dirty = true;
        return;
    }
    bk_runtime_strncpy(title, "Archivos: ", sizeof(title) - 1);
    size_t tlen = bk_runtime_strlen(title);
    bk_runtime_strncpy(title + tlen, st->cwd, sizeof(title) - tlen - 1);
    title[sizeof(title) - 1] = '\0';
    bk_runtime_strncpy(st->window->title, title, sizeof(st->window->title) - 1);
    st->window->title[sizeof(st->window->title) - 1] = '\0';
    st->window->dirty = true;
    bk_runtime_strncpy(st->address, st->cwd, sizeof(st->address) - 1);
    st->address[sizeof(st->address) - 1] = '\0';
}

/* ══════════════════════════════════════════════════
 *  Navegar a subdir / padre
 * ══════════════════════════════════════════════════ */

static void fb_navigate(fb_state_t *st, const char *name) {
    char new_path[VFS_MAX_PATH];

    if (bk_runtime_strcmp(name, "..") == 0) {
        bk_runtime_strncpy(new_path, st->cwd, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = '\0';
        size_t len = bk_runtime_strlen(new_path);
        if (len > 1 && new_path[len - 1] == '/') new_path[--len] = '\0';
        while (len > 1 && new_path[len - 1] != '/') new_path[--len] = '\0';
        if (len > 1) new_path[--len] = '\0';
        if (len == 0) { new_path[0] = '/'; new_path[1] = '\0'; }
    } else {
        bk_runtime_strncpy(new_path, st->cwd, sizeof(new_path) - 1);
        size_t len = bk_runtime_strlen(new_path);
        if (len > 0 && new_path[len - 1] != '/' && len + 1 < sizeof(new_path)) {
            new_path[len++] = '/';
            new_path[len]   = '\0';
        }
        bk_runtime_strncpy(new_path + bk_runtime_strlen(new_path), name,
                 sizeof(new_path) - bk_runtime_strlen(new_path) - 1);
    }

    if (bk_file_chdir(new_path))
        bk_runtime_strncpy(st->cwd, bk_file_getcwd(), sizeof(st->cwd) - 1);
    else
        bk_runtime_strncpy(st->cwd, new_path, sizeof(st->cwd) - 1);

    fb_load_dir(st);
    fb_update_title(st);
}

static bool fb_is_text_file(const char *name) {
    const char *ext = fb_extension(name);
    return fb_ext_is(ext, ".TXT", ".txt");
}

static void fb_open_text(gui_desktop_t *desktop, fb_state_t *st,
                         const char *name) {
    char path[VFS_MAX_PATH];
    bk_runtime_strncpy(path, st->cwd, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    size_t len = bk_runtime_strlen(path);
    if (len > 1 && path[len - 1] != '/') {
        path[len++] = '/';
        path[len] = '\0';
    }
    bk_runtime_strncpy(path + len, name, sizeof(path) - len - 1);
    (void)bk_app_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/TEXTEDITOR.O", path);
}

static void fb_execute_object(gui_desktop_t *desktop, fb_state_t *st,
                              const char *name) {
    char path[VFS_MAX_PATH];
    size_t len;

    bk_runtime_strncpy(path, st->cwd, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    len = bk_runtime_strlen(path);
    if (len > 1 && path[len - 1] != '/' && len + 1 < sizeof(path)) {
        path[len++] = '/';
        path[len] = '\0';
    }
    bk_runtime_strncpy(path + len, name, sizeof(path) - len - 1);
    if (!bk_app_execute_path(desktop, path) && st->window) {
        bool is_pe = bk_app_is_win32(path);
        const char *error = is_pe ? bk_app_pe_last_error() : bk_app_elf_last_error();

        bk_runtime_strncpy(st->window->title, is_pe ? "Error PE: " : "Error ELF: ",
                 sizeof(st->window->title) - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        len = bk_runtime_strlen(st->window->title);
        bk_runtime_strncpy(st->window->title + len, error,
                 sizeof(st->window->title) - len - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        st->window->dirty = true;
    }
}

static bool fb_open_associated(fb_state_t *st, const char *name) {
    const char *value;
    const char *separator;
    size_t value_len;
    char application[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    if (!fb_assoc_value(st, name, &value, &value_len)) return false;
    separator = value;
    while (separator < value + value_len && *separator != '|') separator++;
    if (separator >= value + value_len || separator + 1 >= value + value_len)
        return false;
    value = separator + 1;
    {
        const char *end = value;
        while (*end && *end != '\r' && *end != '\n') end++;
        value_len = (size_t)(end - value);
    }
    if (!value_len || value_len >= sizeof(application)) return false;
    bk_runtime_strncpy(application, value, value_len);
    application[value_len] = '\0';
    fb_join_path(path, sizeof(path), st->cwd, name);
    return bk_app_execute_path_arg(st->desktop, application, path);
}

static bool fb_open_selected(fb_state_t *st) {
    vfs_dir_entry_t *e;
    gui_desktop_t *desktop;
    if (!st || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count) return false;
    e = &st->entries[st->selected];
    desktop = st->desktop;
    if (st->drive_view) {
        uint8_t drive = st->entry_drive[st->selected];
        const char *path = "/";
        bool mounted = true;
        if (drive == FB_DRIVE_CD) path = "/CDROM";
        else if (drive == FB_DRIVE_USB) mounted = bk_device_mount("usb0");
        else if (drive == FB_DRIVE_FLOPPY) mounted = bk_device_mount("fd0");
        else if (drive == FB_DRIVE_HDD) mounted = bk_device_mount("ata0");
        else mounted = false;
        if (!mounted) {
            if (drive == FB_DRIVE_HDD)
                bk_runtime_strcpy(st->status, "No se pudo montar ata0 (MBR/FAT)");
            else if (drive == FB_DRIVE_USB)
                bk_runtime_strcpy(st->status, "No se pudo montar usb0");
            else
                bk_runtime_strcpy(st->status, "La unidad no esta lista");
            if (st->window) st->window->dirty = true;
            return true;
        }
        st->drive_view = false;
        bk_runtime_strncpy(st->cwd, path, sizeof(st->cwd) - 1);
        st->cwd[sizeof(st->cwd) - 1] = '\0';
        (void)bk_file_chdir(path);
        fb_load_dir(st);
        fb_update_title(st);
    } else if (e->type == VFS_NODE_DIR) {
        fb_navigate(st, e->name);
    } else if (fb_ext_is(fb_extension(e->name), ".DVR", ".dvr")) {
        char path[VFS_MAX_PATH];
        fb_join_path(path, sizeof(path), st->cwd, e->name);
        bk_runtime_strcpy(st->status, bk_device_driver_load(path) ? "Controlador cargado" :
                                               "No se pudo cargar el driver");
    } else if (fb_open_associated(st, e->name)) {
        /* Association.INI decides the program before built-in fallbacks. */
    } else if (fb_is_text_file(e->name)) {
        fb_open_text(desktop, st, e->name);
    } else if (fb_ext_is(fb_extension(e->name), ".BMP", ".bmp") ||
               fb_ext_is(fb_extension(e->name), ".GIF", ".gif")) {
        char path[VFS_MAX_PATH];
        fb_join_path(path, sizeof(path), st->cwd, e->name);
        (void)bk_app_execute_path_arg(desktop, "/SYSTEM/PROGRAMS/IMAGEVIEWER.O", path);
    } else if (bk_app_is_object(e->name) ||
               bk_app_is_win32(e->name)) {
        fb_execute_object(desktop, st, e->name);
    } else {
        bk_runtime_strcpy(st->status, "No hay una aplicacion asociada");
    }
    if (st->window) st->window->dirty = true;
    return true;
}

static void fb_properties_paint(gui_window_t *window, gui_surface_t *surface,
                                void *context) {
    fb_properties_t *p = (fb_properties_t *)context;
    gui_rect_t c;
    gui_rect_t page;
    char value[64];
    int x;
    int y;
    if (!p || !window || !surface) return;
    c = bk_gui_window_content_rect_raw(window);
    bk_gui_gfx_fill_rect(surface, c, 0x00D0D0C8);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){c.x + 10, c.y + 6, 82, 22},
                      p->active_tab == 0 ? 0x00FFFFFF : 0x00C0C0C0);
    bk_gui_gfx_draw_rect(surface, (gui_rect_t){c.x + 10, c.y + 6, 82, 22},
                      0x00606060);
    bk_gui_font_draw_string(surface, c.x + 22, c.y + 13, "General",
                         0x00101010, 0, false);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){c.x + 92, c.y + 6, 82, 22},
                      p->active_tab == 1 ? 0x00FFFFFF : 0x00C0C0C0);
    bk_gui_gfx_draw_rect(surface, (gui_rect_t){c.x + 92, c.y + 6, 82, 22},
                      0x00606060);
    bk_gui_font_draw_string(surface, c.x + 102, c.y + 13, "Detalles",
                         0x00101010, 0, false);
    page = (gui_rect_t){c.x + 10, c.y + 27, c.w - 20, c.h - 37};
    bk_gui_gfx_fill_rect(surface, page, 0x00FFFFFF);
    bk_gui_gfx_draw_rect(surface, page, 0x00606060);
    x = page.x + 14;
    y = page.y + 18;
    if (p->active_tab == 0) {
        bk_gui_font_draw_string(surface, x, y, "Nombre:", 0x00101010, 0, false);
        bk_gui_font_draw_string_clipped(surface, x + 82, y, p->entry.name,
                                     0x00101010, page);
        y += 22;
        bk_gui_font_draw_string(surface, x, y, "Tipo:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 82, y,
            p->drive ? "Unidad de almacenamiento" :
            (p->entry.type == VFS_NODE_DIR ? "Carpeta" : "Archivo"),
            0x00101010, 0, false);
        y += 22;
        bk_gui_font_draw_string(surface, x, y, "Ubicacion:", 0x00101010, 0, false);
        bk_gui_font_draw_string_clipped(surface, x + 82, y, p->path,
                                     0x00101010, page);
        y += 22;
        snprintf(value, sizeof(value), "%u bytes", p->entry.size);
        bk_gui_font_draw_string(surface, x, y, "Tamano:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 82, y, value, 0x00101010, 0, false);
    } else {
        snprintf(value, sizeof(value), "0x%02x", p->entry.attributes);
        bk_gui_font_draw_string(surface, x, y, "Atributos FAT:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 105, y, value, 0x00101010, 0, false);
        y += 22;
        bk_gui_font_draw_string(surface, x, y, "Volumen:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 105, y, p->mount, 0x00101010, 0, false);
        y += 22;
        bk_gui_font_draw_string(surface, x, y, "Solo lectura:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 105, y,
            (p->entry.attributes & FAT_ATTR_READ_ONLY) ? "Si" : "No",
            0x00101010, 0, false);
        y += 22;
        bk_gui_font_draw_string(surface, x, y, "Oculto:", 0x00101010, 0, false);
        bk_gui_font_draw_string(surface, x + 105, y,
            (p->entry.attributes & FAT_ATTR_HIDDEN) ? "Si" : "No",
            0x00101010, 0, false);
    }
}

static bool fb_properties_event(gui_window_t *window,
                                const gui_event_t *event, void *context) {
    fb_properties_t *p = (fb_properties_t *)context;
    gui_rect_t c;
    if (!p || !window || !event || event->type != GUI_EVENT_MOUSE_DOWN ||
        event->button != MOUSE_LEFT_BUTTON) return false;
    c = bk_gui_window_content_rect_raw(window);
    if (bk_gui_rect_contains((gui_rect_t){c.x + 10, c.y + 6, 82, 22},
                          event->x, event->y)) p->active_tab = 0;
    else if (bk_gui_rect_contains((gui_rect_t){c.x + 92, c.y + 6, 82, 22},
                               event->x, event->y)) p->active_tab = 1;
    else return false;
    window->dirty = true;
    return true;
}

static void fb_open_properties(fb_state_t *st) {
    fb_properties_t *p;
    gui_window_t *window;
    char title[48];
    const char *mount;
    if (!st || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count) return;
    p = (fb_properties_t *)bk_sys_alloc_zero(sizeof(*p));
    if (!p) return;
    p->entry = st->entries[st->selected];
    p->drive = st->drive_view ? st->entry_drive[st->selected] : 0;
    if (st->drive_view) bk_runtime_strcpy(p->path, "Este equipo");
    else fb_join_path(p->path, sizeof(p->path), st->cwd, p->entry.name);
    mount = bk_device_mount_name();
    bk_runtime_strncpy(p->mount, mount ? mount : "Sin montar", sizeof(p->mount) - 1);
    bk_runtime_strcpy(title, "Propiedades: ");
    bk_runtime_strncpy(title + bk_runtime_strlen(title), p->entry.name,
             sizeof(title) - bk_runtime_strlen(title) - 1);
    window = bk_gui_create_window(st->desktop, 118, 72, 390, 250, title);
    if (!window) { bk_sys_free(p); return; }
    bk_gui_set_window_content(window, fb_properties_paint, p);
    bk_gui_set_window_event_handler(window, fb_properties_event, p);
    window->owner_pid = bk_sys_getpid();
    bk_gui_request_paint();
}

/* ══════════════════════════════════════════════════
 *  Callback botón "Subir"
 * ══════════════════════════════════════════════════ */

static void fb_btn_up_cb(gui_window_t *window UNUSED, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st) return;
    if (!st->drive_view && st->cwd[0] == '/' && st->cwd[1] == '\0') {
        st->drive_view = true;
        fb_load_dir(st);
        fb_update_title(st);
    } else if (!st->drive_view) {
        fb_navigate(st, "..");
    }
}

static bool fb_copy_tree(const char *source, const char *destination,
                         bool directory, uint8_t depth) {
    if (depth > 16) return false;
    if (!directory) {
        void *data = NULL;
        uint32_t size = 0;
        bool ok = bk_file_read_all(source, &data, &size) &&
                  bk_file_write_all(destination, data, size);
        if (data) bk_sys_free(data);
        return ok;
    }
    if (!bk_file_mkdir(destination)) return false;
    {
        vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
        uint32_t count = 0;
        if (!bk_file_list_dir(source, entries, VFS_MAX_DIR_ENTRIES, &count))
            return false;
        for (uint32_t i = 0; i < count; i++) {
            char child_source[VFS_MAX_PATH], child_destination[VFS_MAX_PATH];
            if (bk_runtime_strcmp(entries[i].name, ".") == 0 ||
                bk_runtime_strcmp(entries[i].name, "..") == 0) continue;
            fb_join_path(child_source, sizeof(child_source), source,
                         entries[i].name);
            fb_join_path(child_destination, sizeof(child_destination),
                         destination, entries[i].name);
            if (!fb_copy_tree(child_source, child_destination,
                              entries[i].type == VFS_NODE_DIR,
                              (uint8_t)(depth + 1))) return false;
        }
    }
    return true;
}

static bool fb_remove_tree(const char *path, bool directory, uint8_t depth) {
    if (depth > 16) return false;
    if (directory) {
        vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
        uint32_t count = 0;
        if (!bk_file_list_dir(path, entries, VFS_MAX_DIR_ENTRIES, &count))
            return false;
        for (uint32_t i = 0; i < count; i++) {
            char child[VFS_MAX_PATH];
            if (bk_runtime_strcmp(entries[i].name, ".") == 0 ||
                bk_runtime_strcmp(entries[i].name, "..") == 0) continue;
            fb_join_path(child, sizeof(child), path, entries[i].name);
            if (!fb_remove_tree(child, entries[i].type == VFS_NODE_DIR,
                                (uint8_t)(depth + 1))) return false;
        }
    }
    return bk_file_remove(path);
}

static void fb_delete_dialog_paint(gui_window_t *window,
                                   gui_surface_t *surface, void *context) {
    fb_delete_dialog_t *dialog = (fb_delete_dialog_t *)context;
    gui_rect_t c;
    gui_rect_t message;
    if (!window || !surface || !dialog) return;
    c = bk_gui_window_content_rect_raw(window);
    bk_gui_gfx_fill_rect(surface, c, 0x00D4D0C8);

    if (dialog->warning_icon)
        bk_app_draw_icon(surface, c.x + 18, c.y + 20,
                                 dialog->warning_icon, 44, 44);
    else {
        /* Fallback por si la imagen no está presente en una instalación vieja. */
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){c.x + 18, c.y + 20, 44, 44},
                          0x00E5B52A);
        bk_gui_gfx_draw_rect(surface, (gui_rect_t){c.x + 18, c.y + 20, 44, 44},
                          0x00605010);
        bk_gui_font_draw_string_scaled(surface, c.x + 34, c.y + 29, "!",
                                    0x00202020, 2);
    }

    message = (gui_rect_t){c.x + 76, c.y + 15, c.w - 92, 76};
    bk_gui_font_draw_string_clipped(surface, message.x, message.y + 2,
        "Desea eliminar este elemento?", 0x00101010, message);
    bk_gui_font_draw_string_clipped(surface, message.x, message.y + 24,
        dialog->name, 0x00004080, message);
    bk_gui_font_draw_string_clipped(surface, message.x, message.y + 46,
        dialog->directory ? "Se eliminara tambien todo su contenido."
                          : "Esta accion no se puede deshacer.",
        0x00505050, message);

    bk_gui_gfx_draw_line(surface, c.x + 12, c.y + 99,
                      c.x + c.w - 12, c.y + 99, 0x00808080);
}

static void fb_delete_dialog_button(gui_window_t *window, uint32_t id) {
    fb_delete_dialog_t *dialog = window
        ? (fb_delete_dialog_t *)window->content_context : NULL;
    if (!dialog) return;
    dialog->action = id == dialog->delete_button_id ? 1U : 2U;
    bk_gui_close_window(window);
}

static void fb_delete_dialog_task(void *argument) {
    fb_delete_dialog_t *dialog = (fb_delete_dialog_t *)argument;
    if (!dialog || !dialog->desktop) {
        if (dialog) bk_sys_free(dialog);
        bk_proc_exit();
    }
    dialog->warning_icon = bk_app_load_icon(
        "/ICONS/WARNING.BMP", 44, 44);
    dialog->window = bk_gui_create_window(dialog->desktop, 142, 88,
        410, 180, "Confirmar eliminacion");
    if (dialog->window) {
        gui_widget_t *button;
        bk_gui_set_window_content(dialog->window, fb_delete_dialog_paint, dialog);
        bk_gui_set_window_min_size(dialog->window, 350, 170);
        dialog->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(dialog->window);
        button = bk_gui_widget_create(dialog->desktop, dialog->window,
            GUI_WIDGET_BUTTON, (gui_rect_t){214, 116, 82, 24},
            "Eliminar", fb_delete_dialog_button);
        if (button) dialog->delete_button_id = button->id;
        button = bk_gui_widget_create(dialog->desktop, dialog->window,
            GUI_WIDGET_BUTTON, (gui_rect_t){304, 116, 82, 24},
            "Cancelar", fb_delete_dialog_button);
        if (button) dialog->cancel_button_id = button->id;
        bk_gui_desktop_raise_window(dialog->desktop, dialog->window);
        bk_gui_request_paint();
    }

    while (!bk_proc_exit_requested() && dialog->window &&
           dialog->window->listed && !dialog->action)
        bk_sys_sleep_ticks(2);

    if (dialog->action == 1U && g_fb == dialog->owner &&
        dialog->owner->delete_dialog_open &&
        dialog->owner->delete_dialog_token == dialog->owner_token) {
        bool removed = fb_remove_tree(dialog->path, dialog->directory, 0);
        bk_runtime_strcpy(dialog->owner->status, removed ? "Elemento eliminado" :
                                                "No se pudo eliminar");
        fb_load_dir(dialog->owner);
        if (dialog->owner->window) dialog->owner->window->dirty = true;
    }
    if (g_fb == dialog->owner &&
        dialog->owner->delete_dialog_token == dialog->owner_token)
        dialog->owner->delete_dialog_open = false;
    if (dialog->window) {
        bk_gui_desktop_remove_window(dialog->desktop, dialog->window);
        bk_gui_window_destroy_raw(dialog->window);
        bk_proc_bind_window(NULL);
    }
    if (dialog->warning_icon) bk_sys_free(dialog->warning_icon);
    bk_sys_free(dialog);
    bk_proc_exit();
}

static void fb_open_delete_dialog(fb_state_t *st) {
    fb_delete_dialog_t *dialog;
    vfs_dir_entry_t *entry;
    if (!st || st->drive_view || st->delete_dialog_open || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count) return;
    entry = &st->entries[st->selected];
    if (bk_runtime_strcmp(entry->name, "..") == 0) return;
    dialog = (fb_delete_dialog_t *)bk_sys_alloc_zero(sizeof(*dialog));
    if (!dialog) {
        bk_runtime_strcpy(st->status, "Sin memoria para confirmar");
        return;
    }
    dialog->owner = st;
    dialog->desktop = st->desktop;
    dialog->directory = entry->type == VFS_NODE_DIR;
    bk_runtime_strncpy(dialog->name, entry->name, sizeof(dialog->name) - 1);
    fb_join_path(dialog->path, sizeof(dialog->path), st->cwd, entry->name);
    st->delete_dialog_open = true;
    st->delete_dialog_token = ++g_fb_delete_token;
    if (!st->delete_dialog_token) st->delete_dialog_token = ++g_fb_delete_token;
    dialog->owner_token = st->delete_dialog_token;
    if (bk_proc_spawn_thread("delete-confirm", fb_delete_dialog_task, dialog) < 0) {
        st->delete_dialog_open = false;
        bk_runtime_strcpy(st->status, "No se pudo abrir la confirmacion");
        bk_sys_free(dialog);
    }
}

static void fb_begin_dialog(fb_state_t *st, uint8_t dialog) {
    if (!st || st->drive_view) return;
    st->dialog = dialog;
    st->dialog_input[0] = '\0';
    if (dialog == FB_DIALOG_RENAME && st->selected >= 0 &&
        (uint32_t)st->selected < st->entry_count &&
        bk_runtime_strcmp(st->entries[st->selected].name, "..") != 0)
        bk_runtime_strncpy(st->dialog_input, st->entries[st->selected].name,
                 sizeof(st->dialog_input) - 1);
    if (st->window) st->window->dirty = true;
}

static void fb_finish_dialog(fb_state_t *st) {
    char old_path[VFS_MAX_PATH], new_path[VFS_MAX_PATH];
    if (!st || !st->dialog) return;
    if (st->dialog == FB_DIALOG_NEW_FOLDER) {
        fb_join_path(new_path, sizeof(new_path), st->cwd, st->dialog_input);
        bk_runtime_strcpy(st->status, st->dialog_input[0] && bk_file_mkdir(new_path)
            ? "Carpeta creada" : "No se pudo crear la carpeta");
    } else if (st->selected >= 0 &&
               (uint32_t)st->selected < st->entry_count) {
        vfs_dir_entry_t *entry = &st->entries[st->selected];
        fb_join_path(old_path, sizeof(old_path), st->cwd, entry->name);
        if (st->dialog == FB_DIALOG_RENAME) {
            fb_join_path(new_path, sizeof(new_path), st->cwd,
                         st->dialog_input);
            bk_runtime_strcpy(st->status, st->dialog_input[0] &&
                    bk_file_rename(old_path, new_path)
                    ? "Elemento renombrado" : "No se pudo renombrar");
        }
    }
    st->dialog = FB_DIALOG_NONE;
    fb_load_dir(st);
    if (st->window) st->window->dirty = true;
}

static void fb_btn_copy_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st || st->drive_view || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count) return;
    if (bk_runtime_strcmp(st->entries[st->selected].name, "..") == 0) return;
    fb_join_path(g_fb_clipboard, sizeof(g_fb_clipboard), st->cwd,
                 st->entries[st->selected].name);
    g_fb_clipboard_cut = false;
    g_fb_clipboard_directory =
        st->entries[st->selected].type == VFS_NODE_DIR;
    bk_runtime_strcpy(st->status, "Copiado al portapapeles");
    window->dirty = true;
}

static void fb_btn_cut_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st || st->drive_view || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count ||
        bk_runtime_strcmp(st->entries[st->selected].name, "..") == 0) return;
    fb_join_path(g_fb_clipboard, sizeof(g_fb_clipboard), st->cwd,
                 st->entries[st->selected].name);
    g_fb_clipboard_cut = true;
    g_fb_clipboard_directory =
        st->entries[st->selected].type == VFS_NODE_DIR;
    bk_runtime_strcpy(st->status, "Cortado al portapapeles");
    window->dirty = true;
}

static void fb_btn_paste_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    char destination[VFS_MAX_PATH];
    const char *name;
    if (!st || st->drive_view || !g_fb_clipboard[0]) return;
    name = g_fb_clipboard;
    for (const char *p = g_fb_clipboard; *p; p++) if (*p == '/') name = p + 1;
    fb_join_path(destination, sizeof(destination), st->cwd, name);
    if (bk_runtime_strcmp(destination, g_fb_clipboard) == 0) {
        bk_runtime_strcpy(st->status, "Origen y destino son iguales");
    } else if (g_fb_clipboard_directory &&
               bk_runtime_strncmp(destination, g_fb_clipboard,
                        bk_runtime_strlen(g_fb_clipboard)) == 0 &&
               destination[bk_runtime_strlen(g_fb_clipboard)] == '/') {
        bk_runtime_strcpy(st->status, "No se puede pegar dentro del origen");
    } else if (fb_copy_tree(g_fb_clipboard, destination,
                            g_fb_clipboard_directory, 0)) {
        if (g_fb_clipboard_cut &&
            !fb_remove_tree(g_fb_clipboard, g_fb_clipboard_directory, 0)) {
            bk_runtime_strcpy(st->status, "Copiado; no se pudo quitar el origen");
        } else {
            bk_runtime_strcpy(st->status, g_fb_clipboard_cut ? "Elemento movido" :
                                                    "Elemento pegado");
            if (g_fb_clipboard_cut) g_fb_clipboard[0] = '\0';
        }
        fb_load_dir(st);
    } else bk_runtime_strcpy(st->status, "No se pudo pegar");
    window->dirty = true;
}

static void fb_btn_new_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st || st->drive_view) return;
    fb_begin_dialog(st, FB_DIALOG_NEW_FOLDER);
}

static void fb_btn_refresh_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st) return;
    fb_load_dir(st);
    bk_runtime_strcpy(st->status, "Actualizado");
    window->dirty = true;
}

static void fb_context_cb(gui_window_t *window, uint32_t item_id,
                          void *context) {
    fb_state_t *st = (fb_state_t *)context;
    if (!st || !window) return;
    switch (item_id) {
        case FB_CONTEXT_OPEN: (void)fb_open_selected(st); break;
        case FB_CONTEXT_COPY: fb_btn_copy_cb(window, 0); break;
        case FB_CONTEXT_CUT: fb_btn_cut_cb(window, 0); break;
        case FB_CONTEXT_PASTE: fb_btn_paste_cb(window, 0); break;
        case FB_CONTEXT_RENAME: fb_begin_dialog(st, FB_DIALOG_RENAME); break;
        case FB_CONTEXT_DELETE: fb_open_delete_dialog(st); break;
        case FB_CONTEXT_PROPERTIES: fb_open_properties(st); break;
        case FB_CONTEXT_NEW_FOLDER: fb_btn_new_cb(window, 0); break;
        case FB_CONTEXT_REFRESH: fb_btn_refresh_cb(window, 0); break;
        default: break;
    }
}

static void fb_open_context_menu(fb_state_t *st, int x, int y, int hit) {
    bool has_entry;
    bool can_copy;
    if (!st || !st->window) return;
    has_entry = hit >= 0 && (uint32_t)hit < st->entry_count;
    if (has_entry) st->selected = hit;
    can_copy = has_entry && !st->drive_view &&
               bk_runtime_strcmp(st->entries[hit].name, "..") != 0;
    bk_gui_window_context_clear(st->window);
    if (has_entry) {
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_OPEN,
            "Abrir", true, fb_context_cb, st);
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_COPY,
            "Copiar", can_copy, fb_context_cb, st);
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_CUT,
            "Cortar", can_copy, fb_context_cb, st);
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_RENAME,
            "Renombrar", can_copy, fb_context_cb, st);
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_DELETE,
            "Eliminar", can_copy, fb_context_cb, st);
        (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_PROPERTIES,
            "Propiedades", true, fb_context_cb, st);
    }
    (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_PASTE,
        "Pegar", !st->drive_view && g_fb_clipboard[0], fb_context_cb, st);
    (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_NEW_FOLDER,
        "Nueva carpeta", !st->drive_view, fb_context_cb, st);
    (void)bk_gui_window_context_add_item(st->window, FB_CONTEXT_REFRESH,
        "Actualizar", true, fb_context_cb, st);
    bk_gui_window_context_open(st->window, x, y);
}

/* ══════════════════════════════════════════════════
 *  Pintar lista dentro de la ventana
 * ══════════════════════════════════════════════════ */

static void fb_paint_list(gui_surface_t *surface, fb_state_t *st) {
    gui_rect_t list;
    gui_scrollbar_t scrollbar;
    int cols;
    int visible;

    if (!st->window) return;
    fb_clamp_scroll(st);
    list = fb_list_rect(st);
    if (list.w <= GUI_SCROLLBAR_SIZE || list.h <= 0) return;
    bk_gui_gfx_fill_rect(surface, list, FB_COLOR_BG);
    bk_gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                     GUI_SCROLLBAR_SIZE, list.h},
        (uint32_t)st->scroll, (uint32_t)fb_visible_slots(st), st->entry_count);

    cols = fb_cols_for_rect(list);
    visible = fb_visible_slots(st);
    for (int i = 0; i < visible; i++) {
        int idx = st->scroll + i;
        if ((uint32_t)idx >= st->entry_count) break;
        vfs_dir_entry_t *e = &st->entries[idx];
        int ex = list.x + (i % cols) * FB_TILE_W;
        int ey = list.y + (i / cols) *
                 (st->list_view ? FB_LIST_ROW_H : FB_TILE_H);
        bool sel = (idx == st->selected);
        if (st->list_view) {
            if (sel)
                bk_gui_gfx_fill_rect(surface, (gui_rect_t){ex, ey,
                    list.w - GUI_SCROLLBAR_SIZE, FB_LIST_ROW_H - 1},
                    FB_COLOR_SEL);
            fb_draw_small_icon(surface, ex + 2, ey + 2,
                               fb_icon_for_entry(st, e, (uint32_t)idx));
            bk_gui_font_draw_string_clipped(surface, ex + 26, ey + 8, e->name,
                sel ? FB_COLOR_TEXTSEL : FB_COLOR_TEXT,
                (gui_rect_t){ex + 24, ey, list.w - 116, FB_LIST_ROW_H});
            if (e->type == VFS_NODE_FILE) {
                char size_text[24];
                snprintf(size_text, sizeof(size_text), "%u bytes", e->size);
                bk_gui_font_draw_string_clipped(surface,
                    list.x + list.w - 102, ey + 8, size_text,
                    sel ? FB_COLOR_TEXTSEL : 0x00505050,
                    (gui_rect_t){list.x + list.w - 106, ey, 88,
                                 FB_LIST_ROW_H});
            }
            continue;
        }
        if (sel)
            bk_gui_gfx_fill_rect(surface, (gui_rect_t){ex, ey, FB_TILE_W - 4,
                                                    FB_TILE_H - 3},
                              FB_COLOR_SEL);
        fb_draw_bmp_icon(surface, ex + (FB_TILE_W - FB_ICON_SIZE) / 2, ey + 5,
                         fb_icon_for_entry(st, e, (uint32_t)idx));
        fb_draw_tile_name(surface, ex + 4, ey + 58, e->name,
                          sel ? FB_COLOR_TEXTSEL : FB_COLOR_TEXT,
                          (gui_rect_t){ex + 3, ey + 56, FB_TILE_W - 10, 26});
    }
    bk_gui_scrollbar_paint_vertical(surface, &scrollbar);
}

/* ══════════════════════════════════════════════════
 *  Hit-test en la lista
 * ══════════════════════════════════════════════════ */

static int fb_hit_entry(fb_state_t *st, int ex, int ey) {
    gui_rect_t list;
    int cols;
    int col;
    int row;
    int idx;

    if (!st->window) return -1;
    list = fb_list_rect(st);
    if (list.w <= GUI_SCROLLBAR_SIZE || list.h <= 0) return -1;

    if (ex < list.x || ex >= list.x + list.w - GUI_SCROLLBAR_SIZE) return -1;
    if (ey < list.y || ey >= list.y + list.h) return -1;

    cols = fb_cols_for_rect(list);
    col = st->list_view ? 0 : (ex - list.x) / FB_TILE_W;
    row = (ey - list.y) / (st->list_view ? FB_LIST_ROW_H : FB_TILE_H);
    if (col < 0 || col >= cols) return -1;
    idx = st->scroll + row * cols + col;
    if ((uint32_t)idx >= st->entry_count) return -1;
    return idx;
}

/* ══════════════════════════════════════════════════
 *  Paint del programa
 * ══════════════════════════════════════════════════ */

static void fb_content(gui_window_t *window UNUSED,
                       gui_surface_t *surface, void *context) {
    fb_state_t *st = (fb_state_t *)context;
    if (st) {
        gui_rect_t content = bk_gui_window_content_rect_raw(st->window);
        int x = content.x + 33;
        int y = content.y + 4;
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){x, y, st->window->bounds.w - 42, 20},
                          st->editing_address ? 0x00FFF4C8 : 0x00FFFFFF);
        bk_gui_gfx_draw_rect(surface, (gui_rect_t){x, y, st->window->bounds.w - 42, 20},
                          0x00606060);
        bk_gui_font_draw_string_clipped(surface, x + 4, y + 6,
            st->editing_address ? st->address : st->cwd, 0x00101010,
            (gui_rect_t){x + 3, y + 2, st->window->bounds.w - 48, 16});
        fb_paint_list(surface, st);
        if (st->status[0]) {
            gui_rect_t list = fb_list_rect(st);
            gui_rect_t status = {list.x, list.y + list.h + 2, list.w, 16};
            bk_gui_gfx_fill_rect(surface, status, 0x00E8E8E8);
            bk_gui_gfx_draw_rect(surface, status, 0x00808080);
            bk_gui_font_draw_string_clipped(surface, status.x + 4,
                                         status.y + 4, st->status,
                                         0x00101010, status);
        }
        if (st->dialog) {
            gui_rect_t c = bk_gui_window_content_rect_raw(st->window);
            gui_rect_t box = {c.x + (c.w - 300) / 2,
                              c.y + (c.h - 105) / 2, 300, 105};
            const char *caption = st->dialog == FB_DIALOG_NEW_FOLDER
                ? "Crear carpeta" : "Renombrar";
            bk_gui_gfx_fill_rect(surface, box, 0x00D0D0C8);
            bk_gui_gfx_draw_rect(surface, box, 0x00303030);
            bk_gui_font_draw_string(surface, box.x + 10, box.y + 12, caption,
                                 0x00101010, 0, false);
            bk_gui_gfx_fill_rect(surface,
                (gui_rect_t){box.x + 10, box.y + 35, box.w - 20, 22},
                0x00FFFFFF);
            bk_gui_gfx_draw_rect(surface,
                (gui_rect_t){box.x + 10, box.y + 35, box.w - 20, 22},
                0x00606060);
            bk_gui_font_draw_string_clipped(surface, box.x + 14, box.y + 42,
                st->dialog_input, 0x00101010,
                (gui_rect_t){box.x + 12, box.y + 37, box.w - 24, 18});
            bk_gui_font_draw_string(surface, box.x + 10, box.y + 78,
                                 "Enter = Aceptar   Esc = Cancelar",
                                 0x00303030, 0, false);
        }
    }
}

static void fb_menu_cb(gui_window_t *window, uint32_t item_id,
                       void *context) {
    fb_state_t *st = (fb_state_t *)context;
    if (!st || !window) return;
    switch (item_id) {
        case FB_MENU_OPEN: (void)fb_open_selected(st); break;
        case FB_MENU_NEW_FOLDER: fb_begin_dialog(st, FB_DIALOG_NEW_FOLDER); break;
        case FB_MENU_RENAME: fb_begin_dialog(st, FB_DIALOG_RENAME); break;
        case FB_MENU_DELETE: fb_open_delete_dialog(st); break;
        case FB_MENU_PROPERTIES: fb_open_properties(st); break;
        case FB_MENU_COPY: fb_btn_copy_cb(window, 0); break;
        case FB_MENU_CUT: fb_btn_cut_cb(window, 0); break;
        case FB_MENU_PASTE: fb_btn_paste_cb(window, 0); break;
        case FB_MENU_VIEW_ICONS: st->list_view = false; st->scroll = 0; break;
        case FB_MENU_VIEW_LIST: st->list_view = true; st->scroll = 0; break;
        case FB_MENU_REFRESH: fb_btn_refresh_cb(window, 0); break;
        case FB_MENU_ABOUT:
            bk_about_show(st->desktop, &(bk_about_info_t){
                "Archivos", "Version 1.0",
                "Administrador de archivos de BlesKernOS.",
                "Bles.INC (C) 2026", "/ICONS/FILES.BMP"});
            break;
        default: return;
    }
    window->dirty = true;
}

/* ══════════════════════════════════════════════════
 *  Handle events del programa
 * ══════════════════════════════════════════════════ */

static bool fb_window_event(gui_window_t *window UNUSED,
                            const gui_event_t *event, void *context) {
    fb_state_t *st = (fb_state_t *)context;
    if (!st || !event) return false;

    /* Solo nos interesan eventos dentro de la ventana abierta */
    if (!st->window) return false;

    if (st->dialog && event->type == GUI_EVENT_KEY) {
        size_t len = bk_runtime_strlen(st->dialog_input);
        if (event->key == KEY_ESCAPE) {
            st->dialog = FB_DIALOG_NONE;
        } else if (event->key == KEY_ENTER || event->key == '\n') {
            fb_finish_dialog(st);
        } else if (event->key == KEY_BACKSPACE) {
            if (len) st->dialog_input[len - 1] = '\0';
        } else if (event->key >= 32 &&
                   event->key < 127 && len + 1 < sizeof(st->dialog_input) &&
                   event->key != '/' && event->key != '\\') {
            st->dialog_input[len] = (char)event->key;
            st->dialog_input[len + 1] = '\0';
        }
        st->window->dirty = true;
        return true;
    }

    {
        gui_rect_t list = fb_list_rect(st);
        gui_scrollbar_t scrollbar;
        uint32_t new_scroll;
        bk_gui_scrollbar_init_vertical(&scrollbar,
            (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                         GUI_SCROLLBAR_SIZE, list.h},
            (uint32_t)st->scroll, (uint32_t)fb_visible_slots(st),
            st->entry_count);
        if ((st->scrollbar_drag.active ||
             bk_gui_rect_contains(list, event->x, event->y)) &&
            bk_gui_scrollbar_handle_event_vertical(&scrollbar,
                &st->scrollbar_drag, event,
                (uint32_t)fb_cols_for_rect(list), &new_scroll)) {
            st->scroll = (int)new_scroll;
            fb_clamp_scroll(st);
            st->window->dirty = true;
            return true;
        }
    }

    if (event->type == GUI_EVENT_KEY) {
        if (st->editing_address) {
            size_t len = bk_runtime_strlen(st->address);
            if (event->key == '\n') {
                if (bk_file_chdir(st->address)) {
                    bk_runtime_strncpy(st->cwd, bk_file_getcwd(), sizeof(st->cwd) - 1);
                    st->drive_view = false;
                    fb_load_dir(st);
                    fb_update_title(st);
                    st->status[0] = '\0';
                } else bk_runtime_strcpy(st->status, "Ruta no encontrada");
                st->editing_address = false;
            } else if (event->key == 27) {
                st->editing_address = false;
            } else if (event->key == '\b') {
                if (len) st->address[len - 1] = '\0';
            } else if (event->key >= 32 && event->key < 127 &&
                       len + 1 < sizeof(st->address)) {
                st->address[len] = event->key;
                st->address[len + 1] = '\0';
            }
            st->window->dirty = true;
            return true;
        }
        if (event->key == '\n' && st->selected >= 0 &&
            (uint32_t)st->selected < st->entry_count) {
            (void)fb_open_selected(st);
            return true;
        }
        if (event->key == 'c' || event->key == 'C') {
            fb_btn_copy_cb(st->window, 0); return true;
        }
        if (event->key == 'v' || event->key == 'V') {
            fb_btn_paste_cb(st->window, 0); return true;
        }
        if ((uint8_t)event->key == KEY_F2) {
            fb_begin_dialog(st, FB_DIALOG_RENAME); return true;
        }
        if ((uint8_t)event->key == KEY_DELETE) {
            fb_open_delete_dialog(st); return true;
        }
        if ((uint8_t)event->key == KEY_PGUP) {
            st->scroll -= fb_visible_slots(st);
            fb_clamp_scroll(st);
            st->window->dirty = true;
            return true;
        }
        if ((uint8_t)event->key == KEY_PGDN) {
            st->scroll += fb_visible_slots(st);
            fb_clamp_scroll(st);
            st->window->dirty = true;
            return true;
        }
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        gui_rect_t address_rect = {
            bk_gui_window_content_rect_raw(st->window).x + 33,
            bk_gui_window_content_rect_raw(st->window).y + 4,
            st->window->bounds.w - 42, 20
        };
        int hit = fb_hit_entry(st, event->x, event->y);
        if (event->button == MOUSE_RIGHT_BUTTON) {
            fb_open_context_menu(st, event->x, event->y, hit);
            st->window->dirty = true;
            return true;
        }
        if (!st->drive_view &&
            bk_gui_rect_contains(address_rect, event->x, event->y)) {
            st->editing_address = true;
            bk_runtime_strncpy(st->address, st->cwd, sizeof(st->address) - 1);
            bk_runtime_strcpy(st->status, "Escribe una ruta y pulsa Enter");
            st->window->dirty = true;
            return true;
        }
        if (hit >= 0) {
            st->selected = hit;
            st->window->dirty = true;
            return true;
        }
        return false;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        int hit = fb_hit_entry(st, event->x, event->y);
        if (hit >= 0 && hit == st->selected) {
            uint32_t now   = bk_sys_ticks();
            uint32_t delta = now - st->last_list_click_tick;

            if (delta < FB_DBLCLICK_TICKS &&
                hit == st->last_list_click_idx &&
                st->last_list_click_tick != 0) {
                /* Doble clic en entrada → navegar si es directorio */
                (void)fb_open_selected(st);
                st->last_list_click_tick = 0;
            } else {
                st->last_list_click_tick = now;
                st->last_list_click_idx  = hit;
            }
            st->window->dirty = true;
            return true;
        }
        return false;
    }

    return false;
}

/* ══════════════════════════════════════════════════
 *  Destructor
 * ══════════════════════════════════════════════════ */

static void fb_cleanup(fb_state_t *st) {
    if (!st) return;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }

    /*
     * Los iconos y Associations.INI viven en g_fb_icon_cache.
     * No se liberan por ventana: así el filebrowser no vuelve a leer
     * ICONS.PAK/BMPs cada vez que se abre.
     */
    if (g_fb == st) g_fb = NULL;
    bk_sys_free(st);
}

void filebrowser_open_path(gui_desktop_t *desktop, const char *path) {
    fb_state_t *st;
    if (!desktop) return;

    st = (fb_state_t *)bk_sys_alloc_zero(sizeof(fb_state_t));
    if (!st) return;
    st->desktop = desktop;
    st->selected = -1;
    st->drive_view = !path || !path[0] || bk_runtime_strcmp(path, "/") == 0;
    bk_runtime_strncpy(st->cwd, path && path[0] ? path : bk_file_getcwd(),
             sizeof(st->cwd) - 1);
    st->cwd[sizeof(st->cwd) - 1] = '\0';
    g_fb = st;
    if (bk_proc_spawn_thread("filebrowser", fb_main, st) < 0) {
        fb_cleanup(st);
    }
}

void filebrowser_open_from_desktop(gui_desktop_t *desktop) {
    filebrowser_open_path(desktop, "/");
}

/* ══════════════════════════════════════════════════
 *  Instalación (llamar una vez al arrancar la GUI)
 * ══════════════════════════════════════════════════ */

bool filebrowser_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_fb) return false;
    info->window = g_fb->window;
    info->memory_bytes = (uint32_t)sizeof(*g_fb);
    if (g_fb->folder_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->file_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->text_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->config_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->image_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->object_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->midi_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->hdd_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->cd_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->usb_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->floppy_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->shell_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->editor_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->calc_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->files_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->midamp_icon) info->memory_bytes += FB_ICON_BYTES;
    if (g_fb->window) {
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
        info->memory_bytes += (uint32_t)sizeof(gui_widget_t);
    }
    return true;
}

static void fb_main(void *argument) {
    fb_state_t *st = (fb_state_t *)argument;
    if (!st || !st->desktop) {
        fb_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st) + FB_ICON_BYTES * 16);
    /*
     * Como app externa puede ceder CPU durante la carga de iconos y el
     * escritorio llegar a pintar una primera vez con la lista vacia.
     * Dejamos el estado listo antes de crear la ventana.
     */
    fb_load_resources(st);
    fb_load_dir(st);
    st->window = bk_gui_create_window(st->desktop,
        FB_WIN_X, FB_WIN_Y, FB_WIN_W, FB_WIN_H, "Archivos");
    if (st->window) {
        int file_menu = bk_gui_add_menu(st->window, "File");
        int edit_menu = bk_gui_add_menu(st->window, "Edit");
        int view_menu = bk_gui_add_menu(st->window, "View");
        int help_menu = bk_gui_add_menu(st->window, "Help");
        bk_gui_add_menu_item(st->window, file_menu, FB_MENU_OPEN,
                                 "Abrir", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, file_menu, FB_MENU_NEW_FOLDER,
                                 "Nueva carpeta", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, file_menu, FB_MENU_RENAME,
                                 "Renombrar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, file_menu, FB_MENU_DELETE,
                                 "Eliminar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, file_menu, FB_MENU_PROPERTIES,
                                 "Propiedades", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, edit_menu, FB_MENU_COPY,
                                 "Copiar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, edit_menu, FB_MENU_CUT,
                                 "Cortar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, edit_menu, FB_MENU_PASTE,
                                 "Pegar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, view_menu, FB_MENU_VIEW_ICONS,
                                 "Iconos", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, view_menu, FB_MENU_VIEW_LIST,
                                 "Lista", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, view_menu, FB_MENU_REFRESH,
                                 "Actualizar", fb_menu_cb, st);
        bk_gui_add_menu_item(st->window, help_menu, FB_MENU_ABOUT,
                                 "Acerca de Archivos", fb_menu_cb, st);
        bk_gui_set_window_content(st->window, fb_content, st);
        bk_gui_set_window_event_handler(st->window, fb_window_event, st);
        bk_gui_set_window_min_size(st->window, 220, 150);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);

        gui_widget_t *btn = bk_gui_widget_create(st->desktop, st->window,
            GUI_WIDGET_BUTTON,
            (gui_rect_t){FB_LIST_X, FB_LIST_Y, 25, 20},
            "^", fb_btn_up_cb);
        if (btn) st->btn_up_id = btn->id;
        btn = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){6, 28, 45, 18}, "Copiar", fb_btn_copy_cb);
        if (btn) st->btn_copy_id = btn->id;
        btn = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){55, 28, 42, 18}, "Pegar", fb_btn_paste_cb);
        if (btn) st->btn_paste_id = btn->id;
        btn = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){101, 28, 48, 18}, "Carpeta", fb_btn_new_cb);
        if (btn) st->btn_new_id = btn->id;
        btn = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){153, 28, 58, 18}, "Refrescar", fb_btn_refresh_cb);
        if (btn) st->btn_refresh_id = btn->id;
        fb_update_title(st);
        st->window->dirty = true;
        bk_gui_request_paint();
    }

    while (!bk_proc_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        bk_sys_sleep_ticks(4);
    }

    fb_cleanup(st);
    bk_proc_exit();
}

void filebrowser_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    fb_state_t *st;
    const char *path = bk_app_launch_argument();

    if (!desktop) {
        bk_proc_exit();
    }

    st = (fb_state_t *)bk_sys_alloc_zero(sizeof(fb_state_t));
    if (!st) {
        bk_proc_exit();
    }

    st->desktop = desktop;
    st->selected = -1;
    st->drive_view = !path || !path[0] || bk_runtime_strcmp(path, "/") == 0;
    bk_runtime_strncpy(st->cwd, path && path[0] ? path : bk_file_getcwd(),
             sizeof(st->cwd) - 1);
    st->cwd[sizeof(st->cwd) - 1] = '\0';
    g_fb = st;

    /*
     * Como ELF externo, el navegador debe vivir en esta misma tarea.
     * Si abre una segunda tarea y la principal termina, la ventana no llega
     * a mantenerse estable.
     */
    fb_main(st);
}

bool filebrowser_global_copy(void) {
    if (!g_fb || !g_fb->window || !g_fb->window->focused) return false;
    fb_btn_copy_cb(g_fb->window, 0);
    return true;
}

bool filebrowser_global_paste(void) {
    if (!g_fb || !g_fb->window || !g_fb->window->focused) return false;
    fb_btn_paste_cb(g_fb->window, 0);
    return true;
}
