#include "../include/vesa.h"

static const vesa_driver_ops_t *g_vesa_driver;

bool vesa_register_driver(const vesa_driver_ops_t *ops) {
    if (!ops || !ops->init_from_bootinfo || !ops->attach_lfb ||
        !ops->has_lfb || !ops->can_change_mode || !ops->list_modes ||
        !ops->list_all_modes || !ops->set_mode || !ops->clear_rgb ||
        !ops->enable_page_flip || !ops->page_flip_draw_buffer ||
        !ops->page_flip_commit ||
        !ops->putpixel_rgb || !ops->getpixel_rgb || !ops->fill_rect_rgb)
        return false;
    g_vesa_driver = ops;
    return true;
}

bool vesa_init_from_bootinfo(gfx_info_t *info) {
    return g_vesa_driver && g_vesa_driver->init_from_bootinfo(info);
}
bool vesa_attach_lfb(gfx_info_t *info, uint32_t framebuffer,
                     uint16_t width, uint16_t height, uint16_t pitch,
                     uint8_t bpp) {
    return g_vesa_driver && g_vesa_driver->attach_lfb(
        info, framebuffer, width, height, pitch, bpp);
}
bool vesa_has_lfb(void) {
    return g_vesa_driver && g_vesa_driver->has_lfb();
}
bool vesa_can_change_mode(void) {
    return g_vesa_driver && g_vesa_driver->can_change_mode();
}
bool vesa_list_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                     uint32_t *count, uint8_t preferred_bpp) {
    if (!g_vesa_driver) {
        if (count) *count = 0;
        return false;
    }
    return g_vesa_driver->list_modes(modes, max_modes, count, preferred_bpp);
}
bool vesa_list_all_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                         uint32_t *count) {
    if (!g_vesa_driver) {
        if (count) *count = 0;
        return false;
    }
    return g_vesa_driver->list_all_modes(modes, max_modes, count);
}
bool vesa_set_mode(gfx_info_t *info, uint16_t width, uint16_t height,
                   uint8_t bpp) {
    return g_vesa_driver && g_vesa_driver->set_mode(info, width, height, bpp);
}
bool vesa_enable_page_flip(const gfx_info_t *info) {
    return g_vesa_driver && g_vesa_driver->enable_page_flip(info);
}
uint32_t vesa_page_flip_draw_buffer(const gfx_info_t *info) {
    return g_vesa_driver ? g_vesa_driver->page_flip_draw_buffer(info) : 0U;
}
bool vesa_page_flip_commit(const gfx_info_t *info) {
    return g_vesa_driver && g_vesa_driver->page_flip_commit(info);
}
void vesa_clear_rgb(const gfx_info_t *info, uint32_t rgb) {
    if (g_vesa_driver) g_vesa_driver->clear_rgb(info, rgb);
}
void vesa_putpixel_rgb(const gfx_info_t *info, int x, int y, uint32_t rgb) {
    if (g_vesa_driver) g_vesa_driver->putpixel_rgb(info, x, y, rgb);
}
uint32_t vesa_getpixel_rgb(const gfx_info_t *info, int x, int y) {
    return g_vesa_driver ? g_vesa_driver->getpixel_rgb(info, x, y) : 0;
}
void vesa_fill_rect_rgb(const gfx_info_t *info, int x, int y,
                        int w, int h, uint32_t rgb) {
    if (g_vesa_driver) g_vesa_driver->fill_rect_rgb(info, x, y, w, h, rgb);
}
