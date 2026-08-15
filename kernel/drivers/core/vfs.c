#include "../../include/vfs.h"
#include "../../include/block.h"
#include "../../include/memory.h"
#include "../../include/vga.h"
#include "../../include/iso9660.h"
#include "../../include/bootsplash.h"
#include "../../include/compat_mode.h"
#include "../../include/block.h"

#define VFS_LISTDIR_HARD_LIMIT 8192U

#define VFS_MAX_VOLUMES BLOCK_MAX_DEVICES

typedef struct {
    bool used;
    char name[8];
    fat_fs_t fs;
} vfs_volume_t;

typedef enum {
    VFS_FILE_FAT = 0,
    VFS_FILE_ISO9660
} vfs_file_source_t;

/* BLES_WINE_SFX_IO_CACHE_FIX_20260723
 * FAT files opened by Win32 are cached while the handle is alive.  The old
 * vfs_write() rebuilt the complete file and replaced its cluster chain on
 * every small WriteFile call, leaving other open handles with stale cluster
 * metadata.  A coherent per-open cache makes synchronous reads stable and
 * turns an extractor's hundreds of small writes into one FAT commit. */
#define VFS_READ_CACHE_LIMIT  (16U * 1024U * 1024U)
#define VFS_WRITE_CACHE_LIMIT (64U * 1024U * 1024U)
#define VFS_CACHE_MIN_CAPACITY 4096U

typedef struct {
    bool used;
    uint32_t flags;
    uint32_t offset;
    char path[VFS_MAX_PATH];
    vfs_file_source_t source;
    union {
        struct {
            fat_dir_entry_t entry;
            fat_fs_t fs;
        } fat;
        iso9660_entry_t iso;
    } entry;
    uint8_t *cache;
    uint32_t cache_size;
    uint32_t cache_capacity;
    bool cache_loaded;
    bool cache_dirty;
} vfs_file_t;

static char g_cwd[VFS_MAX_PATH];
/* Mantener la raiz en .data: los literales cortos pueden ser fusionados como
 * sufijos de otras cadenas por el linker, pero esta ruta se entrega como dato
 * estable a FAT durante montajes perezosos. */
static char g_vfs_root_path[2] = {'/', '\0'};
static bool g_vfs_cd_root = false;
static vfs_file_t g_files[VFS_MAX_OPEN_FILES];
static vfs_volume_t g_volumes[VFS_MAX_VOLUMES];

/* BLES_WINE_INSTALL_DIAG_PERF_20260723 */
static const char *g_vfs_last_error = "none";

const char *vfs_last_error_text(void) {
    return g_vfs_last_error;
}

uint32_t vfs_open_file_count(void) {
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < VFS_MAX_OPEN_FILES; i++)
        if (g_files[i].used) count++;
    return count;
}

void vfs_dump_open_files(void) {
    kprintf("[VFS:diag] open handles %u/%u\n",
            vfs_open_file_count(), VFS_MAX_OPEN_FILES);
    for (uint32_t i = 0U; i < VFS_MAX_OPEN_FILES; i++) {
        vfs_file_t *file = &g_files[i];
        if (!file->used) continue;
        kprintf("[VFS:diag] fd=%u flags=%x off=%u size=%u dirty=%u path=%s\n",
                i, file->flags, file->offset,
                file->cache_loaded ? file->cache_size :
                    (file->source == VFS_FILE_FAT
                        ? file->entry.fat.entry.size : file->entry.iso.size),
                file->cache_dirty ? 1U : 0U, file->path);
    }
}

static int vfs_open_failure(const char *path, uint32_t flags,
                            const char *reason) {
    g_vfs_last_error = reason ? reason : "unknown";
    kprintf("[VFS:diag] open FAIL path=%s flags=%x reason=%s "
            "open=%u/%u fat=%s\n",
            path ? path : "(null)", flags, g_vfs_last_error,
            vfs_open_file_count(), VFS_MAX_OPEN_FILES, fat_last_error());
    return -1;
}


static bool vfs_select_fat(const char *full, fat_fs_t *fs,
                           const char **inner);
static bool path_component_equals_ci(const char *a, const char *b);

static uint32_t vfs_access_mode(uint32_t flags) {
    return flags & VFS_O_RDWR;
}

static bool vfs_flags_readable(uint32_t flags) {
    uint32_t mode = vfs_access_mode(flags);
    return mode == VFS_O_RDONLY || mode == VFS_O_RDWR;
}

static bool vfs_flags_writable(uint32_t flags) {
    uint32_t mode = vfs_access_mode(flags);
    return mode == VFS_O_WRONLY || mode == VFS_O_RDWR;
}

static void vfs_drop_cache(vfs_file_t *file) {
    if (!file) return;
    if (file->cache) kfree(file->cache);
    file->cache = NULL;
    file->cache_size = 0U;
    file->cache_capacity = 0U;
    file->cache_loaded = false;
    file->cache_dirty = false;
}

static bool vfs_cache_reserve(vfs_file_t *file, uint32_t needed,
                              uint32_t limit) {
    uint32_t capacity;
    uint8_t *grown;
    if (!file || needed > limit) return false;
    if (needed <= file->cache_capacity) return true;
    capacity = file->cache_capacity ? file->cache_capacity
                                    : VFS_CACHE_MIN_CAPACITY;
    while (capacity < needed) {
        if (capacity >= limit || capacity > limit / 2U) {
            capacity = limit;
            break;
        }
        capacity *= 2U;
    }
    if (capacity < needed) return false;
    grown = file->cache ? (uint8_t *)krealloc(file->cache, capacity)
                        : (uint8_t *)kmalloc(capacity);
    if (!grown) return false;
    file->cache = grown;
    file->cache_capacity = capacity;
    return true;
}

static bool vfs_cache_load(vfs_file_t *file, bool required_for_write) {
    uint32_t size, got = 0U;
    uint32_t limit = required_for_write ? VFS_WRITE_CACHE_LIMIT
                                        : VFS_READ_CACHE_LIMIT;
    if (!file || file->source != VFS_FILE_FAT) return false;
    if (file->cache_loaded) return true;
    size = file->entry.fat.entry.size;
    if (size > limit || !vfs_cache_reserve(file, size, limit)) return false;
    if (size && (!fat_read_file_at(&file->entry.fat.fs,
                                   &file->entry.fat.entry, 0U,
                                   file->cache, size, &got) || got != size)) {
        vfs_drop_cache(file);
        return false;
    }
    file->cache_size = size;
    file->cache_loaded = true;
    file->cache_dirty = false;
    return true;
}

static void vfs_refresh_path_handles(const char *full, vfs_file_t *keep) {
    fat_fs_t fs;
    fat_dir_entry_t entry;
    const char *fat_path;
    if (!full || !vfs_select_fat(full, &fs, &fat_path) ||
        !fat_resolve_path(&fs, fat_path, &entry)) return;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        vfs_file_t *file = &g_files[i];
        if (!file->used || file->source != VFS_FILE_FAT ||
            !path_component_equals_ci(file->path, full)) continue;
        file->entry.fat.fs = fs;
        file->entry.fat.entry = entry;
        if (file != keep && file->cache_loaded && !file->cache_dirty)
            vfs_drop_cache(file);
    }
}

static bool vfs_flush_file(vfs_file_t *file) {
    fat_fs_t fs;
    const char *fat_path;
    if (!file || !file->used) {
        g_vfs_last_error = "invalid VFS file handle";
        return false;
    }
    if (!file->cache_dirty) return true;
    if (file->source != VFS_FILE_FAT || !file->cache_loaded) {
        g_vfs_last_error = "dirty cache is not a FAT file";
        return false;
    }
    if (!vfs_select_fat(file->path, &fs, &fat_path)) {
        g_vfs_last_error = "could not select FAT volume";
        return false;
    }
    if (!fat_write_path(&fs, fat_path, file->cache, file->cache_size)) {
        g_vfs_last_error = fat_last_error();
        kprintf("[VFS:diag] flush FAIL path=%s size=%u reason=%s\n",
                file->path, file->cache_size, g_vfs_last_error);
        return false;
    }
    file->cache_dirty = false;
    vfs_refresh_path_handles(file->path, file);
    file->entry.fat.entry.size = file->cache_size;
    g_vfs_last_error = "none";
    return true;
}

static bool vfs_sync_path(const char *full, vfs_file_t *except) {
    if (!full) return false;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        vfs_file_t *file = &g_files[i];
        if (!file->used || file == except || file->source != VFS_FILE_FAT ||
            !file->cache_dirty ||
            !path_component_equals_ci(file->path, full)) continue;
        if (!vfs_flush_file(file)) return false;
    }
    return true;
}

static void vfs_clear_volume_cache(void) {
    kmemset(g_volumes, 0, sizeof(g_volumes));
}

void vfs_forget_volume(const char *name) {
    if (!name || !name[0]) return;
    for (uint32_t i = 0; i < VFS_MAX_VOLUMES; i++) {
        if (!g_volumes[i].used ||
            kstrcmp(g_volumes[i].name, name) != 0) continue;
        kmemset(&g_volumes[i], 0, sizeof(g_volumes[i]));
    }
}

/* Resuelve /ata0/ruta, /ata1/ruta, /usb0/ruta, etc. sin cambiar el FAT
 * activo del sistema. Las rutas que no comienzan con un dispositivo siguen
 * apuntando al volumen de arranque por compatibilidad. */
static bool vfs_select_fat(const char *full, fat_fs_t *fs,
                           const char **inner) {
    char name[8];
    uint32_t length = 0;
    const char *active;
    const char *rest;
    block_device_t *device;

    if (!full || !fs || full[0] != '/') return false;
    rest = full + 1;
    while (rest[length] && rest[length] != '/' && length + 1 < sizeof(name))
        length++;
    if (length && (rest[length] == '\0' || rest[length] == '/')) {
        kmemset(name, 0, sizeof(name));
        kmemcpy(name, rest, length);
        active = fat_get_active_name();
        device = block_get(name);

        if ((active && kstrcmp(active, name) == 0) || device) {
            if (active && kstrcmp(active, name) == 0) {
                if (!fat_get_current(fs)) return false;
            } else {
                for (uint32_t i = 0; i < VFS_MAX_VOLUMES; i++) {
                    if (g_volumes[i].used &&
                        kstrcmp(g_volumes[i].name, name) == 0) {
                        *fs = g_volumes[i].fs;
                        if (inner) *inner = rest[length] ? rest + length
                                                         : g_vfs_root_path;
                        return true;
                    }
                }
                for (uint32_t i = 0; i < VFS_MAX_VOLUMES; i++) {
                    if (g_volumes[i].used) continue;
                    if (!fat_mount_named(&g_volumes[i].fs, name)) return false;
                    g_volumes[i].used = true;
                    kstrncpy(g_volumes[i].name, name,
                             sizeof(g_volumes[i].name) - 1);
                    *fs = g_volumes[i].fs;
                    if (inner) *inner = rest[length] ? rest + length
                                                     : g_vfs_root_path;
                    return true;
                }
                return false;
            }
            if (inner) *inner = rest[length] ? rest + length
                                             : g_vfs_root_path;
            return true;
        }
    }

    if (!fat_get_current(fs)) return false;
    if (inner) *inner = full;
    return true;
}

static bool vfs_is_block_volume_root(const char *full) {
    char name[8];
    uint32_t length = 0U;
    if (!full || full[0] != '/' || !full[1]) return false;
    while (full[length + 1U] && length + 1U < sizeof(name)) {
        if (full[length + 1U] == '/') return false;
        name[length] = full[length + 1U];
        length++;
    }
    if (full[length + 1U] != '\0' || !length) return false;
    name[length] = '\0';
    return block_get(name) != NULL;
}

static bool vfs_path_has_block_prefix(const char *full) {
    char name[8];
    uint32_t length = 0U;
    if (!full || full[0] != '/' || !full[1]) return false;
    while (full[1U + length] && full[1U + length] != '/' &&
           length + 1U < sizeof(name)) {
        name[length] = full[1U + length];
        length++;
    }
    if (!length) return false;
    name[length] = '\0';
    return block_get(name) != NULL;
}

static bool vfs_iso_path(const char *full, const char **inner) {
    if (!full) return false;
    if (g_vfs_cd_root) {
        /* Durante Setup, / sigue siendo el ISO pero /ata0, /usb0 y /fd0
           deben seguir apuntando a volumenes FAT escribibles. */
        if (vfs_path_has_block_prefix(full)) return false;
        if (inner) *inner = full;
        return true;
    }
    if (kstrncmp(full, "/CDROM", 6) != 0) return false;
    if (full[6] != '\0' && full[6] != '/') return false;
    if (inner) *inner = full[6] ? full + 6 : "/";
    return true;
}

static void path_pop(char *path) {
    size_t len = kstrlen(path);
    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }
    if (path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
    while (len > 1 && path[len - 1] != '/') {
        path[len - 1] = '\0';
        len--;
    }
    if (len > 1) path[len - 1] = '\0';
}

static bool path_append_component(char *out, const char *component) {
    size_t len = kstrlen(out);
    size_t clen = kstrlen(component);
    if (clen == 0) return true;
    if (len + clen + 2 >= VFS_MAX_PATH) return false;
    if (len > 1) {
        out[len++] = '/';
        out[len] = '\0';
    }
    kstrcat(out, component);
    return true;
}

static bool path_component_equals_ci(const char *a, const char *b) {
    char ca;
    char cb;

    if (!a || !b) return false;
    while (*a && *b) {
        ca = *a;
        cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool vfs_normalize_path(const char *path, char *out) {
    char raw[VFS_MAX_PATH];
    char component[VFS_MAX_NAME];
    uint32_t rpos = 0;

    if (!path || !out || !path[0]) return false;
    kmemset(raw, 0, sizeof(raw));
    kmemset(out, 0, VFS_MAX_PATH);

    if (path[0] == '/') {
        kstrncpy(raw, path, sizeof(raw) - 1);
    } else {
        kstrncpy(raw, g_cwd, sizeof(raw) - 1);
        if (kstrcmp(raw, "/") != 0) kstrcat(raw, "/");
        if (kstrlen(raw) + kstrlen(path) >= sizeof(raw)) return false;
        kstrcat(raw, path);
    }

    out[0] = '/';
    out[1] = '\0';

    while (raw[rpos]) {
        uint32_t cpos = 0;
        while (raw[rpos] == '/') rpos++;
        if (!raw[rpos]) break;

        kmemset(component, 0, sizeof(component));
        while (raw[rpos] && raw[rpos] != '/') {
            if (cpos + 1 >= sizeof(component)) return false;
            component[cpos++] = raw[rpos++];
        }
        component[cpos] = '\0';

        if (kstrcmp(component, ".") == 0) continue;
        if (kstrcmp(component, "..") == 0) {
            path_pop(out);
            continue;
        }
        if (path_component_equals_ci(component, "WALLPAPERS"))
            kstrcpy(component, "WALLPAPR");
        if (!path_append_component(out, component)) return false;
    }

    return true;
}

void vfs_init(void) {
    kstrcpy(g_cwd, "/");
    kmemset(g_files, 0, sizeof(g_files));
    vfs_clear_volume_cache();
}

void vfs_use_cdrom_as_root(bool enabled) {
    g_vfs_cd_root = enabled;
    kstrcpy(g_cwd, "/");
}

bool vfs_cdrom_is_root(void) {
    return g_vfs_cd_root;
}

bool vfs_mount(const char *name) {
    if (!fat_set_active(name)) return false;
    vfs_clear_volume_cache();
    kstrcpy(g_cwd, "/");
    return true;
}

bool vfs_mount_default(void) {
    kprintf("[VFS] entrando a vfs_mount_default\n");
    kprintf("[VFS] block_count=%u\n", block_count());

    if (!fat_mount_default()) {
        kprintf("[VFS] fat_mount_default devolvio false\n");
        return false;
    }
    vfs_clear_volume_cache();
    kstrcpy(g_cwd, "/");
    /* BLES_WINE_TEMP_SFX_FIX_20260723
     * TEMP is a system directory, not an application side effect.
     * Old WinZip SFX stubs read TEMP from the environment and may
     * never call GetTempPathA before their first _lcreat. */
    if (vfs_mkdir("/TEMP"))
        kprintf("[VFS] /TEMP listo\n");
    else
        kprintf("[VFS] warning: no se pudo preparar /TEMP\n");
    return true;
}

bool vfs_get_fs_info(fat_fs_t *fs) {
    return fat_get_current(fs);
}

bool vfs_has_cdrom(void) {
    return iso9660_is_mounted();
}

const char *vfs_get_mount_name(void) {
    return fat_get_active_name();
}

const char *vfs_getcwd(void) {
    return g_cwd;
}

bool vfs_chdir(const char *path) {
    fat_fs_t fs;
    fat_dir_entry_t entry;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    iso9660_entry_t iso_entry;
    const char *fat_path;

    if (!vfs_normalize_path(path, full)) return false;
    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            !iso_entry.is_directory) return false;
        kstrncpy(g_cwd, full, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
        return true;
    }
    if (!vfs_select_fat(full, &fs, &fat_path)) return false;
    if (vfs_is_block_volume_root(full)) {
        kstrncpy(g_cwd, full, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
        return true;
    }
    if (!fat_resolve_path(&fs, fat_path, &entry)) return false;
    if (!entry.is_directory) return false;

    kstrncpy(g_cwd, full, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    return true;
}

bool vfs_listdir(const char *path, vfs_dir_entry_t *entries, uint32_t max_entries, uint32_t *count) {
    fat_fs_t fs;
    fat_dir_entry_t dir;
    fat_dir_entry_t *fat_entries;
    char full[VFS_MAX_PATH];
    uint32_t found = 0;
    const char *iso_path;
    iso9660_entry_t iso_dir;
    iso9660_entry_t *iso_entries;
    const char *fat_path;

if (!entries || !count) return false;

if (!path || !path[0])
    path = ".";

if (!vfs_normalize_path(path, full)) {
    kprintf("vfs: normalize fallo\n");
    return false;
}

if (vfs_iso_path(full, &iso_path)) {
    if (max_entries == 0U) { *count = 0U; return true; }
    if (max_entries > VFS_LISTDIR_HARD_LIMIT)
        max_entries = VFS_LISTDIR_HARD_LIMIT;
    iso_entries = (iso9660_entry_t *)kmalloc(sizeof(*iso_entries) * max_entries);
    if (!iso_entries) return false;
    if (!iso9660_resolve(iso_path, &iso_dir) || !iso_dir.is_directory ||
        !iso9660_list(&iso_dir, iso_entries, max_entries, &found)) {
        kfree(iso_entries);
        return false;
    }
    for (uint32_t i = 0; i < found; i++) {
        kmemset(&entries[i], 0, sizeof(entries[i]));
        kstrncpy(entries[i].name, iso_entries[i].name,
                 sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        entries[i].size = iso_entries[i].size;
        entries[i].type = iso_entries[i].is_directory
                        ? VFS_NODE_DIR : VFS_NODE_FILE;
        entries[i].attributes = iso_entries[i].is_directory
                              ? FAT_ATTR_DIRECTORY : FAT_ATTR_READ_ONLY;
    }
    kfree(iso_entries);
    *count = found;
    return true;
}

if (!vfs_select_fat(full, &fs, &fat_path)) return false;

if (max_entries == 0U) { *count = 0U; return true; }
if (max_entries > VFS_LISTDIR_HARD_LIMIT)
    max_entries = VFS_LISTDIR_HARD_LIMIT;
fat_entries = (fat_dir_entry_t *)kmalloc(sizeof(*fat_entries) * max_entries);
if (!fat_entries) return false;

if (vfs_is_block_volume_root(full)) {
    if (!fat_list_root(&fs, fat_entries, max_entries, &found)) {
        kprintf("vfs: fat_list_root fallo (%s)\n", full);
        kfree(fat_entries);
        return false;
    }
    for (uint32_t i = 0; i < found; i++) {
        kmemset(&entries[i], 0, sizeof(entries[i]));
        kstrncpy(entries[i].name, fat_entries[i].name,
                 sizeof(entries[i].name) - 1);
        entries[i].size = fat_entries[i].size;
        entries[i].type = fat_entries[i].is_directory
                        ? VFS_NODE_DIR : VFS_NODE_FILE;
        entries[i].attributes = fat_entries[i].attributes;
    }
    kfree(fat_entries);
    *count = found;
    return true;
}

if (!fat_resolve_path(&fs, fat_path, &dir)) {
    kfree(fat_entries);
    return false;
}

if (!dir.is_directory) {
    kprintf("vfs: no es directorio (%s)\n", full);
    kfree(fat_entries);
    return false;
}

if (!fat_list_dir(&fs, &dir, fat_entries, max_entries, &found)) {
    kprintf("vfs: fat_list_dir fallo\n");
    kfree(fat_entries);
    return false;
}

    for (uint32_t i = 0; i < found; i++) {
        kmemset(&entries[i], 0, sizeof(entries[i]));
        if (path_component_equals_ci(fat_entries[i].name, "WALLPAPR"))
            kstrcpy(entries[i].name, "WALLPAPERS");
        else
            kstrncpy(entries[i].name, fat_entries[i].name,
                     sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        entries[i].size = fat_entries[i].size;
        entries[i].attributes = fat_entries[i].attributes;
        entries[i].type = fat_entries[i].is_directory ? VFS_NODE_DIR : VFS_NODE_FILE;
    }
    if (kstrcmp(fat_path, "/") == 0 && kstrcmp(full, "/") == 0 &&
        iso9660_is_mounted() &&
        found < max_entries) {
        kmemset(&entries[found], 0, sizeof(entries[found]));
        kstrcpy(entries[found].name, "CDROM");
        entries[found].type = VFS_NODE_DIR;
        entries[found].attributes = FAT_ATTR_DIRECTORY | FAT_ATTR_READ_ONLY;
        found++;
    }
    *count = found;
    kfree(fat_entries);
    return true;
}

int vfs_open(const char *path, uint32_t flags) {
    fat_fs_t fs;
    fat_dir_entry_t entry;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    iso9660_entry_t iso_entry;
    const char *fat_path;
    bool writable;

    g_vfs_last_error = "none";
    if (!vfs_normalize_path(path, full))
        return vfs_open_failure(path, flags, "path normalization failed");
    if ((flags & VFS_O_RDWR) == 0U) flags |= VFS_O_RDONLY;
    writable = vfs_flags_writable(flags);

    if (!vfs_sync_path(full, NULL))
        return vfs_open_failure(full, flags, "pending write flush failed");

    if (writable) {
        bool exists;
        if (vfs_iso_path(full, &iso_path))
            return vfs_open_failure(full, flags, "ISO9660 is read-only");
        if (!vfs_select_fat(full, &fs, &fat_path))
            return vfs_open_failure(full, flags, "FAT volume selection failed");
        exists = fat_resolve_path(&fs, fat_path, &entry);
        if (exists && entry.is_directory)
            return vfs_open_failure(full, flags, "target is a directory");
        if (!exists && !(flags & VFS_O_CREATE))
            return vfs_open_failure(full, flags, "file does not exist");
        if ((!exists || (flags & VFS_O_TRUNC)) &&
            !fat_write_path(&fs, fat_path, NULL, 0U))
            return vfs_open_failure(full, flags,
                                    "FAT could not create/truncate file");
        if (!fat_resolve_path(&fs, fat_path, &entry))
            return vfs_open_failure(full, flags,
                                    "created file could not be resolved");

        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (!g_files[i].used) {
                vfs_file_t *file = &g_files[i];
                kmemset(file, 0, sizeof(*file));
                file->used = true;
                file->flags = flags;
                file->source = VFS_FILE_FAT;
                kstrncpy(file->path, full, sizeof(file->path) - 1U);
                file->entry.fat.fs = fs;
                file->entry.fat.entry = entry;
                if (!exists || (flags & VFS_O_TRUNC)) {
                    file->cache_loaded = true;
                    file->cache_size = 0U;
                }
                g_vfs_last_error = "none";
                return i;
            }
        }
        vfs_dump_open_files();
        return vfs_open_failure(full, flags, "VFS file table exhausted");
    }

    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            iso_entry.is_directory)
            return vfs_open_failure(full, flags,
                                    "ISO file not found or is a directory");
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (!g_files[i].used) {
                vfs_file_t *file = &g_files[i];
                kmemset(file, 0, sizeof(*file));
                file->used = true;
                file->flags = flags;
                file->source = VFS_FILE_ISO9660;
                file->entry.iso = iso_entry;
                kstrncpy(file->path, full, sizeof(file->path) - 1U);
                g_vfs_last_error = "none";
                return i;
            }
        }
        vfs_dump_open_files();
        return vfs_open_failure(full, flags, "VFS file table exhausted");
    }

    if (!vfs_select_fat(full, &fs, &fat_path))
        return vfs_open_failure(full, flags, "FAT volume selection failed");
    if (!fat_resolve_path(&fs, fat_path, &entry))
        return vfs_open_failure(full, flags, "file not found");
    if (entry.is_directory)
        return vfs_open_failure(full, flags, "target is a directory");

    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_files[i].used) {
            vfs_file_t *file = &g_files[i];
            kmemset(file, 0, sizeof(*file));
            file->used = true;
            file->flags = flags;
            file->source = VFS_FILE_FAT;
            file->entry.fat.entry = entry;
            file->entry.fat.fs = fs;
            kstrncpy(file->path, full, sizeof(file->path) - 1U);
            g_vfs_last_error = "none";
            return i;
        }
    }
    vfs_dump_open_files();
    return vfs_open_failure(full, flags, "VFS file table exhausted");
}

int vfs_read(int fd, void *buffer, uint32_t size) {
    vfs_file_t *file;
    uint32_t bytes_read = 0U;
    uint32_t available;
    bool must_cache;

    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !buffer) return -1;
    file = &g_files[fd];
    if (!file->used || !vfs_flags_readable(file->flags)) return -1;
    if (!size) return 0;

    if (file->source == VFS_FILE_ISO9660) {
        if (!iso9660_read_at(&file->entry.iso, file->offset,
                             buffer, size, &bytes_read)) return -1;
        file->offset += bytes_read;
        return (int)bytes_read;
    }

    if (!vfs_sync_path(file->path, file)) return -1;
    must_cache = vfs_flags_writable(file->flags);
    if (file->cache_loaded || must_cache ||
        (!compat_mode_is_low_memory() &&
         file->entry.fat.entry.size <= VFS_READ_CACHE_LIMIT)) {
        if (!file->cache_loaded && !vfs_cache_load(file, must_cache)) {
            if (must_cache) return -1;
        } else {
            if (file->offset >= file->cache_size) return 0;
            available = file->cache_size - file->offset;
            if (size > available) size = available;
            kmemcpy(buffer, file->cache + file->offset, size);
            file->offset += size;
            return (int)size;
        }
    }

    if (!fat_read_file_at(&file->entry.fat.fs, &file->entry.fat.entry,
                          file->offset, buffer, size, &bytes_read)) return -1;
    file->offset += bytes_read;
    return (int)bytes_read;
}

int vfs_write(int fd, const void *buffer, uint32_t size) {
    vfs_file_t *file;
    uint32_t end;

    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return -1;
    file = &g_files[fd];
    if (!vfs_flags_writable(file->flags) ||
        file->source != VFS_FILE_FAT || (size && !buffer)) return -1;
    if (!size) return 0;
    if (!vfs_sync_path(file->path, file) ||
        !vfs_cache_load(file, true)) return -1;

    end = file->offset + size;
    if (end < file->offset || end > VFS_WRITE_CACHE_LIMIT ||
        !vfs_cache_reserve(file, end, VFS_WRITE_CACHE_LIMIT)) return -1;
    if (file->offset > file->cache_size)
        kmemset(file->cache + file->cache_size, 0,
                file->offset - file->cache_size);
    kmemcpy(file->cache + file->offset, buffer, size);
    file->offset = end;
    if (end > file->cache_size) file->cache_size = end;
    file->entry.fat.entry.size = file->cache_size;
    file->cache_dirty = true;
    return (int)size;
}

bool vfs_flush(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return false;
    return vfs_flush_file(&g_files[fd]);
}

bool vfs_close(int fd) {
    bool ok;
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return false;
    ok = vfs_flush_file(&g_files[fd]);
    vfs_drop_cache(&g_files[fd]);
    kmemset(&g_files[fd], 0, sizeof(g_files[fd]));
    return ok;
}

int32_t vfs_seek(int fd, int32_t offset, uint32_t origin) {
    vfs_file_t *file;
    int64_t base;
    int64_t target;
    uint32_t size;
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return -1;
    file = &g_files[fd];
    size = file->source == VFS_FILE_ISO9660 ? file->entry.iso.size
         : (file->cache_loaded ? file->cache_size
                               : file->entry.fat.entry.size);
    if (origin == 0U) base = 0;
    else if (origin == 1U) base = file->offset;
    else if (origin == 2U) base = size;
    else return -1;
    target = base + offset;
    if (target < 0 || target > 0x7FFFFFFFLL) return -1;
    file->offset = (uint32_t)target;
    return (int32_t)file->offset;
}

int32_t vfs_tell(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return -1;
    return (int32_t)g_files[fd].offset;
}

int32_t vfs_size(int fd) {
    vfs_file_t *file;
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return -1;
    file = &g_files[fd];
    return (int32_t)(file->source == VFS_FILE_ISO9660 ? file->entry.iso.size
        : (file->cache_loaded ? file->cache_size
                              : file->entry.fat.entry.size));
}

bool vfs_truncate(int fd, uint32_t size) {
    vfs_file_t *file;
    uint32_t old_size;
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return false;
    file = &g_files[fd];
    if (file->source != VFS_FILE_FAT || !vfs_flags_writable(file->flags) ||
        size > VFS_WRITE_CACHE_LIMIT || !vfs_sync_path(file->path, file) ||
        !vfs_cache_load(file, true)) return false;
    old_size = file->cache_size;
    if (!vfs_cache_reserve(file, size, VFS_WRITE_CACHE_LIMIT)) return false;
    if (size > old_size) kmemset(file->cache + old_size, 0, size - old_size);
    file->cache_size = size;
    file->entry.fat.entry.size = size;
    if (file->offset > size) file->offset = size;
    file->cache_dirty = true;
    return true;
}

bool vfs_stat(const char *path, vfs_dir_entry_t *out) {
    fat_fs_t fs;
    fat_dir_entry_t fat_entry;
    iso9660_entry_t iso_entry;
    char full[VFS_MAX_PATH];
    const char *inner;
    if (!path || !out || !vfs_normalize_path(path, full) ||
        !vfs_sync_path(full, NULL)) return false;
    kmemset(out, 0, sizeof(*out));
    if (vfs_iso_path(full, &inner)) {
        if (!iso9660_resolve(inner, &iso_entry)) return false;
        kstrncpy(out->name, iso_entry.name, sizeof(out->name) - 1U);
        out->size = iso_entry.size;
        out->type = iso_entry.is_directory ? VFS_NODE_DIR : VFS_NODE_FILE;
        out->attributes = iso_entry.is_directory ? FAT_ATTR_DIRECTORY : FAT_ATTR_READ_ONLY;
        return true;
    }
    if (!vfs_select_fat(full, &fs, &inner)) return false;
    if (vfs_is_block_volume_root(full)) {
        fat_entry.name[0] = '/';
        fat_entry.name[1] = '\0';
        fat_entry.size = 0U;
        fat_entry.is_directory = true;
        fat_entry.attributes = FAT_ATTR_DIRECTORY;
        fat_entry.first_cluster = fs.type == FAT_TYPE_FAT32
                                ? fs.root_dir_cluster : 0U;
    } else if (!fat_resolve_path(&fs, inner, &fat_entry)) return false;
    kstrncpy(out->name, fat_entry.name, sizeof(out->name) - 1U);
    out->size = fat_entry.size;
    out->type = fat_entry.is_directory ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->attributes = fat_entry.attributes;
    return true;
}

bool vfs_mkdir(const char *path) {
    fat_fs_t fs;
    fat_dir_entry_t existing;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    const char *fat_path;
    if (!path || !vfs_normalize_path(path, full) ||
        vfs_iso_path(full, &iso_path)) return false;
    if (!vfs_select_fat(full, &fs, &fat_path)) return false;
    if (fat_resolve_path(&fs, fat_path, &existing))
        return existing.is_directory;
    return fat_mkdir_path(&fs, fat_path);
}

bool vfs_remove(const char *path) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    const char *fat_path;
    if (!path || !vfs_normalize_path(path, full) ||
        !vfs_sync_path(full, NULL) || vfs_iso_path(full, &iso_path) ||
        !vfs_select_fat(full, &fs, &fat_path)) return false;
    return fat_remove_path(&fs, fat_path);
}

bool vfs_rename(const char *old_path, const char *new_path) {
    fat_fs_t old_fs, new_fs;
    char old_full[VFS_MAX_PATH], new_full[VFS_MAX_PATH];
    const char *iso_path;
    const char *old_inner, *new_inner;
    if (!old_path || !new_path ||
        !vfs_normalize_path(old_path, old_full) ||
        !vfs_normalize_path(new_path, new_full) ||
        !vfs_sync_path(old_full, NULL) || !vfs_sync_path(new_full, NULL) ||
        vfs_iso_path(old_full, &iso_path) || vfs_iso_path(new_full, &iso_path) ||
        !vfs_select_fat(old_full, &old_fs, &old_inner) ||
        !vfs_select_fat(new_full, &new_fs, &new_inner) ||
        old_fs.device != new_fs.device || old_fs.volume_lba != new_fs.volume_lba)
        return false;
    return fat_rename_path(&old_fs, old_inner, new_inner);
}

bool vfs_get_space(uint64_t *total_bytes, uint64_t *free_bytes) {
    fat_fs_t fs;
    return fat_get_current(&fs) && fat_get_space(&fs, total_bytes, free_bytes);
}

bool vfs_get_space_path(const char *path, uint64_t *total_bytes,
                        uint64_t *free_bytes) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *inner;
    if (!path || !total_bytes || !free_bytes ||
        !vfs_normalize_path(path, full) ||
        !vfs_select_fat(full, &fs, &inner)) return false;
    return fat_get_space(&fs, total_bytes, free_bytes);
}

bool vfs_write_all(const char *path, const void *buffer, uint32_t size) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    const char *fat_path;
    bool ok;
    if (!path || (size && !buffer) || !vfs_normalize_path(path, full) ||
        !vfs_sync_path(full, NULL) || vfs_iso_path(full, &iso_path)) return false;
    if (!vfs_select_fat(full, &fs, &fat_path)) return false;
    ok = fat_write_path(&fs, fat_path, buffer, size);
    if (ok) {
        g_vfs_last_error = "none";
        vfs_refresh_path_handles(full, NULL);
    } else {
        g_vfs_last_error = fat_last_error();
    }
    return ok;
}

bool vfs_bulk_write_begin(const char *path) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    const char *fat_path;
    if (!path || !vfs_normalize_path(path, full) ||
        vfs_iso_path(full, &iso_path) ||
        !vfs_select_fat(full, &fs, &fat_path))
        return false;
    return fat_bulk_write_begin(&fs);
}

void vfs_bulk_write_end(void) {
    fat_bulk_write_end();
}

bool vfs_read_all(const char *path, void **buffer, uint32_t *size) {
    fat_fs_t fs;
    fat_dir_entry_t entry;
    char full[VFS_MAX_PATH];
    uint8_t *data;
    const char *iso_path;
    iso9660_entry_t iso_entry;
    uint32_t file_size;
    uint32_t bytes_read = 0U;
    const char *fat_path;
    bool from_iso = false;

    if (!buffer || !size) return false;
    *buffer = NULL;
    *size = 0;
    if (!vfs_normalize_path(path, full) || !vfs_sync_path(full, NULL)) return false;
    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            iso_entry.is_directory) return false;
        file_size = iso_entry.size;
        from_iso = true;
    } else {
        if (!vfs_select_fat(full, &fs, &fat_path) ||
            !fat_resolve_path(&fs, fat_path, &entry) ||
            entry.is_directory) return false;
        file_size = entry.size;
    }
    data = (uint8_t *)kmalloc(file_size + 1);
    if (!data) return false;

    if (file_size == 0) {
        data[0] = '\0';
        *buffer = data;
        *size = 0;
        return true;
    }

    /* vfs_read_all ya posee el buffer final. Pasar por vfs_open/vfs_read
     * creaba además una cache del archivo completo, duplicando temporalmente
     * cada .DVR, .BEX y paquete gráfico. Leer directamente reduce el pico de
     * memoria casi a la mitad en el perfil recomendado de 8 MiB. */
    bootsplash_pulse();
    if (from_iso) {
        if (!iso9660_read_at(&iso_entry, 0U, data, file_size, &bytes_read))
            bytes_read = 0U;
    } else {
        if (!fat_read_file_at(&fs, &entry, 0U, data, file_size, &bytes_read))
            bytes_read = 0U;
    }
    bootsplash_pulse();
    if (bytes_read != file_size) {
        kfree(data);
        return false;
    }

    data[file_size] = '\0';
    *buffer = data;
    *size = file_size;
    return true;
}
