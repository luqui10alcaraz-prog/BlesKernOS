#include "include/types.h"
#include "include/memory.h"
#include "include/vga.h"
#include "include/task.h"

static heap_block_t *heap_head = NULL;
static uint32_t system_total_bytes = HEAP_SIZE;
static uint32_t system_reserved_bytes = 0;

#define ALIGN4(x) (((x) + 3) & ~3)
#define E820_MAP_ADDR     0x00000500U
#define E820_ENTRY_ADDR   (E820_MAP_ADDR + 4U)
#define E820_MAX_ENTRIES  20U
#define U32_MAX_VALUE     0xFFFFFFFFU

typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} PACKED e820_entry_t;

static uint16_t mm_boot_read16(uint32_t address) {
    uint16_t value;
    volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)address;
    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static uint32_t mm_boot_read32(uint32_t address) {
    uint32_t value;
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)address;
    __asm__ volatile ("movl (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static void mm_read_e820_entry(uint32_t index, e820_entry_t *entry) {
    uint32_t offset = E820_ENTRY_ADDR + (index * (uint32_t)sizeof(e820_entry_t));
    if (!entry) return;
    entry->base_low = mm_boot_read32(offset);
    entry->base_high = mm_boot_read32(offset + 4U);
    entry->length_low = mm_boot_read32(offset + 8U);
    entry->length_high = mm_boot_read32(offset + 12U);
    entry->type = mm_boot_read32(offset + 16U);
}

static uint32_t mm_add_clamped(uint32_t left, uint32_t right) {
    if (U32_MAX_VALUE - left < right) return U32_MAX_VALUE;
    return left + right;
}

static uint32_t mm_display_size_value(uint32_t bytes, const char **unit) {
    if (bytes > MEMORY_DISPLAY_MB_THRESHOLD) {
        if (unit) *unit = "MB";
        return (bytes + ((1024U * 1024U) - 1U)) / (1024U * 1024U);
    }
    if (unit) *unit = "KB";
    return (bytes + 1023U) / 1024U;
}

static uint32_t mm_overlap_low_memory(uint32_t start, uint32_t length) {
    uint32_t end;

    if (length == 0 || start >= HEAP_START) return 0;
    end = start + length;
    if (end < start || end > HEAP_START) end = HEAP_START;
    return end > start ? end - start : 0;
}

static void mm_detect_system_memory(void) {
    uint32_t count = mm_boot_read16(E820_MAP_ADDR);
    uint32_t total = 0;
    uint32_t reserved = 0;

    if (count > E820_MAX_ENTRIES) count = E820_MAX_ENTRIES;
    for (uint32_t i = 0; i < count; i++) {
        e820_entry_t entry;
        uint32_t start;
        uint32_t length;

        mm_read_e820_entry(i, &entry);
        if (entry.type != 1U) continue;
        if (entry.base_high != 0U) continue;

        start = entry.base_low;
        length = entry.length_low;
        if (entry.length_high != 0U || start + length < start) {
            length = U32_MAX_VALUE - start;
        }
        if (length == 0U) continue;

        total = mm_add_clamped(total, length);
        reserved = mm_add_clamped(reserved, mm_overlap_low_memory(start, length));
    }

    if (total != 0U) {
        system_total_bytes = total;
        system_reserved_bytes = reserved <= total ? reserved : total;
    }
}

void *kmemset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = (uint8_t)c;
    return dst;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)(*pa - *pb);
        pa++;
        pb++;
    }
    return 0;
}

size_t kstrlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && (*a == *b)) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *kstrcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}

char *kstrcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

void mm_init(void) {
    const char *unit;
    uint32_t ram_value;

    mm_detect_system_memory();
    heap_head = (heap_block_t *)HEAP_START;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size = HEAP_SIZE - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    ram_value = mm_display_size_value(system_total_bytes, &unit);
    kprintf("  [MM] RAM usable: %u %s\n", ram_value, unit);
    kprintf("  [MM] Heap: %x - %x (%u KB)\n", HEAP_START, HEAP_END, HEAP_SIZE / 1024);
}

void *kmalloc(size_t size) {
    if (!size) return NULL;
    task_preempt_disable();
    size = ALIGN4(size);

    heap_block_t *blk = heap_head;
    while (blk) {
        if (blk->free && blk->size >= size) {
            if (blk->size >= size + sizeof(heap_block_t) + 16) {
                uint8_t *raw = (uint8_t *)blk;
                heap_block_t *split = (heap_block_t *)(raw + sizeof(heap_block_t) + size);
                split->magic = HEAP_MAGIC;
                split->size = blk->size - size - sizeof(heap_block_t);
                split->free = true;
                split->next = blk->next;
                split->prev = blk;
                if (blk->next) blk->next->prev = split;
                blk->next = split;
                blk->size = size;
            }
            blk->free = false;
            void *result =
                (void *)((uint8_t *)blk + sizeof(heap_block_t));
            task_preempt_enable();
            return result;
        }
        blk = blk->next;
    }

    kprintf("[MM] ERROR: sin memoria! (pedido %u bytes)\n", size);
    mm_dump();
    task_preempt_enable();
    return NULL;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) kmemset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    task_preempt_disable();

    heap_block_t *blk = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (blk->magic != HEAP_MAGIC) {
        kprintf("[MM] ERROR: intento de liberar bloque corrupto\n");
        task_preempt_enable();
        return;
    }
    if (blk->free) {
        task_preempt_enable();
        return;
    }

    blk->free = true;

    if (blk->next && blk->next->free) {
        blk->size += sizeof(heap_block_t) + blk->next->size;
        blk->next = blk->next->next;
        if (blk->next) blk->next->prev = blk;
    }

    if (blk->prev && blk->prev->free) {
        blk->prev->size += sizeof(heap_block_t) + blk->size;
        blk->prev->next = blk->next;
        if (blk->next) blk->next->prev = blk->prev;
    }
    task_preempt_enable();
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    heap_block_t *blk = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (blk->size >= new_size) return ptr;

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    kmemcpy(new_ptr, ptr, blk->size);
    kfree(ptr);
    return new_ptr;
}

void mm_get_info(heap_info_t *info) {
    task_preempt_disable();
    heap_block_t *blk = heap_head;
    size_t total = 0;
    size_t used = 0;
    uint32_t blocks = 0;
    uint32_t free_blocks = 0;
    uint32_t used_blocks = 0;

    while (blk) {
        blocks++;
        total += blk->size + sizeof(heap_block_t);
        if (blk->free) free_blocks++; else used_blocks++;
        if (!blk->free) used += blk->size;
        blk = blk->next;
    }

    info->total_bytes = total;
    info->used_bytes = used;
    info->free_bytes = total - used;
    info->total_blocks = blocks;
    info->free_blocks = free_blocks;
    info->used_blocks = used_blocks;
    task_preempt_enable();
}

void mm_get_system_info(system_memory_info_t *info) {
    heap_info_t heap;
    uint32_t used;

    if (!info) return;

    mm_get_info(&heap);
    used = system_reserved_bytes;
    if (U32_MAX_VALUE - used < heap.used_bytes) used = U32_MAX_VALUE;
    else used += (uint32_t)heap.used_bytes;
    if (used > system_total_bytes) used = system_total_bytes;

    info->total_bytes = system_total_bytes;
    info->reserved_bytes = system_reserved_bytes;
    info->used_bytes = used;
    info->free_bytes = system_total_bytes - used;
}

void mm_dump(void) {
    heap_block_t *blk = heap_head;
    kprintf("[MM] Dump heap:\n");
    while (blk) {
        kprintf("  block %x: size=%u free=%u\n", (uint32_t)blk, blk->size, blk->free);
        blk = blk->next;
    }
}
