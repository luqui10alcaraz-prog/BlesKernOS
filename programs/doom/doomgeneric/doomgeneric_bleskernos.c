#include "../../doom/doom_port.h"
#include "../../../kernel/include/keyboard.h"
#include "../../../kernel/include/memory.h"
#include "../../../kernel/include/pit.h"
#include "../../../kernel/include/task.h"

#include "doomgeneric.h"

#define DOOM_KEY_RIGHTARROW 0xae
#define DOOM_KEY_LEFTARROW  0xac
#define DOOM_KEY_UPARROW    0xad
#define DOOM_KEY_DOWNARROW  0xaf
#define DOOM_KEY_USE        0xa2
#define DOOM_KEY_FIRE       0xa3
#define DOOM_KEY_ESCAPE     27
#define DOOM_KEY_ENTER      13
#define DOOM_KEY_TAB        9
#define DOOM_KEY_F1         (0x80 + 0x3b)
#define DOOM_KEY_F2         (0x80 + 0x3c)
#define DOOM_KEY_F3         (0x80 + 0x3d)
#define DOOM_KEY_F4         (0x80 + 0x3e)
#define DOOM_KEY_F5         (0x80 + 0x3f)
#define DOOM_KEY_F6         (0x80 + 0x40)
#define DOOM_KEY_F7         (0x80 + 0x41)
#define DOOM_KEY_F8         (0x80 + 0x42)
#define DOOM_KEY_F9         (0x80 + 0x43)
#define DOOM_KEY_F10        (0x80 + 0x44)
#define DOOM_KEY_F11        (0x80 + 0x57)
#define DOOM_KEY_F12        (0x80 + 0x58)
#define DOOM_KEY_BACKSPACE  0x7f
#define DOOM_KEY_RSHIFT     (0x80 + 0x36)
#define DOOM_KEY_RALT       (0x80 + 0x38)

#ifndef DOOM_TARGET_FPS
#define DOOM_TARGET_FPS     35U
#endif

typedef struct {
    gui_window_t *window;
    uint32_t *framebuffer;
    uint16_t width;
    uint16_t height;
    uint32_t start_tick;
    uint32_t next_frame_tick;
} doom_host_t;

static doom_host_t g_host;

void doom_host_attach(gui_window_t *window, uint32_t *framebuffer,
                      uint16_t width, uint16_t height) {
    kbd_key_event_t event;

    g_host.window = window;
    g_host.framebuffer = framebuffer;
    g_host.width = width;
    g_host.height = height;
    g_host.start_tick = pit_get_ticks();
    g_host.next_frame_tick = g_host.start_tick;

    while (kbd_next_event(&event)) {
    }
}

void doom_host_detach(void) {
    kmemset(&g_host, 0, sizeof(g_host));
}

static uint8_t doom_translate_key(uint8_t key) {
    switch (key) {
        case KEY_LEFT: return DOOM_KEY_LEFTARROW;
        case KEY_RIGHT: return DOOM_KEY_RIGHTARROW;
        case KEY_UP: return DOOM_KEY_UPARROW;
        case KEY_DOWN: return DOOM_KEY_DOWNARROW;
        case KEY_SHIFT: return DOOM_KEY_RSHIFT;
        case KEY_CTRL: return DOOM_KEY_FIRE;
        case KEY_ALT: return DOOM_KEY_RALT;
        case KEY_F1: return DOOM_KEY_F1;
        case KEY_F2: return DOOM_KEY_F2;
        case KEY_F3: return DOOM_KEY_F3;
        case KEY_F4: return DOOM_KEY_F4;
        case KEY_F5: return DOOM_KEY_F5;
        case KEY_F6: return DOOM_KEY_F6;
        case KEY_F7: return DOOM_KEY_F7;
        case KEY_F8: return DOOM_KEY_F8;
        case KEY_F9: return DOOM_KEY_F9;
        case KEY_F10: return DOOM_KEY_F10;
        case KEY_F11: return DOOM_KEY_F11;
        case KEY_F12: return DOOM_KEY_F12;
        case ' ': return DOOM_KEY_USE;
        case '\b': return DOOM_KEY_BACKSPACE;
        case '\n': return DOOM_KEY_ENTER;
        case '\t': return DOOM_KEY_TAB;
        case 0x1B: return DOOM_KEY_ESCAPE;
        default: return key;
    }
}

void DG_Init() {
}

static void doom_limit_frame_rate(void) {
    uint32_t hz;
    uint32_t frame_ticks;
    uint32_t now;

    if (DOOM_TARGET_FPS == 0) return;

    hz = pit_get_frequency_hz();
    if (hz == 0) hz = 100;

    frame_ticks = (hz + DOOM_TARGET_FPS - 1U) / DOOM_TARGET_FPS;
    if (frame_ticks == 0) frame_ticks = 1;

    now = pit_get_ticks();

    if (g_host.next_frame_tick == 0) {
        g_host.next_frame_tick = now + frame_ticks;
        return;
    }

    /*
     * Hard cap:
     * Do not trust task_sleep() alone. Some task/scheduler setups may return
     * early or use a different unit. This loop waits until the PIT tick really
     * reaches the next frame deadline.
     */
    while ((int32_t)(pit_get_ticks() - g_host.next_frame_tick) < 0) {
        task_sleep(1);
    }

    now = pit_get_ticks();

    if ((int32_t)(now - (g_host.next_frame_tick + frame_ticks * 4U)) > 0) {
        /*
         * If the game was paused/stalled for too long, resync instead of trying
         * to catch up with many delayed frames.
         */
        g_host.next_frame_tick = now + frame_ticks;
    } else {
        g_host.next_frame_tick += frame_ticks;
    }
}

void DG_DrawFrame() {
    uint32_t pixels;

    if (!g_host.framebuffer || !DG_ScreenBuffer) return;

    doom_limit_frame_rate();

    pixels = (uint32_t)g_host.width * (uint32_t)g_host.height;
    kmemcpy(g_host.framebuffer, DG_ScreenBuffer, pixels * sizeof(uint32_t));
    if (g_host.window) g_host.window->dirty = true;
}

void DG_SleepMs(uint32_t ms) {
    uint32_t hz;
    uint32_t ticks;

    if (ms == 0) return;
    hz = pit_get_frequency_hz();
    if (hz == 0) hz = 100;
    ticks = (ms * hz + 999U) / 1000U;
    if (ticks == 0) ticks = 1;
    task_sleep(ticks);
}

uint32_t DG_GetTicksMs() {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t now = pit_get_ticks();

    if (hz == 0) hz = 100;
    return ((now - g_host.start_tick) * 1000U) / hz;
}

int DG_GetKey(int *pressed, unsigned char *key) {
    kbd_key_event_t event;

    if (!pressed || !key) return 0;
    if (!g_host.window || !g_host.window->focused) {
        while (kbd_next_event(&event)) {
        }
        return 0;
    }

    while (kbd_next_event(&event)) {
        *pressed = event.pressed ? 1 : 0;
        *key = doom_translate_key(event.key);
        if (*key != 0) return 1;
    }

    return 0;
}

void DG_SetWindowTitle(const char *title) {
    if (!g_host.window || !title) return;
    kstrncpy(g_host.window->title, title, sizeof(g_host.window->title) - 1);
    g_host.window->title[sizeof(g_host.window->title) - 1] = '\0';
    g_host.window->dirty = true;
}
