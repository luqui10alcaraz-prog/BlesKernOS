#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define HEAP_START  0x00200000
/*
 * Doom reserva hasta 16 MiB para su zone allocator, además de sus
 * framebuffers y de la memoria que ya consume el escritorio.  Un heap de
 * 16 MiB no puede satisfacer esas reservas aunque QEMU entregue 128 MiB.
 */
#define HEAP_SIZE   0x04000000
#define HEAP_END    (HEAP_START + HEAP_SIZE)
#define HEAP_MAGIC  0xB1E5C0DE
#define MEMORY_DISPLAY_MB_THRESHOLD (64U * 1024U * 1024U)

typedef struct heap_block {
    uint32_t magic;
    size_t size;
    bool free;
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

void mm_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void mm_get_info(heap_info_t *info);
void mm_get_system_info(system_memory_info_t *info);
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
