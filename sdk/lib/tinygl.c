#include <bleskernos_api.h>
#include <bleskernos_tinygl.h>

static void bk_tinygl_zero(void *pointer, uint32_t size)
{
    uint8_t *bytes = (uint8_t *)pointer;
    while (size--) *bytes++ = 0;
}

bk_tinygl_context_t *bk_tinygl_create(uint32_t width, uint32_t height)
{
    return bk_tinygl_create_ex(width, height, BK_TINYGL_RENDERER_AUTO);
}

bk_tinygl_context_t *bk_tinygl_create_ex(uint32_t width, uint32_t height,
                                          bk_tinygl_renderer_t renderer)
{
    bk_tinygl_context_t *context;
    uint32_t pixel_count;

    if (!width || !height) return NULL;
    if (renderer != BK_TINYGL_RENDERER_AUTO &&
        renderer != BK_TINYGL_RENDERER_SOFTWARE &&
        renderer != BK_TINYGL_RENDERER_GPU)
        renderer = BK_TINYGL_RENDERER_AUTO;
    if (width > 4096U || height > 4096U) return NULL;
    if (width > (0xFFFFFFFFU / height)) return NULL;

    pixel_count = width * height;
    if (pixel_count > (0xFFFFFFFFU / (uint32_t)sizeof(PIXEL))) return NULL;

    context = (bk_tinygl_context_t *)bk_sys_alloc(sizeof(*context));
    if (!context) return NULL;
    bk_tinygl_zero(context, sizeof(*context));

    context->pixels = (PIXEL *)bk_sys_alloc(pixel_count * (uint32_t)sizeof(PIXEL));
    if (!context->pixels) {
        bk_sys_free(context);
        return NULL;
    }
    bk_tinygl_zero(context->pixels, pixel_count * (uint32_t)sizeof(PIXEL));

    context->zbuffer = ZB_open((int)width, (int)height,
#if TGL_FEATURE_RENDER_BITS == 32
                               ZB_MODE_RGBA,
#else
                               ZB_MODE_5R6G5B,
#endif
                               0);
    if (!context->zbuffer) {
        bk_sys_free(context->pixels);
        bk_sys_free(context);
        return NULL;
    }

    context->width = width;
    context->height = height;
    context->pitch_bytes = width * (uint32_t)sizeof(PIXEL);
    context->requested_renderer = renderer;
    tglBlesKernOSSetRenderer((GLint)renderer);
    glInit(context->zbuffer);
    context->initialized = true;
    return context;
}


void bk_tinygl_set_renderer(bk_tinygl_context_t *context,
                            bk_tinygl_renderer_t renderer)
{
    if (!context) return;
    if (renderer != BK_TINYGL_RENDERER_AUTO &&
        renderer != BK_TINYGL_RENDERER_SOFTWARE &&
        renderer != BK_TINYGL_RENDERER_GPU) return;
    context->requested_renderer = renderer;
    tglBlesKernOSSetRenderer((GLint)renderer);
}

bk_tinygl_renderer_t bk_tinygl_requested_renderer(
    const bk_tinygl_context_t *context)
{
    return context ? context->requested_renderer : BK_TINYGL_RENDERER_SOFTWARE;
}

bk_tinygl_renderer_t bk_tinygl_active_renderer(
    const bk_tinygl_context_t *context)
{
    if (!context || !context->initialized) return BK_TINYGL_RENDERER_SOFTWARE;
    return (bk_tinygl_renderer_t)tglBlesKernOSGetActiveRenderer();
}

const char *bk_tinygl_renderer_name(const bk_tinygl_context_t *context)
{
    if (!context || !context->initialized) return "TinyGL unavailable";
    return (const char *)tglBlesKernOSRendererName();
}

int bk_tinygl_gpu_available(void)
{
    return tglBlesKernOSGPUAvailable() == GL_TRUE;
}

void bk_tinygl_gpu_get_stats(bk_tinygl_gpu_stats_t *stats)
{
    tglBlesKernOSGPUGetStats(stats);
}

void bk_tinygl_gpu_reset_stats(void)
{
    tglBlesKernOSGPUResetStats();
}

int bk_tinygl_present(bk_tinygl_context_t *context)
{
    if (!context || !context->initialized || !context->zbuffer || !context->pixels)
        return false;
    ZB_copyFrameBuffer(context->zbuffer, context->pixels, (int)context->pitch_bytes);
    return true;
}

int bk_tinygl_present_gpu(bk_tinygl_context_t *context)
{
    if (!context || !context->initialized || !context->zbuffer)
        return false;
    if (bk_tinygl_active_renderer(context) != BK_TINYGL_RENDERER_GPU)
        return bk_tinygl_present(context);
    return tglBlesKernOSGPUFinish() == GL_TRUE;
}

unsigned int bk_tinygl_gpu_surface(const bk_tinygl_context_t *context)
{
    if (!context || !context->initialized ||
        bk_tinygl_active_renderer(context) != BK_TINYGL_RENDERER_GPU)
        return 0U;
    return (unsigned int)tglBlesKernOSGPUSurface();
}

const PIXEL *bk_tinygl_pixels(const bk_tinygl_context_t *context)
{
    return context ? context->pixels : NULL;
}

uint32_t bk_tinygl_width(const bk_tinygl_context_t *context)
{
    return context ? context->width : 0U;
}

uint32_t bk_tinygl_height(const bk_tinygl_context_t *context)
{
    return context ? context->height : 0U;
}

uint32_t bk_tinygl_pitch(const bk_tinygl_context_t *context)
{
    return context ? context->pitch_bytes : 0U;
}

uint32_t bk_tinygl_pixel_rgb(PIXEL pixel)
{
    return ((uint32_t)GET_RED(pixel) << 16) |
           ((uint32_t)GET_GREEN(pixel) << 8) |
           (uint32_t)GET_BLUE(pixel);
}

void bk_tinygl_destroy(bk_tinygl_context_t *context)
{
    if (!context) return;

    if (context->initialized) {
        glClose();
        context->initialized = false;
    }
    if (context->zbuffer) {
        ZB_close(context->zbuffer);
        context->zbuffer = NULL;
    }
    if (context->pixels) {
        bk_sys_free(context->pixels);
        context->pixels = NULL;
    }
    bk_sys_free(context);
}
