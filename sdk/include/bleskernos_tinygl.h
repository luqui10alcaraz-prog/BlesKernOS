#ifndef BLESKERNOS_TINYGL_H
#define BLESKERNOS_TINYGL_H

#include <TGL/gl.h>
#include <tinygl/zbuffer.h>

#define BK_TINYGL_API_VERSION 2U

typedef enum bk_tinygl_renderer {
    BK_TINYGL_RENDERER_AUTO = TGL_BK_RENDERER_AUTO,
    BK_TINYGL_RENDERER_SOFTWARE = TGL_BK_RENDERER_CPU,
    BK_TINYGL_RENDERER_GPU = TGL_BK_RENDERER_GPU
} bk_tinygl_renderer_t;

typedef tgl_bleskernos_gpu_stats_t bk_tinygl_gpu_stats_t;

typedef struct bk_tinygl_context {
    ZBuffer *zbuffer;
    PIXEL *pixels;
    unsigned int width;
    unsigned int height;
    unsigned int pitch_bytes;
    int initialized;
    bk_tinygl_renderer_t requested_renderer;
} bk_tinygl_context_t;

/* Creates a TinyGL context in AUTO mode and makes it current.
 * TinyGL currently has one global current context per process. */
bk_tinygl_context_t *bk_tinygl_create(unsigned int width, unsigned int height);

/* Creates a context with an explicit renderer preference. GPU mode still
 * falls back to software when no compatible GFX3D driver is loaded. */
bk_tinygl_context_t *bk_tinygl_create_ex(unsigned int width,
                                          unsigned int height,
                                          bk_tinygl_renderer_t renderer);
void bk_tinygl_set_renderer(bk_tinygl_context_t *context,
                            bk_tinygl_renderer_t renderer);
bk_tinygl_renderer_t bk_tinygl_requested_renderer(
    const bk_tinygl_context_t *context);
bk_tinygl_renderer_t bk_tinygl_active_renderer(
    const bk_tinygl_context_t *context);
const char *bk_tinygl_renderer_name(const bk_tinygl_context_t *context);
int bk_tinygl_gpu_available(void);
void bk_tinygl_gpu_get_stats(bk_tinygl_gpu_stats_t *stats);
void bk_tinygl_gpu_reset_stats(void);

/* Copies the rendered ZBuffer into the context's linear PIXEL buffer. */
int bk_tinygl_present(bk_tinygl_context_t *context);
/* Completes a GPU frame without copying it back to system RAM. */
int bk_tinygl_present_gpu(bk_tinygl_context_t *context);
unsigned int bk_tinygl_gpu_surface(const bk_tinygl_context_t *context);

/* Accessors for blitting the result into a BlesKernOS GUI surface. */
const PIXEL *bk_tinygl_pixels(const bk_tinygl_context_t *context);
unsigned int bk_tinygl_width(const bk_tinygl_context_t *context);
unsigned int bk_tinygl_height(const bk_tinygl_context_t *context);
unsigned int bk_tinygl_pitch(const bk_tinygl_context_t *context);
unsigned int bk_tinygl_pixel_rgb(PIXEL pixel);

/* Closes TinyGL and releases the ZBuffer and output pixels. */
void bk_tinygl_destroy(bk_tinygl_context_t *context);

/* Last internal TinyGL fatal message, or NULL when none was reported. */
const char *bk_tinygl_last_fatal_error(void);
void bk_tinygl_clear_fatal_error(void);

#endif
