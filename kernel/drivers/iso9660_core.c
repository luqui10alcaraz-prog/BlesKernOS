#include "../include/iso9660.h"

static const iso9660_driver_ops_t *g_iso9660_driver;

bool iso9660_register_driver(const iso9660_driver_ops_t *ops) {
    if (!ops || !ops->mount_default || !ops->is_mounted || !ops->resolve ||
        !ops->list || !ops->read_at) return false;
    g_iso9660_driver = ops;
    return true;
}

bool iso9660_mount_default(void) {
    return g_iso9660_driver && g_iso9660_driver->mount_default();
}
bool iso9660_is_mounted(void) {
    return g_iso9660_driver && g_iso9660_driver->is_mounted();
}
bool iso9660_resolve(const char *path, iso9660_entry_t *entry) {
    return g_iso9660_driver && g_iso9660_driver->resolve(path, entry);
}
bool iso9660_list(const iso9660_entry_t *directory, iso9660_entry_t *entries,
                  uint32_t max_entries, uint32_t *count) {
    return g_iso9660_driver &&
           g_iso9660_driver->list(directory, entries, max_entries, count);
}
bool iso9660_read_at(const iso9660_entry_t *entry, uint32_t offset,
                     void *buffer, uint32_t size, uint32_t *bytes_read) {
    return g_iso9660_driver &&
           g_iso9660_driver->read_at(entry, offset, buffer, size, bytes_read);
}
