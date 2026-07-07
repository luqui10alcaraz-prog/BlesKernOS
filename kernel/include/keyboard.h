#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

#define KEY_NONE        0x00
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0D
#define KEY_ESCAPE      0x1B
#define KEY_DELETE      0x7F

#define KEY_F1          0x80
#define KEY_F2          0x81
#define KEY_F3          0x82
#define KEY_F4          0x83
#define KEY_F5          0x84
#define KEY_F6          0x85
#define KEY_F7          0x86
#define KEY_F8          0x87
#define KEY_F9          0x88
#define KEY_F10         0x89
#define KEY_F11         0x8A
#define KEY_F12         0x8B
#define KEY_UP          0x90
#define KEY_DOWN        0x91
#define KEY_LEFT        0x92
#define KEY_RIGHT       0x93
#define KEY_HOME        0x94
#define KEY_END         0x95
#define KEY_PGUP        0x96
#define KEY_PGDN        0x97
#define KEY_SHIFT       0x98
#define KEY_CTRL        0x99
#define KEY_ALT         0x9A

typedef struct {
    uint8_t key;
    bool pressed;
} kbd_key_event_t;

typedef struct {
    bool shift;
    bool ctrl;
    bool alt;
    bool caps_lock;
    bool num_lock;
} kbd_modifiers_t;

void kbd_init(void);
char kbd_getchar(void);
bool kbd_haschar(void);
void kbd_get_modifiers(kbd_modifiers_t *mods);
bool kbd_has_event(void);
bool kbd_next_event(kbd_key_event_t *event);

#endif
