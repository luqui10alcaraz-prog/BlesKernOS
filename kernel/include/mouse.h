#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04
#define MOUSE_BUTTON_4      0x10
#define MOUSE_BUTTON_5      0x20

typedef struct {
    bool present;
    uint8_t device_id;
    uint8_t packet_size;
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint8_t buttons;
    uint32_t packets;
    uint32_t irq_count;
    uint32_t byte_count;
    uint8_t init_step;
    uint8_t last_error;
} mouse_state_t;

void mouse_init(void);
bool mouse_is_present(void);
void mouse_get_state(mouse_state_t *state);
void mouse_set_bounds(int32_t width, int32_t height);
void mouse_set_position(int32_t x, int32_t y);

#endif
