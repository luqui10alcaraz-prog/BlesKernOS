#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

#define BLOCK_MAX_DEVICES 8
#define BLOCK_SECTOR_SIZE 512

typedef enum {
    BLOCK_DEVICE_NONE = 0,
    BLOCK_DEVICE_ATA,
    BLOCK_DEVICE_FLOPPY,
    BLOCK_DEVICE_ATAPI,
    BLOCK_DEVICE_USB
} block_device_type_t;

typedef struct block_device block_device_t;
typedef bool (*block_read_fn_t)(block_device_t *dev, uint32_t lba, uint8_t count, void *buffer);
typedef bool (*block_write_fn_t)(block_device_t *dev, uint32_t lba, uint8_t count, const void *buffer);

struct block_device {
    char name[8];
    block_device_type_t type;
    uint32_t sector_count;
    uint32_t base_lba;
    uint16_t sector_size;
    bool read_only;
    void *driver_data;
    block_read_fn_t read;
    block_write_fn_t write;
};

void block_init(void);
bool block_register(const char *name, block_device_type_t type, uint32_t sector_count, void *driver_data, block_read_fn_t read);
bool block_register_ex(const char *name, block_device_type_t type, uint32_t sector_count, uint16_t sector_size, bool read_only, void *driver_data, block_read_fn_t read);
block_device_t *block_get(const char *name);
uint32_t block_count(void);
block_device_t *block_at(uint32_t index);
bool block_read(block_device_t *dev, uint32_t lba, uint8_t count, void *buffer);
bool block_set_writer(const char *name, block_write_fn_t write);
bool block_write(block_device_t *dev, uint32_t lba, uint8_t count, const void *buffer);
const char *block_type_name(block_device_type_t type);

#endif
