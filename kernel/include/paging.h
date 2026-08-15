#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGING_PAGE_SIZE 4096U

bool paging_init(void);
bool paging_is_enabled(void);
uint32_t paging_fault_address(void);

/* Complete the boot-time user mapping once mm_init() knows the real heap. */
bool paging_configure_user_layout(uint32_t user_start, uint32_t user_end);

/* Page-aware validation used by the syscall/user-copy layer. */
bool paging_user_range_ok(const void *pointer, uint32_t length,
                          bool require_write);
bool paging_page_present(uint32_t address);
bool paging_page_user(uint32_t address);

/* Runtime permission changes used for real stack guard pages. All addresses
 * and lengths are rounded to 4 KiB pages. */
bool paging_set_range(uint32_t address, uint32_t length, bool present,
                      bool user, bool writable);

/* Shared page tables need a TLB generation handoff on every CPU. The timer
 * scheduler calls paging_sync_cpu() before inspecting task stacks. */
void paging_sync_cpu(void);
uint32_t paging_tlb_generation(void);

/* If a CPU faults on a page that is present in the current page tables but
 * still has an old non-present TLB entry, refresh CR3 and retry once. */
bool paging_recover_stale_fault(uint32_t address);

#endif
