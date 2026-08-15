#ifndef SMP_WATCHDOG_H
#define SMP_WATCHDOG_H

#include "types.h"

void smp_watchdog_init(void);
void smp_watchdog_heartbeat(void);
void smp_watchdog_poll(void);
uint32_t smp_watchdog_stalled_mask(void);

#endif
