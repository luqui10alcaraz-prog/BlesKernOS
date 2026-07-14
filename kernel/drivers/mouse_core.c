#include "../include/mouse.h"
#include "../include/memory.h"

static const mouse_driver_ops_t *g_mouse_driver;
static uint8_t g_fallback_sensitivity = 3;

bool mouse_register_driver(const mouse_driver_ops_t *ops) {
    if (!ops || !ops->is_present || !ops->get_state || !ops->set_bounds ||
        !ops->set_position || !ops->set_sensitivity ||
        !ops->get_sensitivity) return false;
    g_mouse_driver = ops;
    return true;
}

void mouse_init(void) {}
bool mouse_is_present(void) {
    return g_mouse_driver && g_mouse_driver->is_present();
}
void mouse_get_state(mouse_state_t *state) {
    if (!state) return;
    if (g_mouse_driver) g_mouse_driver->get_state(state);
    else kmemset(state, 0, sizeof(*state));
}
void mouse_set_bounds(int32_t width, int32_t height) {
    if (g_mouse_driver) g_mouse_driver->set_bounds(width, height);
}
void mouse_set_position(int32_t x, int32_t y) {
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
