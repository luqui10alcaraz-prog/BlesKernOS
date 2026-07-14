#ifndef DRIVER_H
#define DRIVER_H

#include "types.h"

#define BK_DRIVER_ABI_VERSION 2U
#define BK_DRIVER_MAX_LOADED  16U

typedef struct {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;
    const char *description;
    bool (*init)(void);
    void (*shutdown)(void);
} bk_driver_module_t;

typedef const bk_driver_module_t *(*bk_driver_query_t)(void);

typedef struct {
    char name[32];
    char path[260];
    const char *description;
    void *image;
    void (*shutdown)(void);
} bk_loaded_driver_t;

void driver_loader_init(void);
bool driver_load(const char *path);
uint32_t driver_load_directory(const char *path);
uint32_t driver_count(void);
const bk_loaded_driver_t *driver_at(uint32_t index);
const char *driver_last_error(void);

#endif
