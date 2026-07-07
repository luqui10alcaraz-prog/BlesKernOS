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

#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/block.h"
#include "../kernel/include/iso9660.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/elf_loader.h"

/* ── Ventana ── */
#define FB_WIN_X    60
#define FB_WIN_Y    40
#define FB_WIN_W   300
#define FB_WIN_H   220

/* ── Lista interna ── */
#define FB_LIST_X       6
#define FB_LIST_Y       2
#define FB_TILE_W      76
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

enum {
    FB_DRIVE_NONE = 0,
    FB_DRIVE_HDD,
    FB_DRIVE_CD,
    FB_DRIVE_USB,
    FB_DRIVE_FLOPPY
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

static fb_state_t *g_fb = NULL;
static char g_fb_clipboard[VFS_MAX_PATH];
static void fb_main(void *argument);

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

        if (!fb_name16_eq(e, name)) continue;
        if (w != FB_ICON_SIZE || h != FB_ICON_SIZE) return NULL;
        if (size < need) return NULL;
        if (off > g_fb_icon_cache.pak_size ||
            off + need > g_fb_icon_cache.pak_size) return NULL;

        return (uint32_t *)(data + off);
    }

    return NULL;
}

static bool fb_load_icon_pak(void) {
/*
     * fb_load_icon_pak disabled: central loader.
     *
     * Antes el filebrowser leía /ICONS/ICONS.PAK otra vez con su propia cache.
     * Eso duplicaba la lectura del PAK desde disquete.
     *
     * Ahora dejamos que el fallback use program_load_bmp_icon_scaled().
     * Esa función ya resuelve /ICONS/*.BMP desde el PAK central cacheado.
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
        g_fb_icon_cache.field = program_load_bmp_icon_scaled(path, 48, 48); \
        task_yield(); \
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

    if (!vfs_read_all("/ASSOC.INI",
                      &g_fb_icon_cache.associations,
                      &g_fb_icon_cache.associations_size)) {
        (void)vfs_read_all("/Associations.INI",
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

static bool fb_devices_share_boot_sector(const char *lhs,
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

static bool fb_cdrom_available(void) {
    if (vfs_has_cdrom()) return true;
    if (block_get("cd0")) (void)iso9660_mount_default();
    return vfs_has_cdrom();
}

static bool fb_usb_available(void) {
    return block_get("usb0") != NULL;
}

static bool fb_floppy_available(void) {
    const char *mount = vfs_get_mount_name();

    if (!block_get("fd0")) return false;
    if (mount && kstrcmp(mount, "fd0") == 0) return false;
    if (mount && kstrncmp(mount, "ata", 3) == 0 &&
        fb_devices_share_boot_sector("fd0", mount))
        return false;
    return true;
}

static void fb_join_path(char *out, uint32_t capacity,
                         const char *dir, const char *name) {
    size_t len;
    if (!out || !capacity) return;
    kstrncpy(out, dir ? dir : "/", capacity - 1);
    out[capacity - 1] = '\0';
    len = kstrlen(out);
    if (len > 1 && out[len - 1] != '/' && len + 1 < capacity) {
        out[len++] = '/';
        out[len] = '\0';
    }
    kstrncpy(out + len, name ? name : "", capacity - len - 1);
}

static void fb_draw_bmp_icon(gui_surface_t *s, int x, int y,
                             const uint32_t *pixels) {
    if (!pixels) return;
    program_draw_icon_pixels(s, x, y, pixels, FB_ICON_SIZE, FB_ICON_SIZE);
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
    return kstrcmp(ext, upper) == 0 || kstrcmp(ext, lower) == 0;
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
        const char *line = (const char *)st->associations;
        while (*line) {
            const char *end = line, *eq = NULL;
            while (*end && *end != '\r' && *end != '\n') {
                if (*end == '=' && !eq) eq = end;
                end++;
            }
            if (eq) {
                size_t key_len = (size_t)(eq - line);
                const char *key = line;
                const char *wanted = entry->name;
                if (*ext == '.') {
                    const char *extension = ext + 1;
                    size_t ext_len = kstrlen(extension);
                    bool name_match = key_len == kstrlen(wanted) &&
                                      kstrncmp(key, wanted, key_len) == 0;
                    bool ext_match = key_len == ext_len &&
                                     kstrncmp(key, extension, key_len) == 0;
                    if (name_match || ext_match) {
                        const char *value = eq + 1;
                        size_t value_len = (size_t)(end - value);
#define FB_ASSOC(path, icon) \
                        if (value_len == sizeof(path) - 1 && \
                            kstrncmp(value, path, value_len) == 0) return icon
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
            }
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
    }
    if (fb_ext_is(ext, ".TXT", ".txt")) return st->text_icon;
    if (fb_ext_is(ext, ".INI", ".ini")) return st->config_icon;
    if (fb_ext_is(ext, ".BMP", ".bmp") ||
        fb_ext_is(ext, ".GIF", ".gif")) return st->image_icon;
    if (fb_ext_is(ext, ".O", ".o") ||
        fb_ext_is(ext, ".ELF", ".elf")) return st->object_icon;
    if (fb_ext_is(ext, ".MID", ".mid") ||
        fb_ext_is(ext, ".KAR", ".kar")) return st->midi_icon;
    return st->file_icon;
}

static void fb_add_drive(fb_state_t *st, const char *name, uint8_t drive) {
    vfs_dir_entry_t *entry;
    if (st->entry_count >= VFS_MAX_DIR_ENTRIES) return;
    entry = &st->entries[st->entry_count];
    kstrncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->type = VFS_NODE_DIR;
    entry->size = 0;
    entry->attributes = 0;
    st->entry_drive[st->entry_count++] = drive;
}

static void fb_load_drives(fb_state_t *st) {
    const char *mount = vfs_get_mount_name();
    st->entry_count = 0;
    st->selected = 0;
    st->scroll = 0;
    if (mount && kstrcmp(mount, "fd0") == 0)
        fb_add_drive(st, "Disquete", FB_DRIVE_FLOPPY);
    else
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
        kstrncpy(st->entries[0].name, "..", sizeof(st->entries[0].name) - 1);
        st->entries[0].type = VFS_NODE_DIR;
        st->entries[0].size = 0;
        st->entries[0].attributes = 0;
        st->entry_count = 1;
    }

    vfs_dir_entry_t tmp[VFS_MAX_DIR_ENTRIES];
    uint32_t found = 0;
    if (vfs_listdir(st->cwd, tmp, VFS_MAX_DIR_ENTRIES, &found)) {
        for (uint32_t i = 0; i < found && st->entry_count < VFS_MAX_DIR_ENTRIES; i++) {
            if (tmp[i].name[0] == '.' &&
                (tmp[i].name[1] == '\0' ||
                 (tmp[i].name[1] == '.' && tmp[i].name[2] == '\0'))) continue;
            st->entries[st->entry_count++] = tmp[i];
        }
    }
}

/* ══════════════════════════════════════════════════
 *  Actualizar título de ventana
 * ══════════════════════════════════════════════════ */

static void fb_update_title(fb_state_t *st) {
    if (!st->window) return;
    char title[48];
    if (st->drive_view) {
        kstrncpy(st->window->title, "Este equipo",
                 sizeof(st->window->title) - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        st->window->dirty = true;
        return;
    }
    kstrncpy(title, "Archivos: ", sizeof(title) - 1);
    size_t tlen = kstrlen(title);
    kstrncpy(title + tlen, st->cwd, sizeof(title) - tlen - 1);
    title[sizeof(title) - 1] = '\0';
    kstrncpy(st->window->title, title, sizeof(st->window->title) - 1);
    st->window->title[sizeof(st->window->title) - 1] = '\0';
    st->window->dirty = true;
    kstrncpy(st->address, st->cwd, sizeof(st->address) - 1);
    st->address[sizeof(st->address) - 1] = '\0';
}

/* ══════════════════════════════════════════════════
 *  Navegar a subdir / padre
 * ══════════════════════════════════════════════════ */

static void fb_navigate(fb_state_t *st, const char *name) {
    char new_path[VFS_MAX_PATH];

    if (kstrcmp(name, "..") == 0) {
        kstrncpy(new_path, st->cwd, sizeof(new_path) - 1);
        new_path[sizeof(new_path) - 1] = '\0';
        size_t len = kstrlen(new_path);
        if (len > 1 && new_path[len - 1] == '/') new_path[--len] = '\0';
        while (len > 1 && new_path[len - 1] != '/') new_path[--len] = '\0';
        if (len > 1) new_path[--len] = '\0';
        if (len == 0) { new_path[0] = '/'; new_path[1] = '\0'; }
    } else {
        kstrncpy(new_path, st->cwd, sizeof(new_path) - 1);
        size_t len = kstrlen(new_path);
        if (len > 0 && new_path[len - 1] != '/' && len + 1 < sizeof(new_path)) {
            new_path[len++] = '/';
            new_path[len]   = '\0';
        }
        kstrncpy(new_path + kstrlen(new_path), name,
                 sizeof(new_path) - kstrlen(new_path) - 1);
    }

    if (vfs_chdir(new_path))
        kstrncpy(st->cwd, vfs_getcwd(), sizeof(st->cwd) - 1);
    else
        kstrncpy(st->cwd, new_path, sizeof(st->cwd) - 1);

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
    kstrncpy(path, st->cwd, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    size_t len = kstrlen(path);
    if (len > 1 && path[len - 1] != '/') {
        path[len++] = '/';
        path[len] = '\0';
    }
    kstrncpy(path + len, name, sizeof(path) - len - 1);
    texteditor_open(desktop, path);
}

static void fb_execute_object(gui_desktop_t *desktop, fb_state_t *st,
                              const char *name) {
    char path[VFS_MAX_PATH];
    size_t len;

    kstrncpy(path, st->cwd, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    len = kstrlen(path);
    if (len > 1 && path[len - 1] != '/' && len + 1 < sizeof(path)) {
        path[len++] = '/';
        path[len] = '\0';
    }
    kstrncpy(path + len, name, sizeof(path) - len - 1);
    if (!program_execute_path(desktop, path) && st->window) {
        kstrncpy(st->window->title, "Error ELF: ",
                 sizeof(st->window->title) - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        len = kstrlen(st->window->title);
        kstrncpy(st->window->title + len, elf_last_error(),
                 sizeof(st->window->title) - len - 1);
        st->window->title[sizeof(st->window->title) - 1] = '\0';
        st->window->dirty = true;
    }
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

static void fb_btn_copy_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st || st->drive_view || st->selected < 0 ||
        (uint32_t)st->selected >= st->entry_count) return;
    if (st->entries[st->selected].type != VFS_NODE_FILE) {
        kstrcpy(st->status, "Solo se copian archivos por ahora");
    } else {
        fb_join_path(g_fb_clipboard, sizeof(g_fb_clipboard), st->cwd,
                     st->entries[st->selected].name);
        kstrcpy(st->status, "Archivo copiado");
    }
    window->dirty = true;
}

static void fb_btn_paste_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    void *data = NULL;
    uint32_t size = 0;
    char destination[VFS_MAX_PATH];
    const char *name;
    if (!st || st->drive_view || !g_fb_clipboard[0]) return;
    name = g_fb_clipboard;
    for (const char *p = g_fb_clipboard; *p; p++) if (*p == '/') name = p + 1;
    fb_join_path(destination, sizeof(destination), st->cwd, name);
    if (kstrcmp(destination, g_fb_clipboard) == 0) {
        kstrcpy(st->status, "Origen y destino son iguales");
    } else if (vfs_read_all(g_fb_clipboard, &data, &size) &&
               vfs_write_all(destination, data, size)) {
        kstrcpy(st->status, "Archivo pegado");
        fb_load_dir(st);
    } else kstrcpy(st->status, "No se pudo pegar");
    if (data) kfree(data);
    window->dirty = true;
}

static void fb_btn_new_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    char path[VFS_MAX_PATH];
    char name[13];
    if (!st || st->drive_view) return;
    for (int n = 1; n < 100; n++) {
        kstrcpy(name, "CARPETA");
        if (n < 10) {
            size_t len = kstrlen(name);
            name[len] = (char)('0' + n);
            name[len + 1] = '\0';
        }
        fb_join_path(path, sizeof(path), st->cwd, name);
        if (vfs_mkdir(path)) {
            kstrcpy(st->status, "Carpeta creada");
            fb_load_dir(st);
            window->dirty = true;
            return;
        }
    }
    kstrcpy(st->status, "No se pudo crear la carpeta");
    window->dirty = true;
}

static void fb_btn_refresh_cb(gui_window_t *window, uint32_t id UNUSED) {
    fb_state_t *st = window ? (fb_state_t *)window->content_context : NULL;
    if (!st) return;
    fb_load_dir(st);
    kstrcpy(st->status, "Actualizado");
    window->dirty = true;
}

/* ══════════════════════════════════════════════════
 *  Pintar lista dentro de la ventana
 * ══════════════════════════════════════════════════ */

static void fb_paint_list(gui_surface_t *surface, fb_state_t *st) {
    if (!st->window) return;

    int cx       = st->window->bounds.x + GUI_BORDER_SIZE;
    int cy       = st->window->bounds.y + GUI_TITLEBAR_HEIGHT;
    int cw       = st->window->bounds.w - GUI_BORDER_SIZE * 2;
    int list_top = cy + FB_LIST_Y + 48;
    int list_h   = st->window->bounds.h - GUI_TITLEBAR_HEIGHT
                   - GUI_BORDER_SIZE - 52;

    gui_gfx_fill_rect(surface,
        (gui_rect_t){cx + FB_LIST_X, list_top, cw - FB_LIST_X * 2, list_h},
        FB_COLOR_BG);

    int cols = (cw - FB_LIST_X * 2) / FB_TILE_W;
    if (cols < 1) cols = 1;
    int visible = (list_h / FB_TILE_H) * cols;
    for (int i = 0; i < visible; i++) {
        int idx = st->scroll + i;
        if ((uint32_t)idx >= st->entry_count) break;
        vfs_dir_entry_t *e = &st->entries[idx];
        int ex = cx + FB_LIST_X + (i % cols) * FB_TILE_W;
        int ey = list_top + (i / cols) * FB_TILE_H;
        bool sel = (idx == st->selected);
        if (sel)
            gui_gfx_fill_rect(surface, (gui_rect_t){ex, ey, FB_TILE_W - 4,
                                                    FB_TILE_H - 3},
                              FB_COLOR_SEL);
        fb_draw_bmp_icon(surface, ex + (FB_TILE_W - FB_ICON_SIZE) / 2, ey + 5,
                         fb_icon_for_entry(st, e, (uint32_t)idx));
        gui_font_draw_string_clipped(surface, ex + 4, ey + 58, e->name,
                                     sel ? FB_COLOR_TEXTSEL : FB_COLOR_TEXT,
                                     (gui_rect_t){ex + 3, ey + 56,
                                                  FB_TILE_W - 10, 24});
    }
}

/* ══════════════════════════════════════════════════
 *  Hit-test en la lista
 * ══════════════════════════════════════════════════ */

static int fb_hit_entry(fb_state_t *st, int ex, int ey) {
    if (!st->window) return -1;
    int cx       = st->window->bounds.x + GUI_BORDER_SIZE;
    int cy       = st->window->bounds.y + GUI_TITLEBAR_HEIGHT;
    int cw       = st->window->bounds.w - GUI_BORDER_SIZE * 2;
    int list_top = cy + FB_LIST_Y + 48;
    int list_h   = st->window->bounds.h - GUI_TITLEBAR_HEIGHT
                   - GUI_BORDER_SIZE - 52;

    if (ex < cx + FB_LIST_X || ex > cx + cw - FB_LIST_X) return -1;
    if (ey < list_top || ey > list_top + list_h) return -1;

    int cols = (cw - FB_LIST_X * 2) / FB_TILE_W;
    if (cols < 1) cols = 1;
    int col = (ex - (cx + FB_LIST_X)) / FB_TILE_W;
    int row = (ey - list_top) / FB_TILE_H;
    if (col < 0 || col >= cols) return -1;
    int idx = st->scroll + row * cols + col;
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
        int x = st->window->bounds.x + 35;
        int y = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 4;
        gui_gfx_fill_rect(surface, (gui_rect_t){x, y, st->window->bounds.w - 42, 20},
                          st->editing_address ? 0x00FFF4C8 : 0x00FFFFFF);
        gui_gfx_draw_rect(surface, (gui_rect_t){x, y, st->window->bounds.w - 42, 20},
                          0x00606060);
        gui_font_draw_string_clipped(surface, x + 4, y + 6,
            st->editing_address ? st->address : st->cwd, 0x00101010,
            (gui_rect_t){x + 3, y + 2, st->window->bounds.w - 48, 16});
        fb_paint_list(surface, st);
    }
}

/* ══════════════════════════════════════════════════
 *  Handle events del programa
 * ══════════════════════════════════════════════════ */

static bool fb_window_event(gui_window_t *window UNUSED,
                            const gui_event_t *event, void *context) {
    fb_state_t *st = (fb_state_t *)context;
    gui_desktop_t *desktop = st ? st->desktop : NULL;
    if (!st || !event) return false;

    /* Solo nos interesan eventos dentro de la ventana abierta */
    if (!st->window) return false;

    if (event->type == GUI_EVENT_KEY) {
        if (st->editing_address) {
            size_t len = kstrlen(st->address);
            if (event->key == '\n') {
                if (vfs_chdir(st->address)) {
                    kstrncpy(st->cwd, vfs_getcwd(), sizeof(st->cwd) - 1);
                    st->drive_view = false;
                    fb_load_dir(st);
                    fb_update_title(st);
                    st->status[0] = '\0';
                } else kstrcpy(st->status, "Ruta no encontrada");
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
            vfs_dir_entry_t *e = &st->entries[st->selected];
            if (e->type == VFS_NODE_DIR) fb_navigate(st, e->name);
            return true;
        }
        if (event->key == 'c' || event->key == 'C') {
            fb_btn_copy_cb(st->window, 0); return true;
        }
        if (event->key == 'v' || event->key == 'V') {
            fb_btn_paste_cb(st->window, 0); return true;
        }
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        gui_rect_t address_rect = {
            st->window->bounds.x + 35,
            st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 4,
            st->window->bounds.w - 42, 20
        };
        if (!st->drive_view &&
            gui_rect_contains(address_rect, event->x, event->y)) {
            st->editing_address = true;
            kstrncpy(st->address, st->cwd, sizeof(st->address) - 1);
            kstrcpy(st->status, "Escribe una ruta y pulsa Enter");
            st->window->dirty = true;
            return true;
        }
        int hit = fb_hit_entry(st, event->x, event->y);
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
            uint32_t now   = pit_get_ticks();
            uint32_t delta = now - st->last_list_click_tick;

            if (delta < FB_DBLCLICK_TICKS &&
                hit == st->last_list_click_idx &&
                st->last_list_click_tick != 0) {
                /* Doble clic en entrada → navegar si es directorio */
                vfs_dir_entry_t *e = &st->entries[hit];
                if (st->drive_view) {
                    uint8_t drive = st->entry_drive[hit];
                    const char *path = "/";
                    bool mounted = true;
                    if (drive == FB_DRIVE_CD) {
                        path = "/CDROM";
                    } else if (drive == FB_DRIVE_USB) {
                        mounted = vfs_mount("usb0");
                    } else if (drive == FB_DRIVE_FLOPPY) {
                        mounted = vfs_mount("fd0");
                    } else {
                        mounted = vfs_mount_default();
                    }
                    if (!mounted) {
                        kstrcpy(st->status, "La unidad aun no esta lista");
                        st->window->dirty = true;
                        return true;
                    }
                    st->drive_view = false;
                    kstrncpy(st->cwd, path, sizeof(st->cwd) - 1);
                    st->cwd[sizeof(st->cwd) - 1] = '\0';
                    (void)vfs_chdir(path);
                    fb_load_dir(st);
                    fb_update_title(st);
                } else if (e->type == VFS_NODE_DIR) {
                    fb_navigate(st, e->name);
                } else if (fb_is_text_file(e->name)) {
                    fb_open_text(desktop, st, e->name);
                } else if (fb_ext_is(fb_extension(e->name), ".BMP", ".bmp") ||
                           fb_ext_is(fb_extension(e->name), ".GIF", ".gif")) {
                    char path[VFS_MAX_PATH];
                    fb_join_path(path, sizeof(path), st->cwd, e->name);
                    imageviewer_open(desktop, path);
                } else if (program_is_object(e->name)) {
                    fb_execute_object(desktop, st, e->name);
                }
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
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
    }

    /*
     * Los iconos y Associations.INI viven en g_fb_icon_cache.
     * No se liberan por ventana: así el filebrowser no vuelve a leer
     * ICONS.PAK/BMPs cada vez que se abre.
     */
    if (g_fb == st) g_fb = NULL;
    kfree(st);
}

void filebrowser_open_path(gui_desktop_t *desktop, const char *path) {
    fb_state_t *st;
    if (!desktop) return;

    st = (fb_state_t *)kzalloc(sizeof(fb_state_t));
    if (!st) return;
    st->desktop = desktop;
    st->selected = -1;
    st->drive_view = !path || !path[0] || kstrcmp(path, "/") == 0;
    kstrncpy(st->cwd, path && path[0] ? path : vfs_getcwd(),
             sizeof(st->cwd) - 1);
    st->cwd[sizeof(st->cwd) - 1] = '\0';
    g_fb = st;
    if (task_create("filebrowser", fb_main, st) < 0) {
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
        task_exit();
    }

    task_set_memory_hint(sizeof(*st) + FB_ICON_BYTES * 16);
    st->window = gui_desktop_create_window(st->desktop,
        FB_WIN_X, FB_WIN_Y, FB_WIN_W, FB_WIN_H, "Archivos");
    if (st->window) {
        gui_window_set_content(st->window, fb_content, st);
        gui_window_set_event_handler(st->window, fb_window_event, st);
        gui_window_set_min_size(st->window, 220, 150);
        st->window->owner_pid = task_current_pid();
        task_bind_window(st->window);

        gui_widget_t *btn = gui_widget_create(st->desktop, st->window,
            GUI_WIDGET_BUTTON,
            (gui_rect_t){FB_LIST_X, FB_LIST_Y, 25, 20},
            "^", fb_btn_up_cb);
        if (btn) st->btn_up_id = btn->id;
        btn = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){6, 28, 45, 18}, "Copiar", fb_btn_copy_cb);
        if (btn) st->btn_copy_id = btn->id;
        btn = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){55, 28, 42, 18}, "Pegar", fb_btn_paste_cb);
        if (btn) st->btn_paste_id = btn->id;
        btn = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){101, 28, 48, 18}, "Carpeta", fb_btn_new_cb);
        if (btn) st->btn_new_id = btn->id;
        btn = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){153, 28, 58, 18}, "Refrescar", fb_btn_refresh_cb);
        if (btn) st->btn_refresh_id = btn->id;
        kstrcpy(st->status, "Cargando...");
        fb_load_resources(st);
        fb_load_dir(st);
        st->status[0] = '\0';
        fb_update_title(st);
    }

    while (!task_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        task_sleep(4);
    }

    fb_cleanup(st);
    task_exit();
}

void filebrowser_install(gui_desktop_t *desktop UNUSED) {}

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
