#include "../../include/block.h"
#include "../../include/memory.h"
#include "../../include/task.h"
#include "../../include/bootsplash.h"
#include "../../include/perfmon.h"
#include "../../include/klock.h"

static block_device_t g_devices[BLOCK_MAX_DEVICES];
static uint32_t g_device_count = 0;
static kmutex_t g_block_io_lock = KMUTEX_INITIALIZER;
static krwlock_t g_block_registry_lock = KRWLOCK_INITIALIZER;

/* Controller transactions may sleep while waiting for hardware. Use a
 * task-owned mutex rather than a CPU spinlock so unrelated render/GUI work
 * continues on the other processors. */
static void block_io_lock(void) {
    kmutex_lock(&g_block_io_lock);
}

static void block_io_unlock(void) {
    kmutex_unlock(&g_block_io_lock);
}

void block_init(void) {
    kmemset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    kmutex_init(&g_block_io_lock);
    krwlock_init(&g_block_registry_lock);
}

bool block_register(const char *name, block_device_type_t type, uint32_t sector_count, void *driver_data, block_read_fn_t read) {
    return block_register_ex(name, type, sector_count, BLOCK_SECTOR_SIZE, false, driver_data, read);
}

bool block_register_ex(const char *name, block_device_type_t type, uint32_t sector_count, uint16_t sector_size, bool read_only, void *driver_data, block_read_fn_t read) {
    block_device_t *dev;
    bool ok = false;
    if (!name || !read) return false;
    if (sector_size == 0) sector_size = BLOCK_SECTOR_SIZE;

    krwlock_write_lock(&g_block_registry_lock);
    if (g_device_count < BLOCK_MAX_DEVICES) {
        dev = &g_devices[g_device_count++];
        kmemset(dev, 0, sizeof(*dev));
        kstrncpy(dev->name, name, sizeof(dev->name) - 1);
        dev->type = type;
        dev->sector_count = sector_count;
        dev->base_lba = 0;
        dev->sector_size = sector_size;
        dev->read_only = read_only;
        dev->driver_data = driver_data;
        dev->read = read;
        ok = true;
    }
    krwlock_write_unlock(&g_block_registry_lock);
    return ok;
}

static block_device_t *block_find_unlocked(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < g_device_count; i++)
        if (kstrcmp(g_devices[i].name, name) == 0) return &g_devices[i];
    return NULL;
}

bool block_register_view(const char *name, block_device_t *parent,
                         uint32_t base_lba, uint32_t sector_count) {
    block_device_t *view;
    bool ok = false;
    if (!name || !name[0] || !parent || !parent->read || !sector_count)
        return false;
    if (parent->sector_count &&
        (base_lba >= parent->sector_count ||
         sector_count > parent->sector_count - base_lba)) return false;

    krwlock_write_lock(&g_block_registry_lock);
    if (g_device_count < BLOCK_MAX_DEVICES && !block_find_unlocked(name)) {
        view = &g_devices[g_device_count++];
        kmemset(view, 0, sizeof(*view));
        kstrncpy(view->name, name, sizeof(view->name) - 1U);
        view->type = parent->type;
        view->sector_count = sector_count;
        view->base_lba = parent->base_lba + base_lba;
        view->sector_size = parent->sector_size;
        view->read_only = parent->read_only;
        view->driver_data = parent->driver_data;
        view->read = parent->read;
        view->write = parent->write;
        ok = true;
    }
    krwlock_write_unlock(&g_block_registry_lock);
    return ok;
}

block_device_t *block_get(const char *name) {
    block_device_t *result;
    krwlock_read_lock(&g_block_registry_lock);
    result = block_find_unlocked(name);
    krwlock_read_unlock(&g_block_registry_lock);
    return result;
}

uint32_t block_count(void) {
    uint32_t count;
    krwlock_read_lock(&g_block_registry_lock);
    count = g_device_count;
    krwlock_read_unlock(&g_block_registry_lock);
    return count;
}

block_device_t *block_at(uint32_t index) {
    block_device_t *result = NULL;
    krwlock_read_lock(&g_block_registry_lock);
    if (index < g_device_count) result = &g_devices[index];
    krwlock_read_unlock(&g_block_registry_lock);
    return result;
}

bool block_read(block_device_t *dev, uint32_t lba, uint8_t count, void *buffer) {
    bool result;
    uint64_t perf_started;
    if (!dev || !dev->read || !buffer || count == 0) return false;
    if (dev->sector_count && lba + count > dev->sector_count) return false;

    bootsplash_pulse();
    perf_started = perfmon_scope_begin();
    block_io_lock();
    result = dev->read(dev, dev->base_lba + lba, count, buffer);
    block_io_unlock();
    perfmon_block_complete((uint32_t)dev->type, false, count, result,
                           perf_started);
    bootsplash_pulse();
    return result;
}

bool block_set_writer(const char *name, block_write_fn_t write) {
    block_device_t *dev;
    bool ok = false;
    krwlock_write_lock(&g_block_registry_lock);
    dev = block_find_unlocked(name);
    if (dev) {
        dev->write = write;
        ok = true;
    }
    krwlock_write_unlock(&g_block_registry_lock);
    return ok;
}

bool block_write(block_device_t *dev, uint32_t lba, uint8_t count, const void *buffer) {
    bool result;
    uint64_t perf_started;
    if (!dev || !dev->write || !buffer || count == 0) return false;
    if (dev->read_only) return false;
    if (dev->sector_count && lba + count > dev->sector_count) return false;

    perf_started = perfmon_scope_begin();
    block_io_lock();
    result = dev->write(dev, dev->base_lba + lba, count, buffer);
    block_io_unlock();
    perfmon_block_complete((uint32_t)dev->type, true, count, result,
                           perf_started);
    return result;
}

const char *block_type_name(block_device_type_t type) {
    switch (type) {
        case BLOCK_DEVICE_ATA: return "ata";
        case BLOCK_DEVICE_FLOPPY: return "floppy";
        case BLOCK_DEVICE_ATAPI: return "atapi";
        case BLOCK_DEVICE_USB: return "usb-storage";
        default: return "none";
    }
}
