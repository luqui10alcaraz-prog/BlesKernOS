#ifndef GDT_H
#define GDT_H

#include "types.h"

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
/* Cada CPU carga un selector TSS distinto. Ademas de evitar el bit busy
 * compartido, el selector permite identificar el CPU actual con STR sin una
 * lectura MMIO del LAPIC en cada lock, malloc o cambio de tarea. */
#define GDT_TSS_BASE_INDEX 96U
#define GDT_TSS_SELECTOR(cpu) ((uint16_t)((GDT_TSS_BASE_INDEX + (cpu)) << 3U))
#define GDT_TSS         GDT_TSS_SELECTOR(0U)
#define GDT_USER_FS     0x33

void gdt_init(void);
void gdt_init_ap(uint32_t cpu_index);
void tss_set_kernel_stack(uint32_t stack_top);
void gdt_set_user_fs_base(uint32_t base);
uint32_t gdt_current_cpu_index(void);
uint16_t gdt_alloc_user16_segment(uint32_t base, uint32_t limit,
                                  bool executable, bool writable);
void gdt_free_user16_segment(uint16_t selector);
bool gdt_update_user16_segment(uint16_t selector, uint32_t base,
                               uint32_t limit, bool executable,
                               bool writable);
bool gdt_query_user16_segment(uint16_t selector, uint32_t *base,
                              uint32_t *limit, bool *executable,
                              bool *writable);

#endif
