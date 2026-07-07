#ifndef ISO9660_H
#define ISO9660_H

#include "types.h"
#include "block.h"

#define ISO9660_MAX_NAME 32

typedef struct {
    char name[ISO9660_MAX_NAME];
    uint32_t extent;
    uint32_t size;
    bool is_directory;
} iso9660_entry_t;

bool iso9660_mount_default(void);
bool iso9660_is_mounted(void);
bool iso9660_resolve(const char *path, iso9660_entry_t *entry);
bool iso9660_list(const iso9660_entry_t *directory, iso9660_entry_t *entries,
                  uint32_t max_entries, uint32_t *count);
bool iso9660_read_at(const iso9660_entry_t *entry, uint32_t offset,
                     void *buffer, uint32_t size, uint32_t *bytes_read);

#endif
