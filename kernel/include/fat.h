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
#define FAT_MAX_NAME 256
#define FAT_MAX_DIR_ENTRIES 128

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
    char name[FAT_MAX_NAME];
    char short_name[13];
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

typedef struct {
    bool completed;
    bool boot_sector_valid;
    bool fat_copies_match;
    bool backup_boot_matches;
    uint32_t files;
    uint32_t directories;
    uint32_t allocated_clusters;
    uint32_t referenced_clusters;
    uint32_t free_clusters;
    uint32_t bad_clusters;
    uint32_t reserved_clusters;
    uint32_t fragmented_files;
    uint32_t total_fragments;
    uint32_t largest_free_run;
    uint32_t lost_clusters;
    uint32_t crosslinked_clusters;
    uint32_t circular_chains;
    uint32_t invalid_chains;
    uint32_t size_mismatches;
    uint32_t directory_errors;
    uint32_t fat_mismatch_sectors;
    uint32_t io_errors;
    uint32_t errors;
    uint32_t warnings;
} fat_check_report_t;

typedef struct {
    bool attempted;
    bool completed;
    bool read_only;
    bool scan_incomplete;
    bool fat_copies_synchronized;
    bool backup_boot_repaired;
    uint32_t chains_truncated;
    uint32_t lost_clusters_freed;
    uint32_t unrepaired_crosslinks;
    uint32_t write_errors;
    uint32_t errors_before;
    uint32_t errors_after;
} fat_repair_report_t;

bool fat_mount(fat_fs_t *fs, struct block_device *dev, uint32_t volume_lba);
bool fat_set_active(const char *name);
bool fat_mount_default(void);
bool fat_format(const char *device_name, const char *volume_label);
bool fat_get_current(fat_fs_t *fs);
bool fat_check_current(fat_check_report_t *report);
bool fat_repair_current(fat_repair_report_t *repair,
                        fat_check_report_t *after);
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
bool fat_remove_path(fat_fs_t *fs, const char *path);
bool fat_rename_path(fat_fs_t *fs, const char *old_path, const char *new_path);
bool fat_get_space(const fat_fs_t *fs, uint64_t *total_bytes, uint64_t *free_bytes);

#endif
