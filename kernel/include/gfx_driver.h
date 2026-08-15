#ifndef GFX_DRIVER_H
#define GFX_DRIVER_H

#include "gfx.h"

#define BK_GFX_DRIVER_ABI_VERSION 2U

typedef struct {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;
    uint32_t priority;
    uint32_t capabilities;

    bool (*activate)(gfx_info_t *info, uint16_t preferred_width,
                     uint16_t preferred_height);
    uint32_t (*get_capabilities)(void);
    void (*disable)(void);
    bool (*list_modes)(gfx_display_mode_t *modes, uint32_t max_modes,
                       uint32_t *count);
    bool (*set_mode)(gfx_info_t *info, uint16_t width, uint16_t height,
                     uint8_t bpp);

    bool (*present_buffer)(const gfx_info_t *info, const uint32_t *pixels,
                           uint32_t source_pitch, const gfx_rect_t *rects,
                           uint32_t rect_count, uint32_t *fence_out);
    bool (*update_rect)(int x, int y, int w, int h);
    bool (*flush)(uint32_t *fence_out);
    bool (*wait_fence)(uint32_t fence);

    bool (*fill_rect)(const gfx_info_t *info, int x, int y, int w, int h,
                      uint32_t rgb, uint32_t *fence_out);
    bool (*bitblt)(const gfx_info_t *info, int src_x, int src_y,
                   int dst_x, int dst_y, int w, int h, gfx_rop_t rop,
                   uint32_t *fence_out);

    bool (*cursor_define)(const uint32_t *argb, uint16_t width,
                          uint16_t height, uint16_t hot_x, uint16_t hot_y);
    bool (*cursor_move)(int x, int y);
    bool (*cursor_show)(bool visible);

    bool (*surface_create)(uint16_t width, uint16_t height,
                           gfx_surface_handle_t *handle_out);
    bool (*surface_destroy)(gfx_surface_handle_t handle);
    bool (*surface_upload)(gfx_surface_handle_t handle,
                           const uint32_t *pixels, uint32_t source_pitch,
                           const gfx_rect_t *rect);
    bool (*surface_blit)(const gfx_info_t *info,
                         gfx_surface_handle_t handle,
                         int src_x, int src_y, int dst_x, int dst_y,
                         int w, int h, uint32_t *fence_out);

    bool (*overlay_put)(const gfx_info_t *info, const void *pixels,
                        uint32_t source_pitch, uint16_t source_width,
                        uint16_t source_height, gfx_overlay_format_t format,
                        int dst_x, int dst_y, int dst_w, int dst_h);
    bool (*overlay_stop)(void);
} gfx_driver_ops_t;

bool gfx_register_driver(const gfx_driver_ops_t *ops);

#endif
