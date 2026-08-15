#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "fat.h"

#define VFS_MAX_PATH 260
#define VFS_MAX_NAME 256
#define VFS_MAX_OPEN_FILES 128
#define VFS_MAX_DIR_ENTRIES FAT_MAX_DIR_ENTRIES

#define VFS_O_RDONLY 0x0001
#define VFS_O_WRONLY 0x0002
#define VFS_O_RDWR   0x0003
#define VFS_O_CREATE 0x0010
#define VFS_O_TRUNC  0x0020

typedef enum {
    VFS_NODE_NONE = 0,
    VFS_NODE_FILE,
    VFS_NODE_DIR
} vfs_node_type_t;

typedef struct {
    char name[VFS_MAX_NAME];
    uint32_t size;
    vfs_node_type_t type;
    uint8_t attributes;
} vfs_dir_entry_t;

void vfs_init(void);
bool vfs_mount(const char *name);
bool vfs_mount_default(void);
void vfs_forget_volume(const char *name);
void vfs_use_cdrom_as_root(bool enabled);
bool vfs_cdrom_is_root(void);
bool vfs_get_fs_info(fat_fs_t *fs);
bool vfs_has_cdrom(void);
const char *vfs_get_mount_name(void);
const char *vfs_getcwd(void);
bool vfs_chdir(const char *path);
bool vfs_listdir(const char *path, vfs_dir_entry_t *entries, uint32_t max_entries, uint32_t *count);
int vfs_open(const char *path, uint32_t flags);
int vfs_read(int fd, void *buffer, uint32_t size);
int vfs_write(int fd, const void *buffer, uint32_t size);
/* BLES_WINE_SFX_IO_CACHE_FIX_20260723: commit the coherent per-open FAT cache. */
bool vfs_flush(int fd);
bool vfs_close(int fd);
int32_t vfs_seek(int fd, int32_t offset, uint32_t origin);
int32_t vfs_tell(int fd);
int32_t vfs_size(int fd);
bool vfs_truncate(int fd, uint32_t size);
bool vfs_stat(const char *path, vfs_dir_entry_t *entry);
bool vfs_mkdir(const char *path);
bool vfs_remove(const char *path);
bool vfs_rename(const char *old_path, const char *new_path);
bool vfs_get_space(uint64_t *total_bytes, uint64_t *free_bytes);
bool vfs_get_space_path(const char *path, uint64_t *total_bytes,
                        uint64_t *free_bytes);
bool vfs_read_all(const char *path, void **buffer, uint32_t *size);
bool vfs_write_all(const char *path, const void *buffer, uint32_t size);
/* Keep the target FAT allocation table cached while an installer writes many
 * files. The path only selects the target volume; usually use "/". */
bool vfs_bulk_write_begin(const char *path);
void vfs_bulk_write_end(void);

/* Diagnostico liviano para fallos de instaladores Win32. */
const char *vfs_last_error_text(void);
uint32_t vfs_open_file_count(void);
void vfs_dump_open_files(void);

#endif
