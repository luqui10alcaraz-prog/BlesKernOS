#include "../include/iso9660.h"
#include "../include/ata.h"
#include "../include/memory.h"
#include "../include/vga.h"

#define ISO_SECTOR_SIZE 2048U
#define ISO_PVD_LBA 16U

static block_device_t *g_iso_device;
static iso9660_entry_t g_iso_root;

static bool iso_read_sector(uint32_t lba, void *buffer) {
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (block_read(g_iso_device, lba, 1, buffer)) return true;
        (void)ata_refresh_media("cd0");
    }
    return false;
}

static uint32_t iso_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool iso_name_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool iso_parse_record(const uint8_t *record, iso9660_entry_t *entry) {
    uint8_t name_len;
    uint32_t out = 0;

    if (!record || !entry || record[0] < 34) return false;
    name_len = record[32];
    if (name_len == 1 && (record[33] == 0 || record[33] == 1)) return false;

    kmemset(entry, 0, sizeof(*entry));
    entry->extent = iso_le32(record + 2);
    entry->size = iso_le32(record + 10);
    entry->is_directory = (record[25] & 0x02) != 0;
    for (uint32_t i = 0; i < name_len && out + 1 < sizeof(entry->name); i++) {
        char c = (char)record[33 + i];
        if (c == ';') break;
        entry->name[out++] = c;
    }
    while (out > 0 && entry->name[out - 1] == '.') out--;
    entry->name[out] = '\0';
    return out != 0;
}

bool iso9660_mount_default(void) {
    uint8_t sector[ISO_SECTOR_SIZE];

    g_iso_device = block_get("cd0");
    if (!g_iso_device || g_iso_device->type != BLOCK_DEVICE_ATAPI) return false;
    (void)ata_refresh_media("cd0");
    if (g_iso_device->sector_size != ISO_SECTOR_SIZE ||
        !iso_read_sector(ISO_PVD_LBA, sector)) {
        g_iso_device = NULL;
        return false;
    }
    if (sector[0] != 1 || kmemcmp(sector + 1, "CD001", 5) != 0 ||
        sector[6] != 1) {
        g_iso_device = NULL;
        return false;
    }

    kmemset(&g_iso_root, 0, sizeof(g_iso_root));
    kstrcpy(g_iso_root.name, "/");
    g_iso_root.extent = iso_le32(sector + 156 + 2);
    g_iso_root.size = iso_le32(sector + 156 + 10);
    g_iso_root.is_directory = true;
    return g_iso_root.extent != 0 && g_iso_root.size != 0;
}

bool iso9660_is_mounted(void) {
    return g_iso_device != NULL;
}

bool iso9660_list(const iso9660_entry_t *directory, iso9660_entry_t *entries,
                  uint32_t max_entries, uint32_t *count) {
    uint8_t *data;
    uint32_t position = 0;
    uint32_t found = 0;
    uint32_t got = 0;

    if (!directory || !directory->is_directory || !entries || !count ||
        !g_iso_device) return false;
    data = (uint8_t *)kmalloc(directory->size);
    if (!data) return false;

    iso9660_entry_t file = *directory;
    file.is_directory = false;
    if (!iso9660_read_at(&file, 0, data, directory->size, &got) ||
        got != directory->size) {
        kfree(data);
        return false;
    }

    while (position < directory->size && found < max_entries) {
        uint8_t length = data[position];
        if (length == 0) {
            position = ((position / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }
        if (position + length > directory->size) break;
        if (iso_parse_record(data + position, &entries[found])) found++;
        position += length;
    }
    kfree(data);
    *count = found;
    return true;
}

bool iso9660_resolve(const char *path, iso9660_entry_t *entry) {
    iso9660_entry_t current = g_iso_root;
    iso9660_entry_t *children;
    char component[ISO9660_MAX_NAME];

    if (!g_iso_device || !path || !entry || path[0] != '/') return false;
    while (*path == '/') path++;
    if (!*path) {
        *entry = current;
        return true;
    }
    children = (iso9660_entry_t *)kmalloc(sizeof(*children) * 64);
    if (!children) return false;

    while (*path) {
        uint32_t pos = 0;
        uint32_t count = 0;
        bool found = false;
        while (*path && *path != '/') {
            if (pos + 1 >= sizeof(component)) {
                kfree(children);
                return false;
            }
            component[pos++] = *path++;
        }
        component[pos] = '\0';
        while (*path == '/') path++;
        if (!iso9660_list(&current, children, 64, &count)) {
            kfree(children);
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (iso_name_equal(children[i].name, component)) {
                current = children[i];
                found = true;
                break;
            }
        }
        if (!found) {
            kfree(children);
            return false;
        }
    }
    *entry = current;
    kfree(children);
    return true;
}

bool iso9660_read_at(const iso9660_entry_t *entry, uint32_t offset,
                     void *buffer, uint32_t size, uint32_t *bytes_read) {
    uint8_t sector[ISO_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buffer;
    uint32_t total = 0;

    if (!entry || !buffer || !bytes_read || !g_iso_device) return false;
    *bytes_read = 0;
    if (offset >= entry->size) return true;
    if (size > entry->size - offset) size = entry->size - offset;

    while (total < size) {
        uint32_t absolute = offset + total;
        uint32_t lba = entry->extent + absolute / ISO_SECTOR_SIZE;
        uint32_t within = absolute % ISO_SECTOR_SIZE;
        uint32_t chunk = ISO_SECTOR_SIZE - within;

        /*
         * Los archivos grandes (en particular los WAD de Doom) suelen estar
         * alineados a sectores. Leer varios sectores con un único READ(12)
         * evita miles de transacciones ATAPI independientes.
         */
        if (within == 0 && size - total >= ISO_SECTOR_SIZE) {
            uint32_t sectors = (size - total) / ISO_SECTOR_SIZE;
            if (sectors > 32U) sectors = 32U;
            if (!block_read(g_iso_device, lba, (uint8_t)sectors,
                            out + total)) return false;
            total += sectors * ISO_SECTOR_SIZE;
            continue;
        }

        if (chunk > size - total) chunk = size - total;
        if (!iso_read_sector(lba, sector)) return false;
        kmemcpy(out + total, sector + within, chunk);
        total += chunk;
    }
    *bytes_read = total;
    return true;
}
