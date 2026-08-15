/*
 * TinyGL -> BlesKernOS GFX3D bridge.
 *
 * TinyGL keeps the OpenGL 1.x frontend, matrix stack, lighting, clipping and
 * primitive assembly.  When a GFX3D driver exposing GFX3D_CAP_TINYGL is
 * active, transformed triangles, lines and points are rasterized by the GPU.
 * The public renderer policy can force software rendering; AUTO and GPU both
 * fall back safely when no compatible device is available.
 */

#include "zgl.h"
#include "../../kernel/include/gfx3d.h"

#define TGL_GPU_BATCH_VERTICES 4092U
#define TGL_GPU_TEXTURE_CACHE_SIZE 64U
#define TGL_GPU_RENDER_TARGETS 2U

typedef struct {
    GLTexture *owner;
    gfx3d_surface_handle_t surface;
    uint32_t last_use;
} tgl_gpu_texture_slot_t;

typedef struct {
    ZBuffer *zbuffer;
    gfx3d_surface_handle_t targets[TGL_GPU_RENDER_TARGETS];
    uint32_t target_count;
    uint32_t draw_index;
    uint32_t display_index;
    bool display_valid;
    tgl_gpu_texture_slot_t textures[TGL_GPU_TEXTURE_CACHE_SIZE];
    gfx3d_vertex_t batch[TGL_GPU_BATCH_VERTICES];
    uint32_t batch_count;
    uint32_t batch_flags;
    gfx3d_surface_handle_t batch_texture;
    uint32_t texture_clock;
    bool supported;
    bool frame_active;
    bool failed;
    tgl_bleskernos_gpu_stats_t stats;
} tgl_gpu_state_t;

static tgl_gpu_state_t g_tgl_gpu;
static GLint g_tgl_gpu_renderer = TGL_BK_RENDERER_AUTO;

static bool tgl_gpu_policy_allows_hardware(void) {
    return g_tgl_gpu_renderer != TGL_BK_RENDERER_CPU;
}

static gfx3d_surface_handle_t tgl_gpu_draw_target(void) {
    if (!g_tgl_gpu.target_count || g_tgl_gpu.draw_index >= g_tgl_gpu.target_count)
        return GFX3D_SURFACE_INVALID;
    return g_tgl_gpu.targets[g_tgl_gpu.draw_index];
}

static gfx3d_surface_handle_t tgl_gpu_display_target(void) {
    if (g_tgl_gpu.display_valid &&
        g_tgl_gpu.display_index < g_tgl_gpu.target_count)
        return g_tgl_gpu.targets[g_tgl_gpu.display_index];
    return tgl_gpu_draw_target();
}

static void tgl_gpu_destroy_targets(void) {
    uint32_t i;
    for (i = 0U; i < TGL_GPU_RENDER_TARGETS; ++i) {
        if (g_tgl_gpu.targets[i])
            (void)gfx3d_surface_destroy(g_tgl_gpu.targets[i]);
        g_tgl_gpu.targets[i] = GFX3D_SURFACE_INVALID;
    }
    g_tgl_gpu.target_count = 0U;
    g_tgl_gpu.draw_index = 0U;
    g_tgl_gpu.display_index = 0U;
    g_tgl_gpu.display_valid = false;
}

static uint8_t tgl_gpu_color_component(GLint value) {
    value >>= COLOR_SHIFT;
    if (value < 0) return 0U;
    if (value > 255) return 255U;
    return (uint8_t)value;
}

static uint8_t tgl_gpu_alpha_component(GLfloat value) {
    if (value <= 0.0f) return 0U;
    if (value >= 1.0f) return 255U;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t tgl_gpu_vertex_color(const GLVertex *vertex) {
    return ((uint32_t)tgl_gpu_alpha_component(vertex->color.W) << 24) |
           ((uint32_t)tgl_gpu_color_component(vertex->zp.r) << 16) |
           ((uint32_t)tgl_gpu_color_component(vertex->zp.g) << 8) |
           tgl_gpu_color_component(vertex->zp.b);
}

static float tgl_gpu_vertex_depth(const GLVertex *vertex) {
    uint32_t depth = (uint32_t)vertex->zp.z >> ZB_POINT_Z_FRAC_BITS;
    if (depth > 65535U) depth = 65535U;
    return (float)depth / 65535.0f;
}

static void tgl_gpu_convert_vertex_color(gfx3d_vertex_t *out,
                                         const GLVertex *vertex,
                                         uint32_t color) {
    float w = vertex->pc.W;
    out->x = (float)vertex->zp.x;
    out->y = (float)vertex->zp.y;
    out->z = tgl_gpu_vertex_depth(vertex);
    out->rhw = (w > -0.000001f && w < 0.000001f) ? 1.0f : 1.0f / w;
    out->color = color;
    out->u = vertex->tex_coord.X;
    out->v = vertex->tex_coord.Y;
}

static void tgl_gpu_convert_vertex(gfx3d_vertex_t *out,
                                   const GLVertex *vertex) {
    tgl_gpu_convert_vertex_color(out, vertex, tgl_gpu_vertex_color(vertex));
}

static void tgl_gpu_destroy_texture_slot(tgl_gpu_texture_slot_t *slot) {
    if (!slot) return;
    if (slot->surface) (void)gfx3d_surface_destroy(slot->surface);
    slot->owner = NULL;
    slot->surface = GFX3D_SURFACE_INVALID;
    slot->last_use = 0U;
}

static void tgl_gpu_destroy_textures(void) {
    uint32_t i;
    for (i = 0U; i < TGL_GPU_TEXTURE_CACHE_SIZE; ++i)
        tgl_gpu_destroy_texture_slot(&g_tgl_gpu.textures[i]);
}

static bool tgl_gpu_flush_batch(void) {
    uint32_t fence = 0U;
    if (!g_tgl_gpu.frame_active || !g_tgl_gpu.batch_count) return true;
    if (!gfx3d_draw_triangles(tgl_gpu_draw_target(), g_tgl_gpu.batch_texture,
                              g_tgl_gpu.batch, g_tgl_gpu.batch_count,
                              g_tgl_gpu.batch_flags, &fence)) {
        g_tgl_gpu.failed = true;
        g_tgl_gpu.batch_count = 0U;
        g_tgl_gpu.stats.fallbacks++;
        return false;
    }
    g_tgl_gpu.stats.draw_calls++;
    g_tgl_gpu.batch_count = 0U;
    return true;
}

static tgl_gpu_texture_slot_t *tgl_gpu_find_texture(GLTexture *texture) {
    uint32_t i;
    for (i = 0U; i < TGL_GPU_TEXTURE_CACHE_SIZE; ++i) {
        if (g_tgl_gpu.textures[i].owner == texture &&
            g_tgl_gpu.textures[i].surface)
            return &g_tgl_gpu.textures[i];
    }
    return NULL;
}

static tgl_gpu_texture_slot_t *tgl_gpu_choose_texture_slot(void) {
    tgl_gpu_texture_slot_t *oldest = &g_tgl_gpu.textures[0];
    uint32_t i;
    for (i = 0U; i < TGL_GPU_TEXTURE_CACHE_SIZE; ++i) {
        tgl_gpu_texture_slot_t *slot = &g_tgl_gpu.textures[i];
        if (!slot->owner || !slot->surface) return slot;
        if (slot->last_use < oldest->last_use) oldest = slot;
    }
    return oldest;
}

static gfx3d_surface_handle_t tgl_gpu_sync_texture(GLTexture *texture) {
#if TGL_FEATURE_RENDER_BITS == 32
    gfx3d_surface_desc_t desc;
    gfx_rect_t full;
    GLImage *image;
    tgl_gpu_texture_slot_t *slot;

    if (!texture) return GFX3D_SURFACE_INVALID;
    slot = tgl_gpu_find_texture(texture);
    if (slot) {
        slot->last_use = ++g_tgl_gpu.texture_clock;
        return slot->surface;
    }
    if (!tgl_gpu_flush_batch()) return GFX3D_SURFACE_INVALID;
    image = &texture->images[0];
    if (!image->xsize || !image->ysize) return GFX3D_SURFACE_INVALID;

    slot = tgl_gpu_choose_texture_slot();
    if (slot->surface == g_tgl_gpu.batch_texture &&
        !tgl_gpu_flush_batch()) return GFX3D_SURFACE_INVALID;
    tgl_gpu_destroy_texture_slot(slot);

    desc.width = (uint16_t)image->xsize;
    desc.height = (uint16_t)image->ysize;
    desc.format = GFX3D_FORMAT_ARGB8888;
    desc.flags = GFX3D_SURFACE_TEXTURE | GFX3D_SURFACE_DYNAMIC;
    if (!gfx3d_surface_create(&desc, &slot->surface))
        return GFX3D_SURFACE_INVALID;
    full = (gfx_rect_t){0, 0, image->xsize, image->ysize};
    if (!gfx3d_surface_upload(slot->surface,
                              (const uint32_t *)image->pixmap,
                              (uint32_t)image->xsize, &full)) {
        tgl_gpu_destroy_texture_slot(slot);
        return GFX3D_SURFACE_INVALID;
    }
    slot->owner = texture;
    slot->last_use = ++g_tgl_gpu.texture_clock;
    g_tgl_gpu.stats.texture_uploads++;
    return slot->surface;
#else
    (void)texture;
    return GFX3D_SURFACE_INVALID;
#endif
}

void tgl_gpu_context_init(ZBuffer *zbuffer) {
    gfx3d_surface_desc_t desc;
    tgl_bleskernos_gpu_stats_t old_stats = g_tgl_gpu.stats;
    uint32_t i;

    tgl_gpu_destroy_targets();
    tgl_gpu_destroy_textures();
    g_tgl_gpu = (tgl_gpu_state_t){0};
    g_tgl_gpu.stats = old_stats;
    g_tgl_gpu.zbuffer = zbuffer;
    if (!zbuffer || TGL_FEATURE_RENDER_BITS != 32 || !gfx3d_available() ||
        !(gfx3d_capabilities() & GFX3D_CAP_TINYGL)) return;
    desc.width = (uint16_t)zbuffer->xsize;
    desc.height = (uint16_t)zbuffer->ysize;
    desc.format = GFX3D_FORMAT_ARGB8888;
    desc.flags = GFX3D_SURFACE_RENDER_TARGET | GFX3D_SURFACE_TEXTURE |
                 GFX3D_SURFACE_DYNAMIC | GFX3D_SURFACE_TINYGL;
    for (i = 0U; i < TGL_GPU_RENDER_TARGETS; ++i) {
        if (!gfx3d_surface_create(&desc, &g_tgl_gpu.targets[i])) break;
        g_tgl_gpu.target_count++;
    }
    if (g_tgl_gpu.target_count != TGL_GPU_RENDER_TARGETS) {
        tgl_gpu_destroy_targets();
        return;
    }
    g_tgl_gpu.supported = true;
}

void tgl_gpu_context_close(ZBuffer *zbuffer) {
    tgl_bleskernos_gpu_stats_t old_stats;
    if (zbuffer && g_tgl_gpu.zbuffer != zbuffer) return;
    if (g_tgl_gpu.frame_active) (void)tgl_gpu_resolve(g_tgl_gpu.zbuffer);
    else (void)tgl_gpu_flush_batch();
    old_stats = g_tgl_gpu.stats;
    tgl_gpu_destroy_textures();
    tgl_gpu_destroy_targets();
    g_tgl_gpu = (tgl_gpu_state_t){0};
    g_tgl_gpu.stats = old_stats;
}

void tgl_gpu_context_resize(ZBuffer *zbuffer) {
    if (!zbuffer || g_tgl_gpu.zbuffer != zbuffer) return;
    tgl_gpu_context_close(zbuffer);
    tgl_gpu_context_init(zbuffer);
}

static uint32_t tgl_gpu_clear_color(const GLContext *context) {
    uint32_t a = tgl_gpu_alpha_component(context->clear_color.W);
    uint32_t r = tgl_gpu_alpha_component(context->clear_color.X);
    uint32_t g = tgl_gpu_alpha_component(context->clear_color.Y);
    uint32_t b = tgl_gpu_alpha_component(context->clear_color.Z);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t tgl_gpu_draw_flags(const GLContext *context,
                                   bool textured) {
    uint32_t flags = GFX3D_DRAW_REVERSED_DEPTH;
    if (context->zb->depth_test) flags |= GFX3D_DRAW_DEPTH_TEST;
    if (context->zb->depth_write) flags |= GFX3D_DRAW_DEPTH_WRITE;
    if (context->zb->enable_blend) flags |= GFX3D_DRAW_BLEND;
    if (textured) {
        GLTexture *texture = context->current_texture;
        flags |= GFX3D_DRAW_TEXTURED;
        if (!texture || texture->min_filter == GL_LINEAR ||
            texture->mag_filter == GL_LINEAR ||
            texture->min_filter == GL_NEAREST_MIPMAP_LINEAR ||
            texture->min_filter == GL_LINEAR_MIPMAP_NEAREST ||
            texture->min_filter == GL_LINEAR_MIPMAP_LINEAR)
            flags |= GFX3D_DRAW_LINEAR;
        if (!texture || texture->wrap_s == GL_REPEAT)
            flags |= GFX3D_DRAW_REPEAT_U;
        if (!texture || texture->wrap_t == GL_REPEAT)
            flags |= GFX3D_DRAW_REPEAT_V;
    }
    return flags;
}

static bool tgl_gpu_blend_supported(const GLContext *context) {
    if (!context->zb->enable_blend) return true;
    return context->zb->blendeq == GL_FUNC_ADD &&
           context->zb->sfactor == GL_SRC_ALPHA &&
           context->zb->dfactor == GL_ONE_MINUS_SRC_ALPHA;
}

void tgl_gpu_begin_frame(GLContext *context, GLint clear_mask) {
    uint32_t flags;
    if (!context || !g_tgl_gpu.supported ||
        !tgl_gpu_policy_allows_hardware() ||
        g_tgl_gpu.zbuffer != context->zb) return;
    if (g_tgl_gpu.frame_active) tgl_gpu_prepare_cpu(context->zb);
    /* A color clear is the reliable frame boundary used by TinyGL programs. */
    if (!(clear_mask & GL_COLOR_BUFFER_BIT)) return;
    g_tgl_gpu.batch_count = 0U;
    g_tgl_gpu.failed = false;
    if (g_tgl_gpu.target_count > 1U && g_tgl_gpu.display_valid)
        g_tgl_gpu.draw_index = g_tgl_gpu.display_index ^ 1U;
    else
        g_tgl_gpu.draw_index = 0U;
    flags = tgl_gpu_draw_flags(context, context->texture_2d_enabled != 0);
    if (clear_mask & GL_COLOR_BUFFER_BIT) flags |= GFX3D_DRAW_CLEAR_COLOR;
    if (clear_mask & GL_DEPTH_BUFFER_BIT) flags |= GFX3D_DRAW_CLEAR_DEPTH;
    g_tgl_gpu.batch_flags = flags &
        ~(GFX3D_DRAW_CLEAR_COLOR | GFX3D_DRAW_CLEAR_DEPTH);
    g_tgl_gpu.batch_texture = GFX3D_SURFACE_INVALID;
    g_tgl_gpu.frame_active = gfx3d_begin(
        tgl_gpu_draw_target(), tgl_gpu_clear_color(context), 0.0f, flags);
    if (g_tgl_gpu.frame_active) g_tgl_gpu.stats.frames_started++;
    else g_tgl_gpu.stats.fallbacks++;
}

static bool tgl_gpu_append(GLContext *context,
                           gfx3d_surface_handle_t texture,
                           uint32_t flags,
                           const gfx3d_vertex_t *vertices,
                           uint32_t count) {
    uint32_t i;
    if (!context || !vertices || !count || !g_tgl_gpu.frame_active ||
        g_tgl_gpu.failed || g_tgl_gpu.zbuffer != context->zb)
        return false;
    if (g_tgl_gpu.batch_count &&
        (g_tgl_gpu.batch_flags != flags ||
         g_tgl_gpu.batch_texture != texture) &&
        !tgl_gpu_flush_batch()) {
        tgl_gpu_prepare_cpu(context->zb);
        return false;
    }
    g_tgl_gpu.batch_flags = flags;
    g_tgl_gpu.batch_texture = texture;
    if (g_tgl_gpu.batch_count + count > TGL_GPU_BATCH_VERTICES &&
        !tgl_gpu_flush_batch()) {
        tgl_gpu_prepare_cpu(context->zb);
        return false;
    }
    if (count > TGL_GPU_BATCH_VERTICES) return false;
    for (i = 0U; i < count; ++i)
        g_tgl_gpu.batch[g_tgl_gpu.batch_count++] = vertices[i];
    return true;
}

bool tgl_gpu_submit_triangle(GLContext *context, const GLVertex *a,
                             const GLVertex *b, const GLVertex *c) {
    uint32_t flags, flat_color = 0U;
    bool flat_shaded;
    gfx3d_surface_handle_t texture = GFX3D_SURFACE_INVALID;
    gfx3d_vertex_t vertices[3];

    if (!context || !a || !b || !c || !g_tgl_gpu.frame_active ||
        !tgl_gpu_policy_allows_hardware() || !tgl_gpu_blend_supported(context))
        return false;
    flags = tgl_gpu_draw_flags(context, context->texture_2d_enabled != 0);
    if (context->texture_2d_enabled) {
        if (!context->current_texture) return false;
        texture = tgl_gpu_sync_texture(context->current_texture);
        if (!texture) {
            tgl_gpu_prepare_cpu(context->zb);
            return false;
        }
    }
    flat_shaded = context->current_shade_model != GL_SMOOTH;
    if (flat_shaded) flat_color = tgl_gpu_vertex_color(c);
    if (flat_shaded) {
        tgl_gpu_convert_vertex_color(&vertices[0], a, flat_color);
        tgl_gpu_convert_vertex_color(&vertices[1], b, flat_color);
        tgl_gpu_convert_vertex_color(&vertices[2], c, flat_color);
    } else {
        tgl_gpu_convert_vertex(&vertices[0], a);
        tgl_gpu_convert_vertex(&vertices[1], b);
        tgl_gpu_convert_vertex(&vertices[2], c);
    }
    if (!tgl_gpu_append(context, texture, flags, vertices, 3U)) return false;
    g_tgl_gpu.stats.triangles_submitted++;
    return true;
}

bool tgl_gpu_submit_line(GLContext *context, const GLVertex *a,
                         const GLVertex *b) {
    gfx3d_vertex_t vertices[6], av, bv;
    float dx, dy, length, px, py;
    uint32_t flags;

    if (!context || !a || !b || !g_tgl_gpu.frame_active ||
        !tgl_gpu_policy_allows_hardware() || !tgl_gpu_blend_supported(context))
        return false;
    tgl_gpu_convert_vertex(&av, a);
    tgl_gpu_convert_vertex(&bv, b);
    dx = bv.x - av.x;
    dy = bv.y - av.y;
    length = (float)sqrt((double)(dx * dx + dy * dy));
    if (length < 0.0001f) return tgl_gpu_submit_point(context, a);
    px = (-dy / length) * 0.5f;
    py = ( dx / length) * 0.5f;

    vertices[0] = av; vertices[0].x += px; vertices[0].y += py;
    vertices[1] = bv; vertices[1].x += px; vertices[1].y += py;
    vertices[2] = bv; vertices[2].x -= px; vertices[2].y -= py;
    vertices[3] = av; vertices[3].x += px; vertices[3].y += py;
    vertices[4] = bv; vertices[4].x -= px; vertices[4].y -= py;
    vertices[5] = av; vertices[5].x -= px; vertices[5].y -= py;
    flags = tgl_gpu_draw_flags(context, false);
    if (!tgl_gpu_append(context, GFX3D_SURFACE_INVALID,
                        flags, vertices, 6U)) return false;
    g_tgl_gpu.stats.lines_submitted++;
    return true;
}

bool tgl_gpu_submit_point(GLContext *context, const GLVertex *point) {
    gfx3d_vertex_t vertices[6], center;
    float half;
    uint32_t flags;

    if (!context || !point || !g_tgl_gpu.frame_active ||
        !tgl_gpu_policy_allows_hardware() || !tgl_gpu_blend_supported(context))
        return false;
    tgl_gpu_convert_vertex(&center, point);
    half = context->zb->pointsize * 0.5f;
    if (half < 0.5f) half = 0.5f;
#define SET_POINT_VERTEX(index_, ox_, oy_) do { \
        vertices[index_] = center; \
        vertices[index_].x += (ox_); \
        vertices[index_].y += (oy_); \
    } while (0)
    SET_POINT_VERTEX(0, -half, -half);
    SET_POINT_VERTEX(1,  half, -half);
    SET_POINT_VERTEX(2,  half,  half);
    SET_POINT_VERTEX(3, -half, -half);
    SET_POINT_VERTEX(4,  half,  half);
    SET_POINT_VERTEX(5, -half,  half);
#undef SET_POINT_VERTEX
    flags = tgl_gpu_draw_flags(context, false);
    if (!tgl_gpu_append(context, GFX3D_SURFACE_INVALID,
                        flags, vertices, 6U)) return false;
    g_tgl_gpu.stats.points_submitted++;
    return true;
}

static bool tgl_gpu_finish_frame(ZBuffer *zbuffer, bool download) {
    uint32_t fence = 0U;
    gfx_rect_t rect;
    bool ok;
    if (!zbuffer || g_tgl_gpu.zbuffer != zbuffer ||
        !g_tgl_gpu.frame_active) return true;
    ok = tgl_gpu_flush_batch() && gfx3d_end(tgl_gpu_draw_target(), &fence);
    if (ok && fence) ok = gfx3d_wait_fence(fence);
    rect = (gfx_rect_t){0, 0, zbuffer->xsize, zbuffer->ysize};
    if (ok && download)
        ok = gfx3d_surface_download(tgl_gpu_draw_target(),
            (uint32_t *)zbuffer->pbuf, (uint32_t)zbuffer->xsize, &rect);
    g_tgl_gpu.frame_active = false;
    g_tgl_gpu.batch_count = 0U;
    if (ok) {
        /* Keep the completed front buffer visible while the next frame is
           cleared and rendered in the back buffer. Publish after the fence. */
        if (!download) {
            g_tgl_gpu.display_index = g_tgl_gpu.draw_index;
            g_tgl_gpu.display_valid = true;
        }
        g_tgl_gpu.stats.frames_resolved++;
#if TGL_HAS(DIRTY_RECTANGLE)
        if (download)
        ZB_markFullDirty(zbuffer);
#endif
        return true;
    }
    g_tgl_gpu.failed = true;
    g_tgl_gpu.stats.fallbacks++;
    return false;
}

bool tgl_gpu_resolve(ZBuffer *zbuffer) {
    return tgl_gpu_finish_frame(zbuffer, true);
}

void tgl_gpu_prepare_cpu(ZBuffer *zbuffer) {
    (void)tgl_gpu_resolve(zbuffer);
}

void tgl_gpu_flush(void) {
    (void)tgl_gpu_flush_batch();
}

void tgl_gpu_texture_invalidated(GLTexture *texture) {
    uint32_t i;
    if (!texture) return;
    for (i = 0U; i < TGL_GPU_TEXTURE_CACHE_SIZE; ++i) {
        tgl_gpu_texture_slot_t *slot = &g_tgl_gpu.textures[i];
        if (slot->owner != texture) continue;
        if (slot->surface == g_tgl_gpu.batch_texture)
            (void)tgl_gpu_flush_batch();
        tgl_gpu_destroy_texture_slot(slot);
        return;
    }
}

GLint tglBlesKernOSGPUAvailable(void) {
    return g_tgl_gpu.supported ? GL_TRUE : GL_FALSE;
}

GLint tglBlesKernOSGPUActive(void) {
    return g_tgl_gpu.frame_active ? GL_TRUE : GL_FALSE;
}

GLuint tglBlesKernOSGPUSurface(void) {
    return (GLuint)tgl_gpu_display_target();
}

GLint tglBlesKernOSGPUResolve(void) {
    return tgl_gpu_resolve(g_tgl_gpu.zbuffer) ? GL_TRUE : GL_FALSE;
}

GLint tglBlesKernOSGPUFinish(void) {
    return tgl_gpu_finish_frame(g_tgl_gpu.zbuffer, false) ? GL_TRUE : GL_FALSE;
}

void tglBlesKernOSSetRenderer(GLint renderer) {
    if (renderer != TGL_BK_RENDERER_AUTO &&
        renderer != TGL_BK_RENDERER_CPU &&
        renderer != TGL_BK_RENDERER_GPU) return;
    if (g_tgl_gpu.frame_active) tgl_gpu_prepare_cpu(g_tgl_gpu.zbuffer);
    g_tgl_gpu_renderer = renderer;
}

GLint tglBlesKernOSGetRenderer(void) {
    return g_tgl_gpu_renderer;
}

GLint tglBlesKernOSGetActiveRenderer(void) {
    if (tgl_gpu_policy_allows_hardware() && g_tgl_gpu.supported)
        return TGL_BK_RENDERER_GPU;
    return TGL_BK_RENDERER_CPU;
}

const GLubyte *tglBlesKernOSRendererName(void) {
    if (tglBlesKernOSGetActiveRenderer() == TGL_BK_RENDERER_GPU)
        return (const GLubyte *)"BlesKernOS GFX3D";
    return (const GLubyte *)"TinyGL software rasterizer";
}

void tglBlesKernOSGPUGetStats(tgl_bleskernos_gpu_stats_t *stats) {
    if (stats) *stats = g_tgl_gpu.stats;
}

void tglBlesKernOSGPUResetStats(void) {
    g_tgl_gpu.stats = (tgl_bleskernos_gpu_stats_t){0};
}
