#include "../include/block.h"
#include "../include/memory.h"
#include "../include/task.h"

static block_device_t g_devices[BLOCK_MAX_DEVICES];
static uint32_t g_device_count = 0;
static volatile uint32_t g_io_owner = 0;
static uint32_t g_io_depth = 0;

static uint32_t block_irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void block_irq_restore(uint32_t flags) {
    if (flags & (1U << 9)) __asm__ volatile ("sti" : : : "memory");
}

/* Serialize controller transactions without freezing every other task. */
static void block_io_lock(void) {
    uint32_t pid = task_current_pid();
    for (;;) {
        uint32_t flags = block_irq_save();
        if (g_io_owner == 0 || g_io_owner == pid) {
            g_io_owner = pid;
            g_io_depth++;
            block_irq_restore(flags);
            return;
        }
        block_irq_restore(flags);
        if (flags & (1U << 9)) task_yield();
    }
}

static void block_io_unlock(void) {
    uint32_t flags = block_irq_save();
    if (g_io_owner == task_current_pid() && g_io_depth) {
        if (--g_io_depth == 0) g_io_owner = 0;
    }
    block_irq_restore(flags);
}

void block_init(void) {
    kmemset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    g_io_owner = 0;
    g_io_depth = 0;
}

bool block_register(const char *name, block_device_type_t type, uint32_t sector_count, void *driver_data, block_read_fn_t read) {
    return block_register_ex(name, type, sector_count, BLOCK_SECTOR_SIZE, false, driver_data, read);
}

bool block_register_ex(const char *name, block_device_type_t type, uint32_t sector_count, uint16_t sector_size, bool read_only, void *driver_data, block_read_fn_t read) {
    if (!name || !read || g_device_count >= BLOCK_MAX_DEVICES) return false;
    if (sector_size == 0) sector_size = BLOCK_SECTOR_SIZE;

    block_device_t *dev = &g_devices[g_device_count++];
    kmemset(dev, 0, sizeof(*dev));
    kstrncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->type = type;
    dev->sector_count = sector_count;
    dev->base_lba = 0;
    dev->sector_size = sector_size;
    dev->read_only = read_only;
    dev->driver_data = driver_data;
    dev->read = read;
    return true;
}

block_device_t *block_get(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (kstrcmp(g_devices[i].name, name) == 0) return &g_devices[i];
    }
    return NULL;
}

uint32_t block_count(void) {
    return g_device_count;
}

block_device_t *block_at(uint32_t index) {
    if (index >= g_device_count) return NULL;
    return &g_devices[index];
}

bool block_read(block_device_t *dev, uint32_t lba, uint8_t count, void *buffer) {
    bool result;
    if (!dev || !dev->read || !buffer || count == 0) return false;
    if (dev->sector_count && lba + count > dev->sector_count) return false;

    block_io_lock();
    result = dev->read(dev, dev->base_lba + lba, count, buffer);
    block_io_unlock();
    return result;
}

bool block_set_writer(const char *name, block_write_fn_t write) {
    block_device_t *dev = block_get(name);
    if (!dev) return false;
    dev->write = write;
    return true;
}

bool block_write(block_device_t *dev, uint32_t lba, uint8_t count, const void *buffer) {
    bool result;
    if (!dev || !dev->write || !buffer || count == 0) return false;
    if (dev->read_only) return false;
    if (dev->sector_count && lba + count > dev->sector_count) return false;

    block_io_lock();
    result = dev->write(dev, dev->base_lba + lba, count, buffer);
    block_io_unlock();
    return result;
}

const char *block_type_name(block_device_type_t type) {
    switch (type) {
        case BLOCK_DEVICE_ATA: return "ata";
        case BLOCK_DEVICE_FLOPPY: return "floppy";
        case BLOCK_DEVICE_ATAPI: return "atapi";
        default: return "none";
    }
}
