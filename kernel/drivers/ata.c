#include "../include/ata.h"
#include "../include/block.h"
#include "../include/memory.h"
#include "../include/pic.h"
#include "../include/vga.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT0   2
#define ATA_REG_LBA0        3
#define ATA_REG_LBA1        4
#define ATA_REG_LBA2        5
#define ATA_REG_HDDEVSEL    6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

#define ATA_STATUS_ERR      0x01
#define ATA_STATUS_DRQ      0x08
#define ATA_STATUS_DF       0x20
#define ATA_STATUS_BSY      0x80

#define ATA_CMD_IDENTIFY          0xEC
#define ATA_CMD_READ_PIO          0x20
#define ATA_CMD_WRITE_PIO         0x30
#define ATA_CMD_CACHE_FLUSH       0xE7
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_PACKET   0xA1

#define ATAPI_SIGNATURE_LBA1      0x14
#define ATAPI_SIGNATURE_LBA2      0xEB
#define ATAPI_DEFAULT_BLOCK_SIZE  2048U

typedef enum {
    ATA_TRANSPORT_ATA = 0,
    ATA_TRANSPORT_ATAPI
} ata_transport_t;

typedef struct {
    uint16_t io;
    uint16_t ctrl;
    uint8_t slave;
    ata_transport_t transport;
    uint32_t sectors;
    uint16_t block_size;
    char name[8];
} ata_device_t;

static ata_device_t g_ata_devices[4];
static uint32_t g_ata_device_count = 0;
static uint32_t g_ata_disk_count = 0;
static uint32_t g_atapi_count = 0;

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void insw(uint16_t port, void *buffer, uint32_t word_count) {
    __asm__ volatile ("cld; rep insw"
                      : "+D"(buffer), "+c"(word_count)
                      : "d"(port)
                      : "memory");
}

static inline void outsw(uint16_t port, const void *buffer, uint32_t word_count) {
    __asm__ volatile ("cld; rep outsw"
                      : "+S"(buffer), "+c"(word_count)
                      : "d"(port));
}

static void ata_delay_400ns(uint16_t ctrl) {
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
    (void)inb(ctrl);
}

static bool ata_wait_not_busy(uint16_t io) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(io + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if ((status & ATA_STATUS_BSY) == 0) return true;
    }
    return false;
}

static bool ata_wait_drq(uint16_t io) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(io + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return false;
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ)) return true;
    }
    return false;
}

static bool ata_wait_packet_drq(uint16_t io) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(io + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (i > 4 && (status & (ATA_STATUS_ERR | ATA_STATUS_DF))) return false;
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ)) return true;
    }
    return false;
}

static bool ata_wait_packet_complete(uint16_t io) {
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t status = inb(io + ATA_REG_STATUS);
        if (status == 0xFF) return false;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return false;
        if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == 0) return true;
    }
    return false;
}

static void ata_select_lba28(const ata_device_t *dev, uint32_t lba) {
    outb(dev->io + ATA_REG_HDDEVSEL,
         (uint8_t)(0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F)));
    ata_delay_400ns(dev->ctrl);
}

static void ata_select_packet(const ata_device_t *dev) {
    outb(dev->io + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (dev->slave << 4)));
    ata_delay_400ns(dev->ctrl);
}

static uint32_t read_be32(const uint8_t *buffer) {
    return ((uint32_t)buffer[0] << 24) |
           ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) |
           (uint32_t)buffer[3];
}

static bool ata_identify_packet(uint16_t io, uint16_t ctrl, uint8_t slave, uint16_t *identify) {
    outb(io + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (slave << 4)));
    ata_delay_400ns(ctrl);

    outb(io + ATA_REG_SECCOUNT0, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);

    if (!ata_wait_not_busy(io)) return false;
    if (!ata_wait_drq(io)) return false;

    for (uint32_t i = 0; i < 256; i++) identify[i] = inw(io + ATA_REG_DATA);
    ata_delay_400ns(ctrl);
    return true;
}

static bool ata_atapi_begin_packet(const ata_device_t *dev, uint16_t max_byte_count) {
    ata_select_packet(dev);
    if (!ata_wait_not_busy(dev->io)) return false;

    outb(dev->io + ATA_REG_FEATURES, 0);
    outb(dev->io + ATA_REG_SECCOUNT0, 0);
    outb(dev->io + ATA_REG_LBA0, 0);
    outb(dev->io + ATA_REG_LBA1, (uint8_t)(max_byte_count & 0xFF));
    outb(dev->io + ATA_REG_LBA2, (uint8_t)((max_byte_count >> 8) & 0xFF));
    outb(dev->io + ATA_REG_COMMAND, ATA_CMD_PACKET);

    return ata_wait_packet_drq(dev->io);
}

static bool ata_atapi_packet_read(ata_device_t *dev, const uint8_t packet[12],
                                  uint16_t expected_bytes, void *buffer) {
    uint16_t byte_count;

    if (!dev || !packet || !buffer || expected_bytes == 0) return false;
    if (!ata_atapi_begin_packet(dev, expected_bytes)) return false;

    outsw(dev->io + ATA_REG_DATA, packet, 6);
    if (!ata_wait_drq(dev->io)) return false;

    byte_count = (uint16_t)(((uint16_t)inb(dev->io + ATA_REG_LBA2) << 8) |
                            inb(dev->io + ATA_REG_LBA1));
    if (byte_count == 0 || byte_count > expected_bytes) return false;

    insw(dev->io + ATA_REG_DATA, buffer, byte_count / 2U);
    ata_delay_400ns(dev->ctrl);
    return ata_wait_packet_complete(dev->io);
}

static bool ata_atapi_update_capacity(ata_device_t *dev, block_device_t *block) {
    uint8_t packet[12];
    uint8_t capacity[8];
    uint32_t last_lba;
    uint32_t block_size;

    if (!dev) return false;

    kmemset(packet, 0, sizeof(packet));
    packet[0] = 0x25;
    if (!ata_atapi_packet_read(dev, packet, sizeof(capacity), capacity)) {
        dev->sectors = 0;
        if (dev->block_size == 0) dev->block_size = ATAPI_DEFAULT_BLOCK_SIZE;
        if (block) {
            block->sector_count = dev->sectors;
            block->sector_size = dev->block_size;
        }
        return false;
    }

    last_lba = read_be32(capacity);
    block_size = read_be32(capacity + 4);
    if (block_size == 0 || block_size > 0xFFFFU) return false;

    dev->sectors = last_lba + 1U;
    dev->block_size = (uint16_t)block_size;
    if (block) {
        block->sector_count = dev->sectors;
        block->sector_size = dev->block_size;
    }
    return true;
}

static bool ata_read_block(block_device_t *block, uint32_t lba, uint8_t count, void *buffer) {
    ata_device_t *dev = (ata_device_t *)block->driver_data;
    uint16_t *out = (uint16_t *)buffer;

    if (!dev || !buffer || count == 0) return false;
    if (lba > 0x0FFFFFFF || (uint32_t)count + lba > 0x10000000U) return false;

    ata_select_lba28(dev, lba);
    if (!ata_wait_not_busy(dev->io)) return false;

    outb(dev->io + ATA_REG_SECCOUNT0, count);
    outb(dev->io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(dev->io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    for (uint8_t s = 0; s < count; s++) {
        if (!ata_wait_drq(dev->io)) return false;
        for (uint32_t i = 0; i < BLOCK_SECTOR_SIZE / 2U; i++) *out++ = inw(dev->io + ATA_REG_DATA);
        ata_delay_400ns(dev->ctrl);
    }
    return true;
}

static bool ata_atapi_read_block(block_device_t *block, uint32_t lba, uint8_t count, void *buffer) {
    ata_device_t *dev = (ata_device_t *)block->driver_data;
    uint8_t packet[12];
    uint8_t *dst = (uint8_t *)buffer;

    if (!dev || !buffer || count == 0) return false;
    if (dev->block_size == 0) dev->block_size = ATAPI_DEFAULT_BLOCK_SIZE;

    kmemset(packet, 0, sizeof(packet));
    packet[0] = 0xA8;
    packet[2] = (uint8_t)((lba >> 24) & 0xFF);
    packet[3] = (uint8_t)((lba >> 16) & 0xFF);
    packet[4] = (uint8_t)((lba >> 8) & 0xFF);
    packet[5] = (uint8_t)(lba & 0xFF);
    packet[9] = count;

    if (!ata_atapi_begin_packet(dev, dev->block_size)) return false;
    outsw(dev->io + ATA_REG_DATA, packet, 6);

    for (uint8_t sector = 0; sector < count; sector++) {
        uint16_t byte_count;

        if (!ata_wait_drq(dev->io)) return false;
        byte_count = (uint16_t)(((uint16_t)inb(dev->io + ATA_REG_LBA2) << 8) |
                                inb(dev->io + ATA_REG_LBA1));
        if (byte_count == 0 || byte_count > dev->block_size) return false;

        insw(dev->io + ATA_REG_DATA, dst + (sector * dev->block_size), byte_count / 2U);
        if (byte_count < dev->block_size) {
            kmemset(dst + (sector * dev->block_size) + byte_count, 0,
                    dev->block_size - byte_count);
        }
        ata_delay_400ns(dev->ctrl);
    }

    return ata_wait_packet_complete(dev->io);
}

static bool ata_write_block(block_device_t *block, uint32_t lba, uint8_t count,
                            const void *buffer) {
    ata_device_t *dev = (ata_device_t *)block->driver_data;
    const uint16_t *in = (const uint16_t *)buffer;

    if (!dev || !buffer || count == 0 || lba > 0x0FFFFFFF) return false;

    ata_select_lba28(dev, lba);
    if (!ata_wait_not_busy(dev->io)) return false;

    outb(dev->io + ATA_REG_SECCOUNT0, count);
    outb(dev->io + ATA_REG_LBA0, (uint8_t)lba);
    outb(dev->io + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(dev->io + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    outb(dev->io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    for (uint8_t s = 0; s < count; s++) {
        if (!ata_wait_drq(dev->io)) return false;
        for (uint32_t i = 0; i < BLOCK_SECTOR_SIZE / 2U; i++) outw(dev->io + ATA_REG_DATA, *in++);
        ata_delay_400ns(dev->ctrl);
    }

    outb(dev->io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_wait_not_busy(dev->io);
}

static void ata_register_ata(uint16_t io, uint16_t ctrl, uint8_t slave, const uint16_t *identify) {
    ata_device_t *dev;

    if (g_ata_device_count >= 4) return;

    dev = &g_ata_devices[g_ata_device_count++];
    kmemset(dev, 0, sizeof(*dev));
    dev->io = io;
    dev->ctrl = ctrl;
    dev->slave = slave;
    dev->transport = ATA_TRANSPORT_ATA;
    dev->sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
    dev->block_size = BLOCK_SECTOR_SIZE;

    if (dev->sectors == 0) {
        g_ata_device_count--;
        return;
    }

    dev->name[0] = 'a';
    dev->name[1] = 't';
    dev->name[2] = 'a';
    dev->name[3] = (char)('0' + g_ata_disk_count++);
    dev->name[4] = '\0';

    if (!block_register_ex(dev->name, BLOCK_DEVICE_ATA, dev->sectors,
                           dev->block_size, false, dev, ata_read_block)) {
        g_ata_device_count--;
        return;
    }
    block_set_writer(dev->name, ata_write_block);
    kprintf("  [ATA] %s: %u sectores de %u bytes\n",
            dev->name, dev->sectors, dev->block_size);
}

static void ata_register_atapi(uint16_t io, uint16_t ctrl, uint8_t slave,
                               const uint16_t *identify UNUSED) {
    ata_device_t *dev;
    block_device_t *block;

    if (g_ata_device_count >= 4) return;

    dev = &g_ata_devices[g_ata_device_count++];
    kmemset(dev, 0, sizeof(*dev));
    dev->io = io;
    dev->ctrl = ctrl;
    dev->slave = slave;
    dev->transport = ATA_TRANSPORT_ATAPI;
    dev->block_size = ATAPI_DEFAULT_BLOCK_SIZE;
    dev->name[0] = 'c';
    dev->name[1] = 'd';
    dev->name[2] = (char)('0' + g_atapi_count++);
    dev->name[3] = '\0';

    ata_atapi_update_capacity(dev, NULL);
    if (!block_register_ex(dev->name, BLOCK_DEVICE_ATAPI, dev->sectors,
                           dev->block_size, true, dev, ata_atapi_read_block)) {
        g_ata_device_count--;
        return;
    }

    block = block_get(dev->name);
    ata_atapi_update_capacity(dev, block);
    if (dev->sectors) {
        kprintf("  [ATAPI] %s: %u bloques de %u bytes\n",
                dev->name, dev->sectors, dev->block_size);
    } else {
        kprintf("  [ATAPI] %s: unidad detectada sin medio listo\n", dev->name);
    }
}

static void ata_probe_device(uint16_t io, uint16_t ctrl, uint8_t slave) {
    uint16_t identify[256];
    uint8_t status;
    uint8_t lba1;
    uint8_t lba2;

    outb(io + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (slave << 4)));
    ata_delay_400ns(ctrl);

    outb(io + ATA_REG_SECCOUNT0, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    status = inb(io + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) return;
    if (!ata_wait_not_busy(io)) return;

    lba1 = inb(io + ATA_REG_LBA1);
    lba2 = inb(io + ATA_REG_LBA2);

    if (lba1 == 0 && lba2 == 0) {
        if (!ata_wait_drq(io)) return;
        for (uint32_t i = 0; i < 256; i++) identify[i] = inw(io + ATA_REG_DATA);
        ata_delay_400ns(ctrl);
        ata_register_ata(io, ctrl, slave, identify);
        return;
    }

    if (lba1 == ATAPI_SIGNATURE_LBA1 && lba2 == ATAPI_SIGNATURE_LBA2) {
        if (!ata_identify_packet(io, ctrl, slave, identify)) return;
        ata_register_atapi(io, ctrl, slave, identify);
    }
}

static void ata_probe(uint16_t io, uint16_t ctrl) {
    if (inb(io + ATA_REG_STATUS) == 0xFF) return;

    ata_probe_device(io, ctrl, 0);
    ata_probe_device(io, ctrl, 1);
}

void ata_init(void) {
    kmemset(g_ata_devices, 0, sizeof(g_ata_devices));
    g_ata_device_count = 0;
    g_ata_disk_count = 0;
    g_atapi_count = 0;

    ata_probe(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL);
    ata_probe(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL);
}

bool ata_refresh_media(const char *name) {
    block_device_t *block = block_get(name);
    ata_device_t *dev;

    if (!block || block->type != BLOCK_DEVICE_ATAPI) return false;
    dev = (ata_device_t *)block->driver_data;
    if (!dev) return false;

    return ata_atapi_update_capacity(dev, block);
}
