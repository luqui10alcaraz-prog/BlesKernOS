#ifndef BLESKERNOS_MESA_H
#define BLESKERNOS_MESA_H

#include <bleskernos_api.h>
#include <GL/gl.h>
#include <GL/osmesa.h>

#define BK_MESA_API_VERSION 3U
#define BK_MESA_MAX_DIMENSION 2048U

typedef enum bk_mesa_renderer {
    BK_MESA_RENDERER_AUTO = OSMESA_BLESK_RENDERER_AUTO,
    BK_MESA_RENDERER_SOFTWARE = OSMESA_BLESK_RENDERER_SOFTWARE,
    BK_MESA_RENDERER_GPU = OSMESA_BLESK_RENDERER_GPU,
    BK_MESA_RENDERER_GPU_STRICT = OSMESA_BLESK_RENDERER_GPU_STRICT
} bk_mesa_renderer_t;

typedef OSMesaBleskStats bk_mesa_gpu_stats_t;
typedef OSMesaBleskFallback bk_mesa_fallback_t;

typedef struct bk_mesa_context {
    OSMesaContext osmesa;
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    uint32_t depth_bits;
    uint32_t stencil_bits;
    uint32_t accum_bits;
    bool owns_pixels;
    bool initialized;
} bk_mesa_context_t;

/* AUTO is the default: use GFX3D when the current OpenGL state can be
 * represented by the active driver and fall back to Mesa SWRAST otherwise. */
bk_mesa_context_t *bk_mesa_create(uint32_t width, uint32_t height);

bk_mesa_context_t *bk_mesa_create_renderer(uint32_t width, uint32_t height,
                                            bk_mesa_renderer_t renderer);

bk_mesa_context_t *bk_mesa_create_ex(uint32_t width, uint32_t height,
                                      uint32_t depth_bits,
                                      uint32_t stencil_bits,
                                      uint32_t accum_bits);

bk_mesa_context_t *bk_mesa_create_ex_renderer(uint32_t width, uint32_t height,
                                               uint32_t depth_bits,
                                               uint32_t stencil_bits,
                                               uint32_t accum_bits,
                                               bk_mesa_renderer_t renderer);

/* Binds an existing ARGB8888 CPU buffer. pitch_bytes is measured in bytes,
 * must be a multiple of four and at least width * 4. */
bk_mesa_context_t *bk_mesa_create_for_buffer(uint32_t width, uint32_t height,
                                              uint32_t *pixels,
                                              uint32_t pitch_bytes,
                                              uint32_t depth_bits,
                                              uint32_t stencil_bits,
                                              uint32_t accum_bits);

bk_mesa_context_t *bk_mesa_create_for_buffer_renderer(
    uint32_t width, uint32_t height, uint32_t *pixels, uint32_t pitch_bytes,
    uint32_t depth_bits, uint32_t stencil_bits, uint32_t accum_bits,
    bk_mesa_renderer_t renderer);

bool bk_mesa_make_current(bk_mesa_context_t *context);
bool bk_mesa_resize(bk_mesa_context_t *context,
                    uint32_t width, uint32_t height);

/* Finishes rendering and resolves the GPU color surface into pixels[].
 * Use this before reading/copying the CPU buffer. */
bool bk_mesa_present(bk_mesa_context_t *context);

/* Finishes rendering but keeps the color result on the GPU.  On success,
 * bk_mesa_gpu_surface() returns a GFX3D surface handle that can be presented
 * or composited without a GPU->CPU copy. */
bool bk_mesa_present_gpu(bk_mesa_context_t *context);

bool bk_mesa_set_renderer(bk_mesa_context_t *context,
                          bk_mesa_renderer_t renderer);
bk_mesa_renderer_t bk_mesa_requested_renderer(
    const bk_mesa_context_t *context);
bk_mesa_renderer_t bk_mesa_active_renderer(
    const bk_mesa_context_t *context);
bool bk_mesa_gpu_available(const bk_mesa_context_t *context);
uint32_t bk_mesa_gpu_surface(bk_mesa_context_t *context);
void bk_mesa_get_gpu_stats(const bk_mesa_context_t *context,
                           bk_mesa_gpu_stats_t *stats);
void bk_mesa_reset_gpu_stats(bk_mesa_context_t *context);
bk_mesa_fallback_t bk_mesa_last_fallback(const bk_mesa_context_t *context);
const char *bk_mesa_last_fallback_string(const bk_mesa_context_t *context);
bool bk_mesa_strict_failed(const bk_mesa_context_t *context);
void bk_mesa_clear_error(bk_mesa_context_t *context);

/* Connects the context's GPU surface directly to an existing GUI window.
 * This performs no GPU->RAM readback. Call after bk_mesa_present_gpu(). */
bool bk_mesa_attach_to_window(bk_mesa_context_t *context,
                              bk_gui_window_t *window,
                              bk_gui_rect_t screen_rect);
void bk_mesa_detach_from_window(bk_gui_window_t *window);

const uint32_t *bk_mesa_pixels(const bk_mesa_context_t *context);
uint32_t *bk_mesa_pixels_mutable(bk_mesa_context_t *context);
uint32_t bk_mesa_width(const bk_mesa_context_t *context);
uint32_t bk_mesa_height(const bk_mesa_context_t *context);
uint32_t bk_mesa_pitch(const bk_mesa_context_t *context);
const char *bk_mesa_renderer_name(void);
const char *bk_mesa_context_renderer_name(const bk_mesa_context_t *context);
const char *bk_mesa_version_string(void);
void bk_mesa_destroy(bk_mesa_context_t *context);

#endif
