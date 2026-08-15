#include <bleskernos_mesa.h>

static void bk_mesa_zero(void *pointer, uint32_t size)
{
    uint8_t *bytes = (uint8_t *)pointer;
    while (size--) *bytes++ = 0;
}

static bool bk_mesa_dimensions_valid(uint32_t width, uint32_t height)
{
    if (!width || !height) return false;
    if (width > BK_MESA_MAX_DIMENSION || height > BK_MESA_MAX_DIMENSION)
        return false;
    if (width > 0xFFFFFFFFU / height) return false;
    if (width * height > 0xFFFFFFFFU / 4U) return false;
    return true;
}

static bool bk_mesa_renderer_valid(bk_mesa_renderer_t renderer)
{
    return renderer == BK_MESA_RENDERER_AUTO ||
           renderer == BK_MESA_RENDERER_SOFTWARE ||
           renderer == BK_MESA_RENDERER_GPU ||
           renderer == BK_MESA_RENDERER_GPU_STRICT;
}

static bool bk_mesa_bind(bk_mesa_context_t *context)
{
    if (!context || !context->osmesa || !context->pixels) return false;
    if (!OSMesaMakeCurrent(context->osmesa, context->pixels,
                           GL_UNSIGNED_BYTE,
                           (GLsizei)context->width,
                           (GLsizei)context->height))
        return false;

    OSMesaPixelStore(OSMESA_ROW_LENGTH, (GLint)(context->pitch_bytes / 4U));
    OSMesaPixelStore(OSMESA_Y_UP, 0);
    return true;
}

static bk_mesa_context_t *bk_mesa_create_common(
    uint32_t width, uint32_t height, uint32_t *pixels, uint32_t pitch_bytes,
    uint32_t depth_bits, uint32_t stencil_bits, uint32_t accum_bits,
    bool owns_pixels, bk_mesa_renderer_t renderer)
{
    bk_mesa_context_t *context;

    if (!bk_mesa_dimensions_valid(width, height)) return NULL;
    if (!pixels || pitch_bytes < width * 4U || (pitch_bytes & 3U)) return NULL;
    if (depth_bits > 32U || stencil_bits > 16U || accum_bits > 16U)
        return NULL;
    if (!bk_mesa_renderer_valid(renderer)) return NULL;

    context = (bk_mesa_context_t *)bk_sys_alloc(sizeof(*context));
    if (!context) return NULL;
    bk_mesa_zero(context, sizeof(*context));

    context->osmesa = OSMesaCreateContextExt(OSMESA_BGRA,
                                              (GLint)depth_bits,
                                              (GLint)stencil_bits,
                                              (GLint)accum_bits,
                                              NULL);
    if (!context->osmesa) {
        bk_sys_free(context);
        return NULL;
    }

    if (!OSMesaBleskSetRenderer(context->osmesa, (GLint)renderer)) {
        OSMesaDestroyContext(context->osmesa);
        bk_sys_free(context);
        return NULL;
    }

    context->pixels = pixels;
    context->width = width;
    context->height = height;
    context->pitch_bytes = pitch_bytes;
    context->depth_bits = depth_bits;
    context->stencil_bits = stencil_bits;
    context->accum_bits = accum_bits;
    context->owns_pixels = owns_pixels;

    if (!bk_mesa_bind(context)) {
        OSMesaDestroyContext(context->osmesa);
        bk_sys_free(context);
        return NULL;
    }

    context->initialized = true;
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    return context;
}

bk_mesa_context_t *bk_mesa_create(uint32_t width, uint32_t height)
{
    return bk_mesa_create_renderer(width, height, BK_MESA_RENDERER_AUTO);
}

bk_mesa_context_t *bk_mesa_create_renderer(uint32_t width, uint32_t height,
                                            bk_mesa_renderer_t renderer)
{
    return bk_mesa_create_ex_renderer(width, height, 16U, 8U, 0U, renderer);
}

bk_mesa_context_t *bk_mesa_create_ex(uint32_t width, uint32_t height,
                                      uint32_t depth_bits,
                                      uint32_t stencil_bits,
                                      uint32_t accum_bits)
{
    return bk_mesa_create_ex_renderer(width, height, depth_bits, stencil_bits,
                                      accum_bits, BK_MESA_RENDERER_AUTO);
}

bk_mesa_context_t *bk_mesa_create_ex_renderer(uint32_t width, uint32_t height,
                                               uint32_t depth_bits,
                                               uint32_t stencil_bits,
                                               uint32_t accum_bits,
                                               bk_mesa_renderer_t renderer)
{
    uint32_t *pixels;
    uint32_t bytes;
    bk_mesa_context_t *context;

    if (!bk_mesa_dimensions_valid(width, height)) return NULL;
    bytes = width * height * 4U;
    pixels = (uint32_t *)bk_sys_alloc(bytes);
    if (!pixels) return NULL;
    bk_mesa_zero(pixels, bytes);

    context = bk_mesa_create_common(width, height, pixels, width * 4U,
                                    depth_bits, stencil_bits, accum_bits,
                                    true, renderer);
    if (!context) bk_sys_free(pixels);
    return context;
}

bk_mesa_context_t *bk_mesa_create_for_buffer(uint32_t width, uint32_t height,
                                              uint32_t *pixels,
                                              uint32_t pitch_bytes,
                                              uint32_t depth_bits,
                                              uint32_t stencil_bits,
                                              uint32_t accum_bits)
{
    return bk_mesa_create_for_buffer_renderer(
        width, height, pixels, pitch_bytes, depth_bits, stencil_bits,
        accum_bits, BK_MESA_RENDERER_AUTO);
}

bk_mesa_context_t *bk_mesa_create_for_buffer_renderer(
    uint32_t width, uint32_t height, uint32_t *pixels, uint32_t pitch_bytes,
    uint32_t depth_bits, uint32_t stencil_bits, uint32_t accum_bits,
    bk_mesa_renderer_t renderer)
{
    return bk_mesa_create_common(width, height, pixels, pitch_bytes,
                                 depth_bits, stencil_bits, accum_bits,
                                 false, renderer);
}

bool bk_mesa_make_current(bk_mesa_context_t *context)
{
    if (!context || !context->initialized) return false;
    return bk_mesa_bind(context);
}

bool bk_mesa_resize(bk_mesa_context_t *context,
                    uint32_t width, uint32_t height)
{
    uint32_t *replacement;
    uint32_t bytes;

    if (!context || !context->initialized || !context->owns_pixels)
        return false;
    if (!bk_mesa_dimensions_valid(width, height)) return false;
    if (width == context->width && height == context->height) return true;

    bytes = width * height * 4U;
    replacement = (uint32_t *)bk_sys_alloc(bytes);
    if (!replacement) return false;
    bk_mesa_zero(replacement, bytes);

    {
        uint32_t *old_pixels = context->pixels;
        uint32_t old_width = context->width;
        uint32_t old_height = context->height;
        uint32_t old_pitch = context->pitch_bytes;

        context->pixels = replacement;
        context->width = width;
        context->height = height;
        context->pitch_bytes = width * 4U;
        if (!bk_mesa_bind(context)) {
            context->pixels = old_pixels;
            context->width = old_width;
            context->height = old_height;
            context->pitch_bytes = old_pitch;
            (void)bk_mesa_bind(context);
            bk_sys_free(replacement);
            return false;
        }
        bk_sys_free(old_pixels);
    }

    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    return true;
}

bool bk_mesa_present(bk_mesa_context_t *context)
{
    if (!context || !context->initialized) return false;
    if (!bk_mesa_make_current(context)) return false;
    glFinish();
    return OSMesaBleskResolve(context->osmesa) ? true : false;
}

bool bk_mesa_present_gpu(bk_mesa_context_t *context)
{
    if (!context || !context->initialized) return false;
    if (!bk_mesa_make_current(context)) return false;
    glFinish();
    return OSMesaBleskGPUSurface(context->osmesa) != 0U;
}

bool bk_mesa_set_renderer(bk_mesa_context_t *context,
                          bk_mesa_renderer_t renderer)
{
    if (!context || !context->initialized || !bk_mesa_renderer_valid(renderer))
        return false;
    return OSMesaBleskSetRenderer(context->osmesa, (GLint)renderer)
           ? true : false;
}

bk_mesa_renderer_t bk_mesa_requested_renderer(
    const bk_mesa_context_t *context)
{
    if (!context || !context->osmesa) return BK_MESA_RENDERER_SOFTWARE;
    return (bk_mesa_renderer_t)OSMesaBleskGetRenderer(context->osmesa);
}

bk_mesa_renderer_t bk_mesa_active_renderer(
    const bk_mesa_context_t *context)
{
    if (!context || !context->osmesa) return BK_MESA_RENDERER_SOFTWARE;
    return (bk_mesa_renderer_t)OSMesaBleskGetActiveRenderer(context->osmesa);
}

bool bk_mesa_gpu_available(const bk_mesa_context_t *context)
{
    return context && context->osmesa &&
           OSMesaBleskGPUAvailable(context->osmesa) ? true : false;
}

uint32_t bk_mesa_gpu_surface(bk_mesa_context_t *context)
{
    if (!context || !context->osmesa) return 0U;
    return (uint32_t)OSMesaBleskGPUSurface(context->osmesa);
}

void bk_mesa_get_gpu_stats(const bk_mesa_context_t *context,
                           bk_mesa_gpu_stats_t *stats)
{
    if (!stats) return;
    bk_mesa_zero(stats, sizeof(*stats));
    if (context && context->osmesa)
        OSMesaBleskGetStats(context->osmesa, stats);
}

void bk_mesa_reset_gpu_stats(bk_mesa_context_t *context)
{
    if (context && context->osmesa)
        OSMesaBleskResetStats(context->osmesa);
}

bk_mesa_fallback_t bk_mesa_last_fallback(const bk_mesa_context_t *context)
{
    if (!context || !context->osmesa)
        return (bk_mesa_fallback_t)OSMESA_BLESK_FALLBACK_DRIVER_UNAVAILABLE;
    return (bk_mesa_fallback_t)OSMesaBleskLastFallback(context->osmesa);
}

const char *bk_mesa_last_fallback_string(const bk_mesa_context_t *context)
{
    if (!context || !context->osmesa) return "driver GFX3D no disponible";
    return OSMesaBleskLastFallbackString(context->osmesa);
}

bool bk_mesa_strict_failed(const bk_mesa_context_t *context)
{
    return context && context->osmesa &&
           OSMesaBleskStrictFailed(context->osmesa) ? true : false;
}

void bk_mesa_clear_error(bk_mesa_context_t *context)
{
    if (context && context->osmesa) OSMesaBleskClearError(context->osmesa);
}

bool bk_mesa_attach_to_window(bk_mesa_context_t *context,
                              bk_gui_window_t *window,
                              bk_gui_rect_t screen_rect)
{
    uint32_t surface;
    if (!context || !window || !bk_gui_gpu_viewport_supported()) return false;
    if (!bk_mesa_present_gpu(context)) return false;
    surface = bk_mesa_gpu_surface(context);
    if (!surface || context->width > 65535U || context->height > 65535U)
        return false;
    return bk_gui_window_set_gpu_viewport(window, surface,
        (uint16_t)context->width, (uint16_t)context->height, screen_rect);
}

void bk_mesa_detach_from_window(bk_gui_window_t *window)
{
    if (window) bk_gui_window_clear_gpu_viewport(window);
}

const uint32_t *bk_mesa_pixels(const bk_mesa_context_t *context)
{
    return context ? context->pixels : NULL;
}

uint32_t *bk_mesa_pixels_mutable(bk_mesa_context_t *context)
{
    return context ? context->pixels : NULL;
}

uint32_t bk_mesa_width(const bk_mesa_context_t *context)
{
    return context ? context->width : 0U;
}

uint32_t bk_mesa_height(const bk_mesa_context_t *context)
{
    return context ? context->height : 0U;
}

uint32_t bk_mesa_pitch(const bk_mesa_context_t *context)
{
    return context ? context->pitch_bytes : 0U;
}

const char *bk_mesa_renderer_name(void)
{
    const GLubyte *name = glGetString(GL_RENDERER);
    return name ? (const char *)name : "Mesa 3.5 unavailable";
}

const char *bk_mesa_context_renderer_name(const bk_mesa_context_t *context)
{
    if (!context || !context->osmesa) return "Mesa 3.5 unavailable";
    return OSMesaBleskRendererName(context->osmesa);
}

const char *bk_mesa_version_string(void)
{
    const GLubyte *version = glGetString(GL_VERSION);
    return version ? (const char *)version : "Mesa 3.5";
}

void bk_mesa_destroy(bk_mesa_context_t *context)
{
    if (!context) return;
    if (context->osmesa) {
        OSMesaDestroyContext(context->osmesa);
        context->osmesa = NULL;
    }
    if (context->owns_pixels && context->pixels) {
        bk_sys_free(context->pixels);
        context->pixels = NULL;
    }
    context->initialized = false;
    bk_sys_free(context);
}
