#include "include/gdt.h"
#include "include/memory.h"
#include "include/smp.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} PACKED gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} PACKED gdt_ptr_t;

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} PACKED tss_entry_t;

#define GDT_BASE_ENTRIES 7U
#define GDT_MAX_ENTRIES 128U
static gdt_entry_t gdt_per_cpu[SMP_MAX_CPUS][GDT_MAX_ENTRIES];
static bool gdt_user16_used[GDT_MAX_ENTRIES];
static gdt_ptr_t gdt_ptr_per_cpu[SMP_MAX_CPUS];
static tss_entry_t tss_per_cpu[SMP_MAX_CPUS];

extern void gdt_flush(const gdt_ptr_t *ptr);
extern void tss_flush(uint16_t selector);

static void gdt_set_in(gdt_entry_t *table, uint32_t index, uint32_t base,
                       uint32_t limit, uint8_t access, uint8_t flags) {
    table[index].base_low = (uint16_t)base;
    table[index].base_middle = (uint8_t)(base >> 16);
    table[index].base_high = (uint8_t)(base >> 24);
    table[index].limit_low = (uint16_t)limit;
    table[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    table[index].granularity |= flags & 0xF0;
    table[index].access = access;
}

static uint32_t gdt_local_cpu(void) {
    uint32_t cpu = smp_cpu_index();
    return cpu < SMP_MAX_CPUS ? cpu : 0U;
}

static void gdt_load_cpu(uint32_t cpu) {
    gdt_flush(&gdt_ptr_per_cpu[cpu]);
    tss_flush(GDT_TSS_SELECTOR(cpu));
}

uint32_t gdt_current_cpu_index(void) {
    uint16_t selector;
    uint32_t index;
    __asm__ volatile ("str %0" : "=r"(selector));
    index = (uint32_t)(selector >> 3U);
    if (index < GDT_TSS_BASE_INDEX ||
        index >= GDT_TSS_BASE_INDEX + SMP_MAX_CPUS) return SMP_MAX_CPUS;
    return index - GDT_TSS_BASE_INDEX;
}

void gdt_init(void) {
    gdt_entry_t *gdt = gdt_per_cpu[0];
    tss_entry_t *tss = &tss_per_cpu[0];
    uint32_t tss_base = (uint32_t)(uintptr_t)tss;

    kmemset(gdt_per_cpu, 0, sizeof(gdt_per_cpu));
    kmemset(tss_per_cpu, 0, sizeof(tss_per_cpu));
    kmemset(gdt_user16_used, 0, sizeof(gdt_user16_used));
    gdt_ptr_per_cpu[0].limit = sizeof(gdt_per_cpu[0]) - 1U;
    gdt_ptr_per_cpu[0].base = (uint32_t)(uintptr_t)gdt;

    gdt_set_in(gdt, 1, 0, 0xFFFFF, 0x9A, 0xC0);
    gdt_set_in(gdt, 2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_in(gdt, 3, 0, 0xFFFFF, 0xFA, 0xC0);
    gdt_set_in(gdt, 4, 0, 0xFFFFF, 0xF2, 0xC0);
    /* El descriptor historico #5 queda vacio; los TSS por CPU viven en un
     * rango reservado que tambien codifica el indice del procesador. */
    gdt_set_in(gdt, GDT_TSS_BASE_INDEX, tss_base,
               sizeof(*tss) - 1U, 0x89, 0x00);
    gdt_set_in(gdt, 6, 0, 0x0FFF, 0xF2, 0x40);

    tss->ss0 = GDT_KERNEL_DATA;
    tss->iomap_base = sizeof(*tss);
    gdt_load_cpu(0U);
}

void gdt_init_ap(uint32_t cpu_index) {
    gdt_entry_t *gdt;
    tss_entry_t *tss;
    if (cpu_index == 0U || cpu_index >= SMP_MAX_CPUS) return;
    gdt = gdt_per_cpu[cpu_index];
    tss = &tss_per_cpu[cpu_index];
    kmemcpy(gdt, gdt_per_cpu[0], sizeof(gdt_per_cpu[0]));
    kmemset(tss, 0, sizeof(*tss));
    tss->ss0 = GDT_KERNEL_DATA;
    tss->iomap_base = sizeof(*tss);
    gdt_set_in(gdt, GDT_TSS_BASE_INDEX + cpu_index,
               (uint32_t)(uintptr_t)tss, sizeof(*tss) - 1U, 0x89, 0x00);
    gdt_set_in(gdt, 6, 0U, 0x0FFFU, 0xF2U, 0x40U);
    gdt_ptr_per_cpu[cpu_index].limit = sizeof(gdt_per_cpu[0]) - 1U;
    gdt_ptr_per_cpu[cpu_index].base = (uint32_t)(uintptr_t)gdt;
    gdt_load_cpu(cpu_index);
}

void tss_set_kernel_stack(uint32_t stack_top) {
    tss_per_cpu[gdt_local_cpu()].esp0 = stack_top;
}

void gdt_set_user_fs_base(uint32_t base) {
    uint32_t cpu = gdt_local_cpu();
    gdt_set_in(gdt_per_cpu[cpu], 6, base, 0x0FFF, 0xF2, 0x40);
}

uint16_t gdt_alloc_user16_segment(uint32_t base, uint32_t limit,
                                  bool executable, bool writable) {
    uint8_t access;
    if (limit > 0xFFFFU) return 0;
    access = executable ? 0xFAU : (writable ? 0xF2U : 0xF0U);
    for (uint32_t i = GDT_BASE_ENTRIES; i < GDT_MAX_ENTRIES; i++) {
        if (i >= GDT_TSS_BASE_INDEX &&
            i < GDT_TSS_BASE_INDEX + SMP_MAX_CPUS) continue;
        if (gdt_user16_used[i]) continue;
        gdt_user16_used[i] = true;
        /* G=0 y D/B=0: limite en bytes y operandos/stack de 16 bits. */
        gdt_set_in(gdt_per_cpu[0], i, base, limit, access, 0x00U);
        return (uint16_t)((i << 3U) | 3U);
    }
    return 0;
}

void gdt_free_user16_segment(uint16_t selector) {
    uint32_t index = selector >> 3U;
    if (index < GDT_BASE_ENTRIES || index >= GDT_MAX_ENTRIES ||
        (index >= GDT_TSS_BASE_INDEX &&
         index < GDT_TSS_BASE_INDEX + SMP_MAX_CPUS)) return;
    kmemset(&gdt_per_cpu[0][index], 0, sizeof(gdt_per_cpu[0][index]));
    gdt_user16_used[index] = false;
}

bool gdt_update_user16_segment(uint16_t selector, uint32_t base,
                               uint32_t limit, bool executable,
                               bool writable) {
    uint32_t index = selector >> 3U;
    uint8_t access;
    if ((selector & 7U) != 3U || index < GDT_BASE_ENTRIES ||
        index >= GDT_MAX_ENTRIES ||
        (index >= GDT_TSS_BASE_INDEX &&
         index < GDT_TSS_BASE_INDEX + SMP_MAX_CPUS) ||
        !gdt_user16_used[index] ||
        limit > 0xFFFFU) return false;
    access = executable ? 0xFAU : (writable ? 0xF2U : 0xF0U);
    gdt_set_in(gdt_per_cpu[0], index, base, limit, access, 0x00U);
    return true;
}

bool gdt_query_user16_segment(uint16_t selector, uint32_t *base,
                              uint32_t *limit, bool *executable,
                              bool *writable) {
    uint32_t index = selector >> 3U;
    uint32_t descriptor_limit;
    if ((selector & 7U) != 3U || index < GDT_BASE_ENTRIES ||
        index >= GDT_MAX_ENTRIES ||
        (index >= GDT_TSS_BASE_INDEX &&
         index < GDT_TSS_BASE_INDEX + SMP_MAX_CPUS) ||
        !gdt_user16_used[index]) return false;
    if (base) *base = (uint32_t)gdt_per_cpu[0][index].base_low |
        ((uint32_t)gdt_per_cpu[0][index].base_middle << 16U) |
        ((uint32_t)gdt_per_cpu[0][index].base_high << 24U);
    descriptor_limit = (uint32_t)gdt_per_cpu[0][index].limit_low |
        ((uint32_t)(gdt_per_cpu[0][index].granularity & 0x0FU) << 16U);
    if (gdt_per_cpu[0][index].granularity & 0x80U)
        descriptor_limit = (descriptor_limit << 12U) | 0xFFFU;
    if (limit) *limit = descriptor_limit;
    if (executable) *executable = (gdt_per_cpu[0][index].access & 0x08U) != 0U;
    if (writable) *writable = (gdt_per_cpu[0][index].access & 0x02U) != 0U;
    return true;
}
