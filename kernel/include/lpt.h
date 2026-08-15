#ifndef BK_LPT_H
#define BK_LPT_H

#include "types.h"

#define LPT_MAX_PORTS 3U
#define LPT_MAX_VIRTUAL_PROVIDERS 4U

typedef struct {
    char name[16];
    uint16_t base;
    uint8_t raw_status;
    bool present;
    bool busy;
    bool selected;
    bool paper_out;
    bool error;
    bool acknowledged;
    bool virtual_port;
} lpt_port_info_t;

typedef struct {
    uint32_t (*count)(void);
    bool (*info)(uint32_t index, lpt_port_info_t *info);
    int32_t (*write)(uint32_t index, const void *data, uint32_t length,
                     uint32_t idle_timeout_ms);
} lpt_virtual_provider_t;

void lpt_init(void);
bool lpt_register_virtual_provider(const lpt_virtual_provider_t *provider);
bool lpt_unregister_virtual_provider(const lpt_virtual_provider_t *provider);
uint32_t lpt_port_count(void);
bool lpt_port_info(uint32_t index, lpt_port_info_t *info);
int32_t lpt_write(uint32_t index, const void *data, uint32_t length,
                  uint32_t idle_timeout_ms);

#endif
