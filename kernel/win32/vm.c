#include "vm.h"
#include "../include/memory.h"
#include "../include/task.h"

#define ERROR_INVALID_ADDRESS 487U
#define ERROR_INVALID_PARAMETER 87U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define MEM_COMMIT 0x1000U
#define MEM_RESERVE 0x2000U
#define MEM_DECOMMIT 0x4000U
#define MEM_RELEASE 0x8000U
#define MEM_FREE 0x10000U
#define MEM_PRIVATE 0x20000U
#define PAGE_NOACCESS 0x01U
#define PAGE_READONLY 0x02U
#define PAGE_READWRITE 0x04U
#define PAGE_WRITECOPY 0x08U
#define PAGE_EXECUTE 0x10U
#define PAGE_EXECUTE_READ 0x20U
#define PAGE_EXECUTE_READWRITE 0x40U
#define PAGE_EXECUTE_WRITECOPY 0x80U
#define VM_REGION_MAX 128U
#define VM_PAGE_SIZE 4096U
/* Win32 reserves address-space regions on 64 KiB boundaries.  The classic
 * MSVC small-block heap derives data addresses from that alignment. */
#define VM_ALLOCATION_GRANULARITY 65536U

typedef struct {
    bool used;
    uint32_t owner_process_id;
    uint8_t *allocation;
    uint8_t *base;
    uint32_t size;
    uint32_t state;
    uint32_t allocation_protect;
    uint32_t protect;
    uint32_t type;
} vm_region_t;

static vm_region_t regions[VM_REGION_MAX];

static uint32_t align_page(uint32_t value) {
    if (value > 0xFFFFF000U) return 0U;
    return (value + VM_PAGE_SIZE - 1U) & ~(VM_PAGE_SIZE - 1U);
}

static bool protect_valid(uint32_t protect) {
    uint32_t base = protect & 0xFFU;
    return base == PAGE_NOACCESS || base == PAGE_READONLY ||
           base == PAGE_READWRITE || base == PAGE_WRITECOPY ||
           base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ ||
           base == PAGE_EXECUTE_READWRITE ||
           base == PAGE_EXECUTE_WRITECOPY;
}

static vm_region_t *find_region(uint32_t owner, const void *address) {
    uintptr_t value = (uintptr_t)address;
    for (uint32_t i = 0; i < VM_REGION_MAX; i++) {
        uintptr_t start, end;
        vm_region_t *region = &regions[i];
        if (!region->used || region->owner_process_id != owner) continue;
        start = (uintptr_t)region->base;
        end = start + region->size;
        if (end >= start && value >= start && value < end) return region;
    }
    return NULL;
}

void *win32_vm_alloc(void *address, uint32_t size, uint32_t type,
                     uint32_t protect, uint32_t *error) {
    uint32_t owner = task_current_process_id();
    uint32_t aligned = align_page(size);
    vm_region_t *region;
    if (error) *error = 0U;
    if (!aligned || !(type & (MEM_COMMIT | MEM_RESERVE)) ||
        (type & ~(MEM_COMMIT | MEM_RESERVE)) || !protect_valid(protect)) {
        if (error) *error = ERROR_INVALID_PARAMETER;
        return NULL;
    }

    if (address) {
        uintptr_t start;
        uintptr_t requested = (uintptr_t)address;
        region = find_region(owner, address);
        if (!region || !(type & MEM_COMMIT) ||
            (requested & (VM_PAGE_SIZE - 1U)) != 0U) {
            if (error) *error = ERROR_INVALID_ADDRESS;
            return NULL;
        }
        start = (uintptr_t)region->base;
        if (requested + aligned < requested ||
            requested + aligned > start + region->size) {
            if (error) *error = ERROR_INVALID_ADDRESS;
            return NULL;
        }
        region->state = MEM_COMMIT;
        region->protect = protect;
        kmemset((void *)requested, 0, aligned);
        return address;
    }

    for (uint32_t i = 0; i < VM_REGION_MAX; i++) {
        uint32_t allocation_size;
        uintptr_t aligned_base;
        uint8_t *allocation;
        region = &regions[i];
        if (region->used) continue;
        if (aligned > 0xFFFFFFFFU - (VM_ALLOCATION_GRANULARITY - 1U)) {
            if (error) *error = ERROR_NOT_ENOUGH_MEMORY;
            return NULL;
        }
        allocation_size = aligned + VM_ALLOCATION_GRANULARITY - 1U;
        allocation = (uint8_t *)kzalloc(allocation_size);
        if (!allocation) {
            if (error) *error = ERROR_NOT_ENOUGH_MEMORY;
            return NULL;
        }
        aligned_base =
            ((uintptr_t)allocation + VM_ALLOCATION_GRANULARITY - 1U) &
            ~(uintptr_t)(VM_ALLOCATION_GRANULARITY - 1U);
        kmemset(region, 0, sizeof(*region));
        region->used = true;
        region->owner_process_id = owner;
        region->allocation = allocation;
        region->base = (uint8_t *)aligned_base;
        region->size = aligned;
        region->state = (type & MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
        region->allocation_protect = protect;
        region->protect = (type & MEM_COMMIT) ? protect : PAGE_NOACCESS;
        region->type = MEM_PRIVATE;
        return region->base;
    }
    if (error) *error = ERROR_NOT_ENOUGH_MEMORY;
    return NULL;
}

bool win32_vm_free(void *address, uint32_t size, uint32_t type,
                   uint32_t *error) {
    uint32_t owner = task_current_process_id();
    vm_region_t *region = find_region(owner, address);
    if (error) *error = 0U;
    if (!address || !region || region->base != (uint8_t *)address) {
        if (error) *error = ERROR_INVALID_ADDRESS;
        return false;
    }
    if (type == MEM_RELEASE) {
        if (size != 0U) {
            if (error) *error = ERROR_INVALID_PARAMETER;
            return false;
        }
        kfree(region->allocation);
        kmemset(region, 0, sizeof(*region));
        return true;
    }
    if (type == MEM_DECOMMIT) {
        if (!size || align_page(size) > region->size) {
            if (error) *error = ERROR_INVALID_PARAMETER;
            return false;
        }
        kmemset(region->base, 0, region->size);
        region->state = MEM_RESERVE;
        region->protect = PAGE_NOACCESS;
        return true;
    }
    if (error) *error = ERROR_INVALID_PARAMETER;
    return false;
}

bool win32_vm_protect(void *address, uint32_t size, uint32_t protect,
                      uint32_t *old_protect, uint32_t *error) {
    uint32_t owner = task_current_process_id();
    vm_region_t *region = find_region(owner, address);
    uintptr_t start;
    if (error) *error = 0U;
    if (!address || !size || !old_protect || !protect_valid(protect) ||
        !region || region->state != MEM_COMMIT) {
        if (error) *error = ERROR_INVALID_PARAMETER;
        return false;
    }
    start = (uintptr_t)region->base;
    if ((uintptr_t)address + size < (uintptr_t)address ||
        (uintptr_t)address + size > start + region->size) {
        if (error) *error = ERROR_INVALID_ADDRESS;
        return false;
    }
    *old_protect = region->protect;
    region->protect = protect;
    return true;
}

bool win32_vm_query(const void *address, win32_memory_basic_information_t *info) {
    uint32_t owner = task_current_process_id();
    vm_region_t *region;
    uintptr_t page;
    if (!info) return false;
    kmemset(info, 0, sizeof(*info));
    region = find_region(owner, address);
    if (region) {
        info->base_address = region->base;
        info->allocation_base = region->base;
        info->allocation_protect = region->allocation_protect;
        info->region_size = region->size;
        info->state = region->state;
        info->protect = region->protect;
        info->type = region->type;
        return true;
    }
    page = (uintptr_t)address & ~(uintptr_t)(VM_PAGE_SIZE - 1U);
    info->base_address = (void *)page;
    info->region_size = VM_PAGE_SIZE;
    info->state = MEM_FREE;
    info->protect = PAGE_NOACCESS;
    return true;
}

void win32_vm_cleanup_process(uint32_t process_id) {
    for (uint32_t i = 0; i < VM_REGION_MAX; i++) {
        vm_region_t *region = &regions[i];
        if (!region->used || region->owner_process_id != process_id) continue;
        if (region->allocation) kfree(region->allocation);
        kmemset(region, 0, sizeof(*region));
    }
}
