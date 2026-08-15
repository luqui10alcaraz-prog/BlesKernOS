#include "../../include/mouse.h"
#include "../../include/memory.h"
#include "../../include/task.h"

static const mouse_driver_ops_t *g_mouse_driver;
static uint8_t g_fallback_sensitivity = 3;
static mouse_state_t g_injected_mouse;
static bool g_injected_active;
static int32_t g_bound_width = 800;
static int32_t g_bound_height = 600;

static int32_t mouse_clamp(int32_t value, int32_t limit) {
    if (value < 0) return 0;
    if (limit > 0 && value >= limit) return limit - 1;
    return value;
}

bool mouse_register_driver(const mouse_driver_ops_t *ops) {
    if (!ops || !ops->is_present || !ops->get_state || !ops->set_bounds ||
        !ops->set_position || !ops->set_sensitivity ||
        !ops->get_sensitivity) return false;
    g_mouse_driver = ops;
    return true;
}

void mouse_init(void) {}

bool mouse_is_present(void) {
    return g_injected_active || (g_mouse_driver && g_mouse_driver->is_present());
}

void mouse_get_state(mouse_state_t *state) {
    if (!state) return;
    task_preempt_disable();
    if (g_injected_active) *state = g_injected_mouse;
    else if (g_mouse_driver) g_mouse_driver->get_state(state);
    else kmemset(state, 0, sizeof(*state));
    task_preempt_enable();
}

void mouse_set_bounds(int32_t width, int32_t height) {
    if (width > 0) g_bound_width = width;
    if (height > 0) g_bound_height = height;
    if (g_injected_active) {
        g_injected_mouse.x = mouse_clamp(g_injected_mouse.x, g_bound_width);
        g_injected_mouse.y = mouse_clamp(g_injected_mouse.y, g_bound_height);
    }
    if (g_mouse_driver) g_mouse_driver->set_bounds(width, height);
}

void mouse_set_position(int32_t x, int32_t y) {
    if (g_injected_active) {
        g_injected_mouse.x = mouse_clamp(x, g_bound_width);
        g_injected_mouse.y = mouse_clamp(y, g_bound_height);
    }
    if (g_mouse_driver) g_mouse_driver->set_position(x, y);
}

void mouse_set_sensitivity(uint8_t sensitivity) {
    if (sensitivity < 1) sensitivity = 1;
    if (sensitivity > 5) sensitivity = 5;
    g_fallback_sensitivity = sensitivity;
    if (g_mouse_driver) g_mouse_driver->set_sensitivity(sensitivity);
}

uint8_t mouse_get_sensitivity(void) {
    return g_mouse_driver ? g_mouse_driver->get_sensitivity()
                          : g_fallback_sensitivity;
}

void mouse_inject_relative(int32_t dx, int32_t dy, int32_t wheel,
                           uint8_t buttons) {
    uint8_t sensitivity = mouse_get_sensitivity();
    mouse_state_t base;
    bool need_base = !g_injected_active;
    kmemset(&base, 0, sizeof(base));
    if (need_base) {
        if (g_mouse_driver && g_mouse_driver->is_present())
            g_mouse_driver->get_state(&base);
        else {
            base.x = g_bound_width / 2;
            base.y = g_bound_height / 2;
        }
    }
    if (sensitivity > 3U) {
        dx = dx * (int32_t)(sensitivity - 1U) / 2;
        dy = dy * (int32_t)(sensitivity - 1U) / 2;
    } else if (sensitivity < 3U) {
        dx /= (int32_t)(4U - sensitivity);
        dy /= (int32_t)(4U - sensitivity);
    }
    task_preempt_disable();
    if (!g_injected_active) {
        g_injected_mouse = base;
        g_injected_mouse.device_id = 0xFFU;
        g_injected_mouse.packet_size = 4U;
        g_injected_mouse.present = true;
        g_injected_active = true;
    }
    g_injected_mouse.dx = dx;
    g_injected_mouse.dy = dy;
    g_injected_mouse.x = mouse_clamp(g_injected_mouse.x + dx, g_bound_width);
    g_injected_mouse.y = mouse_clamp(g_injected_mouse.y + dy, g_bound_height);
    g_injected_mouse.wheel += wheel;
    g_injected_mouse.buttons = buttons;
    g_injected_mouse.packets++;
    task_preempt_enable();
}

void mouse_inject_disconnect(void) {
    task_preempt_disable();
    g_injected_active = false;
    kmemset(&g_injected_mouse, 0, sizeof(g_injected_mouse));
    task_preempt_enable();
}
