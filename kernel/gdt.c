#include "include/gdt.h"
#include "include/memory.h"

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

static gdt_entry_t gdt[6];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss;

extern void gdt_flush(const gdt_ptr_t *ptr);
extern void tss_flush(void);

static void gdt_set(uint32_t index, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags) {
    gdt[index].base_low = (uint16_t)base;
    gdt[index].base_middle = (uint8_t)(base >> 16);
    gdt[index].base_high = (uint8_t)(base >> 24);
    gdt[index].limit_low = (uint16_t)limit;
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= flags & 0xF0;
    gdt[index].access = access;
}

void gdt_init(void) {
    uint32_t tss_base = (uint32_t)(uintptr_t)&tss;

    kmemset(gdt, 0, sizeof(gdt));
    kmemset(&tss, 0, sizeof(tss));
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)(uintptr_t)gdt;

    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);
    gdt_set(5, tss_base, sizeof(tss) - 1, 0x89, 0x00);

    tss.ss0 = GDT_KERNEL_DATA;
    tss.iomap_base = sizeof(tss);
    gdt_flush(&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint32_t stack_top) {
    tss.esp0 = stack_top;
}
