#include "include/floppy_installer.h"
#include "include/block.h"
#include "include/fat.h"
#include "include/keyboard.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/types.h"
#include "include/vfs.h"
#include "include/vga.h"
#include "stdio.h"
#include "string.h"

#define FI_STAGE2_LBA 1U
#define FI_STAGE2_SECTORS 8U
#define FI_KERNEL_LBA 9U
#define FI_KERNEL_RESERVED_SECTORS 2048U
#define FI_FSINFO_SECTOR (FI_KERNEL_LBA + FI_KERNEL_RESERVED_SECTORS)
#define FI_BACKUP_BOOT_SECTOR (FI_FSINFO_SECTOR + 1U)
#define FI_BACKUP_FSINFO_SECTOR (FI_FSINFO_SECTOR + 2U)
#define FI_RESERVED_SECTORS (FI_BACKUP_FSINFO_SECTOR + 1U)
#define FI_MIN_SECTORS 81920U
#define FI_MAX_TARGETS 8U
#define FI_IO_BUFFER 8192U
#define VGA_MEM ((volatile uint16_t *)0xB8000U)

typedef struct {
    block_device_t *dev;
    char name[8];
    uint32_t size_mb;
    bool partition;
} fi_target_t;

static uint16_t cell(char c, uint8_t attr) {
    return (uint16_t)(((uint16_t)attr << 8) | (uint8_t)c);
}

static void clear(void) {
    for (uint32_t i = 0; i < 80U * 25U; i++) VGA_MEM[i] = cell(' ', 0x1FU);
}

static void text(uint32_t x, uint32_t y, const char *s, uint8_t attr) {
    if (!s || y >= 25U) return;
    while (*s && x < 80U) VGA_MEM[y * 80U + x++] = cell(*s++, attr);
}

static void center(uint32_t y, const char *s, uint8_t attr) {
    uint32_t len = (uint32_t)kstrlen(s ? s : "");
    text(len < 80U ? (80U - len) / 2U : 0U, y, s, attr);
}

static void frame(const char *title, const char *footer) {
    clear();
    center(1, "BlesKernOS 0.8 - Instalador en disquetes", 0x1FU);
    if (title) center(4, title, 0x1EU);
    if (footer) text(2, 23, footer, 0x70U);
    vga_set_cursor(79, 24);
}

/*
 * El instalador multidisquete corre antes del scheduler/GUI y necesita seguir
 * aceptando teclado incluso si el IRQ1 queda perdido durante el arranque desde
 * FDC. Para este modo usamos lectura PS/2 por polling y dejamos IRQ1 enmascarado.
 * QEMU y el hardware AT compatible entregan scancodes Set 1.
 */
static uint8_t fi_map_scancode(uint8_t scancode, bool shifted) {
    switch (scancode) {
        case 0x01: return KEY_ESCAPE;
        case 0x0E: return KEY_BACKSPACE;
        case 0x1C: return KEY_ENTER;
        case 0x39: return ' ';
        case 0x10: return shifted ? 'Q' : 'q';
        case 0x11: return shifted ? 'W' : 'w';
        case 0x12: return shifted ? 'E' : 'e';
        case 0x13: return shifted ? 'R' : 'r';
        case 0x14: return shifted ? 'T' : 't';
        case 0x15: return shifted ? 'Y' : 'y';
        case 0x16: return shifted ? 'U' : 'u';
        case 0x17: return shifted ? 'I' : 'i';
        case 0x18: return shifted ? 'O' : 'o';
        case 0x19: return shifted ? 'P' : 'p';
        case 0x1E: return shifted ? 'A' : 'a';
        case 0x1F: return shifted ? 'S' : 's';
        case 0x20: return shifted ? 'D' : 'd';
        case 0x21: return shifted ? 'F' : 'f';
        case 0x22: return shifted ? 'G' : 'g';
        case 0x23: return shifted ? 'H' : 'h';
        case 0x24: return shifted ? 'J' : 'j';
        case 0x25: return shifted ? 'K' : 'k';
        case 0x26: return shifted ? 'L' : 'l';
        case 0x2C: return shifted ? 'Z' : 'z';
        case 0x2D: return shifted ? 'X' : 'x';
        case 0x2E: return shifted ? 'C' : 'c';
        case 0x2F: return shifted ? 'V' : 'v';
        case 0x30: return shifted ? 'B' : 'b';
        case 0x31: return shifted ? 'N' : 'n';
        case 0x32: return shifted ? 'M' : 'm';
        default: return 0U;
    }
}

static uint8_t key(void) {
    static bool shifted = false;
    static bool extended = false;

    for (;;) {
        uint8_t scancode;
        while ((inb(0x64U) & 0x01U) == 0U)
            __asm__ volatile ("pause");
        scancode = inb(0x60U);

        if (scancode == 0xE0U) {
            extended = true;
            continue;
        }
        if (scancode == 0x2AU || scancode == 0x36U) {
            shifted = true;
            extended = false;
            continue;
        }
        if (scancode == 0xAAU || scancode == 0xB6U) {
            shifted = false;
            extended = false;
            continue;
        }
        if ((scancode & 0x80U) != 0U) {
            extended = false;
            continue;
        }

        if (extended) {
            extended = false;
            if (scancode == 0x48U) return KEY_UP;
            if (scancode == 0x50U) return KEY_DOWN;
            if (scancode == 0x4BU) return KEY_LEFT;
            if (scancode == 0x4DU) return KEY_RIGHT;
            continue;
        }

        {
            uint8_t translated = fi_map_scancode(scancode, shifted);
            if (translated != 0U) return translated;
        }
    }
}

static void wait_enter(void) {
    while (key() != KEY_ENTER) { }
}

static void reboot(void) {
    frame("Instalacion copiada", "Reiniciando desde el disco duro...");
    center(10, "El primer arranque descomprimira el paquete BKL.", 0x1FU);
    for (uint32_t i = 0; i < 300000U; i++) io_wait();
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}

static uint32_t fi_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool fi_partition_type_is_fat(uint8_t type) {
    switch (type) {
        case 0x01U: case 0x04U: case 0x06U: case 0x0BU: case 0x0CU:
        case 0x0EU: case 0x11U: case 0x14U: case 0x16U: case 0x1BU:
        case 0x1CU: case 0x1EU:
            return true;
        default:
            return false;
    }
}

static bool fi_has_partition_table(block_device_t *device) {
    uint8_t mbr[512];
    if (!device || !block_read(device, 0U, 1U, mbr) ||
        mbr[510] != 0x55U || mbr[511] != 0xAAU) return false;
    for (uint32_t i = 0; i < 4U; i++) {
        uint32_t off = 446U + i * 16U;
        if (mbr[off + 4U] && mbr[off + 4U] != 0xEEU &&
            fi_le32(mbr + off + 8U) && fi_le32(mbr + off + 12U)) return true;
    }
    return false;
}

static void fi_register_partition_views(void) {
    uint32_t physical_count = block_count();
    for (uint32_t i = 0; i < physical_count; i++) {
        block_device_t *dev = block_at(i);
        uint8_t mbr[512];
        if (!dev || dev->type == BLOCK_DEVICE_FLOPPY ||
            dev->type == BLOCK_DEVICE_ATAPI || dev->read_only || !dev->write ||
            dev->sector_size != 512U || !block_read(dev, 0U, 1U, mbr) ||
            mbr[510] != 0x55U || mbr[511] != 0xAAU) continue;
        for (uint32_t part = 0; part < 4U; part++) {
            uint32_t off = 446U + part * 16U;
            uint8_t type = mbr[off + 4U];
            uint32_t first = fi_le32(mbr + off + 8U);
            uint32_t sectors = fi_le32(mbr + off + 12U);
            char name[8];
            if (!fi_partition_type_is_fat(type) || !first ||
                sectors < 131072U ||
                (dev->sector_count &&
                 (first >= dev->sector_count || sectors > dev->sector_count - first)))
                continue;
            snprintf(name, sizeof(name), "%sp%u", dev->name, part + 1U);
            (void)block_register_view(name, dev, first, sectors);
        }
    }
}

static uint32_t collect(fi_target_t *targets) {
    uint32_t count = 0U;
    fi_register_partition_views();
    for (uint32_t i = 0; i < block_count() && count < FI_MAX_TARGETS; i++) {
        block_device_t *dev = block_at(i);
        if (!dev || dev->type == BLOCK_DEVICE_FLOPPY ||
            dev->type == BLOCK_DEVICE_ATAPI || dev->read_only || !dev->write ||
            dev->sector_size != 512U || dev->sector_count < FI_MIN_SECTORS)
            continue;
        if (dev->base_lba == 0U && fi_has_partition_table(dev)) continue;
        targets[count].dev = dev;
        kstrncpy(targets[count].name, dev->name,
                 sizeof(targets[count].name) - 1U);
        targets[count].size_mb = dev->sector_count / 2048U;
        targets[count].partition = dev->base_lba != 0U;
        count++;
    }
    return count;
}

static int choose_target(fi_target_t *targets, uint32_t count) {
    uint32_t selected = 0U;
    for (;;) {
        frame("Seleccione el disco de destino",
              "Flechas: mover  Enter: elegir  Esc: cancelar");
        if (!count) center(10, "No se encontro un disco ATA/USB apto.", 0x4FU);
        for (uint32_t i = 0; i < count; i++) {
            char line[64];
            snprintf(line, sizeof(line), "%s  %u MiB  %s", targets[i].name,
                     targets[i].size_mb,
                     targets[i].partition ? "particion" : "disco");
            text(20, 8U + i, line, i == selected ? 0x70U : 0x1FU);
        }
        uint8_t k = key();
        if (k == KEY_ESCAPE) return -1;
        if (k == KEY_UP && selected) selected--;
        else if (k == KEY_DOWN && selected + 1U < count) selected++;
        else if ((k == KEY_ENTER || k == '\n') && count) return (int)selected;
    }
}

static bool confirm(const fi_target_t *target) {
    char typed[7];
    uint32_t len = 0U;
    kmemset(typed, 0, sizeof(typed));
    for (;;) {
        char line[64];
        frame("Confirmacion destructiva", "Escriba BORRAR y pulse Enter");
        snprintf(line, sizeof(line), "Se eliminaran los datos de %s (%u MiB)",
                 target->name, target->size_mb);
        center(8, line, 0x4FU);
        text(32, 13, typed, 0x70U);
        uint8_t k = key();
        if (k == KEY_ESCAPE) return false;
        if (k == KEY_BACKSPACE && len) typed[--len] = '\0';
        else if (k == KEY_ENTER || k == '\n') {
            if (kstrcmp(typed, "BORRAR") == 0) return true;
            len = 0U; typed[0] = '\0';
        } else {
            if (k >= 'a' && k <= 'z') k = (uint8_t)(k - 'a' + 'A');
            if (k >= 'A' && k <= 'Z' && len < 6U) {
                typed[len++] = (char)k; typed[len] = '\0';
            }
        }
    }
}

static bool read_file(const char *path, void **data, uint32_t *size) {
    vfs_forget_volume("fd0");
    return vfs_read_all(path, data, size);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool disk_identity(uint32_t expected, uint32_t *total) {
    void *raw = NULL;
    uint32_t size = 0U;
    uint8_t *p;
    if (!read_file("/fd0/DISK.ID", &raw, &size) || size < 16U) return false;
    p = (uint8_t *)raw;
    if (p[0] != 'B' || p[1] != 'K' || p[2] != 'D' || p[3] != '1' ||
        get32(p + 4U) != expected) { kfree(raw); return false; }
    *total = get32(p + 8U);
    kfree(raw);
    return *total >= 3U && expected <= *total;
}

static bool write_raw_memory(block_device_t *dev, uint32_t lba,
                             uint32_t max_sectors, const uint8_t *data,
                             uint32_t size) {
    uint8_t sector[512];
    uint32_t sectors = (size + 511U) / 512U;
    if (!dev || !data || !sectors || sectors > max_sectors) return false;
    for (uint32_t i = 0; i < sectors; i++) {
        uint32_t offset = i * 512U;
        uint32_t chunk = size - offset;
        if (chunk > 512U) chunk = 512U;
        kmemset(sector, 0, sizeof(sector));
        kmemcpy(sector, data + offset, chunk);
        if (!block_write(dev, lba + i, 1U, sector)) return false;
    }
    return true;
}

static bool install_boot(fi_target_t *target) {
    void *boot = NULL, *stage2 = NULL, *kernel = NULL;
    uint32_t boot_size = 0U, stage2_size = 0U, kernel_size = 0U;
    uint8_t sector[512];
    bool ok = false;
    if (!read_file("/fd0/BOOTF32.BIN", &boot, &boot_size) || boot_size != 512U ||
        !read_file("/fd0/STAGE2.BIN", &stage2, &stage2_size) ||
        !read_file("/fd0/KERNEL.BIN", &kernel, &kernel_size)) goto done;
    if (!block_read(target->dev, 0U, 1U, sector)) goto done;
    kmemcpy(sector, boot, 3U);
    kmemcpy(sector + 90U, (uint8_t *)boot + 90U, 422U);
    sector[28] = (uint8_t)(target->dev->base_lba);
    sector[29] = (uint8_t)(target->dev->base_lba >> 8);
    sector[30] = (uint8_t)(target->dev->base_lba >> 16);
    sector[31] = (uint8_t)(target->dev->base_lba >> 24);
    sector[510] = 0x55U; sector[511] = 0xAAU;
    if (!block_write(target->dev, 0U, 1U, sector) ||
        !block_write(target->dev, FI_BACKUP_BOOT_SECTOR, 1U, sector) ||
        !write_raw_memory(target->dev, FI_STAGE2_LBA, FI_STAGE2_SECTORS,
                          stage2, stage2_size) ||
        !write_raw_memory(target->dev, FI_KERNEL_LBA,
                          FI_KERNEL_RESERVED_SECTORS, kernel, kernel_size))
        goto done;
    ok = true;
done:
    if (boot) kfree(boot);
    if (stage2) kfree(stage2);
    if (kernel) kfree(kernel);
    return ok;
}

static bool copy_part(fi_target_t *target, uint32_t part_index) {
    void *raw = NULL;
    uint32_t size = 0U;
    char destination[64];
    if (!read_file("/fd0/PART.BKL", &raw, &size)) return false;
    snprintf(destination, sizeof(destination), "/%s/SETUP/PART%03u.BKL",
             target->name, part_index);
    if (!vfs_write_all(destination, raw, size)) { kfree(raw); return false; }
    kfree(raw);
    return true;
}

static bool wait_disk(uint32_t disk, uint32_t *total) {
    for (;;) {
        char line[72];
        frame("Cambio de disquete", "Inserte el disco indicado y pulse Enter");
        snprintf(line, sizeof(line), "Inserte el disquete %u", disk);
        center(9, line, 0x1FU);
        center(12, "Setup verificara automaticamente su numero.", 0x1FU);
        wait_enter();
        if (disk_identity(disk, total)) return true;
        frame("Disquete incorrecto o ilegible", "Enter: volver a intentar");
        center(10, "No coincide el numero o no se pudo leer DISK.ID", 0x4FU);
        wait_enter();
    }
}

void floppy_installer_run(void) {
    /* Evita que el handler IRQ1 consuma 0x60 antes del lector por polling. */
    pic_mask_irq(1U);
    while ((inb(0x64U) & 0x01U) != 0U) (void)inb(0x60U);
restart:
    fi_target_t targets[FI_MAX_TARGETS];
    uint32_t count, total_disks = 0U;
    int selected;
    fi_target_t *target;
    char root[32];
    uint8_t pending[8] = {'B','K','P','1',0,0,0,0};

    frame("Bienvenido", "Enter: continuar");
    center(8, "Esta edicion copia paquetes BKL desde varios disquetes.", 0x1FU);
    center(11, "La descompresion se realiza en el primer arranque.", 0x1FU);
    wait_enter();
    count = collect(targets);
    selected = choose_target(targets, count);
    if (selected < 0) for (;;) __asm__ volatile ("hlt");
    target = &targets[(uint32_t)selected];
    if (!confirm(target)) goto restart;

    frame("Formateando FAT32", "No apague el equipo");
    vfs_forget_volume(target->name);
    fat_forget_device(target->name);
    if (!fat_format_bootable(target->name, "BLESKERNOS", FI_RESERVED_SECTORS)) {
        frame("Error de formato", "Enter: detener");
        center(10, fat_format_last_error(), 0x4FU); wait_enter();
        for (;;) __asm__ volatile ("hlt");
    }
    snprintf(root, sizeof(root), "/%s", target->name);
    vfs_forget_volume(target->name);
    {
        char setup[32];
        vfs_dir_entry_t entry;
        snprintf(setup, sizeof(setup), "/%s/SETUP", target->name);
        if (!vfs_stat(root, &entry) || entry.type != VFS_NODE_DIR ||
            (!vfs_stat(setup, &entry) && !vfs_mkdir(setup))) {
            frame("Error", "Enter: detener");
            center(10, "No se pudo crear el directorio SETUP.", 0x4FU);
            wait_enter();
            for (;;) __asm__ volatile ("hlt");
        }
    }

    for (uint32_t disk = 2U;; disk++) {
        if (!wait_disk(disk, &total_disks)) for (;;) __asm__ volatile ("hlt");
        frame("Copiando paquete BKL", "No retire el disquete durante la copia");
        {
            char line[64];
            snprintf(line, sizeof(line), "Copiando disquete %u de %u", disk,
                     total_disks);
            center(10, line, 0x1FU);
        }
        if (disk == 2U && !install_boot(target)) {
            frame("Error", "Enter: detener");
            center(10, "No se pudo instalar Stage 1, Stage 2 o kernel.", 0x4FU);
            wait_enter(); for (;;) __asm__ volatile ("hlt");
        }
        if (disk >= 3U && !copy_part(target, disk - 3U)) {
            frame("Error de copia", "Enter: detener");
            center(10, "No se pudo copiar PART.BKL al disco duro.", 0x4FU);
            wait_enter(); for (;;) __asm__ volatile ("hlt");
        }
        if (disk >= total_disks) break;
    }

    {
        uint32_t parts = total_disks - 2U;
        pending[4] = (uint8_t)parts;
        pending[5] = (uint8_t)(parts >> 8);
        pending[6] = (uint8_t)(parts >> 16);
        pending[7] = (uint8_t)(parts >> 24);
        char marker[64];
        snprintf(marker, sizeof(marker), "/%s/SETUP/PENDING.DAT", target->name);
        if (!vfs_write_all(marker, pending, sizeof(pending))) {
            frame("Error", "Enter: detener");
            center(10, "No se pudo crear PENDING.DAT", 0x4FU);
            wait_enter(); for (;;) __asm__ volatile ("hlt");
        }
    }
    reboot();
}
