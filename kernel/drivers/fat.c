#include "../include/fat.h"
#include "../include/block.h"
#include "../include/memory.h"
#include "../include/vga.h"

static fat_fs_t g_current_fs;
static bool g_fs_ready = false;
static const char *g_active_name = "none";

/*
 * Cache minimo de un sector FAT/VFS. En floppy evita releer una y otra vez
 * la FAT/root directory desde el FDC sincrono.
 */
static bool g_sector_cache_valid = false;
static block_device_t *g_sector_cache_dev = NULL;
static uint32_t g_sector_cache_lba = 0;
static uint8_t g_sector_cache[BLOCK_SECTOR_SIZE];

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void copy_name(const uint8_t *raw_name, char *out) {
    int i;
    for (i = 0; i < 8 && raw_name[i] != ' '; i++) {
        out[i] = (char)raw_name[i];
    }
    if (raw_name[8] != ' ' && raw_name[8] != 0) {
        out[i++] = '.';
        for (int j = 0; j < 3 && raw_name[8 + j] != ' '; j++) {
            out[i++] = (char)raw_name[8 + j];
        }
    }
    out[i] = '\0';
}

static bool name_equals(const char *a, const char *b) {
    char ca = *a;
    char cb = *b;
    while (ca && cb) {
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
        a++;
        b++;
        ca = *a;
        cb = *b;
    }
    return ca == cb;
}

static bool parse_dir_entry(const uint8_t *entry, fat_dir_entry_t *out) {
    if (!entry || !out) return false;
    if (entry[0] == 0x00) return false;
    if (entry[0] == 0xE5) return false;
    if (entry[11] == FAT_ATTR_LONG_NAME) return false;
    if (entry[11] & FAT_ATTR_VOLUME_ID) return false;

    kmemset(out, 0, sizeof(*out));
    copy_name(entry, out->name);
    out->attributes = entry[11];
    out->size = read_le32(entry + 28);
    out->first_cluster = (uint32_t)read_le16(entry + 26) | ((uint32_t)read_le16(entry + 20) << 16);
    out->is_directory = (out->attributes & FAT_ATTR_DIRECTORY) != 0;
    return true;
}

static bool read_sector(const fat_fs_t *fs, uint32_t sector, void *buffer) {
    uint32_t lba;

    if (!fs || !fs->device || !buffer) return false;
    if (fs->bytes_per_sector != BLOCK_SECTOR_SIZE) return false;

    lba = fs->volume_lba + sector;
    if (g_sector_cache_valid &&
        g_sector_cache_dev == fs->device &&
        g_sector_cache_lba == lba) {
        kmemcpy(buffer, g_sector_cache, BLOCK_SECTOR_SIZE);
        return true;
    }

    if (!block_read(fs->device, lba, 1, buffer)) return false;

    g_sector_cache_valid = true;
    g_sector_cache_dev = fs->device;
    g_sector_cache_lba = lba;
    kmemcpy(g_sector_cache, buffer, BLOCK_SECTOR_SIZE);
    return true;
}

static bool read_cluster(const fat_fs_t *fs, uint32_t cluster, void *buffer) {
    if (!fs || !buffer) return false;
    uint32_t sector = fs->first_data_sector + ((cluster - 2) * fs->sectors_per_cluster);
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (!read_sector(fs, sector + i, (uint8_t *)buffer + (i * fs->bytes_per_sector))) {
            return false;
        }
    }
    (void)cluster_size;
    return true;
}

static uint32_t get_next_cluster(const fat_fs_t *fs, uint32_t cluster) {
    uint32_t fat_offset;
    uint32_t fat_sector;
    uint32_t entry_offset;
    uint8_t *fat_entry = NULL;
    uint32_t next = 0xFFFFFFFF;

    if (!fs || cluster < 2) return 0xFFFFFFFF;

    /*
     * Una entrada FAT puede comenzar al final de un sector. En ese caso se
     * lee el sector siguiente completo en fat_entry + bytes_per_sector, por
     * lo que el buffer debe contener dos sectores, no sólo unos bytes extra.
     */
    fat_entry = (uint8_t *)kmalloc(fs->bytes_per_sector * 2U);
    if (!fat_entry) return 0xFFFFFFFF;

    if (fs->type == FAT_TYPE_FAT12) {
        fat_offset = cluster + (cluster / 2);
        fat_sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
        entry_offset = fat_offset % fs->bytes_per_sector;
        if (!read_sector(fs, fat_sector, fat_entry)) {
            kfree(fat_entry);
            return 0xFFFFFFFF;
        }
        if (entry_offset + 2 > fs->bytes_per_sector) {
            if (!read_sector(fs, fat_sector + 1, fat_entry + fs->bytes_per_sector)) {
                kfree(fat_entry);
                return 0xFFFFFFFF;
            }
        }
        next = read_le16(fat_entry + entry_offset);
        if (cluster & 1) next >>= 4;
        else next &= 0x0FFF;
        if (next >= FAT_CLUSTER_END_12) {
            kfree(fat_entry);
            return 0xFFFFFFFF;
        }
        kfree(fat_entry);
        return next;
    }

    if (fs->type == FAT_TYPE_FAT16) {
        fat_offset = cluster * 2;
        fat_sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
        entry_offset = fat_offset % fs->bytes_per_sector;
        if (!read_sector(fs, fat_sector, fat_entry)) {
            kfree(fat_entry);
            return 0xFFFFFFFF;
        }
        if (entry_offset + 2 > fs->bytes_per_sector) {
            if (!read_sector(fs, fat_sector + 1, fat_entry + fs->bytes_per_sector)) {
                kfree(fat_entry);
                return 0xFFFFFFFF;
            }
        }
        next = read_le16(fat_entry + entry_offset);
        if (next >= FAT_CLUSTER_END_16) {
            kfree(fat_entry);
            return 0xFFFFFFFF;
        }
        kfree(fat_entry);
        return next;
    }

    fat_offset = cluster * 4;
    fat_sector = fs->first_fat_sector + (fat_offset / fs->bytes_per_sector);
    entry_offset = fat_offset % fs->bytes_per_sector;
    if (!read_sector(fs, fat_sector, fat_entry)) {
        kfree(fat_entry);
        return 0xFFFFFFFF;
    }
    if (entry_offset + 4 > fs->bytes_per_sector) {
        if (!read_sector(fs, fat_sector + 1, fat_entry + fs->bytes_per_sector)) {
            kfree(fat_entry);
            return 0xFFFFFFFF;
        }
    }
    next = read_le32(fat_entry + entry_offset) & 0x0FFFFFFF;
    if (next >= FAT_CLUSTER_END_32) {
        kfree(fat_entry);
        return 0xFFFFFFFF;
    }
    kfree(fat_entry);
    return next;
}

static bool list_directory_chain(const fat_fs_t *fs, uint32_t start_cluster, fat_dir_entry_t *entries, uint32_t max_entries, uint32_t *count) {
    uint32_t current_cluster = start_cluster;
    uint32_t found = 0;
    bool done = false;
    uint8_t *cluster_buf = NULL;
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;

    if ((fs->type == FAT_TYPE_FAT12 || fs->type == FAT_TYPE_FAT16) && start_cluster == 0) {
        uint32_t root_dir_sector = fs->first_fat_sector + (fs->num_fats * fs->fat_size_sectors);
        uint32_t sector = root_dir_sector;
        uint32_t sectors_left = fs->root_dir_sectors;
        while (sectors_left > 0 && found < max_entries) {
            uint8_t sector_buf[512];
            if (!read_sector(fs, sector, sector_buf)) break;
            for (uint32_t i = 0; i < (fs->bytes_per_sector / 32); i++) {
                const uint8_t *entry = sector_buf + (i * 32);
                fat_dir_entry_t parsed;
                if (!parse_dir_entry(entry, &parsed)) {
                    if (entry[0] == 0x00) {
                        done = true;
                        break;
                    }
                    continue;
                }
                if (found < max_entries) {
                    entries[found++] = parsed;
                }
            }
            sectors_left--;
            sector++;
            if (done) break;
        }
        if (count) *count = found;
        return true;
    }

    cluster_buf = (uint8_t *)kmalloc(cluster_size);
    if (!cluster_buf) return false;

    while (current_cluster >= 2 && !done && found < max_entries) {
        if (!read_cluster(fs, current_cluster, cluster_buf)) break;
        for (uint32_t i = 0; i < (cluster_size / 32); i++) {
            const uint8_t *entry = cluster_buf + (i * 32);
            fat_dir_entry_t parsed;
            if (!parse_dir_entry(entry, &parsed)) {
                if (entry[0] == 0x00) {
                    done = true;
                    break;
                }
                continue;
            }
            if (found < max_entries) {
                entries[found++] = parsed;
            }
        }
        if (done) break;
        if (found >= max_entries) break;
        current_cluster = get_next_cluster(fs, current_cluster);
        if (current_cluster == 0xFFFFFFFF) break;
    }

    if (count) *count = found;
    kfree(cluster_buf);
    return true;
}

static bool looks_like_fat_bpb(const uint8_t *bpb) {
    uint16_t bytes_per_sector = read_le16(bpb + 11);
    uint8_t sectors_per_cluster = bpb[13];
    uint8_t num_fats = bpb[16];

    if (bpb[510] != 0x55 || bpb[511] != 0xAA) return false;
    if (bytes_per_sector != BLOCK_SECTOR_SIZE) return false;
    if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) return false;
    if (num_fats == 0) return false;
    return true;
}

bool fat_mount(fat_fs_t *fs, block_device_t *dev, uint32_t volume_lba) {
    uint8_t bpb[512];
    uint32_t data_sectors;

    if (!fs || !dev) {
        kprintf("fat_mount: fs/dev NULL\n");
        return false;
    }

    kmemset(fs, 0, sizeof(*fs));

    if (!block_read(dev, volume_lba, 1, bpb)) {
        kprintf("fat_mount: block_read fallo\n");
        return false;
    }

    kprintf("fat_mount: block_read OK\n");
    kprintf("OEM: ");
    for (uint32_t i = 0; i < 8; i++) kprintf("%c", bpb[3 + i]);
    kprintf("\n");
    kprintf("Firma: %x %x\n", bpb[510], bpb[511]);

    if (!looks_like_fat_bpb(bpb)) {
        kprintf("fat_mount: BPB invalido\n");
        return false;
    }

    kprintf("fat_mount: BPB valido\n");

    fs->device = dev;
    fs->volume_lba = volume_lba;
    fs->bytes_per_sector = read_le16(bpb + 11);
    fs->sectors_per_cluster = bpb[13];
    fs->reserved_sector_count = read_le16(bpb + 14);
    fs->num_fats = bpb[16];
    fs->root_entry_count = read_le16(bpb + 17);
    fs->total_sectors = read_le16(bpb + 19);

    if (fs->total_sectors == 0) {
        fs->total_sectors = read_le32(bpb + 32);
    }

    fs->fat_size_sectors = read_le16(bpb + 22);
    fs->first_fat_sector = fs->reserved_sector_count;
    fs->root_dir_sectors = ((fs->root_entry_count * 32) + (fs->bytes_per_sector - 1)) / fs->bytes_per_sector;

    if (fs->fat_size_sectors == 0) {
        fs->fat_size_sectors = read_le32(bpb + 36);
    }

    fs->first_data_sector = fs->reserved_sector_count + (fs->num_fats * fs->fat_size_sectors) + fs->root_dir_sectors;

    kprintf("BPS=%u SPC=%u Reserved=%u FATs=%u Root=%u Total=%u FATSize=%u FirstData=%u\n",
        fs->bytes_per_sector,
        fs->sectors_per_cluster,
        fs->reserved_sector_count,
        fs->num_fats,
        fs->root_entry_count,
        fs->total_sectors,
        fs->fat_size_sectors,
        fs->first_data_sector);

    if (fs->total_sectors <= fs->first_data_sector) {
        kprintf("fat_mount: total_sectors <= first_data_sector\n");
        return false;
    }

    data_sectors = fs->total_sectors - fs->first_data_sector;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;

    kprintf("Clusters=%u\n", fs->total_clusters);

    fs->root_dir_cluster = 2;

    if (fs->total_clusters < 4085) {
        fs->type = FAT_TYPE_FAT12;
    } else if (fs->total_clusters < 65525) {
        fs->type = FAT_TYPE_FAT16;
    } else {
        fs->type = FAT_TYPE_FAT32;
    }

    kprintf("Tipo FAT=%u\n", fs->type);

    if (fs->type == FAT_TYPE_FAT32) {
        fs->root_dir_cluster = read_le32(bpb + 44);
        fs->first_data_sector = fs->reserved_sector_count + (fs->num_fats * fs->fat_size_sectors) + 0;
        fs->root_dir_sectors = 0;
        fs->total_clusters = (fs->total_sectors - fs->first_data_sector) / fs->sectors_per_cluster;
        kmemcpy(fs->volume_label, bpb + 71, 11);
        kmemcpy(fs->fs_name, bpb + 82, 8);
    } else {
        kmemcpy(fs->volume_label, bpb + 43, 11);
        kmemcpy(fs->fs_name, bpb + 54, 8);
    }

    fs->volume_label[11] = '\0';
    fs->fs_name[8] = '\0';

    kprintf("Label=%s\n", fs->volume_label);
    kprintf("FS=%s\n", fs->fs_name);

    g_current_fs = *fs;
    g_fs_ready = true;
    g_active_name = dev->name;

    kprintf("fat_mount OK\n");

    return true;
}

static bool parse_partition_name(const char *name, char *dev_name, uint32_t *part_index) {
    size_t len;
    if (!name || !dev_name || !part_index) return false;

    len = kstrlen(name);
    if (len < 3 || name[len - 2] != 'p') return false;
    if (name[len - 1] < '1' || name[len - 1] > '4') return false;
    if (len >= 8) return false;

    kmemset(dev_name, 0, 8);
    for (size_t i = 0; i < len - 2 && i < 7; i++) {
        dev_name[i] = name[i];
    }
    *part_index = (uint32_t)(name[len - 1] - '1');
    return true;
}

static bool read_partition_lba(block_device_t *dev, uint32_t part_index, uint32_t *lba) {
    uint8_t mbr[512];
    uint32_t off;
    if (!dev || !lba || part_index >= 4) return false;
    if (!block_read(dev, 0, 1, mbr)) return false;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return false;
    off = 446 + (part_index * 16);
    if (mbr[off + 4] == 0) return false;
    *lba = read_le32(mbr + off + 8);
    return *lba != 0;
}

static bool is_supported_partition_type(uint8_t type) {
    switch (type) {
        case 0x01: /* FAT12 */
        case 0x04: /* FAT16 <32M */
        case 0x06: /* FAT16 */
        case 0x0B: /* FAT32 CHS */
        case 0x0C: /* FAT32 LBA */
        case 0x0E: /* FAT16 LBA */
            return true;
        default:
            return false;
    }
}

static bool partition_entry_lba(const uint8_t *sector, uint32_t index,
                                const block_device_t *dev, uint32_t *lba_out) {
    uint32_t off;
    uint8_t bootable;
    uint8_t type;
    uint32_t lba;
    uint32_t sectors;

    if (!sector || !lba_out || index >= 4) return false;

    off = 446 + (index * 16);
    bootable = sector[off + 0];
    type = sector[off + 4];
    lba = read_le32(sector + off + 8);
    sectors = read_le32(sector + off + 12);

    if (type == 0 || lba == 0 || sectors == 0) return false;
    if (bootable != 0x00 && bootable != 0x80) return false;
    if (!is_supported_partition_type(type)) return false;

    if (dev && dev->sector_count) {
        if (lba >= dev->sector_count) return false;
        if (sectors > dev->sector_count - lba) return false;
    }

    *lba_out = lba;
    return true;
}

static bool fat_try_mount_device(fat_fs_t *fs, block_device_t *dev, const char *active_name) {
    uint8_t sector[512];

    if (!fs || !dev) return false;

    /*
     * Primero probamos superfloppy/volumen directo.
     *
     * Un boot sector FAT tambien termina en 55 AA, pero los bytes 446..509
     * pertenecen al codigo del bootloader, no a una tabla de particiones.
     * Si interpretamos esos bytes como MBR, aparecen "particiones" falsas y
     * nunca llegamos a montar LBA 0. Eso es justo lo que pasa en la imagen
     * de disquete de BlesKernOS.
     */
    if (fat_mount(fs, dev, 0)) {
        g_active_name = active_name;
        return true;
    }

    /*
     * Si LBA 0 no era FAT, entonces si intentamos tratarlo como MBR.
     * Solo aceptamos entradas de particion razonables para evitar falsos
     * positivos dentro de codigo de arranque.
     */
    if (!block_read(dev, 0, 1, sector)) return false;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return false;

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t lba = 0;
        if (!partition_entry_lba(sector, i, dev, &lba)) continue;
        if (fat_mount(fs, dev, lba)) {
            g_active_name = active_name;
            return true;
        }
    }

    return false;
}

static inline uint32_t fat_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void fat_irq_restore(uint32_t flags) {
    if (flags & (1U << 9)) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

/*
 * IMPORTANTE:
 * En i386, el compilador puede transformar una copia de struct en REP MOVSL.
 * REP MOVSL usa DS:ESI como origen y ES:EDI como destino.
 *
 * Si alguna IRQ/syscall/task/ring3 dejó ES con un selector que no es el
 * selector de datos del kernel, la copia "*fs = g_current_fs" dispara
 * GENERAL PROTECTION FAULT (#13) en hardware real.
 *
 * Este helper deja los segmentos de datos apuntando otra vez al data segment
 * del kernel antes de copiar estructuras.
 */
static inline void fat_reload_kernel_segments(void) {
    __asm__ volatile (
        "cld\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        :
        : "ax", "memory"
    );
}

bool fat_get_current(fat_fs_t *fs) {
    uint32_t flags;

    if (!fs) return false;

    /*
     * Mantener la copia corta e indivisible: si una IRQ entra entre recargar
     * ES y copiar, y el handler no restaura ES, vuelve el bug.
     */
    flags = fat_irq_save();
    fat_reload_kernel_segments();

    if (!g_fs_ready) {
        fat_irq_restore(flags);
        return false;
    }

    /*
     * No usar "*fs = g_current_fs".
     * Eso generó:
     *   rep movsl %ds:(%esi),%es:(%edi)
     * y en hardware real explotó con #GP si ES estaba sucio.
     */
    fs->device = g_current_fs.device;
    fs->volume_lba = g_current_fs.volume_lba;
    fs->type = g_current_fs.type;
    fs->bytes_per_sector = g_current_fs.bytes_per_sector;
    fs->sectors_per_cluster = g_current_fs.sectors_per_cluster;
    fs->reserved_sector_count = g_current_fs.reserved_sector_count;
    fs->num_fats = g_current_fs.num_fats;
    fs->root_entry_count = g_current_fs.root_entry_count;
    fs->total_sectors = g_current_fs.total_sectors;
    fs->fat_size_sectors = g_current_fs.fat_size_sectors;
    fs->first_fat_sector = g_current_fs.first_fat_sector;
    fs->first_data_sector = g_current_fs.first_data_sector;
    fs->total_clusters = g_current_fs.total_clusters;
    fs->root_dir_cluster = g_current_fs.root_dir_cluster;
    fs->root_dir_sectors = g_current_fs.root_dir_sectors;

    for (uint32_t i = 0; i < sizeof(fs->volume_label); i++) {
        fs->volume_label[i] = g_current_fs.volume_label[i];
    }

    for (uint32_t i = 0; i < sizeof(fs->fs_name); i++) {
        fs->fs_name[i] = g_current_fs.fs_name[i];
    }

    fat_irq_restore(flags);
    return true;
}
bool fat_set_active(const char *name) {
    fat_fs_t fs;
    block_device_t *dev;
    char dev_name[8];
    uint32_t part_index = 0;
    uint32_t lba = 0;

    if (!name) {
        kprintf("fat_set_active: name NULL\n");
        return false;
    }

    kprintf("fat_set_active(%s)\n", name);

    if (parse_partition_name(name, dev_name, &part_index)) {
        kprintf("Es particion: %s p%u\n", dev_name, part_index);

        dev = block_get(dev_name);
        if (!dev) {
            kprintf("block_get(%s) = NULL\n", dev_name);
            return false;
        }

        kprintf("Dispositivo encontrado\n");

        if (!read_partition_lba(dev, part_index, &lba)) {
            kprintf("read_partition_lba fallo\n");
            return false;
        }

        kprintf("LBA = %u\n", lba);

        if (!fat_mount(&fs, dev, lba)) {
            kprintf("fat_mount fallo\n");
            return false;
        }
    } else {
        kprintf("No es particion, buscando %s\n", name);

        dev = block_get(name);
        if (!dev) {
            kprintf("block_get(%s) = NULL\n", name);
            return false;
        }

        kprintf("Dispositivo encontrado\n");

        if (!fat_try_mount_device(&fs, dev, name)) {
            kprintf("fat_try_mount_device fallo\n");
            return false;
        }

        kprintf("fat_try_mount_device OK\n");
    }

    g_current_fs = fs;
    g_fs_ready = true;
    g_active_name = name;

    kprintf("fat_set_active OK\n");
    return true;
}

bool fat_mount_default(void) {
    /*
     * Para builds booteadas desde disquete real o QEMU if=floppy, la raiz
     * vive normalmente en fd0. ATA queda como fallback para builds donde la
     * imagen se expone como disco duro.
     */
    const char *preferred[] = {"fd0", "ata0", "ata1", "ata2", "ata3"};

    for (uint32_t i = 0; i < sizeof(preferred)/sizeof(preferred[0]); i++) {
        kprintf("Intentando montar %s...\n", preferred[i]);

        if (fat_set_active(preferred[i])) {
            kprintf("Montado %s OK\n", preferred[i]);
            return true;
        }

        kprintf("Fallo %s\n", preferred[i]);
    }

    return false;
}

const char *fat_get_active_name(void) {
    return g_active_name;
}

void fat_print_info(const fat_fs_t *fs) {
    if (!fs) return;
    kprintf("FAT%u device=%s lba=%u label=%s\n", fs->type, fs->device ? fs->device->name : "?", fs->volume_lba, fs->volume_label);
    kprintf("  bytes/sector=%u sectors/cluster=%u\n", fs->bytes_per_sector, fs->sectors_per_cluster);
    kprintf("  clusters=%u root_cluster=%u\n", fs->total_clusters, fs->root_dir_cluster);
}

bool fat_list_root(const fat_fs_t *fs, fat_dir_entry_t *entries, uint32_t max_entries, uint32_t *count) {
    if (!fs || !entries || !count) return false;
    if (fs->type == FAT_TYPE_FAT12 || fs->type == FAT_TYPE_FAT16) {
        return list_directory_chain(fs, 0, entries, max_entries, count);
    }
    return list_directory_chain(fs, fs->root_dir_cluster, entries, max_entries, count);
}

bool fat_list_dir(const fat_fs_t *fs, const fat_dir_entry_t *dir, fat_dir_entry_t *entries, uint32_t max_entries, uint32_t *count) {
    if (!fs || !entries || !count) return false;
    if (!dir || dir->first_cluster == 0) return fat_list_root(fs, entries, max_entries, count);
    if (!dir->is_directory) return false;
    return list_directory_chain(fs, dir->first_cluster, entries, max_entries, count);
}

static bool fat_find_in_dir(const fat_fs_t *fs, const fat_dir_entry_t *dir, const char *name, fat_dir_entry_t *entry) {
    fat_dir_entry_t entries[64];
    uint32_t count = 0;
    if (!fs || !name || !entry) return false;
    if (!fat_list_dir(fs, dir, entries, 64, &count)) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (name_equals(entries[i].name, name)) {
            *entry = entries[i];
            return true;
        }
    }
    return false;
}

bool fat_find_file(const fat_fs_t *fs, const char *name, fat_dir_entry_t *entry) {
    return fat_find_in_dir(fs, NULL, name, entry);
}

bool fat_resolve_path(const fat_fs_t *fs, const char *path, fat_dir_entry_t *entry) {
    fat_dir_entry_t current;
    fat_dir_entry_t next;
    char component[13];
    uint32_t pos = 0;

    if (!fs || !path || !entry) return false;
    if (path[0] != '/') return false;

    kmemset(&current, 0, sizeof(current));
    current.is_directory = true;
    current.first_cluster = (fs->type == FAT_TYPE_FAT32) ? fs->root_dir_cluster : 0;
    kstrcpy(current.name, "/");

    while (*path == '/') path++;
    if (*path == '\0') {
        *entry = current;
        return true;
    }

    while (true) {
        pos = 0;
        kmemset(component, 0, sizeof(component));
        while (*path && *path != '/') {
            if (pos + 1 >= sizeof(component)) return false;
            component[pos++] = *path++;
        }
        component[pos] = '\0';

        while (*path == '/') path++;
        if (component[0] == '\0' || kstrcmp(component, ".") == 0) {
            if (*path == '\0') {
                *entry = current;
                return true;
            }
            continue;
        }
        if (kstrcmp(component, "..") == 0 && current.first_cluster == 0) {
            if (*path == '\0') {
                *entry = current;
                return true;
            }
            continue;
        }

        if (!current.is_directory) return false;
        if (!fat_find_in_dir(fs, &current, component, &next)) return false;

        current = next;
        if (*path == '\0') {
            *entry = current;
            return true;
        }
    }
}

bool fat_read_file_at(const fat_fs_t *fs, const fat_dir_entry_t *entry, uint32_t file_offset, void *buffer, uint32_t max_size, uint32_t *bytes_read) {
    uint8_t *cluster_buf = NULL;
    uint32_t cluster = entry ? entry->first_cluster : 0;
    uint32_t bytes_left = entry ? entry->size : 0;
    uint32_t out_offset = 0;
    uint32_t cluster_size = 0;
    uint32_t skipped = 0;
    bool io_ok = true;

    if (!fs || !entry || !buffer || !bytes_read) return false;
    *bytes_read = 0;
    if (entry->is_directory) return false;
    if (file_offset >= entry->size) return true;
    if (!cluster) return false;

    cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    cluster_buf = (uint8_t *)kmalloc(cluster_size);
    if (!cluster_buf) return false;

    while (cluster >= 2 && bytes_left > 0) {
        if (!read_cluster(fs, cluster, cluster_buf)) {
            io_ok = false;
            break;
        }
        uint32_t cluster_file_bytes = cluster_size;
        uint32_t local_offset = 0;
        if (cluster_file_bytes > bytes_left) cluster_file_bytes = bytes_left;

        if (skipped + cluster_file_bytes <= file_offset) {
            skipped += cluster_file_bytes;
            bytes_left -= cluster_file_bytes;
        } else {
            uint32_t chunk = cluster_file_bytes;
            if (file_offset > skipped) {
                local_offset = file_offset - skipped;
                chunk -= local_offset;
            }
            if (chunk > max_size - out_offset) chunk = max_size - out_offset;
            if (chunk > 0) {
                kmemcpy((uint8_t *)buffer + out_offset, cluster_buf + local_offset, chunk);
                out_offset += chunk;
            }
            skipped += cluster_file_bytes;
            bytes_left -= cluster_file_bytes;
        }
        if (bytes_left == 0 || out_offset == max_size) break;
        cluster = get_next_cluster(fs, cluster);
        if (cluster == 0xFFFFFFFF) {
            io_ok = false;
            break;
        }
    }

    *bytes_read = out_offset;
    kfree(cluster_buf);
    return io_ok;
}

bool fat_read_file(const fat_fs_t *fs, const fat_dir_entry_t *entry, void *buffer, uint32_t max_size, uint32_t *bytes_read) {
    return fat_read_file_at(fs, entry, 0, buffer, max_size, bytes_read);
}

static bool fat_device_writable(const fat_fs_t *fs) {
    return fs && fs->device && !fs->device->read_only && fs->device->write;
}

static bool write_sector(const fat_fs_t *fs, uint32_t sector, const void *buffer) {
    uint32_t lba;

    if (!fat_device_writable(fs) || !buffer) return false;
    lba = fs->volume_lba + sector;
    if (!block_write(fs->device, lba, 1, buffer)) return false;

    if (g_sector_cache_valid &&
        g_sector_cache_dev == fs->device &&
        g_sector_cache_lba == lba) {
        g_sector_cache_valid = false;
    }
    return true;
}

static uint16_t fat12_table_get(const uint8_t *fat, uint32_t cluster) {
    uint32_t offset = cluster + cluster / 2;
    uint16_t value = read_le16(fat + offset);
    return (cluster & 1) ? (value >> 4) : (value & 0x0FFF);
}

static void fat12_table_set(uint8_t *fat, uint32_t cluster, uint16_t value) {
    uint32_t offset = cluster + cluster / 2;
    value &= 0x0FFF;
    if (cluster & 1) {
        fat[offset] = (uint8_t)((fat[offset] & 0x0F) | (value << 4));
        fat[offset + 1] = (uint8_t)(value >> 4);
    } else {
        fat[offset] = (uint8_t)value;
        fat[offset + 1] = (uint8_t)((fat[offset + 1] & 0xF0) |
                                    ((value >> 8) & 0x0F));
    }
}

static bool fat_make_short_name(const char *name, uint8_t raw[11]) {
    int base = 0;
    int ext = 0;
    bool in_ext = false;
    if (!name || !name[0]) return false;
    kmemset(raw, ' ', 11);
    while (*name) {
        char c = *name++;
        if (c == '.') {
            if (in_ext) return false;
            in_ext = true;
            continue;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '/' || c == '\\' || c == ' ' || (uint8_t)c < 32) return false;
        if (!in_ext) {
            if (base >= 8) return false;
            raw[base++] = (uint8_t)c;
        } else {
            if (ext >= 3) return false;
            raw[8 + ext++] = (uint8_t)c;
        }
    }
    return base > 0;
}

static bool fat_split_parent(const char *path, char *parent, char *name) {
    size_t len;
    size_t slash = 0;
    if (!path || path[0] != '/') return false;
    len = kstrlen(path);
    if (len < 2 || len >= 128) return false;
    for (size_t i = 1; i < len; i++) if (path[i] == '/') slash = i;
    if (slash == 0) {
        kstrcpy(parent, "/");
        kstrncpy(name, path + 1, 12);
    } else {
        kstrncpy(parent, path, slash);
        parent[slash] = '\0';
        kstrncpy(name, path + slash + 1, 12);
    }
    name[12] = '\0';
    return name[0] != '\0';
}

static bool fat_find_dir_slot(const fat_fs_t *fs, const fat_dir_entry_t *dir,
                              const uint8_t raw_name[11], uint32_t *sector_out,
                              uint32_t *offset_out, fat_dir_entry_t *old,
                              bool *exists) {
    uint32_t first_sector;
    uint32_t sectors;
    uint32_t free_sector = 0xFFFFFFFF;
    uint32_t free_offset = 0;
    if (!dir || !dir->is_directory) return false;
    if (dir->first_cluster == 0 && fs->type != FAT_TYPE_FAT32) {
        first_sector = fs->first_fat_sector +
                       fs->num_fats * fs->fat_size_sectors;
        sectors = fs->root_dir_sectors;
    } else {
        if (dir->first_cluster < 2) return false;
        first_sector = fs->first_data_sector +
                       (dir->first_cluster - 2) * fs->sectors_per_cluster;
        sectors = fs->sectors_per_cluster;
    }
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t buffer[512];
        if (!read_sector(fs, first_sector + s, buffer)) return false;
        for (uint32_t off = 0; off < 512; off += 32) {
            uint8_t *entry = buffer + off;
            if ((entry[0] == 0 || entry[0] == 0xE5) &&
                free_sector == 0xFFFFFFFF) {
                free_sector = first_sector + s;
                free_offset = off;
            }
            if (entry[0] && entry[0] != 0xE5 &&
                entry[11] != FAT_ATTR_LONG_NAME &&
                kmemcmp(entry, raw_name, 11) == 0) {
                if (old) parse_dir_entry(entry, old);
                *sector_out = first_sector + s;
                *offset_out = off;
                *exists = true;
                return true;
            }
        }
    }
    if (free_sector == 0xFFFFFFFF) return false;
    *sector_out = free_sector;
    *offset_out = free_offset;
    *exists = false;
    return true;
}

static bool fat_commit_table(const fat_fs_t *fs, const uint8_t *table) {
    for (uint8_t copy = 0; copy < fs->num_fats; copy++)
        for (uint32_t s = 0; s < fs->fat_size_sectors; s++)
            if (!write_sector(fs, fs->first_fat_sector +
                              copy * fs->fat_size_sectors + s,
                              table + s * fs->bytes_per_sector))
                return false;
    return true;
}

static bool fat_store_path(fat_fs_t *fs, const char *path, const void *data,
                           uint32_t size, bool directory) {
    char parent_path[128];
    char name[13];
    uint8_t raw_name[11];
    fat_dir_entry_t parent;
    fat_dir_entry_t old;
    bool exists;
    uint32_t dir_sector, dir_offset;
    uint32_t fat_bytes;
    uint8_t *table;
    uint32_t cluster_size;
    uint32_t needed;
    uint32_t first = 0;
    uint32_t previous = 0;

    /*
     * Si el dispositivo no tiene writer, no intentes modificar FAT.
     * fd0 por ahora es lectura solamente; sin este corte temprano, cualquier
     * vfs_write_all()/mkdir escanea la FAT y lee varios sectores del floppy
     * antes de fallar, congelando la GUI.
     */
    if (!fat_device_writable(fs)) return false;

    if (!fs || fs->type != FAT_TYPE_FAT12 ||
        !fat_split_parent(path, parent_path, name) ||
        !fat_make_short_name(name, raw_name) ||
        !fat_resolve_path(fs, parent_path, &parent) || !parent.is_directory)
        return false;
    if (!fat_find_dir_slot(fs, &parent, raw_name, &dir_sector, &dir_offset,
                           &old, &exists))
        return false;
    if (exists && old.is_directory != directory) return false;

    fat_bytes = fs->fat_size_sectors * fs->bytes_per_sector;
    table = (uint8_t *)kmalloc(fat_bytes);
    if (!table) return false;
    for (uint32_t s = 0; s < fs->fat_size_sectors; s++)
        if (!read_sector(fs, fs->first_fat_sector + s,
                         table + s * fs->bytes_per_sector)) {
            kfree(table);
            return false;
        }

    if (exists && old.first_cluster >= 2) {
        uint32_t cluster = old.first_cluster;
        while (cluster >= 2 && cluster < 0x0FF8) {
            uint16_t next = fat12_table_get(table, cluster);
            fat12_table_set(table, cluster, 0);
            cluster = next;
        }
    }

    cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    needed = directory ? 1 : ((size + cluster_size - 1) / cluster_size);
    for (uint32_t n = 0; n < needed; n++) {
        uint32_t cluster = 0;
        for (uint32_t candidate = 2;
             candidate < fs->total_clusters + 2; candidate++) {
            if (fat12_table_get(table, candidate) == 0) {
                cluster = candidate;
                break;
            }
        }
        if (!cluster) {
            kfree(table);
            return false;
        }
        fat12_table_set(table, cluster, 0x0FFF);
        if (previous) fat12_table_set(table, previous, (uint16_t)cluster);
        else first = cluster;
        previous = cluster;
    }

    if (!fat_commit_table(fs, table)) {
        kfree(table);
        return false;
    }
    kfree(table);

    if (directory) {
        uint8_t cluster_data[512];
        kmemset(cluster_data, 0, sizeof(cluster_data));
        kmemset(cluster_data, ' ', 11);
        cluster_data[0] = '.';
        cluster_data[11] = FAT_ATTR_DIRECTORY;
        cluster_data[26] = (uint8_t)first;
        cluster_data[27] = (uint8_t)(first >> 8);
        kmemset(cluster_data + 32, ' ', 11);
        cluster_data[32] = '.';
        cluster_data[33] = '.';
        cluster_data[43] = FAT_ATTR_DIRECTORY;
        cluster_data[58] = (uint8_t)parent.first_cluster;
        cluster_data[59] = (uint8_t)(parent.first_cluster >> 8);
        if (!write_sector(fs, fs->first_data_sector + (first - 2) *
                          fs->sectors_per_cluster, cluster_data))
            return false;
    } else {
        uint32_t written = 0;
        uint32_t cluster = first;
        while (cluster >= 2 && written < size) {
            for (uint8_t s = 0; s < fs->sectors_per_cluster &&
                 written < size; s++) {
                uint8_t sector_data[512];
                uint32_t chunk = size - written;
                if (chunk > 512) chunk = 512;
                kmemset(sector_data, 0, sizeof(sector_data));
                kmemcpy(sector_data, (const uint8_t *)data + written, chunk);
                if (!write_sector(fs, fs->first_data_sector +
                                  (cluster - 2) * fs->sectors_per_cluster + s,
                                  sector_data))
                    return false;
                written += chunk;
            }
            cluster = get_next_cluster(fs, cluster);
        }
    }

    {
        uint8_t sector_data[512];
        if (!read_sector(fs, dir_sector, sector_data)) return false;
        uint8_t *entry = sector_data + dir_offset;
        kmemset(entry, 0, 32);
        kmemcpy(entry, raw_name, 11);
        entry[11] = directory ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
        entry[26] = (uint8_t)first;
        entry[27] = (uint8_t)(first >> 8);
        entry[28] = (uint8_t)size;
        entry[29] = (uint8_t)(size >> 8);
        entry[30] = (uint8_t)(size >> 16);
        entry[31] = (uint8_t)(size >> 24);
        return write_sector(fs, dir_sector, sector_data);
    }
}

bool fat_write_path(fat_fs_t *fs, const char *path, const void *data,
                    uint32_t size) {
    if (size && !data) return false;
    return fat_store_path(fs, path, data, size, false);
}

bool fat_mkdir_path(fat_fs_t *fs, const char *path) {
    return fat_store_path(fs, path, NULL, 0, true);
}
