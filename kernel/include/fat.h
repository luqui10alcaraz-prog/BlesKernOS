#ifndef FAT_H
#define FAT_H

#include "types.h"

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LONG_NAME 0x0F

#define FAT_CLUSTER_END_16 0xFFF8
#define FAT_CLUSTER_BAD_16 0xFFF7
#define FAT_CLUSTER_END_32 0x0FFFFFF8
#define FAT_CLUSTER_BAD_32 0x0FFFFFF7
#define FAT_CLUSTER_END_12 0x0FF8
#define FAT_CLUSTER_BAD_12 0x0FF7

struct block_device;

typedef enum {
    FAT_TYPE_NONE = 0,
    FAT_TYPE_FAT12 = 12,
    FAT_TYPE_FAT16 = 16,
    FAT_TYPE_FAT32 = 32
} fat_type_t;

typedef struct {
    char name[13];
    uint8_t attributes;
    uint32_t size;
    uint32_t first_cluster;
    bool is_directory;
} fat_dir_entry_t;

typedef struct {
    struct block_device *device;
    uint32_t volume_lba;
    fat_type_t type;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint32_t total_sectors;
    uint32_t fat_size_sectors;
    uint32_t first_fat_sector;
    uint32_t first_data_sector;
    uint32_t total_clusters;
    uint32_t root_dir_cluster;
    uint16_t root_dir_sectors;
    char volume_label[12];
    char fs_name[9];
} fat_fs_t;

bool fat_mount(fat_fs_t *fs, struct block_device *dev, uint32_t volume_lba);
bool fat_set_active(const char *name);
bool fat_mount_default(void);
bool fat_get_current(fat_fs_t *fs);
const char *fat_get_active_name(void);
void fat_print_info(const fat_fs_t *fs);
bool fat_list_root(const fat_fs_t *fs, fat_dir_entry_t *entries, uint32_t max_entries, uint32_t *count);
bool fat_list_dir(const fat_fs_t *fs, const fat_dir_entry_t *dir, fat_dir_entry_t *entries, uint32_t max_entries, uint32_t *count);
bool fat_find_file(const fat_fs_t *fs, const char *name, fat_dir_entry_t *entry);
bool fat_resolve_path(const fat_fs_t *fs, const char *path, fat_dir_entry_t *entry);
bool fat_read_file(const fat_fs_t *fs, const fat_dir_entry_t *entry, void *buffer, uint32_t max_size, uint32_t *bytes_read);
bool fat_read_file_at(const fat_fs_t *fs, const fat_dir_entry_t *entry, uint32_t offset, void *buffer, uint32_t max_size, uint32_t *bytes_read);
bool fat_write_path(fat_fs_t *fs, const char *path, const void *data, uint32_t size);
bool fat_mkdir_path(fat_fs_t *fs, const char *path);

#endif
