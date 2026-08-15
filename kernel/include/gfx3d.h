#ifndef BK_GFX3D_H
#define BK_GFX3D_H

#include "types.h"
#include "gfx.h"

#define GFX3D_SURFACE_INVALID 0U

typedef uint32_t gfx3d_surface_handle_t;

/* Capabilities published by the active 3D extension. */
#define GFX3D_CAP_FIXED_FUNCTION  (1U << 0)
#define GFX3D_CAP_RENDER_TARGETS  (1U << 1)
#define GFX3D_CAP_VERTEX_BUFFERS  (1U << 2)
#define GFX3D_CAP_SURFACE_DMA     (1U << 3)
#define GFX3D_CAP_PRESENT         (1U << 4)
#define GFX3D_CAP_ALPHA_BLEND     (1U << 5)
#define GFX3D_CAP_TEXTURES        (1U << 6)
#define GFX3D_CAP_SCALE           (1U << 7)
#define GFX3D_CAP_TRANSFORM       (1U << 8)
#define GFX3D_CAP_WINDOW_SURFACES (1U << 9)
#define GFX3D_CAP_GLYPH_ATLAS     (1U << 10)
#define GFX3D_CAP_TINYGL          (1U << 11)
#define GFX3D_CAP_DEPTH_BUFFER          (1U << 12)
#define GFX3D_CAP_DEPTH_SURFACE_IO      (1U << 13)
#define GFX3D_CAP_DEPTH_FUNCS           (1U << 14)
#define GFX3D_CAP_BLEND_ADDITIVE        (1U << 15)
#define GFX3D_CAP_TEXTURE_REGION_UPLOAD (1U << 16)

typedef enum {
    GFX3D_FORMAT_XRGB8888 = 1,
    GFX3D_FORMAT_ARGB8888 = 2,
    GFX3D_FORMAT_Z16 = 3,
} gfx3d_format_t;

#define GFX3D_SURFACE_RENDER_TARGET (1U << 0)
#define GFX3D_SURFACE_TEXTURE       (1U << 1)
#define GFX3D_SURFACE_DYNAMIC       (1U << 2)
#define GFX3D_SURFACE_WINDOW        (1U << 3)
#define GFX3D_SURFACE_TINYGL        (1U << 4)
#define GFX3D_SURFACE_DEPTH         (1U << 5)

typedef struct {
    uint16_t width;
    uint16_t height;
    gfx3d_format_t format;
    uint32_t flags;
} gfx3d_surface_desc_t;

typedef enum {
    GFX3D_FILTER_NEAREST = 0,
    GFX3D_FILTER_LINEAR = 1,
} gfx3d_filter_t;

/* Affine 2D matrix: [m00 m01 tx; m10 m11 ty; 0 0 1]. */
typedef struct {
    float m00, m01, tx;
    float m10, m11, ty;
} gfx3d_transform2d_t;

typedef struct {
    gfx_rect_t source;
    gfx3d_transform2d_t transform;
    gfx_rect_t clip;
    uint8_t opacity;
    gfx3d_filter_t filter;
    bool source_premultiplied;
    /* Multiplicative ARGB. Zero means opaque white for compatibility. */
    uint32_t modulation_color;
} gfx3d_composite_t;

/* POSITIONT fixed-function vertex. TinyGL sends transformed screen-space
 * coordinates while VMware performs rasterization, depth, texture sampling
 * and blending. */
typedef struct PACKED {
    float x;
    float y;
    float z;
    float rhw;
    uint32_t color;
    float u;
    float v;
} gfx3d_vertex_t;
typedef char gfx3d_vertex_layout_must_be_28_bytes[
    sizeof(gfx3d_vertex_t) == 28U ? 1 : -1];

#define GFX3D_DRAW_DEPTH_TEST     (1U << 0)
#define GFX3D_DRAW_DEPTH_WRITE    (1U << 1)
#define GFX3D_DRAW_BLEND          (1U << 2)
#define GFX3D_DRAW_TEXTURED       (1U << 3)
#define GFX3D_DRAW_LINEAR         (1U << 4)
#define GFX3D_DRAW_CLEAR_COLOR    (1U << 5)
#define GFX3D_DRAW_CLEAR_DEPTH    (1U << 6)
#define GFX3D_DRAW_REVERSED_DEPTH (1U << 7)
#define GFX3D_DRAW_PREMULTIPLIED  (1U << 8)
#define GFX3D_DRAW_REPEAT_U       (1U << 9)
#define GFX3D_DRAW_REPEAT_V       (1U << 10)

/* Extended fixed-function state.  The legacy REVERSED_DEPTH bit remains
 * accepted for TinyGL; Mesa uses the explicit compare field below. */
#define GFX3D_DRAW_DEPTH_FUNC_VALID (1U << 11)
#define GFX3D_DRAW_DEPTH_FUNC_SHIFT 12U
#define GFX3D_DRAW_DEPTH_FUNC_MASK  (7U << GFX3D_DRAW_DEPTH_FUNC_SHIFT)
#define GFX3D_DRAW_BLEND_ADDITIVE   (1U << 15)

typedef enum {
    GFX3D_DEPTH_NEVER = 0,
    GFX3D_DEPTH_LESS = 1,
    GFX3D_DEPTH_EQUAL = 2,
    GFX3D_DEPTH_LEQUAL = 3,
    GFX3D_DEPTH_GREATER = 4,
    GFX3D_DEPTH_NOTEQUAL = 5,
    GFX3D_DEPTH_GEQUAL = 6,
    GFX3D_DEPTH_ALWAYS = 7,
} gfx3d_depth_func_t;

#define GFX3D_DRAW_DEPTH_FUNC(func_) \
    (GFX3D_DRAW_DEPTH_FUNC_VALID | \
     (((uint32_t)(func_) & 7U) << GFX3D_DRAW_DEPTH_FUNC_SHIFT))

static inline gfx3d_depth_func_t gfx3d_draw_depth_func(uint32_t flags) {
    if (flags & GFX3D_DRAW_DEPTH_FUNC_VALID)
        return (gfx3d_depth_func_t)
            ((flags & GFX3D_DRAW_DEPTH_FUNC_MASK) >>
             GFX3D_DRAW_DEPTH_FUNC_SHIFT);
    return (flags & GFX3D_DRAW_REVERSED_DEPTH)
        ? GFX3D_DEPTH_GEQUAL : GFX3D_DEPTH_LEQUAL;
}

typedef struct {
    bool available;
    const char *driver_name;
    const char *transport_name;
    uint32_t capabilities;
    uint32_t host_hw_version;
    uint32_t guest_hw_version;
    uint32_t transport_generation;
} gfx3d_info_t;

bool gfx3d_available(void);
bool gfx3d_get_info(gfx3d_info_t *info);
const char *gfx3d_driver_name(void);
uint32_t gfx3d_capabilities(void);

bool gfx3d_surface_create(const gfx3d_surface_desc_t *desc,
                          gfx3d_surface_handle_t *handle_out);
bool gfx3d_surface_destroy(gfx3d_surface_handle_t handle);
bool gfx3d_surface_upload(gfx3d_surface_handle_t handle,
                          const uint32_t *pixels, uint32_t source_pitch,
                          const gfx_rect_t *rect);
/* Upload a tightly-addressable source region to a different destination
 * origin. source_pitch is measured in pixels and pixels points at source
 * coordinate (0,0), not at destination_x/destination_y. */
bool gfx3d_surface_upload_region(gfx3d_surface_handle_t handle,
                                 const uint32_t *pixels,
                                 uint32_t source_pitch,
                                 uint32_t destination_x,
                                 uint32_t destination_y,
                                 uint32_t width, uint32_t height);
bool gfx3d_surface_download(gfx3d_surface_handle_t handle,
                            uint32_t *pixels, uint32_t destination_pitch,
                            const gfx_rect_t *rect);
bool gfx3d_surface_clear(gfx3d_surface_handle_t handle, uint32_t color,
                         uint32_t *fence_out);
bool gfx3d_surface_composite(gfx3d_surface_handle_t source,
                             gfx3d_surface_handle_t destination,
                             const gfx3d_composite_t *operation,
                             uint32_t *fence_out);
bool gfx3d_surface_present(gfx3d_surface_handle_t handle,
                           const gfx_rect_t *rect, uint32_t *fence_out);

/* Persistent Z16 buffer attached to a render target.  This is the bridge
 * needed by hybrid Mesa frames: SWRAST and the GPU can exchange depth just
 * like they already exchange the color surface. pitch is in uint16 samples. */
bool gfx3d_depth_upload(gfx3d_surface_handle_t target,
                        const uint16_t *depth, uint32_t source_pitch,
                        const gfx_rect_t *rect);
bool gfx3d_depth_download(gfx3d_surface_handle_t target,
                          uint16_t *depth, uint32_t destination_pitch,
                          const gfx_rect_t *rect);

bool gfx3d_begin(gfx3d_surface_handle_t target, uint32_t clear_color,
                 float clear_depth, uint32_t flags);
bool gfx3d_draw_triangles(gfx3d_surface_handle_t target,
                          gfx3d_surface_handle_t texture,
                          const gfx3d_vertex_t *vertices,
                          uint32_t vertex_count, uint32_t flags,
                          uint32_t *fence_out);
bool gfx3d_end(gfx3d_surface_handle_t target, uint32_t *fence_out);

bool gfx3d_wait_fence(uint32_t fence);
bool gfx3d_selftest(uint32_t *fence_out);
void gfx3d_reset(void);

#endif
