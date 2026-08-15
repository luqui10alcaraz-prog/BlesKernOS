#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

/* El perfil de 8 MiB aprovecha el hueco posterior a la pila temporal de
 * Stage 2. La vista PE histórica se conserva desde 4 MiB; sólo el heap
 * general puede comenzar antes. */
#define HEAP_START          0x00350000U
#define PE_FIXED_VIEW_START 0x00400000U
#define HEAP_LOW_START      HEAP_START
#define HEAP_ALLOC_START    0x00800000U
#define PE_FIXED_VIEW_END   HEAP_ALLOC_START
#define HEAP_DEFAULT_SIZE 0x04000000U
#define HEAP_LIMIT_MAX    0x40000000U
#define HEAP_END          (mm_heap_end())
#define HEAP_SIZE         (mm_heap_size())
#define HEAP_MAGIC  0xB1E5C0DE
#define MEMORY_DISPLAY_MB_THRESHOLD (64U * 1024U * 1024U)

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    bool free;
    uint32_t owner_process_id;
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t used_blocks;
} heap_info_t;

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
    size_t reserved_bytes;
} system_memory_info_t;

/* Copy the bootloader E820 map out of the first physical page before
 * paging installs the null-page guard. Must run exactly once during early
 * BSP boot, while physical address 0x500 is still readable. */
void mm_boot_snapshot(void);
/* Cached legacy boot data from the first physical page. These accessors are
 * safe after the null-page guard is enabled; before the snapshot they fall
 * back to the firmware buffer only while paging is still disabled. */
uint16_t mm_boot_equipment_word(void);
uint16_t mm_boot_ebda_segment(void);
uint16_t mm_boot_conventional_kb(void);
uint8_t mm_boot_vesa_read8(uint32_t offset);
uint16_t mm_boot_vesa_read16(uint32_t offset);
uint32_t mm_boot_vesa_read32(uint32_t offset);
void mm_init(void);
uint32_t mm_heap_start(void);
uint32_t mm_heap_end(void);
uint32_t mm_physical_top(void);
uint32_t mm_total_bytes(void);
size_t mm_heap_size(void);

/* Page-aligned stacks with non-present guard pages. `allocation_out` is an
 * opaque token required by mm_free_guarded_stack(); callers use the returned
 * address as the usable stack base. */
void *mm_alloc_guarded_stack(uint32_t requested_size, bool user_stack,
                             void **allocation_out,
                             uint32_t *usable_size_out);
void mm_free_guarded_stack(void *allocation, void *stack,
                           uint32_t usable_size);

void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
size_t mm_allocation_size(const void *ptr);
void mm_get_info(heap_info_t *info);
void mm_get_system_info(system_memory_info_t *info);
size_t mm_get_process_usage(uint32_t process_id);
void mm_get_process_usage_batch(const uint32_t *process_ids, size_t *usage,
                                uint32_t count);
bool mm_set_allocation_owner(void *ptr, uint32_t process_id);
size_t mm_release_process(uint32_t process_id);
void mm_dump(void);
void *kmemset(void *dst, int c, size_t n);
void *kmemcpy(void *dst, const void *src, size_t n);
int kmemcmp(const void *a, const void *b, size_t n);
size_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
char *kstrcpy(char *dst, const char *src);
char *kstrncpy(char *dst, const char *src, size_t n);
char *kstrcat(char *dst, const char *src);

#endif
