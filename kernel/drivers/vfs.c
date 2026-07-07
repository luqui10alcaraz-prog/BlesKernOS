#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/vga.h"
#include "../include/iso9660.h"

typedef enum {
    VFS_FILE_FAT = 0,
    VFS_FILE_ISO9660
} vfs_file_source_t;

typedef struct {
    bool used;
    uint32_t flags;
    uint32_t offset;
    vfs_file_source_t source;
    union {
        fat_dir_entry_t fat;
        iso9660_entry_t iso;
    } entry;
} vfs_file_t;

static char g_cwd[VFS_MAX_PATH];
static vfs_file_t g_files[VFS_MAX_OPEN_FILES];

static bool vfs_iso_path(const char *full, const char **inner) {
    if (!full || kstrncmp(full, "/CDROM", 6) != 0) return false;
    if (full[6] != '\0' && full[6] != '/') return false;
    if (inner) *inner = full[6] ? full + 6 : "/";
    return true;
}

static bool vfs_has_mount(void) {
    fat_fs_t fs;
    return fat_get_current(&fs);
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

static bool vfs_normalize_path(const char *path, char *out) {
    char raw[VFS_MAX_PATH];
    char component[16];
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
        if (!path_append_component(out, component)) return false;
    }

    return true;
}

void vfs_init(void) {
    kstrcpy(g_cwd, "/");
    kmemset(g_files, 0, sizeof(g_files));
    if (iso9660_mount_default())
        kprintf("[VFS] ISO9660 montado en /CDROM\n");
}

bool vfs_mount(const char *name) {
    if (!fat_set_active(name)) return false;
    kstrcpy(g_cwd, "/");
    return true;
}

bool vfs_mount_default(void) {
    if (!fat_mount_default()) return false;
    kstrcpy(g_cwd, "/");
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

    if (!vfs_normalize_path(path, full)) return false;
    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            !iso_entry.is_directory) return false;
        kstrncpy(g_cwd, full, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
        return true;
    }
    if (!vfs_has_mount() || !fat_get_current(&fs)) return false;
    if (!fat_resolve_path(&fs, full, &entry)) return false;
    if (!entry.is_directory) return false;

    kstrncpy(g_cwd, full, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    return true;
}

bool vfs_listdir(const char *path, vfs_dir_entry_t *entries, uint32_t max_entries, uint32_t *count) {
    fat_fs_t fs;
    fat_dir_entry_t dir;
    fat_dir_entry_t fat_entries[VFS_MAX_DIR_ENTRIES];
    char full[VFS_MAX_PATH];
    uint32_t found = 0;
    const char *iso_path;
    iso9660_entry_t iso_dir;
    iso9660_entry_t *iso_entries;

if (!entries || !count) return false;

if (!path || !path[0])
    path = ".";

if (!vfs_normalize_path(path, full)) {
    kprintf("vfs: normalize fallo\n");
    return false;
}

if (vfs_iso_path(full, &iso_path)) {
    if (max_entries > VFS_MAX_DIR_ENTRIES) max_entries = VFS_MAX_DIR_ENTRIES;
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

if (!fat_get_current(&fs)) return false;

if (!fat_resolve_path(&fs, full, &dir)) {
    kprintf("vfs: resolve fallo (%s)\n", full);
    return false;
}

if (!dir.is_directory) {
    kprintf("vfs: no es directorio\n");
    return false;
}

if (max_entries > VFS_MAX_DIR_ENTRIES)
    max_entries = VFS_MAX_DIR_ENTRIES;

if (!fat_list_dir(&fs, &dir, fat_entries, max_entries, &found)) {
    kprintf("vfs: fat_list_dir fallo\n");
    return false;
}

    for (uint32_t i = 0; i < found; i++) {
        kmemset(&entries[i], 0, sizeof(entries[i]));
        kstrncpy(entries[i].name, fat_entries[i].name, sizeof(entries[i].name) - 1);
        entries[i].size = fat_entries[i].size;
        entries[i].attributes = fat_entries[i].attributes;
        entries[i].type = fat_entries[i].is_directory ? VFS_NODE_DIR : VFS_NODE_FILE;
    }
    if (kstrcmp(full, "/") == 0 && iso9660_is_mounted() &&
        found < max_entries) {
        kmemset(&entries[found], 0, sizeof(entries[found]));
        kstrcpy(entries[found].name, "CDROM");
        entries[found].type = VFS_NODE_DIR;
        entries[found].attributes = FAT_ATTR_DIRECTORY | FAT_ATTR_READ_ONLY;
        found++;
    }
    *count = found;
    return true;
}

int vfs_open(const char *path, uint32_t flags) {
    fat_fs_t fs;
    fat_dir_entry_t entry;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    iso9660_entry_t iso_entry;

    if (flags & VFS_O_WRONLY) return -1;
    if (!vfs_normalize_path(path, full)) return -1;
    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            iso_entry.is_directory) return -1;
        for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
            if (!g_files[i].used) {
                g_files[i].used = true;
                g_files[i].flags = flags ? flags : VFS_O_RDONLY;
                g_files[i].offset = 0;
                g_files[i].source = VFS_FILE_ISO9660;
                g_files[i].entry.iso = iso_entry;
                return i;
            }
        }
        return -1;
    }
    if (!fat_get_current(&fs)) return -1;
    if (!fat_resolve_path(&fs, full, &entry)) return -1;
    if (entry.is_directory) return -1;

    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_files[i].used) {
            g_files[i].used = true;
            g_files[i].flags = flags ? flags : VFS_O_RDONLY;
            g_files[i].offset = 0;
            g_files[i].source = VFS_FILE_FAT;
            g_files[i].entry.fat = entry;
            return i;
        }
    }
    return -1;
}

int vfs_read(int fd, void *buffer, uint32_t size) {
    fat_fs_t fs;
    uint32_t bytes_read = 0;
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !buffer || size == 0) return -1;
    if (!g_files[fd].used) return -1;
    if (g_files[fd].source == VFS_FILE_ISO9660) {
        if (!iso9660_read_at(&g_files[fd].entry.iso, g_files[fd].offset,
                             buffer, size, &bytes_read)) return -1;
    } else {
        if (!fat_get_current(&fs) ||
            !fat_read_file_at(&fs, &g_files[fd].entry.fat,
                              g_files[fd].offset, buffer, size,
                              &bytes_read)) return -1;
    }
    g_files[fd].offset += bytes_read;
    return (int)bytes_read;
}

int vfs_write(int fd, const void *buffer UNUSED, uint32_t size UNUSED) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return -1;
    return -1;
}

bool vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_files[fd].used) return false;
    kmemset(&g_files[fd], 0, sizeof(g_files[fd]));
    return true;
}

bool vfs_mkdir(const char *path) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    if (!path || !fat_get_current(&fs) ||
        !vfs_normalize_path(path, full) ||
        vfs_iso_path(full, &iso_path)) return false;
    return fat_mkdir_path(&fs, full);
}

bool vfs_write_all(const char *path, const void *buffer, uint32_t size) {
    fat_fs_t fs;
    char full[VFS_MAX_PATH];
    const char *iso_path;
    if (!path || (size && !buffer) || !fat_get_current(&fs) ||
        !vfs_normalize_path(path, full) ||
        vfs_iso_path(full, &iso_path)) return false;
    return fat_write_path(&fs, full, buffer, size);
}

bool vfs_read_all(const char *path, void **buffer, uint32_t *size) {
    int fd;
    int got;
    fat_fs_t fs;
    fat_dir_entry_t entry;
    char full[VFS_MAX_PATH];
    uint8_t *data;
    const char *iso_path;
    iso9660_entry_t iso_entry;
    uint32_t file_size;

    if (!buffer || !size) return false;
    *buffer = NULL;
    *size = 0;
    if (!vfs_normalize_path(path, full)) return false;
    if (vfs_iso_path(full, &iso_path)) {
        if (!iso9660_resolve(iso_path, &iso_entry) ||
            iso_entry.is_directory) return false;
        file_size = iso_entry.size;
    } else {
        if (!fat_get_current(&fs) ||
            !fat_resolve_path(&fs, full, &entry) ||
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

    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kfree(data);
        return false;
    }
    got = vfs_read(fd, data, file_size);
    vfs_close(fd);
    if (got < 0 || (uint32_t)got != file_size) {
        kfree(data);
        return false;
    }

    data[got] = '\0';
    *buffer = data;
    *size = (uint32_t)got;
    return true;
}
