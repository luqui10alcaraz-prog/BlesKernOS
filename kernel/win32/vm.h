#ifndef BLESKERNOS_WIN32_VM_H
#define BLESKERNOS_WIN32_VM_H

#include "../include/types.h"

typedef struct {
    void *base_address;
    void *allocation_base;
    uint32_t allocation_protect;
    uint32_t region_size;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
} win32_memory_basic_information_t;

void *win32_vm_alloc(void *address, uint32_t size, uint32_t type,
                     uint32_t protect, uint32_t *error);
bool win32_vm_free(void *address, uint32_t size, uint32_t type,
                   uint32_t *error);
bool win32_vm_protect(void *address, uint32_t size, uint32_t protect,
                      uint32_t *old_protect, uint32_t *error);
bool win32_vm_query(const void *address, win32_memory_basic_information_t *info);
void win32_vm_cleanup_process(uint32_t process_id);

#endif
