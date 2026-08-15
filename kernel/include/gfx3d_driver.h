#ifndef BK_GFX3D_DRIVER_H
#define BK_GFX3D_DRIVER_H

#include "gfx3d.h"

#define BK_GFX3D_DRIVER_ABI_VERSION 3U

typedef struct gfx3d_driver_ops {
    uint32_t abi_version;
    uint32_t descriptor_size;
    const char *name;
    uint32_t priority;
    uint32_t capabilities;

    bool (*probe)(gfx3d_info_t *info);
    void (*reset)(void);
    bool (*surface_create)(const gfx3d_surface_desc_t *desc,
                           gfx3d_surface_handle_t *handle_out);
    bool (*surface_destroy)(gfx3d_surface_handle_t handle);
    bool (*surface_upload)(gfx3d_surface_handle_t handle,
                           const uint32_t *pixels, uint32_t source_pitch,
                           const gfx_rect_t *rect);
    bool (*surface_download)(gfx3d_surface_handle_t handle,
                             uint32_t *pixels, uint32_t destination_pitch,
                             const gfx_rect_t *rect);
    bool (*surface_clear)(gfx3d_surface_handle_t handle, uint32_t color,
                          uint32_t *fence_out);
    bool (*surface_composite)(gfx3d_surface_handle_t source,
                              gfx3d_surface_handle_t destination,
                              const gfx3d_composite_t *operation,
                              uint32_t *fence_out);
    bool (*surface_present)(gfx3d_surface_handle_t handle,
                            const gfx_rect_t *rect, uint32_t *fence_out);
    bool (*begin)(gfx3d_surface_handle_t target, uint32_t clear_color,
                  float clear_depth, uint32_t flags);
    bool (*draw_triangles)(gfx3d_surface_handle_t target,
                           gfx3d_surface_handle_t texture,
                           const gfx3d_vertex_t *vertices,
                           uint32_t vertex_count, uint32_t flags,
                           uint32_t *fence_out);
    bool (*end)(gfx3d_surface_handle_t target, uint32_t *fence_out);
    bool (*wait_fence)(uint32_t fence);
    bool (*selftest)(uint32_t *fence_out);

    /* ABI v3 optional operations.  Drivers advertise the matching capability
     * bit only when the callback is implemented. */
    bool (*surface_upload_region)(gfx3d_surface_handle_t handle,
                                  const uint32_t *pixels,
                                  uint32_t source_pitch,
                                  uint32_t destination_x,
                                  uint32_t destination_y,
                                  uint32_t width, uint32_t height);
    bool (*depth_upload)(gfx3d_surface_handle_t target,
                         const uint16_t *depth, uint32_t source_pitch,
                         const gfx_rect_t *rect);
    bool (*depth_download)(gfx3d_surface_handle_t target,
                           uint16_t *depth, uint32_t destination_pitch,
                           const gfx_rect_t *rect);
} gfx3d_driver_ops_t;

bool gfx3d_register_driver(const gfx3d_driver_ops_t *ops);

#endif
