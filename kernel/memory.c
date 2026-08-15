#include "include/types.h"
#include "include/memory.h"
#include "include/vga.h"
#include "include/task.h"
#include "include/klock.h"
#include "include/paging.h"

static heap_block_t *heap_head = NULL;
static uint32_t system_total_bytes = HEAP_DEFAULT_SIZE;
static uint32_t system_reserved_bytes = 0;
static uint32_t system_physical_top = HEAP_ALLOC_START + HEAP_DEFAULT_SIZE;
static uint32_t heap_start_address = HEAP_ALLOC_START;
static uint32_t heap_end_address = HEAP_ALLOC_START + HEAP_DEFAULT_SIZE;

extern uint8_t __bss_end;

#define ALIGN4(x) (((x) + 3) & ~3)
#define E820_MAP_ADDR     0x00000500U
#define E820_ENTRY_ADDR   (E820_MAP_ADDR + 4U)
#define E820_MAX_ENTRIES  20U
#define BDA_EBDA_SEGMENT_ADDR   0x0000040EU
#define BDA_EQUIPMENT_WORD_ADDR 0x00000410U
#define BDA_CONVENTIONAL_KB_ADDR 0x00000413U
#define VESA_BOOTINFO_ADDR      0x00000700U
#define VESA_BOOTINFO_SIZE      16U
#define U32_MAX_VALUE     0xFFFFFFFFU
#define MM_PAGE_SIZE      0x1000U
#define MM_LOW_HEAP_SWITCH_TOP (12U * 1024U * 1024U)
#define MM_BOOT_STACK_TOP 0x00350000U
#define MM_BOOT_STACK_RESERVE (64U * 1024U)

typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} PACKED e820_entry_t;

/* Stage 2 publishes E820 at physical 0x500, which belongs to the page that
 * Phase 1 intentionally makes non-present. Keep a supervisor-only snapshot
 * in kernel BSS before enabling paging; no post-paging code may dereference
 * the bootloader buffer directly. */
static e820_entry_t g_boot_e820[E820_MAX_ENTRIES];
static uint32_t g_boot_e820_count;
static bool g_boot_e820_ready;
static uint8_t g_boot_vesa_info[VESA_BOOTINFO_SIZE];
static uint16_t g_boot_ebda_segment;
static uint16_t g_boot_equipment_word;
static uint16_t g_boot_conventional_kb;
static bool g_boot_legacy_ready;


/* The heap metadata is shared by every CPU. This is a short raw critical
 * section, not a kernel-wide gate: render, VFS and GUI code on other CPUs may
 * continue until they actually need an allocation. */
static kspinlock_t g_mm_heap_lock = KSPINLOCK_INITIALIZER;

static uint32_t mm_lock(void) {
    return kspin_lock_irqsave(&g_mm_heap_lock);
}

static void mm_unlock(uint32_t flags) {
    kspin_unlock_irqrestore(&g_mm_heap_lock, flags);
}

static bool mm_block_valid_locked(const heap_block_t *block) {
    uintptr_t address;
    uintptr_t payload_end;

    if (!block) return false;
    address = (uintptr_t)block;
    if (address < heap_start_address ||
        address > (uintptr_t)heap_end_address - sizeof(*block)) return false;
    if (block->magic != HEAP_MAGIC ||
        block->size > (size_t)((uintptr_t)heap_end_address - address -
                              sizeof(*block))) return false;
    payload_end = address + sizeof(*block) + block->size;
    if (block->next) {
        if ((uintptr_t)block->next != payload_end ||
            payload_end > (uintptr_t)heap_end_address - sizeof(*block) ||
            block->next->prev != block) return false;
    } else if (payload_end != (uintptr_t)heap_end_address) {
        return false;
    }
    return true;
}

static heap_block_t *mm_find_block_locked(const void *ptr) {
    heap_block_t *block;
    uintptr_t target;
    uint32_t limit;

    if (!ptr) return NULL;
    target = (uintptr_t)ptr;
    if (target < heap_start_address + sizeof(heap_block_t) ||
        target >= (uintptr_t)heap_end_address) return NULL;
    limit = (uint32_t)(mm_heap_size() / sizeof(heap_block_t)) + 1U;
    block = heap_head;
    while (block && limit--) {
        if (!mm_block_valid_locked(block)) return NULL;
        if ((uintptr_t)block + sizeof(*block) == target) return block;
        block = block->next;
    }
    return NULL;
}

static void mm_dump_locked(void);

static uint8_t mm_boot_read8(uint32_t address) {
    uint8_t value;
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)address;
    __asm__ volatile ("movb (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

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
    uint32_t offset;
    if (!entry) return;
    if (g_boot_e820_ready) {
        if (index < g_boot_e820_count) *entry = g_boot_e820[index];
        else kmemset(entry, 0, sizeof(*entry));
        return;
    }
    offset = E820_ENTRY_ADDR + (index * (uint32_t)sizeof(e820_entry_t));
    entry->base_low = mm_boot_read32(offset);
    entry->base_high = mm_boot_read32(offset + 4U);
    entry->length_low = mm_boot_read32(offset + 8U);
    entry->length_high = mm_boot_read32(offset + 12U);
    entry->type = mm_boot_read32(offset + 16U);
}

void mm_boot_snapshot(void) {
    uint32_t count;

    if (g_boot_e820_ready && g_boot_legacy_ready) return;

    /* All three structures live in physical page zero. Copy every piece of
     * firmware/bootloader state used by the kernel before paging makes that
     * page non-present. Preserving only E820 was insufficient: graphics still
     * consulted 0x700 and the BIOS equipment word at 0x410. */
    count = mm_boot_read16(E820_MAP_ADDR);
    if (count > E820_MAX_ENTRIES) count = E820_MAX_ENTRIES;
    for (uint32_t i = 0U; i < count; i++)
        mm_read_e820_entry(i, &g_boot_e820[i]);
    g_boot_e820_count = count;

    g_boot_ebda_segment = mm_boot_read16(BDA_EBDA_SEGMENT_ADDR);
    g_boot_equipment_word = mm_boot_read16(BDA_EQUIPMENT_WORD_ADDR);
    g_boot_conventional_kb = mm_boot_read16(BDA_CONVENTIONAL_KB_ADDR);
    for (uint32_t i = 0U; i < VESA_BOOTINFO_SIZE; i++)
        g_boot_vesa_info[i] = mm_boot_read8(VESA_BOOTINFO_ADDR + i);

    /* Publish readiness last so no future SMP reader can observe a partially
     * copied snapshot. BSP boot is still single-CPU here, but the ordering is
     * part of the interface. */
    __asm__ volatile ("" : : : "memory");
    g_boot_legacy_ready = true;
    g_boot_e820_ready = true;
}

uint16_t mm_boot_ebda_segment(void) {
    if (g_boot_legacy_ready) return g_boot_ebda_segment;
    if (paging_is_enabled()) return 0U;
    return mm_boot_read16(BDA_EBDA_SEGMENT_ADDR);
}

uint16_t mm_boot_equipment_word(void) {
    if (g_boot_legacy_ready) return g_boot_equipment_word;
    if (paging_is_enabled()) return 0U;
    return mm_boot_read16(BDA_EQUIPMENT_WORD_ADDR);
}

uint16_t mm_boot_conventional_kb(void) {
    if (g_boot_legacy_ready) return g_boot_conventional_kb;
    if (paging_is_enabled()) return 0U;
    return mm_boot_read16(BDA_CONVENTIONAL_KB_ADDR);
}

uint8_t mm_boot_vesa_read8(uint32_t offset) {
    if (offset >= VESA_BOOTINFO_SIZE) return 0U;
    if (g_boot_legacy_ready) return g_boot_vesa_info[offset];
    if (paging_is_enabled()) return 0U;
    return mm_boot_read8(VESA_BOOTINFO_ADDR + offset);
}

uint16_t mm_boot_vesa_read16(uint32_t offset) {
    uint16_t value;
    if (offset > VESA_BOOTINFO_SIZE - 2U) return 0U;
    value = (uint16_t)mm_boot_vesa_read8(offset);
    value |= (uint16_t)((uint16_t)mm_boot_vesa_read8(offset + 1U) << 8);
    return value;
}

uint32_t mm_boot_vesa_read32(uint32_t offset) {
    uint32_t value;
    if (offset > VESA_BOOTINFO_SIZE - 4U) return 0U;
    value = (uint32_t)mm_boot_vesa_read8(offset);
    value |= (uint32_t)mm_boot_vesa_read8(offset + 1U) << 8;
    value |= (uint32_t)mm_boot_vesa_read8(offset + 2U) << 16;
    value |= (uint32_t)mm_boot_vesa_read8(offset + 3U) << 24;
    return value;
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

static uint32_t mm_align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static bool mm_entry_range(const e820_entry_t *entry, uint32_t *start_out,
                           uint32_t *end_out) {
    uint32_t start;
    uint32_t length;
    uint32_t end;

    if (!entry || entry->type != 1U || entry->base_high != 0U) return false;
    start = entry->base_low;
    length = entry->length_low;
    if (!length) return false;
    if (entry->length_high != 0U || start + length < start)
        end = U32_MAX_VALUE;
    else
        end = start + length;
    if (start_out) *start_out = start;
    if (end_out) *end_out = end;
    return end > start;
}

static uint32_t mm_contiguous_usable_end(uint32_t address, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        e820_entry_t entry;
        uint32_t start;
        uint32_t end;
        mm_read_e820_entry(i, &entry);
        if (!mm_entry_range(&entry, &start, &end)) continue;
        if (address >= start && address < end) return end;
    }
    return 0U;
}

static void mm_detect_system_memory(void) {
    uint32_t count;
    uint32_t total = 0U;
    uint32_t physical_top = 0U;
    uint32_t candidate_start;
    uint32_t candidate_end;

    if (!g_boot_e820_ready) {
        /* Reading 0x500 after paging is enabled would fault before IDT setup
         * and therefore become a triple fault. Keep the conservative built-in
         * defaults rather than touching the now-unmapped null page. */
        if (paging_is_enabled()) {
            kprintf("[MM] ERROR: E820 no fue preservado antes de paging; "
                    "usando memoria por defecto\n");
            count = 0U;
        } else {
            mm_boot_snapshot();
            count = g_boot_e820_count;
        }
    } else {
        count = g_boot_e820_count;
    }
    for (uint32_t i = 0; i < count; i++) {
        e820_entry_t entry;
        uint32_t start;
        uint32_t end;
        uint32_t length;

        mm_read_e820_entry(i, &entry);
        if (!mm_entry_range(&entry, &start, &end)) continue;
        length = end - start;
        total = mm_add_clamped(total, length);
        if (end > physical_top) physical_top = end;
    }

    if (total != 0U) system_total_bytes = total;
    if (physical_top != 0U) system_physical_top = physical_top;
    if (system_physical_top > HEAP_LIMIT_MAX)
        system_physical_top = HEAP_LIMIT_MAX;
    system_physical_top &= ~(MM_PAGE_SIZE - 1U);

    /* La pila temporal ocupa 3.4-3.5 MiB, justo después del máximo de .bss.
     * Desde 5 MiB el heap puede comenzar en 3.5 MiB; con 12 MiB o más se
     * conserva la vista PE fija 4-8 MiB y el heap tradicional en 8 MiB. */
    if (system_physical_top <= HEAP_LOW_START) {
        candidate_start = mm_align_up((uint32_t)(uintptr_t)&__bss_end,
                                      MM_PAGE_SIZE);
        candidate_end = MM_BOOT_STACK_TOP - MM_BOOT_STACK_RESERVE;
        if (candidate_end > system_physical_top) candidate_end = system_physical_top;
    } else if (system_physical_top < MM_LOW_HEAP_SWITCH_TOP) {
        candidate_start = HEAP_LOW_START;
        candidate_end = mm_contiguous_usable_end(candidate_start, count);
    } else {
        candidate_start = HEAP_ALLOC_START;
        candidate_end = mm_contiguous_usable_end(candidate_start, count);
    }

    if (!candidate_end) candidate_end = system_physical_top;
    if (candidate_end > HEAP_LIMIT_MAX) candidate_end = HEAP_LIMIT_MAX;
    candidate_start = mm_align_up(candidate_start, MM_PAGE_SIZE);
    candidate_end &= ~(MM_PAGE_SIZE - 1U);

    /* Deje al menos una pagina para metadata y allocations pequeñas. */
    if (candidate_end <= candidate_start + MM_PAGE_SIZE) {
        heap_start_address = candidate_start;
        heap_end_address = candidate_start;
    } else {
        heap_start_address = candidate_start;
        heap_end_address = candidate_end;
    }

    system_reserved_bytes = system_total_bytes > mm_heap_size()
        ? system_total_bytes - (uint32_t)mm_heap_size() : 0U;
}

uint32_t mm_heap_start(void) { return heap_start_address; }
uint32_t mm_heap_end(void) { return heap_end_address; }
uint32_t mm_physical_top(void) { return system_physical_top; }
uint32_t mm_total_bytes(void) { return system_total_bytes; }


size_t mm_heap_size(void) {
    return heap_end_address > heap_start_address ? heap_end_address - heap_start_address : 0U;
}

void *kmemset(void *dst, int c, size_t n) {
    void *original = dst;
    uint8_t *bytes = (uint8_t *)dst;
    uint8_t value = (uint8_t)c;

    while (n && ((uintptr_t)bytes & 3U)) {
        *bytes++ = value;
        n--;
    }
    if (n >= 32U) {
        uint32_t dword = (uint32_t)value * 0x01010101U;
        uint32_t count = (uint32_t)(n >> 2);
        uint32_t *out = (uint32_t *)(void *)bytes;
        __asm__ volatile ("cld; rep stosl"
                          : "+D"(out), "+c"(count)
                          : "a"(dword) : "memory");
        bytes = (uint8_t *)(void *)out;
        n &= 3U;
    }
    while (n--) *bytes++ = value;
    return original;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    void *original = dst;
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    /* REP MOVSL es especialmente eficaz para superficies y caches grandes en
     * P6 (Pentium Pro/II/III). Para tamaños pequeños se evita su coste fijo. */
    if ((((uintptr_t)d | (uintptr_t)s) & 3U) == 0U && n >= 32U) {
        uint32_t count = (uint32_t)(n >> 2);
        uint32_t *out = (uint32_t *)(void *)d;
        const uint32_t *in = (const uint32_t *)(const void *)s;
        __asm__ volatile ("cld; rep movsl"
                          : "+D"(out), "+S"(in), "+c"(count)
                          : : "memory");
        d = (uint8_t *)(void *)out;
        s = (const uint8_t *)(const void *)in;
        n &= 3U;
    }
    while (n--) *d++ = *s++;
    return original;
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
    heap_head = (heap_block_t *)(uintptr_t)heap_start_address;
    if (mm_heap_size() <= sizeof(heap_block_t)) {
        kprintf("[MM] ERROR: no hay region contigua para el heap\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    heap_head->magic = HEAP_MAGIC;
    heap_head->size = mm_heap_size() - sizeof(heap_block_t);
    heap_head->free = true;
    heap_head->owner_process_id = 0U;
    heap_head->next = NULL;
    heap_head->prev = NULL;
    ram_value = mm_display_size_value(system_total_bytes, &unit);
    kprintf("  [MM] RAM usable: %u %s\n", ram_value, unit);
    kprintf("  [MM] Heap dinamico: %x - %x (%u KB)\n", mm_heap_start(),
            mm_heap_end(), (uint32_t)(mm_heap_size() / 1024U));
    if (mm_heap_start() >= PE_FIXED_VIEW_END)
        kprintf("  [MM] Vista PE fija: %x - %x\n",
                PE_FIXED_VIEW_START, PE_FIXED_VIEW_END);
    else
        kprintf("  [MM] Vista PE fija desactivada: heap reducido ocupa %x-%x\n",
                PE_FIXED_VIEW_START, PE_FIXED_VIEW_END);
    /* BLES_WINE_PREFERRED_VIEW_FIX_20260723 */
    if (paging_is_enabled() &&
        !paging_configure_user_layout(mm_heap_start(), mm_heap_end())) {
        kprintf("[PAGING] ERROR: no se pudo publicar el heap Ring 3\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

void *kmalloc(size_t size) {
    uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uint32_t flags;
    heap_block_t *blk;
    bool corrupt = false;

    if (!size || size > U32_MAX_VALUE - 3U) return NULL;
    size = ALIGN4(size);
    flags = mm_lock();

    blk = heap_head;
    while (blk) {
        uint32_t address = (uint32_t)(uintptr_t)blk;
        if (!mm_block_valid_locked(blk)) {
            kprintf("[MM] CORRUPCION heap bloque=%x magic=%x size=%u caller=%x\n",
                    address,
                    address >= heap_start_address &&
                    address <= heap_end_address - sizeof(heap_block_t)
                        ? blk->magic : 0U,
                    address >= heap_start_address &&
                    address <= heap_end_address - sizeof(heap_block_t)
                        ? (uint32_t)blk->size : 0U,
                    caller);
            corrupt = true;
            break;
        }
        if (blk->free && blk->size >= size) {
            if (blk->size >= size + sizeof(heap_block_t) + 16U) {
                uint8_t *raw = (uint8_t *)blk;
                heap_block_t *split = (heap_block_t *)(
                    raw + sizeof(heap_block_t) + size);
                split->magic = HEAP_MAGIC;
                split->size = blk->size - size - sizeof(heap_block_t);
                split->free = true;
                split->owner_process_id = 0U;
                split->next = blk->next;
                split->prev = blk;
                if (blk->next) blk->next->prev = split;
                blk->next = split;
                blk->size = size;
            }
            blk->free = false;
            blk->owner_process_id = task_current_process_id();
            void *result = (void *)((uint8_t *)blk + sizeof(heap_block_t));
            mm_unlock(flags);
            return result;
        }
        blk = blk->next;
    }

    if (!corrupt) {
        kprintf("[MM] ERROR: sin memoria! pedido=%u caller=%x pid=%u\n",
                size, caller, task_current_process_id());
    }
    mm_dump_locked();
    mm_unlock(flags);
    return NULL;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) kmemset(ptr, 0, size);
    return ptr;
}

void *mm_alloc_guarded_stack(uint32_t requested_size, bool user_stack,
                             void **allocation_out,
                             uint32_t *usable_size_out) {
    uint32_t usable_size;
    uint32_t total_size;
    uint8_t *raw;
    uint8_t *stack;
    uint8_t *lower_guard;
    uint8_t *upper_guard;

    if (allocation_out) *allocation_out = NULL;
    if (usable_size_out) *usable_size_out = 0U;
    if (requested_size < 16384U) requested_size = 16384U;
    if (requested_size > 0xFFFFFFFFU - (PAGING_PAGE_SIZE - 1U))
        return NULL;
    usable_size = (requested_size + PAGING_PAGE_SIZE - 1U) &
                  ~(PAGING_PAGE_SIZE - 1U);
    if (usable_size > 0xFFFFFFFFU - PAGING_PAGE_SIZE * 3U) return NULL;
    total_size = usable_size + PAGING_PAGE_SIZE * 3U;
    raw = (uint8_t *)kmalloc(total_size);
    if (!raw) return NULL;
    (void)mm_set_allocation_owner(raw, 0U);

    stack = (uint8_t *)(((uintptr_t)raw + PAGING_PAGE_SIZE * 2U - 1U) &
                        ~(uintptr_t)(PAGING_PAGE_SIZE - 1U));
    lower_guard = stack - PAGING_PAGE_SIZE;
    upper_guard = stack + usable_size;
    if (lower_guard < raw ||
        upper_guard + PAGING_PAGE_SIZE > raw + total_size) {
        kfree(raw);
        return NULL;
    }

    kmemset(stack, 0, usable_size);
    if (!paging_set_range((uint32_t)(uintptr_t)lower_guard,
                          PAGING_PAGE_SIZE, false, false, false) ||
        !paging_set_range((uint32_t)(uintptr_t)stack, usable_size,
                          true, user_stack, true) ||
        !paging_set_range((uint32_t)(uintptr_t)upper_guard,
                          PAGING_PAGE_SIZE, false, false, false)) {
        (void)paging_set_range((uint32_t)(uintptr_t)lower_guard,
            usable_size + PAGING_PAGE_SIZE * 2U, true, true, true);
        kfree(raw);
        return NULL;
    }

    if (allocation_out) *allocation_out = raw;
    if (usable_size_out) *usable_size_out = usable_size;
    return stack;
}

void mm_free_guarded_stack(void *allocation, void *stack,
                           uint32_t usable_size) {
    uint32_t rounded;
    uint32_t lower;
    if (!allocation || !stack || !usable_size) return;
    rounded = (usable_size + PAGING_PAGE_SIZE - 1U) &
              ~(PAGING_PAGE_SIZE - 1U);
    lower = (uint32_t)(uintptr_t)stack - PAGING_PAGE_SIZE;
    /* Return the pages to the heap's default Ring-3 mapping before another
     * allocation reuses them. The shared TLB generation prevents stale guard
     * entries on a CPU that previously ran this task. */
    (void)paging_set_range(lower, rounded + PAGING_PAGE_SIZE * 2U,
                           true, true, true);
    kfree(allocation);
}

void kfree(void *ptr) {
    uint32_t flags;
    heap_block_t *blk;

    if (!ptr) return;
    flags = mm_lock();
    blk = mm_find_block_locked(ptr);
    if (!blk) {
        kprintf("[MM] ERROR: free invalido ptr=%x caller=%x\n",
                (uint32_t)(uintptr_t)ptr,
                (uint32_t)(uintptr_t)__builtin_return_address(0));
        mm_unlock(flags);
        return;
    }
    if (blk->free) {
        mm_unlock(flags);
        return;
    }

    blk->free = true;
    blk->owner_process_id = 0U;

    if (blk->next && mm_block_valid_locked(blk->next) && blk->next->free) {
        heap_block_t *next = blk->next;
        blk->size += sizeof(heap_block_t) + next->size;
        blk->next = next->next;
        if (blk->next) blk->next->prev = blk;
    }

    if (blk->prev && mm_block_valid_locked(blk->prev) && blk->prev->free) {
        heap_block_t *prev = blk->prev;
        prev->size += sizeof(heap_block_t) + blk->size;
        prev->next = blk->next;
        if (prev->next) prev->next->prev = prev;
    }
    mm_unlock(flags);
}

size_t mm_allocation_size(const void *ptr) {
    uint32_t flags;
    heap_block_t *block;
    size_t size = 0U;

    if (!ptr) return 0U;
    flags = mm_lock();
    block = mm_find_block_locked(ptr);
    if (block && !block->free) size = block->size;
    mm_unlock(flags);
    return size;
}

void *krealloc(void *ptr, size_t new_size) {
    uint32_t flags;
    heap_block_t *block;
    size_t old_size;
    void *new_ptr;

    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    flags = mm_lock();
    block = mm_find_block_locked(ptr);
    if (!block || block->free) {
        mm_unlock(flags);
        return NULL;
    }
    old_size = block->size;
    if (old_size >= new_size) {
        mm_unlock(flags);
        return ptr;
    }
    mm_unlock(flags);

    new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;
    kmemcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}

void mm_get_info(heap_info_t *info) {
    uint32_t flags;
    heap_block_t *blk;
    size_t total = 0U;
    size_t used = 0U;
    uint32_t blocks = 0U;
    uint32_t free_blocks = 0U;
    uint32_t used_blocks = 0U;

    if (!info) return;
    flags = mm_lock();
    blk = heap_head;
    while (blk && mm_block_valid_locked(blk)) {
        blocks++;
        total += blk->size + sizeof(heap_block_t);
        if (blk->free) free_blocks++; else used_blocks++;
        if (!blk->free) used += blk->size;
        blk = blk->next;
    }

    info->total_bytes = total;
    info->used_bytes = used;
    info->free_bytes = total >= used ? total - used : 0U;
    info->total_blocks = blocks;
    info->free_blocks = free_blocks;
    info->used_blocks = used_blocks;
    mm_unlock(flags);
}

bool mm_set_allocation_owner(void *ptr, uint32_t process_id) {
    uint32_t flags;
    heap_block_t *blk;
    bool result = false;

    if (!ptr) return false;
    flags = mm_lock();
    blk = mm_find_block_locked(ptr);
    if (blk && !blk->free) {
        blk->owner_process_id = process_id;
        result = true;
    }
    mm_unlock(flags);
    return result;
}

size_t mm_get_process_usage(uint32_t process_id) {
    uint32_t flags;
    heap_block_t *blk;
    size_t used = 0U;

    if (!process_id) return 0U;
    flags = mm_lock();
    blk = heap_head;
    while (blk && mm_block_valid_locked(blk)) {
        if (!blk->free && blk->owner_process_id == process_id)
            used += blk->size;
        blk = blk->next;
    }
    mm_unlock(flags);
    return used;
}

void mm_get_process_usage_batch(const uint32_t *process_ids, size_t *usage,
                                uint32_t count) {
    uint32_t flags;
    heap_block_t *blk;

    if (!process_ids || !usage || !count) return;
    for (uint32_t i = 0U; i < count; i++) usage[i] = 0U;

    /* Un solo recorrido del heap para todos los procesos. Process Manager
     * antes recorria la lista completa una vez por fila, manteniendo el lock
     * de memoria durante miles de comparaciones y frenando a 3D Plus. */
    flags = mm_lock();
    blk = heap_head;
    while (blk && mm_block_valid_locked(blk)) {
        if (!blk->free && blk->owner_process_id) {
            for (uint32_t i = 0U; i < count; i++) {
                if (process_ids[i] == blk->owner_process_id) {
                    usage[i] += blk->size;
                    break;
                }
            }
        }
        blk = blk->next;
    }
    mm_unlock(flags);
}

size_t mm_release_process(uint32_t process_id) {
    uint32_t flags;
    heap_block_t *blk;
    size_t released = 0U;

    if (!process_id) return 0U;
    flags = mm_lock();
    for (blk = heap_head; blk && mm_block_valid_locked(blk); blk = blk->next) {
        if (blk->free || blk->owner_process_id != process_id) continue;
        released += blk->size;
        blk->free = true;
        blk->owner_process_id = 0U;
    }
    blk = heap_head;
    while (blk && mm_block_valid_locked(blk) && blk->next) {
        if (!mm_block_valid_locked(blk->next)) break;
        if (blk->free && blk->next->free) {
            heap_block_t *next = blk->next;
            blk->size += sizeof(heap_block_t) + next->size;
            blk->next = next->next;
            if (blk->next) blk->next->prev = blk;
            continue;
        }
        blk = blk->next;
    }
    mm_unlock(flags);
    return released;
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

static void mm_dump_locked(void) {
    heap_block_t *blk = heap_head;
    heap_block_t *tail[16];
    uint32_t count = 0U, tail_count = 0U, tail_next = 0U;
    size_t used = 0U, free_bytes = 0U, largest_free = 0U;

    kprintf("[MM] Resumen heap:\n");
    while (blk && count < 65536U) {
        uint32_t address = (uint32_t)(uintptr_t)blk;
        if (!mm_block_valid_locked(blk)) {
            uint32_t magic = 0U;
            uint32_t size = 0U;
            if (address >= heap_start_address &&
                address <= heap_end_address - sizeof(heap_block_t)) {
                magic = blk->magic;
                size = (uint32_t)blk->size;
            }
            kprintf("  CORRUPTO block=%x magic=%x size=%u tras=%u bloques\n",
                    address, magic, size, count);
            break;
        }
        if (blk->free) {
            free_bytes += blk->size;
            if (blk->size > largest_free) largest_free = blk->size;
        } else {
            used += blk->size;
        }
        tail[tail_next] = blk;
        tail_next = (tail_next + 1U) % 16U;
        if (tail_count < 16U) tail_count++;
        count++;
        blk = blk->next;
    }
    kprintf("  bloques=%u usados=%u libres=%u mayor_libre=%u\n",
            count, (uint32_t)used, (uint32_t)free_bytes,
            (uint32_t)largest_free);
    kprintf("  ultimos bloques:\n");
    for (uint32_t i = 0U; i < tail_count; i++) {
        uint32_t index = (tail_next + 16U - tail_count + i) % 16U;
        blk = tail[index];
        kprintf("  block %x: size=%u free=%u owner=%u magic=%x\n",
                (uint32_t)(uintptr_t)blk, (uint32_t)blk->size, blk->free,
                blk->owner_process_id, blk->magic);
    }
}

void mm_dump(void) {
    uint32_t flags = mm_lock();
    mm_dump_locked();
    mm_unlock(flags);
}
