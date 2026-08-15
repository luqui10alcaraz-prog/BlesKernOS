#include "include/types.h"
#include "include/keyboard.h"
#include "include/pic.h"
#include "include/vga.h"
#include "include/idt.h"

static const char kbd_map_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static const char kbd_map_shifted[128] = {
    0, 27, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

#define KBD_BUF_SIZE 256
static char kbd_buffer[KBD_BUF_SIZE];
static uint32_t kbd_buf_head = 0;
static uint32_t kbd_buf_tail = 0;
static kbd_modifiers_t mods = {false, false, false, false, false};

static bool buf_full(void) {
    return ((kbd_buf_head + 1) % KBD_BUF_SIZE) == kbd_buf_tail;
}

static bool buf_empty(void) {
    return kbd_buf_head == kbd_buf_tail;
}

static void buf_push(char c) {
    if (!buf_full()) {
        kbd_buffer[kbd_buf_head] = c;
        kbd_buf_head = (kbd_buf_head + 1) % KBD_BUF_SIZE;
    }
}

static char buf_pop(void) {
    char c = kbd_buffer[kbd_buf_tail];
    kbd_buf_tail = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
    return c;
}

static void kbd_irq_handler(registers_t *regs UNUSED) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    bool key_released = (scancode & 0x80) != 0;
    uint8_t key = scancode & 0x7F;

    if (key_released) {
        switch (key) {
            case 0x2A:
            case 0x36: mods.shift = false; break;
            case 0x1D: mods.ctrl = false; break;
            case 0x38: mods.alt = false; break;
            case 0x3A: mods.caps_lock = !mods.caps_lock; break;
            default: break;
        }
        return;
    }

    if (key >= 128) return;

    switch (key) {
        case 0x2A:
        case 0x36: mods.shift = true; break;
        case 0x1D: mods.ctrl = true; break;
        case 0x38: mods.alt = true; break;
        case 0x3A: mods.caps_lock = !mods.caps_lock; break;
        default:
            if (mods.ctrl && key == 0x2E) {
                buf_push(KEY_ESCAPE);
            } else {
                bool use_shift = mods.shift;
                const char *map = use_shift ? kbd_map_shifted : kbd_map_normal;
                char c = map[key];
                if (c) buf_push(c);
            }
            break;
    }
}

void kbd_init(void) {
    irq_install_handler(1, kbd_irq_handler);
}

char kbd_getchar(void) {
    while (buf_empty()) {
        __asm__ volatile ("hlt");
    }
    return buf_pop();
}

bool kbd_haschar(void) {
    return !buf_empty();
}

void kbd_get_modifiers(kbd_modifiers_t *mods_out) {
    if (mods_out) *mods_out = mods;
}
