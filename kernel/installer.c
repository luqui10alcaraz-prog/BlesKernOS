#include "include/installer.h"
#include "include/block.h"
#include "include/fat.h"
#include "include/iso9660.h"
#include "include/keyboard.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/types.h"
#include "include/vfs.h"
#include "include/vga.h"
#include "stdio.h"
#include "string.h"

#define SETUP_VERSION "0.8"
#define SETUP_MAX_TARGETS 8U
#define SETUP_MAX_ENTRIES 256U
#define SETUP_MAX_DEPTH 16U

#define INSTALL_STAGE2_LBA 1U
#define INSTALL_STAGE2_SECTORS 8U
#define INSTALL_KERNEL_LBA 9U
#define INSTALL_KERNEL_RESERVED_SECTORS 2048U
#define INSTALL_FSINFO_SECTOR \
    (INSTALL_KERNEL_LBA + INSTALL_KERNEL_RESERVED_SECTORS)
#define INSTALL_BACKUP_BOOT_SECTOR (INSTALL_FSINFO_SECTOR + 1U)
#define INSTALL_BACKUP_FSINFO_SECTOR (INSTALL_FSINFO_SECTOR + 2U)
#define INSTALL_RESERVED_SECTORS (INSTALL_BACKUP_FSINFO_SECTOR + 1U)

#define VGA_TEXT_MEMORY ((volatile uint16_t *)0x000B8000U)
#define UI_WIDTH 80
#define UI_HEIGHT 25

#define UI_ATTR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))
#define UI_NORMAL UI_ATTR(VGA_BLACK, VGA_LIGHT_GREY)
#define UI_DESKTOP UI_ATTR(VGA_WHITE, VGA_BLUE)
#define UI_TITLE UI_ATTR(VGA_WHITE, VGA_BLUE)
#define UI_STATUS UI_ATTR(VGA_BLACK, VGA_LIGHT_GREY)
#define UI_SELECT UI_ATTR(VGA_WHITE, VGA_BLUE)
#define UI_WARNING UI_ATTR(VGA_YELLOW, VGA_RED)
#define UI_OK UI_ATTR(VGA_LIGHT_GREEN, VGA_BLACK)
#define UI_ERROR UI_ATTR(VGA_LIGHT_RED, VGA_BLACK)

#define KEY_BYTE(c) ((uint8_t)(c))

typedef struct {
    block_device_t *device;
    char name[8];
    char type[16];
    uint32_t size_mb;
    bool floppy;
    bool partition;
} installer_target_t;

typedef struct {
    bool mounted;
    bool fat32;
    bool boot_layout;
    bool has_files;
    uint32_t root_entries;
    uint32_t reserved_sectors;
    char label[12];
} installer_disk_info_t;

typedef struct {
    uint32_t files_total;
    uint32_t files_done;
    uint32_t directories;
    uint32_t bytes_total;
    uint32_t bytes_done;
    char current_path[VFS_MAX_PATH];
    char error[VFS_MAX_PATH];
} installer_copy_state_t;

static installer_copy_state_t g_copy;

static uint16_t ui_cell(char character, uint8_t attribute) {
    return (uint16_t)((uint16_t)attribute << 8) | (uint8_t)character;
}

static void ui_fill(int x, int y, int width, int height, char character,
                    uint8_t attribute) {
    if (x < 0 || y < 0 || width <= 0 || height <= 0) return;
    for (int row = 0; row < height && y + row < UI_HEIGHT; row++) {
        for (int column = 0; column < width && x + column < UI_WIDTH;
             column++) {
            VGA_TEXT_MEMORY[(y + row) * UI_WIDTH + x + column] =
                ui_cell(character, attribute);
        }
    }
}

static void ui_text(int x, int y, const char *text, uint8_t attribute) {
    if (!text || y < 0 || y >= UI_HEIGHT) return;
    while (*text && x < UI_WIDTH) {
        if (x >= 0)
            VGA_TEXT_MEMORY[y * UI_WIDTH + x] = ui_cell(*text, attribute);
        x++;
        text++;
    }
}

static void ui_text_clipped(int x, int y, const char *text, int width,
                            uint8_t attribute) {
    int written = 0;
    if (!text || width <= 0) return;
    while (*text && written < width) {
        char value = *text++;
        if ((uint8_t)value < 32U) value = ' ';
        if (x + written >= 0 && x + written < UI_WIDTH &&
            y >= 0 && y < UI_HEIGHT) {
            VGA_TEXT_MEMORY[y * UI_WIDTH + x + written] =
                ui_cell(value, attribute);
        }
        written++;
    }
    while (written < width) {
        if (x + written >= 0 && x + written < UI_WIDTH &&
            y >= 0 && y < UI_HEIGHT) {
            VGA_TEXT_MEMORY[y * UI_WIDTH + x + written] =
                ui_cell(' ', attribute);
        }
        written++;
    }
}

static void ui_box(int x, int y, int width, int height, uint8_t attribute) {
    if (width < 2 || height < 2) return;
    ui_fill(x, y, width, height, ' ', attribute);
    for (int column = 1; column < width - 1; column++) {
        ui_fill(x + column, y, 1, 1, '-', attribute);
        ui_fill(x + column, y + height - 1, 1, 1, '-', attribute);
    }
    for (int row = 1; row < height - 1; row++) {
        ui_fill(x, y + row, 1, 1, '|', attribute);
        ui_fill(x + width - 1, y + row, 1, 1, '|', attribute);
    }
    ui_fill(x, y, 1, 1, '+', attribute);
    ui_fill(x + width - 1, y, 1, 1, '+', attribute);
    ui_fill(x, y + height - 1, 1, 1, '+', attribute);
    ui_fill(x + width - 1, y + height - 1, 1, 1, '+', attribute);
}

static void ui_center(int y, const char *text, uint8_t attribute) {
    int length = (int)kstrlen(text ? text : "");
    int x = (UI_WIDTH - length) / 2;
    ui_text(x < 0 ? 0 : x, y, text, attribute);
}

static void ui_frame(const char *subtitle, const char *footer) {
    char title[80];
    ui_fill(0, 0, UI_WIDTH, UI_HEIGHT, ' ', UI_DESKTOP);
    ui_fill(0, 0, UI_WIDTH, 1, ' ', UI_TITLE);
    snprintf(title, sizeof(title), " BlesKernOS Setup %s ", SETUP_VERSION);
    ui_center(0, title, UI_TITLE);
    ui_box(2, 2, 76, 20, UI_NORMAL);
    if (subtitle) ui_text(5, 3, subtitle, UI_NORMAL);
    ui_fill(0, 24, UI_WIDTH, 1, ' ', UI_STATUS);
    if (footer) ui_text_clipped(2, 24, footer, 76, UI_STATUS);
    vga_set_cursor(79, 24);
}

static uint8_t ui_wait_key(void) {
    return KEY_BYTE(kbd_getchar());
}

static bool ui_wait_enter_or_escape(void) {
    for (;;) {
        uint8_t key = ui_wait_key();
        if (key == KEY_ENTER || key == '\n') return true;
        if (key == KEY_ESCAPE) return false;
    }
}

static void ui_progress(uint32_t current, uint32_t total,
                        const char *message) {
    char count[48];
    uint32_t filled = total ? (current * 50U) / total : 0U;
    if (filled > 50U) filled = 50U;
    ui_frame("Instalando BlesKernOS", "No apague el equipo ni retire el CD.");
    ui_text_clipped(7, 7, message ? message : "Procesando...", 66,
                    UI_NORMAL);
    ui_fill(14, 11, 52, 3, ' ', UI_NORMAL);
    ui_fill(14, 11, 52, 1, '[', UI_NORMAL);
    ui_fill(65, 11, 1, 1, ']', UI_NORMAL);
    for (uint32_t i = 0; i < 50U; i++) {
        ui_fill(15 + (int)i, 11, 1, 1, i < filled ? '#' : '.',
                i < filled ? UI_SELECT : UI_NORMAL);
    }
    snprintf(count, sizeof(count), "%u de %u archivos", current, total);
    ui_center(14, count, UI_NORMAL);
    ui_text_clipped(7, 17, g_copy.current_path, 66, UI_NORMAL);
}

static void installer_halt_screen(const char *message) {
    ui_frame("Setup detenido", "Puede apagar o reiniciar el equipo.");
    ui_center(9, message ? message : "Instalador finalizado.", UI_NORMAL);
    ui_center(12, "El sistema no fue modificado despues de este punto.",
              UI_NORMAL);
    for (;;) __asm__ volatile ("hlt");
}

static void installer_reboot(void) {
    ui_fill(0, 24, UI_WIDTH, 1, ' ', UI_STATUS);
    ui_text(2, 24, "Reiniciando...", UI_STATUS);
    for (uint32_t wait = 0; wait < 100000U; wait++) io_wait();
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}

static uint32_t installer_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool installer_partition_type_is_fat(uint8_t type) {
    switch (type) {
        case 0x01U: case 0x04U: case 0x06U: case 0x0BU: case 0x0CU:
        case 0x0EU: case 0x11U: case 0x14U: case 0x16U: case 0x1BU:
        case 0x1CU: case 0x1EU:
            return true;
        default:
            return false;
    }
}

static bool installer_has_partition_table(block_device_t *device) {
    uint8_t mbr[BLOCK_SECTOR_SIZE];
    if (!device || device->type == BLOCK_DEVICE_FLOPPY ||
        !block_read(device, 0U, 1U, mbr) ||
        mbr[510] != 0x55U || mbr[511] != 0xAAU) return false;
    for (uint32_t index = 0; index < 4U; index++) {
        uint32_t offset = 446U + index * 16U;
        uint8_t type = mbr[offset + 4U];
        uint32_t first = installer_le32(mbr + offset + 8U);
        uint32_t sectors = installer_le32(mbr + offset + 12U);
        if (type && first && sectors && type != 0xEEU) return true;
    }
    return false;
}

/* Expose MBR primary partitions as bounded block devices. This makes FAT
 * formatting operate relative to the selected partition instead of LBA 0 of
 * the physical disk. Extended containers and GPT protective entries are not
 * installable targets. Existing MBR bytes are never modified. */
static void installer_register_partition_views(void) {
    uint32_t physical_count = block_count();
    for (uint32_t index = 0; index < physical_count; index++) {
        block_device_t *device = block_at(index);
        uint8_t mbr[BLOCK_SECTOR_SIZE];
        if (!device || device->type == BLOCK_DEVICE_FLOPPY ||
            device->type == BLOCK_DEVICE_ATAPI || device->read_only ||
            !device->write || device->sector_size != BLOCK_SECTOR_SIZE ||
            !block_read(device, 0U, 1U, mbr) ||
            mbr[510] != 0x55U || mbr[511] != 0xAAU) continue;
        for (uint32_t part = 0; part < 4U; part++) {
            uint32_t offset = 446U + part * 16U;
            uint8_t type = mbr[offset + 4U];
            uint32_t first = installer_le32(mbr + offset + 8U);
            uint32_t sectors = installer_le32(mbr + offset + 12U);
            char name[8];
            if (!installer_partition_type_is_fat(type) || !first ||
                sectors < 131072U ||
                (device->sector_count &&
                 (first >= device->sector_count ||
                  sectors > device->sector_count - first))) continue;
            snprintf(name, sizeof(name), "%sp%u", device->name, part + 1U);
            (void)block_register_view(name, device, first, sectors);
        }
    }
}

static uint32_t installer_collect_targets(installer_target_t *targets,
                                          uint32_t capacity) {
    uint32_t count = 0;
    if (!targets || !capacity) return 0;
    installer_register_partition_views();
    for (uint32_t index = 0; index < block_count() && count < capacity;
         index++) {
        block_device_t *device = block_at(index);
        installer_target_t *target;
        if (!device || device->type == BLOCK_DEVICE_ATAPI ||
            device->read_only || !device->write ||
            device->sector_size != BLOCK_SECTOR_SIZE) continue;
        /* A partitioned physical disk is intentionally hidden. Setup lists
         * its bounded p1..p4 views, preventing an accidental whole-disk wipe. */
        if (device->base_lba == 0U && device->type != BLOCK_DEVICE_FLOPPY &&
            installer_has_partition_table(device)) continue;
        target = &targets[count++];
        kmemset(target, 0, sizeof(*target));
        target->device = device;
        kstrncpy(target->name, device->name, sizeof(target->name) - 1U);
        kstrncpy(target->type, block_type_name(device->type),
                 sizeof(target->type) - 1U);
        target->size_mb = device->sector_count / 2048U;
        target->floppy = device->type == BLOCK_DEVICE_FLOPPY;
        target->partition = device->base_lba != 0U;
    }
    return count;
}

static int installer_choose_target(installer_target_t *targets,
                                   uint32_t count) {
    uint32_t selected = 0;
    for (;;) {
        ui_frame("Seleccione el dispositivo de destino",
                 "Flechas: mover  Enter: opciones  R: actualizar  Esc: salir");
        if (!count) {
            ui_center(9, "No se encontraron dispositivos escribibles.",
                      UI_NORMAL);
            ui_center(11, "Conecte un disco y reinicie Setup.", UI_NORMAL);
        }
        for (uint32_t index = 0; index < count && index < 12U; index++) {
            char line[68];
            uint8_t attr = index == selected ? UI_SELECT : UI_NORMAL;
            snprintf(line, sizeof(line), "%-7s  %-12s  %u MiB  %s",
                     targets[index].name, targets[index].type,
                     targets[index].size_mb,
                     targets[index].floppy ? "formato FAT12" :
                     (targets[index].partition ? "particion instalable" : "disco instalable"));
            ui_text_clipped(7, 6 + (int)index, line, 66, attr);
        }
        for (;;) {
            uint8_t key = ui_wait_key();
            if (key == KEY_ESCAPE) return -1;
            if (key == 'r' || key == 'R') return -2;
            if (!count) continue;
            if (key == KEY_UP && selected > 0U) {
                selected--;
                break;
            }
            if (key == KEY_DOWN && selected + 1U < count) {
                selected++;
                break;
            }
            if (key == KEY_ENTER || key == '\n') return (int)selected;
        }
    }
}

static bool installer_confirm_destroy(const installer_target_t *target,
                                      const char *operation) {
    static const char confirmation[] = "BORRAR";
    char typed[sizeof(confirmation)];
    char line[72];
    uint32_t length = 0U;

    kmemset(typed, 0, sizeof(typed));
    for (;;) {
        ui_frame("Confirmacion destructiva",
                 "Escriba BORRAR para continuar. Esc: cancelar");
        snprintf(line, sizeof(line), "%s en %s (%u MiB)",
                 operation ? operation : "Operacion", target->name,
                 target->size_mb);
        ui_center(6, line, UI_NORMAL);
        ui_fill(6, 9, 68, 5, ' ', UI_WARNING);
        ui_center(9, "ADVERTENCIA: SE ELIMINARAN TODOS LOS ARCHIVOS",
                  UI_WARNING);
        ui_center(11, "Esta operacion no se puede deshacer.", UI_WARNING);
        ui_text(25, 16, "Confirmacion: [", UI_NORMAL);
        ui_text_clipped(40, 16, typed, 6, UI_SELECT);
        ui_text(46, 16, "]", UI_NORMAL);

        {
            uint8_t key = ui_wait_key();
            if (key == KEY_ESCAPE) return false;
            if (key == KEY_BACKSPACE) {
                if (length) typed[--length] = '\0';
                continue;
            }
            if (key == KEY_ENTER || key == '\n') {
                if (kstrcmp(typed, confirmation) == 0) return true;
                length = 0U;
                typed[0] = '\0';
                continue;
            }
            if (key >= 'a' && key <= 'z') key = (uint8_t)(key - 'a' + 'A');
            if (key >= 'A' && key <= 'Z' && length + 1U < sizeof(typed)) {
                typed[length++] = (char)key;
                typed[length] = '\0';
            }
        }
    }
}

static void installer_probe_disk(const installer_target_t *target,
                                 installer_disk_info_t *info) {
    char root[VFS_MAX_PATH];
    fat_fs_t fs;
    vfs_dir_entry_t entries[32];
    uint32_t count = 0U;

    if (!info) return;
    kmemset(info, 0, sizeof(*info));
    if (!target || !target->device) return;

    snprintf(root, sizeof(root), "/%s", target->name);
    vfs_forget_volume(target->name);
    if (fat_mount_named(&fs, target->name)) {
        info->mounted = true;
        info->fat32 = fs.type == FAT_TYPE_FAT32;
        info->reserved_sectors = fs.reserved_sector_count;
        info->boot_layout = info->fat32 &&
                            fs.reserved_sector_count >= INSTALL_RESERVED_SECTORS;
        kstrncpy(info->label, fs.volume_label, sizeof(info->label) - 1U);
    }
    if (vfs_listdir(root, entries, 32U, &count)) {
        info->root_entries = count;
        info->has_files = count != 0U;
    }
    vfs_forget_volume(target->name);
}

static bool installer_confirm_existing_files(const installer_target_t *target,
                                             const installer_disk_info_t *info) {
    char line[72];
    ui_frame("Instalar sin formatear",
             "I: continuar conservando archivos  Esc: volver");
    snprintf(line, sizeof(line), "Destino: %s, FAT32, %u entradas visibles",
             target->name, info ? info->root_entries : 0U);
    ui_center(6, line, UI_NORMAL);
    ui_fill(5, 9, 70, 6, ' ', UI_WARNING);
    ui_center(9, "El disco ya contiene archivos.", UI_WARNING);
    ui_center(11, "Setup conservara archivos ajenos, pero reemplazara",
              UI_WARNING);
    ui_center(12, "/SYSTEM, /PROGRAMS y otros archivos de BlesKernOS.",
              UI_WARNING);
    ui_center(16, "Se recomienda hacer una copia de seguridad.", UI_NORMAL);
    for (;;) {
        uint8_t key = ui_wait_key();
        if (key == KEY_ESCAPE) return false;
        if (key == 'i' || key == 'I') return true;
    }
}

static void installer_floppy_format(const installer_target_t *target) {
    ui_frame("Formateando disquete", "Espere mientras se escribe FAT12...");
    ui_center(7, "Los disquetes de 1.44 MB usan FAT12, no FAT32.", UI_NORMAL);
    ui_center(9, "FAT32 necesita muchos mas clusters y no es valido aqui.",
              UI_NORMAL);
    ui_center(12, "Formateando...", UI_NORMAL);
    vfs_forget_volume(target->name);
    if (!fat_format(target->name, "BLESFLOPPY")) {
        ui_frame("Error al formatear", "Enter: volver a la lista");
        ui_center(9, "No se pudo escribir el disquete.", UI_ERROR);
        ui_center(11, "Revise el medio, la proteccion y la unidad.", UI_NORMAL);
        (void)ui_wait_enter_or_escape();
        return;
    }
    ui_frame("Disquete listo", "Enter: volver a la lista");
    ui_center(8, "Formato FAT12 completado correctamente.", UI_OK);
    ui_center(11, "La instalacion completa no cabe en 1.44 MB.", UI_NORMAL);
    (void)ui_wait_enter_or_escape();
}

static bool installer_iso_file(const char *path, void **buffer,
                               uint32_t *size) {
    iso9660_entry_t entry;
    uint8_t *data;
    uint32_t got = 0;
    if (!path || !buffer || !size || !iso9660_resolve(path, &entry) ||
        entry.is_directory) return false;
    data = (uint8_t *)kmalloc(entry.size ? entry.size : 1U);
    if (!data) return false;
    if (entry.size &&
        (!iso9660_read_at(&entry, 0, data, entry.size, &got) ||
         got != entry.size)) {
        kfree(data);
        return false;
    }
    *buffer = data;
    *size = entry.size;
    return true;
}

static bool installer_write_raw(block_device_t *device, const char *iso_path,
                                uint32_t first_lba, uint32_t max_sectors) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint8_t *data;
    uint32_t size;
    uint32_t sectors;
    if (!device || !installer_iso_file(iso_path, (void **)&data, &size))
        return false;
    sectors = (size + BLOCK_SECTOR_SIZE - 1U) / BLOCK_SECTOR_SIZE;
    if (!sectors || sectors > max_sectors ||
        first_lba + sectors > device->sector_count) {
        kfree(data);
        return false;
    }
    for (uint32_t sector_index = 0; sector_index < sectors; sector_index++) {
        uint32_t offset = sector_index * BLOCK_SECTOR_SIZE;
        uint32_t chunk = size - offset;
        if (chunk > BLOCK_SECTOR_SIZE) chunk = BLOCK_SECTOR_SIZE;
        kmemset(sector, 0, sizeof(sector));
        if (chunk) kmemcpy(sector, data + offset, chunk);
        if (!block_write(device, first_lba + sector_index, 1, sector)) {
            kfree(data);
            return false;
        }
    }
    kfree(data);
    return true;
}

static bool installer_patch_boot_sector(block_device_t *device) {
    uint8_t *template_data;
    uint32_t template_size;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (!device ||
        !installer_iso_file("/INSTALL/BOOTF32.BIN",
                            (void **)&template_data, &template_size))
        return false;
    if (template_size != BLOCK_SECTOR_SIZE) {
        kfree(template_data);
        return false;
    }
    if (!block_read(device, 0, 1, sector)) {
        kfree(template_data);
        return false;
    }
    kmemcpy(sector, template_data, 3U);
    kmemcpy(sector + 90U, template_data + 90U,
            BLOCK_SECTOR_SIZE - 90U);
    /* FAT32 BPB_HiddSec: Stage 1 and Stage 2 add this value to every BIOS
     * read. Zero for a superfloppy, partition start LBA for ata0pN. */
    sector[28] = (uint8_t)(device->base_lba);
    sector[29] = (uint8_t)(device->base_lba >> 8);
    sector[30] = (uint8_t)(device->base_lba >> 16);
    sector[31] = (uint8_t)(device->base_lba >> 24);
    sector[510] = 0x55U;
    sector[511] = 0xAAU;
    kfree(template_data);
    if (!block_write(device, 0, 1, sector) ||
        !block_write(device, INSTALL_BACKUP_BOOT_SECTOR, 1, sector))
        return false;
    return true;
}

static bool installer_should_skip(const char *parent, const char *name) {
    if (!parent || !name) return true;
    if (kstrcmp(parent, "/") != 0) return false;
    return kstrcmp(name, "INSTALL") == 0 ||
           kstrcmp(name, "BOOT.IMG") == 0 ||
           kstrcmp(name, "BOOT.CAT") == 0 ||
           kstrcmp(name, "CDINFO.TXT") == 0;
}

static bool installer_join(char *out, uint32_t capacity, const char *parent,
                           const char *name) {
    uint32_t parent_length;
    uint32_t name_length;
    if (!out || !capacity || !parent || !name) return false;
    parent_length = (uint32_t)kstrlen(parent);
    name_length = (uint32_t)kstrlen(name);
    if (parent_length + name_length + 2U > capacity) return false;
    kstrncpy(out, parent, capacity - 1U);
    out[capacity - 1U] = '\0';
    if (kstrcmp(parent, "/") != 0) kstrcat(out, "/");
    kstrcat(out, name);
    return true;
}

static bool installer_scan_tree(const char *source_path, uint32_t depth) {
    iso9660_entry_t directory;
    iso9660_entry_t *entries;
    uint32_t count = 0;
    if (!source_path || depth > SETUP_MAX_DEPTH ||
        !iso9660_resolve(source_path, &directory) ||
        !directory.is_directory) return false;
    entries = (iso9660_entry_t *)kmalloc(sizeof(*entries) * SETUP_MAX_ENTRIES);
    if (!entries) return false;
    if (!iso9660_list(&directory, entries, SETUP_MAX_ENTRIES, &count)) {
        kfree(entries);
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        char child[VFS_MAX_PATH];
        if (installer_should_skip(source_path, entries[index].name)) continue;
        if (!installer_join(child, sizeof(child), source_path,
                            entries[index].name)) {
            kfree(entries);
            return false;
        }
        if (entries[index].is_directory) {
            g_copy.directories++;
            if (!installer_scan_tree(child, depth + 1U)) {
                kfree(entries);
                return false;
            }
        } else {
            g_copy.files_total++;
            if (0xFFFFFFFFU - g_copy.bytes_total < entries[index].size)
                g_copy.bytes_total = 0xFFFFFFFFU;
            else
                g_copy.bytes_total += entries[index].size;
        }
    }
    kfree(entries);
    return true;
}

static bool installer_ensure_directory(const char *path) {
    vfs_dir_entry_t entry;
    if (!path || !path[0]) return false;
    if (vfs_stat(path, &entry)) return entry.type == VFS_NODE_DIR;
    return vfs_mkdir(path);
}

static bool installer_remove_tree(const char *path, uint32_t depth) {
    vfs_dir_entry_t entry;
    vfs_dir_entry_t *entries;
    uint32_t count = 0U;
    if (!path || depth > SETUP_MAX_DEPTH) return false;
    if (!vfs_stat(path, &entry)) return true;
    if (entry.type != VFS_NODE_DIR) return vfs_remove(path);
    entries = (vfs_dir_entry_t *)kmalloc(sizeof(*entries) * SETUP_MAX_ENTRIES);
    if (!entries) return false;
    if (!vfs_listdir(path, entries, SETUP_MAX_ENTRIES, &count)) {
        kfree(entries);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        char child[VFS_MAX_PATH];
        if (kstrcmp(entries[i].name, ".") == 0 ||
            kstrcmp(entries[i].name, "..") == 0) continue;
        if (!installer_join(child, sizeof(child), path, entries[i].name) ||
            !installer_remove_tree(child, depth + 1U)) {
            kfree(entries);
            return false;
        }
    }
    kfree(entries);
    return vfs_remove(path);
}

static bool installer_clear_owned_tree(const char *destination_root) {
    static const char *owned[] = {
        "SYSTEM", "PROGRAMS", "ICONS", "DESKTOP", "DOCUMENTS"
    };
    char path[VFS_MAX_PATH];
    for (uint32_t i = 0; i < sizeof(owned) / sizeof(owned[0]); i++) {
        if (!installer_join(path, sizeof(path), destination_root, owned[i]) ||
            !installer_remove_tree(path, 0U)) return false;
    }
    return true;
}

static bool installer_copy_tree(const char *source_path,
                                const char *destination_path,
                                uint32_t depth) {
    iso9660_entry_t directory;
    iso9660_entry_t *entries;
    uint32_t count = 0;
    if (!source_path || !destination_path || depth > SETUP_MAX_DEPTH ||
        !iso9660_resolve(source_path, &directory) ||
        !directory.is_directory) return false;
    entries = (iso9660_entry_t *)kmalloc(sizeof(*entries) * SETUP_MAX_ENTRIES);
    if (!entries) return false;
    if (!iso9660_list(&directory, entries, SETUP_MAX_ENTRIES, &count)) {
        kfree(entries);
        return false;
    }
    for (uint32_t index = 0; index < count; index++) {
        char source_child[VFS_MAX_PATH];
        char destination_child[VFS_MAX_PATH];
        if (installer_should_skip(source_path, entries[index].name)) continue;
        if (!installer_join(source_child, sizeof(source_child), source_path,
                            entries[index].name) ||
            !installer_join(destination_child, sizeof(destination_child),
                            destination_path, entries[index].name)) {
            kstrncpy(g_copy.error, entries[index].name,
                     sizeof(g_copy.error) - 1U);
            kfree(entries);
            return false;
        }
        if (entries[index].is_directory) {
            if (!installer_ensure_directory(destination_child) ||
                !installer_copy_tree(source_child, destination_child,
                                     depth + 1U)) {
                kstrncpy(g_copy.error, destination_child,
                         sizeof(g_copy.error) - 1U);
                kfree(entries);
                return false;
            }
        } else {
            uint8_t *data = (uint8_t *)kmalloc(entries[index].size
                                               ? entries[index].size : 1U);
            uint32_t got = 0;
            if (!data ||
                (entries[index].size &&
                 (!iso9660_read_at(&entries[index], 0, data,
                                   entries[index].size, &got) ||
                  got != entries[index].size)) ||
                !vfs_write_all(destination_child, data,
                               entries[index].size)) {
                if (data) kfree(data);
                kstrncpy(g_copy.error, destination_child,
                         sizeof(g_copy.error) - 1U);
                kfree(entries);
                return false;
            }
            kfree(data);
            g_copy.files_done++;
            if (0xFFFFFFFFU - g_copy.bytes_done < entries[index].size)
                g_copy.bytes_done = 0xFFFFFFFFU;
            else
                g_copy.bytes_done += entries[index].size;
            kstrncpy(g_copy.current_path, source_child,
                     sizeof(g_copy.current_path) - 1U);
            g_copy.current_path[sizeof(g_copy.current_path) - 1U] = '\0';
            ui_progress(g_copy.files_done, g_copy.files_total,
                        "Copiando bootstrap y paquetes BKL3...");
        }
    }
    kfree(entries);
    return true;
}

static bool installer_verify(const installer_target_t *target) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    char path[VFS_MAX_PATH];
    vfs_dir_entry_t entry;
    if (!block_read(target->device, 0, 1, sector) ||
        sector[510] != 0x55U || sector[511] != 0xAAU) return false;
    snprintf(path, sizeof(path), "/%s/SYSTEM/PROGRAMS/SETUP.BEX",
             target->name);
    if (!vfs_stat(path, &entry) || entry.type != VFS_NODE_FILE || !entry.size)
        return false;
    snprintf(path, sizeof(path), "/%s/SYSTEM/USER/START.INI", target->name);
    if (!vfs_stat(path, &entry) || entry.type != VFS_NODE_FILE || !entry.size)
        return false;
    snprintf(path, sizeof(path), "/%s/SYSTEM/SETUP/CORE.BKL", target->name);
    if (!vfs_stat(path, &entry) || entry.type != VFS_NODE_FILE || !entry.size)
        return false;
    snprintf(path, sizeof(path), "/%s/SYSTEM/DRIVERS/VESA.DVR",
             target->name);
    return vfs_stat(path, &entry) && entry.type == VFS_NODE_FILE && entry.size;
}

static bool installer_install_target(const installer_target_t *target,
                                     bool format_first) {
    char destination_root[16];
    kmemset(&g_copy, 0, sizeof(g_copy));
    kstrcpy(g_copy.current_path, "Preparando instalacion...");
    ui_progress(0, 1, "Analizando el contenido del CD...");
    if (!installer_scan_tree("/INSTALL/BOOTSTRAP", 0U)) {
        kstrcpy(g_copy.error, "No se pudo leer el arbol ISO9660");
        return false;
    }
    if (!g_copy.files_total) {
        kstrcpy(g_copy.error, "El CD no contiene el bootstrap BKL3");
        return false;
    }

    if (format_first) {
        kstrcpy(g_copy.current_path, "Creando volumen FAT32...");
        ui_progress(0, g_copy.files_total, "Formateando el disco destino...");
        vfs_forget_volume(target->name);
        fat_forget_device(target->name);
        kprintf("[SETUP] Formato FAT32: destino=%s sectores=%u bps=%u "
                "ro=%u writer=%s reserved=%u\n",
                target->name, target->device->sector_count,
                target->device->sector_size,
                target->device->read_only ? 1U : 0U,
                target->device->write ? "si" : "no",
                INSTALL_RESERVED_SECTORS);
        if (!fat_format_bootable(target->name, "BLESKERNOS",
                                 INSTALL_RESERVED_SECTORS)) {
            uint32_t failed_lba = fat_format_last_error_lba();
            const char *reason = fat_format_last_error();
            if (failed_lba == FAT_FORMAT_LBA_NONE)
                snprintf(g_copy.error, sizeof(g_copy.error), "%s",
                         reason ? reason : "No se pudo crear FAT32");
            else
                snprintf(g_copy.error, sizeof(g_copy.error), "%s (LBA %u)",
                         reason ? reason : "Error escribiendo FAT32",
                         failed_lba);
            kprintf("[SETUP] FAT32 fallo: %s\n", g_copy.error);
            return false;
        }
    } else {
        kstrcpy(g_copy.current_path, "Conservando volumen FAT32...");
        ui_progress(0, g_copy.files_total,
                    "Preparando instalacion sin formatear...");
        vfs_forget_volume(target->name);
        fat_forget_device(target->name);
    }

    kstrcpy(g_copy.current_path, "Instalando sector de arranque...");
    ui_progress(0, g_copy.files_total, "Instalando el arranque de disco...");
    if (!installer_patch_boot_sector(target->device) ||
        !installer_write_raw(target->device, "/INSTALL/STAGE2.BIN",
                             INSTALL_STAGE2_LBA,
                             INSTALL_STAGE2_SECTORS) ||
        !installer_write_raw(target->device, "/INSTALL/KERNEL.BIN",
                             INSTALL_KERNEL_LBA,
                             INSTALL_KERNEL_RESERVED_SECTORS)) {
        kstrcpy(g_copy.error, "No se pudo instalar boot, Stage 2 o kernel");
        return false;
    }

    snprintf(destination_root, sizeof(destination_root), "/%s", target->name);
    kstrcpy(g_copy.current_path, "Montando el nuevo volumen...");
    ui_progress(0, g_copy.files_total, "Preparando directorios...");
    {
        vfs_dir_entry_t root_entry;

        /* /ata0, /usb0, etc. son puntos de volumen virtuales del VFS.
         * No deben crearse con mkdir: la ruta FAT interna de su raiz es
         * vacia y fat_mkdir_path("") la rechaza correctamente.
         * vfs_stat fuerza el montaje nominal y valida la raiz existente. */
        vfs_forget_volume(target->name);
        if (!vfs_stat(destination_root, &root_entry)) {
            snprintf(g_copy.error, sizeof(g_copy.error),
                     "No se pudo montar %s despues del formato",
                     target->name);
            return false;
        }
        if (root_entry.type != VFS_NODE_DIR) {
            snprintf(g_copy.error, sizeof(g_copy.error),
                     "%s no expone una raiz FAT valida", target->name);
            return false;
        }
    }
    kstrcpy(g_copy.current_path, "Limpiando instalacion anterior...");
    ui_progress(0, g_copy.files_total,
                "Reemplazando componentes propios de BlesKernOS...");
    if (!installer_clear_owned_tree(destination_root)) {
        kstrcpy(g_copy.error, "No se pudo limpiar la instalacion anterior");
        return false;
    }
    if (!installer_copy_tree("/INSTALL/BOOTSTRAP", destination_root, 0U)) return false;

    kstrcpy(g_copy.current_path, "Comprobando archivos y arranque...");
    ui_progress(g_copy.files_total, g_copy.files_total,
                "Verificando la instalacion...");
    if (!installer_verify(target)) {
        kstrcpy(g_copy.error, "La verificacion final fallo");
        return false;
    }
    return true;
}

static int installer_format_menu(const installer_target_t *target) {
    installer_disk_info_t info;
    char line[72];

    installer_probe_disk(target, &info);
    for (;;) {
        ui_frame("Opciones de formato",
                 "A: formatear e instalar  F: solo formatear  Esc: volver");
        snprintf(line, sizeof(line), "Dispositivo: %s  Tipo: %s  Tamano: %u MiB",
                 target->name, target->type, target->size_mb);
        ui_text(6, 6, line, UI_NORMAL);
        snprintf(line, sizeof(line), "Estado: %s  Etiqueta: %s",
                 info.mounted ? (info.fat32 ? "FAT32 detectado" : "FAT detectado")
                              : "sin FAT reconocido",
                 info.label[0] ? info.label : "(ninguna)");
        ui_text_clipped(6, 8, line, 68, UI_NORMAL);
        ui_box(6, 11, 68, 7, UI_NORMAL);
        ui_text(9, 12, "A  Formatear FAT32 e instalar BlesKernOS User", UI_NORMAL);
        ui_text(9, 14, "F  Formatear FAT32 solamente", UI_NORMAL);
        ui_text(9, 16, "Esc  Volver sin modificar el disco", UI_NORMAL);
        ui_center(20, "Ambas opciones de formato eliminan todos los archivos.",
                  UI_WARNING);
        {
            uint8_t key = ui_wait_key();
            if (key == KEY_ESCAPE) return 0;
            if (key == 'a' || key == 'A') return 2;
            if (key == 'f' || key == 'F') return 1;
        }
    }
}

static int installer_target_actions(const installer_target_t *target,
                                    installer_disk_info_t *info) {
    char line[72];
    installer_probe_disk(target, info);
    for (;;) {
        ui_frame("Opciones del disco",
                 "I: instalar  F: opciones de formato  Esc: volver");
        snprintf(line, sizeof(line), "%s (%s, %u MiB%s)", target->name,
                 target->type, target->size_mb,
                 target->partition ? ", particion MBR" : "");
        ui_center(5, line, UI_NORMAL);
        snprintf(line, sizeof(line), "Sistema de archivos: %s",
                 info->mounted ? (info->fat32 ? "FAT32" : "FAT12/FAT16")
                               : "no reconocido");
        ui_text(8, 8, line, UI_NORMAL);
        snprintf(line, sizeof(line), "Archivos visibles: %u  Reservados: %u sectores",
                 info->root_entries, info->reserved_sectors);
        ui_text(8, 10, line, UI_NORMAL);
        ui_box(7, 13, 66, 6, UI_NORMAL);
        ui_text(10, 14, "I  Instalar BlesKernOS User", UI_NORMAL);
        ui_text(10, 16, "F  Abrir menu de formato", UI_NORMAL);
        if (!info->boot_layout)
            ui_center(20, "Para instalar, este disco debe formatearse primero.",
                      UI_WARNING);
        else if (info->has_files)
            ui_center(20, "Hay archivos: Setup advertira antes de continuar.",
                      UI_WARNING);
        {
            uint8_t key = ui_wait_key();
            if (key == KEY_ESCAPE) return 0;
            if (key == 'f' || key == 'F') return 2;
            if (key == 'i' || key == 'I') return 1;
        }
    }
}

static void installer_show_error(const installer_target_t *target) {
    char line[72];
    ui_frame("La instalacion no pudo completarse",
             "Enter: volver a la lista   Esc: detener Setup");
    ui_center(6, "BlesKernOS no fue instalado correctamente.", UI_ERROR);
    snprintf(line, sizeof(line), "Destino: %s", target->name);
    ui_text(7, 9, line, UI_NORMAL);
    ui_text_clipped(7, 11, g_copy.error[0] ? g_copy.error
                                          : "Error desconocido",
                    66, UI_NORMAL);
    ui_center(15, "Puede corregir el problema e intentarlo otra vez.",
              UI_NORMAL);
    if (!ui_wait_enter_or_escape())
        installer_halt_screen("Setup fue cancelado por el usuario.");
}

static void installer_show_complete(const installer_target_t *target) {
    char line[72];
    ui_frame("Instalacion completada",
             "R: reiniciar desde el disco   Esc: apagar mas tarde");
    ui_center(6, "El bootstrap de BlesKernOS se copio correctamente.", UI_OK);
    snprintf(line, sizeof(line), "Sistema instalado en %s (%u MiB).",
             target->name, target->size_mb);
    ui_center(9, line, UI_NORMAL);
    ui_center(12, "El primer arranque abrira el asistente grafico SETUP.BEX.",
              UI_NORMAL);
    ui_center(15, "Alli podra elegir usuario, zona horaria y la capa Wine.",
              UI_NORMAL);
    for (;;) {
        uint8_t key = ui_wait_key();
        if (key == 'r' || key == 'R') installer_reboot();
        if (key == KEY_ESCAPE)
            installer_halt_screen("Instalacion finalizada. Retire el CD.");
    }
}

void installer_run(void) {
    installer_target_t targets[SETUP_MAX_TARGETS];

    vga_init();
    ui_frame("Bienvenido", "Enter: continuar   Esc: salir");
    ui_center(6, "Bienvenido al instalador de BlesKernOS 0.8", UI_NORMAL);
    ui_center(9, "Este asistente copiara el arranque y paquetes BKL3", UI_NORMAL);
    ui_center(10, "a un disco ATA o USB y lo dejara arrancable.", UI_NORMAL);
    ui_center(13, "Tambien puede formatear disquetes de 1.44 MB en FAT12.",
              UI_NORMAL);
    ui_center(16, "La instalacion en disco utiliza FAT32.", UI_NORMAL);
    if (!ui_wait_enter_or_escape())
        installer_halt_screen("Setup fue cancelado antes de comenzar.");

    for (;;) {
        uint32_t count = installer_collect_targets(targets,
                                                   SETUP_MAX_TARGETS);
        int chosen = installer_choose_target(targets, count);
        installer_target_t *target;
        if (chosen == -2) continue;
        if (chosen < 0)
            installer_halt_screen("Setup fue cancelado por el usuario.");
        target = &targets[(uint32_t)chosen];

        if (target->floppy) {
            if (installer_confirm_destroy(target, "Formatear FAT12"))
                installer_floppy_format(target);
            continue;
        }
        if (target->device->sector_count < 196608U) {
            ui_frame("Disco demasiado pequeno", "Enter: volver");
            ui_center(8, "La instalacion BKL3 requiere al menos 96 MiB temporales.",
                      UI_ERROR);
            ui_center(11, "Los paquetes se eliminan al terminar, pero necesitan espacio temporal.",
                      UI_NORMAL);
            (void)ui_wait_enter_or_escape();
            continue;
        }
        for (;;) {
            installer_disk_info_t info;
            int action = installer_target_actions(target, &info);
            if (action == 0) break;
            if (action == 2) {
                int format_action = installer_format_menu(target);
                if (format_action == 0) continue;
                if (!installer_confirm_destroy(target,
                        format_action == 2 ? "Formatear e instalar"
                                           : "Formatear FAT32"))
                    continue;
                if (format_action == 1) {
                    ui_frame("Formateando FAT32", "Espere...");
                    vfs_forget_volume(target->name);
                    fat_forget_device(target->name);
                    if (!fat_format_bootable(target->name, "BLESKERNOS",
                                             INSTALL_RESERVED_SECTORS)) {
                        ui_frame("Error al formatear", "Enter: volver");
                        ui_text_clipped(7, 10, fat_format_last_error(), 66,
                                        UI_ERROR);
                        (void)ui_wait_enter_or_escape();
                    } else {
                        ui_frame("Formato completado", "Enter: volver");
                        ui_center(10, "El disco FAT32 esta listo.", UI_OK);
                        (void)ui_wait_enter_or_escape();
                    }
                    continue;
                }
                if (!installer_install_target(target, true)) {
                    installer_show_error(target);
                    continue;
                }
                installer_show_complete(target);
            }
            if (action == 1) {
                if (!info.boot_layout) {
                    ui_frame("Formato necesario", "Enter: volver");
                    ui_center(8, "El FAT32 actual no reserva espacio suficiente",
                              UI_ERROR);
                    ui_center(10, "para Stage 2 y el kernel de BlesKernOS.",
                              UI_NORMAL);
                    ui_center(13, "Use F y elija Formatear e instalar.",
                              UI_NORMAL);
                    (void)ui_wait_enter_or_escape();
                    continue;
                }
                if (info.has_files &&
                    !installer_confirm_existing_files(target, &info))
                    continue;
                if (!installer_install_target(target, false)) {
                    installer_show_error(target);
                    continue;
                }
                installer_show_complete(target);
            }
        }
    }
}
