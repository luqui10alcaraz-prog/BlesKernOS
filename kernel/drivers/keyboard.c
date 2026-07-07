#include "../include/types.h"
#include "../include/keyboard.h"
#include "../include/pic.h"
#include "../include/vga.h"
#include "../include/idt.h"

#define KBD_BUF_SIZE 256
static char kbd_buffer[KBD_BUF_SIZE];
static volatile uint32_t kbd_buf_head = 0;
static volatile uint32_t kbd_buf_tail = 0;
static kbd_key_event_t kbd_events[KBD_BUF_SIZE];
static volatile uint32_t kbd_evt_head = 0;
static volatile uint32_t kbd_evt_tail = 0;
static volatile bool kbd_extended = false;
static volatile bool kbd_break = false;
static volatile kbd_modifiers_t mods = {false, false, false, false, false};

static bool buf_full(void) {
    return ((kbd_buf_head + 1) % KBD_BUF_SIZE) == kbd_buf_tail;
}

static bool buf_empty(void) {
    return kbd_buf_head == kbd_buf_tail;
}

static bool evt_full(void) {
    return ((kbd_evt_head + 1) % KBD_BUF_SIZE) == kbd_evt_tail;
}

static bool evt_empty(void) {
    return kbd_evt_head == kbd_evt_tail;
}

static void buf_push(char c) {
    cli();
    if (!buf_full()) {
        kbd_buffer[kbd_buf_head] = c;
        kbd_buf_head = (kbd_buf_head + 1) % KBD_BUF_SIZE;
    }
    sti();
}

static char buf_pop(void) {
    cli();
    if (buf_empty()) {
        sti();
        return 0;
    }
    char c = kbd_buffer[kbd_buf_tail];
    kbd_buf_tail = (kbd_buf_tail + 1) % KBD_BUF_SIZE;
    sti();
    return c;
}

static void evt_push(uint8_t key, bool pressed) {
    cli();
    if (!evt_full()) {
        kbd_events[kbd_evt_head].key = key;
        kbd_events[kbd_evt_head].pressed = pressed;
        kbd_evt_head = (kbd_evt_head + 1) % KBD_BUF_SIZE;
    }
    sti();
}

static bool evt_pop(kbd_key_event_t *event) {
    cli();
    if (evt_empty()) {
        sti();
        return false;
    }
    *event = kbd_events[kbd_evt_tail];
    kbd_evt_tail = (kbd_evt_tail + 1) % KBD_BUF_SIZE;
    sti();
    return true;
}

static void kbd_wait_input(void) {
    while (inb(KBD_STATUS_PORT) & 0x02) {
        __asm__ volatile ("pause");
    }
}

static void kbd_wait_output(void) {
    while (!(inb(KBD_STATUS_PORT) & 0x01)) {
        __asm__ volatile ("pause");
    }
}

static void kbd_flush(void) {
    while (inb(KBD_STATUS_PORT) & 0x01) {
        (void)inb(KBD_DATA_PORT);
    }
}

static void kbd_send_cmd(uint8_t cmd) {
    kbd_wait_input();
    outb(KBD_DATA_PORT, cmd);
    kbd_wait_output();
    (void)inb(KBD_DATA_PORT);
}

static char kbd_map_scancode(uint8_t scancode, bool shifted, bool caps_lock) {
    char c = 0;

    switch (scancode) {
        case 0x01: c = 0x1B; break; /* Esc */
        case 0x02: c = shifted ? '!' : '1'; break;
        case 0x03: c = shifted ? '@' : '2'; break;
        case 0x04: c = shifted ? '#' : '3'; break;
        case 0x05: c = shifted ? '$' : '4'; break;
        case 0x06: c = shifted ? '%' : '5'; break;
        case 0x07: c = shifted ? '^' : '6'; break;
        case 0x08: c = shifted ? '&' : '7'; break;
        case 0x09: c = shifted ? '*' : '8'; break;
        case 0x0A: c = shifted ? '(' : '9'; break;
        case 0x0B: c = shifted ? ')' : '0'; break;
        case 0x0C: c = shifted ? '_' : '-'; break;
        case 0x0D: c = shifted ? '+' : '='; break;
        case 0x0E: c = '\b'; break;
        case 0x0F: c = '\t'; break;
        case 0x10: c = 'q'; break;
        case 0x11: c = 'w'; break;
        case 0x12: c = 'e'; break;
        case 0x13: c = 'r'; break;
        case 0x14: c = 't'; break;
        case 0x15: c = 'y'; break;
        case 0x16: c = 'u'; break;
        case 0x17: c = 'i'; break;
        case 0x18: c = 'o'; break;
        case 0x19: c = 'p'; break;
        case 0x1A: c = '['; break;
        case 0x1B: c = ']'; break;
        case 0x1C: c = '\n'; break;
        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;
        case 0x27: c = ';'; break;
        case 0x28: c = '\''; break;
        case 0x29: c = '`'; break;
        case 0x2B: c = '\\'; break;
        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;
        case 0x33: c = ','; break;
        case 0x34: c = '.'; break;
        case 0x35: c = '/'; break;
        case 0x39: c = ' '; break;
        default: return 0;
    }

    if (c >= 'a' && c <= 'z') {
        if ((caps_lock && !shifted) || (!caps_lock && shifted)) {
            c = (char)('A' + (c - 'a'));
        }
    } else if (c == '`') {
        c = shifted ? '~' : '`';
    } else if (c == '[') {
        c = shifted ? '{' : '[';
    } else if (c == ']') {
        c = shifted ? '}' : ']';
    } else if (c == '\\') {
        c = shifted ? '|' : '\\';
    } else if (c == ';') {
        c = shifted ? ':' : ';';
    } else if (c == '\'') {
        c = shifted ? '"' : '\'';
    } else if (c == ',') {
        c = shifted ? '<' : ',';
    } else if (c == '.') {
        c = shifted ? '>' : '.';
    } else if (c == '/') {
        c = shifted ? '?' : '/';
    }

    return c;
}

static void kbd_push_key_event(uint8_t scancode, bool pressed, bool extended) {
    uint8_t key = 0;

    if (extended) {
        switch (scancode) {
            case 0x48:
            case 0x75: key = KEY_UP; break;
            case 0x50:
            case 0x72: key = KEY_DOWN; break;
            case 0x4B:
            case 0x6B: key = KEY_LEFT; break;
            case 0x4D:
            case 0x74: key = KEY_RIGHT; break;
            default: break;
        }
    } else {
        switch (scancode) {
            case 0x2A:
            case 0x36: key = KEY_SHIFT; break;
            case 0x1D: key = KEY_CTRL; break;
            case 0x38: key = KEY_ALT; break;
            case 0x3B: key = KEY_F1; break;
            case 0x3C: key = KEY_F2; break;
            case 0x3D: key = KEY_F3; break;
            case 0x3E: key = KEY_F4; break;
            case 0x3F: key = KEY_F5; break;
            case 0x40: key = KEY_F6; break;
            case 0x41: key = KEY_F7; break;
            case 0x42: key = KEY_F8; break;
            case 0x43: key = KEY_F9; break;
            case 0x44: key = KEY_F10; break;
            case 0x57: key = KEY_F11; break;
            case 0x58: key = KEY_F12; break;
            default: {
                char c = kbd_map_scancode(scancode, false, false);
                if (c != 0) key = (uint8_t)c;
                break;
            }
        }
    }

    if (key != 0) evt_push(key, pressed);
}

static void kbd_irq_handler(registers_t *regs UNUSED) {
    uint8_t status = inb(KBD_STATUS_PORT);
    uint8_t scancode;

    if ((status & 0x01) == 0) return;
    if (status & 0x20) return;

    scancode = inb(KBD_DATA_PORT);

    if (scancode == 0xE0) {
        kbd_extended = true;
        return;
    }

    if (scancode == 0xF0) {
        kbd_break = true;
        return;
    }

    bool released = kbd_break;
    kbd_break = false;

    if (scancode == 0xE1) {
        return;
    }

    /* El controlador PS/2 de PC normalmente traduce a Set 1, donde el bit
     * alto marca key-up (por ejemplo Ctrl: 0x1D -> 0x9D). También conservamos
     * el prefijo 0xF0 de Set 2 para teclados/controladores sin traducción. */
    if (scancode & 0x80) {
        released = true;
        scancode &= 0x7F;
    }

    if (released) {
        kbd_push_key_event(scancode, false, kbd_extended);
        switch (scancode) {
            case 0x2A:
            case 0x36: mods.shift = false; break;
            case 0x1D: mods.ctrl = false; break;
            case 0x38: mods.alt = false; break;
            case 0x3A: break;
            default: break;
        }
        kbd_extended = false;
        return;
    }

    switch (scancode) {
        case 0x2A:
        case 0x36: mods.shift = true; break;
        case 0x1D: mods.ctrl = true; break;
        case 0x38: mods.alt = true; break;
        case 0x3A:
            mods.caps_lock = !mods.caps_lock;
            break;
        case 0x48:
        case 0x75:
            if (kbd_extended) {
                buf_push(KEY_UP);
                evt_push(KEY_UP, true);
            }
            kbd_extended = false;
            return;
        case 0x50:
        case 0x72:
            if (kbd_extended) {
                buf_push(KEY_DOWN);
                evt_push(KEY_DOWN, true);
            }
            kbd_extended = false;
            return;
        case 0x4B:
        case 0x6B:
            if (kbd_extended) {
                buf_push(KEY_LEFT);
                evt_push(KEY_LEFT, true);
            }
            kbd_extended = false;
            return;
        case 0x4D:
        case 0x74:
            if (kbd_extended) {
                buf_push(KEY_RIGHT);
                evt_push(KEY_RIGHT, true);
            }
            kbd_extended = false;
            return;
        default:
            if (scancode >= 0x80) {
                kbd_extended = false;
                return;
            }
            break;
    }

    if (scancode < 0x80) {
        char c = kbd_map_scancode(scancode, mods.shift, mods.caps_lock);
        kbd_push_key_event(scancode, true, false);
        if (c != 0) {
            buf_push(c);
        }
    }

    kbd_extended = false;
}

void kbd_init(void) {
    kbd_flush();
    kbd_send_cmd(0xF4);
    irq_install_handler(1, kbd_irq_handler);
}

char kbd_getchar(void) {
    while (true) {
        cli();
        if (!buf_empty()) {
            char c = buf_pop();
            sti();
            return c;
        }
        sti();
        __asm__ volatile ("hlt");
    }
}

bool kbd_haschar(void) {
    return !buf_empty();
}

void kbd_get_modifiers(kbd_modifiers_t *mods_out) {
    if (mods_out) {
        *mods_out = mods;
    }
}

bool kbd_has_event(void) {
    return !evt_empty();
}

bool kbd_next_event(kbd_key_event_t *event) {
    if (!event) return false;
    return evt_pop(event);
}
