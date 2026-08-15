#ifndef PIT_H
#define PIT_H

#include "types.h"

void pit_init(void);
uint32_t pit_get_ticks(void);
uint32_t pit_get_uptime_seconds(void);
uint32_t pit_get_frequency_hz(void);

#endif
