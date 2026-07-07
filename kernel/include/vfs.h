#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "fat.h"

#define VFS_MAX_PATH 128
#define VFS_MAX_OPEN_FILES 8
#define VFS_MAX_DIR_ENTRIES 64

#define VFS_O_RDONLY 0x0001
#define VFS_O_WRONLY 0x0002
#define VFS_O_RDWR   0x0003

typedef enum {
    VFS_NODE_NONE = 0,
    VFS_NODE_FILE,
    VFS_NODE_DIR
} vfs_node_type_t;

typedef struct {
    char name[13];
    uint32_t size;
    vfs_node_type_t type;
    uint8_t attributes;
} vfs_dir_entry_t;

void vfs_init(void);
bool vfs_mount(const char *name);
bool vfs_mount_default(void);
bool vfs_get_fs_info(fat_fs_t *fs);
bool vfs_has_cdrom(void);
const char *vfs_get_mount_name(void);
const char *vfs_getcwd(void);
bool vfs_chdir(const char *path);
bool vfs_listdir(const char *path, vfs_dir_entry_t *entries, uint32_t max_entries, uint32_t *count);
int vfs_open(const char *path, uint32_t flags);
int vfs_read(int fd, void *buffer, uint32_t size);
int vfs_write(int fd, const void *buffer, uint32_t size);
bool vfs_close(int fd);
bool vfs_mkdir(const char *path);
bool vfs_read_all(const char *path, void **buffer, uint32_t *size);
bool vfs_write_all(const char *path, const void *buffer, uint32_t size);

#endif
