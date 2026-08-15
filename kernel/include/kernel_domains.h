#ifndef KERNEL_DOMAINS_H
#define KERNEL_DOMAINS_H

#include "types.h"

/* Fixed acquisition order. Never reorder these bits without updating the
 * documentation: ordered multi-domain acquisition is the deadlock rule. */
typedef enum {
    KDOMAIN_TASK   = 1U << 0,
    KDOMAIN_VFS    = 1U << 1,
    KDOMAIN_GUI    = 1U << 2,
    KDOMAIN_GFX    = 1U << 3,
    KDOMAIN_NET    = 1U << 4,
    KDOMAIN_AUDIO  = 1U << 5,
    KDOMAIN_DRIVER = 1U << 6,
    KDOMAIN_WINE   = 1U << 7,
    KDOMAIN_LEGACY = 1U << 8
} kernel_domain_mask_t;

#define KDOMAIN_COUNT 9U

typedef struct {
    uint16_t depth[KDOMAIN_COUNT];
} kernel_domain_snapshot_t;

void kernel_domains_init(void);
uint32_t kernel_domain_mask_for_api(const char *name);
void kernel_domains_enter(uint32_t mask);
void kernel_domains_exit(uint32_t mask);
void kernel_domains_drop_current(kernel_domain_snapshot_t *snapshot);
void kernel_domains_restore(const kernel_domain_snapshot_t *snapshot);
void kernel_domains_abandon_owner(uint32_t owner);
void kernel_domains_abandon_current(void);
const char *kernel_domain_name(uint32_t index);
uint32_t kernel_domains_held_mask(void);
uint32_t kernel_domains_order_violations(void);
void kernel_domains_dump_current(void);

#endif
