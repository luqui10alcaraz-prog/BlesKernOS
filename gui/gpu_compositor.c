#include "gui.h"

#include "../kernel/include/gfx.h"
#include "../kernel/include/gfx3d.h"
#include "../kernel/include/memory.h"

#define GPU_COMP_MAX_WINDOWS 12U
#define GPU_GLYPH_ATLAS_W 128U
#define GPU_GLYPH_ATLAS_H 128U
#define GPU_GLYPH_CELL 8U
#define GPU_CDE_INACTIVE_TITLE 0x009F9692U

/* This compositor is deliberately an optional layer.  It is entered only
 * when the active driver exposes the fixed-function SVGA3D path.  QEMU's
 * legacy VMware VGA device therefore keeps the existing CPU compositor. */

typedef struct {
    gui_window_t *window;
    gfx3d_surface_handle_t surface;
    uint16_t width;
    uint16_t height;
    bool seen;
} gpu_window_surface_t;

typedef struct {
    bool ready;
    bool frame_active;
    uint32_t generation;
    uint16_t width;
    uint16_t height;
    gfx3d_surface_handle_t background;
    gfx3d_surface_handle_t target;
    gfx3d_surface_handle_t overlay;
    gfx3d_surface_handle_t glyph_atlas;
    gpu_window_surface_t windows[GPU_COMP_MAX_WINDOWS];
    uint32_t *scratch;
    uint32_t scratch_pixels;
    uint32_t *scene;
    uint32_t scene_pixels;
    uint32_t *overlay_pixels;
    uint32_t overlay_capacity;
} gpu_compositor_state_t;

static gpu_compositor_state_t g_gpu;

static uint32_t gpu_required_caps(void) {
    return GFX3D_CAP_FIXED_FUNCTION | GFX3D_CAP_RENDER_TARGETS |
           GFX3D_CAP_PRESENT | GFX3D_CAP_ALPHA_BLEND |
           GFX3D_CAP_TEXTURES | GFX3D_CAP_SCALE |
           GFX3D_CAP_TRANSFORM | GFX3D_CAP_GLYPH_ATLAS |
           GFX3D_CAP_WINDOW_SURFACES;
}

static bool gpu_has_required_caps(void) {
    uint32_t caps = gfx3d_capabilities();
    return (caps & gpu_required_caps()) == gpu_required_caps();
}

static void gpu_destroy_handle(gfx3d_surface_handle_t *handle) {
    if (!handle || !*handle) return;
    (void)gfx3d_surface_destroy(*handle);
    *handle = GFX3D_SURFACE_INVALID;
}

static void gpu_release_resources(void) {
    for (uint32_t i = 0; i < GPU_COMP_MAX_WINDOWS; i++) {
        gpu_destroy_handle(&g_gpu.windows[i].surface);
        g_gpu.windows[i].window = NULL;
        g_gpu.windows[i].width = 0U;
        g_gpu.windows[i].height = 0U;
        g_gpu.windows[i].seen = false;
    }
    gpu_destroy_handle(&g_gpu.background);
    gpu_destroy_handle(&g_gpu.target);
    gpu_destroy_handle(&g_gpu.overlay);
    gpu_destroy_handle(&g_gpu.glyph_atlas);
    if (g_gpu.scratch) kfree(g_gpu.scratch);
    if (g_gpu.scene) kfree(g_gpu.scene);
    if (g_gpu.overlay_pixels) kfree(g_gpu.overlay_pixels);
    g_gpu.scratch = NULL;
    g_gpu.scene = NULL;
    g_gpu.overlay_pixels = NULL;
    g_gpu.scratch_pixels = 0U;
    g_gpu.scene_pixels = 0U;
    g_gpu.overlay_capacity = 0U;
    g_gpu.ready = false;
    g_gpu.frame_active = false;
    g_gpu.width = 0U;
    g_gpu.height = 0U;
}

static bool gpu_ensure_buffer(uint32_t **buffer, uint32_t *capacity,
                              uint32_t pixels) {
    uint32_t *new_buffer;
    if (!buffer || !capacity || !pixels) return false;
    if (*buffer && *capacity >= pixels) return true;
    new_buffer = (uint32_t *)krealloc(*buffer, pixels * sizeof(uint32_t));
    if (!new_buffer) return false;
    *buffer = new_buffer;
    *capacity = pixels;
    return true;
}

static bool gpu_create_surface(uint16_t width, uint16_t height,
                               gfx3d_format_t format, uint32_t flags,
                               gfx3d_surface_handle_t *handle) {
    gfx3d_surface_desc_t desc;
    if (!handle) return false;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.flags = flags;
    return gfx3d_surface_create(&desc, handle);
}

static bool gpu_upload_full(gfx3d_surface_handle_t handle,
                            const uint32_t *pixels,
                            uint16_t width, uint16_t height) {
    gfx_rect_t rect = {0, 0, width, height};
    return handle && pixels &&
           gfx3d_surface_upload(handle, pixels, width, &rect);
}

static bool gpu_build_glyph_atlas(void) {
    const uint32_t pixel_count = GPU_GLYPH_ATLAS_W * GPU_GLYPH_ATLAS_H;
    uint32_t *pixels = (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t));
    uint8_t rows[8];
    bool ok = false;
    if (!pixels) return false;
    kmemset(pixels, 0, pixel_count * sizeof(uint32_t));
    for (uint32_t glyph = 0U; glyph < 256U; glyph++) {
        uint32_t cell_x = (glyph & 15U) * GPU_GLYPH_CELL;
        uint32_t cell_y = (glyph >> 4U) * GPU_GLYPH_CELL;
        if (!gui_font_get_glyph8((uint8_t)glyph, rows)) goto done;
        for (uint32_t y = 0U; y < 8U; y++) {
            for (uint32_t x = 0U; x < 8U; x++) {
                if (rows[y] & (0x80U >> x))
                    pixels[(cell_y + y) * GPU_GLYPH_ATLAS_W + cell_x + x] =
                        0xFFFFFFFFU;
            }
        }
    }
    ok = gpu_upload_full(g_gpu.glyph_atlas, pixels,
                         GPU_GLYPH_ATLAS_W, GPU_GLYPH_ATLAS_H);
done:
    kfree(pixels);
    return ok;
}

static bool gpu_prepare(uint16_t width, uint16_t height) {
    gfx3d_info_t info;
    uint32_t generation;
    uint32_t pixels;
    if (!gfx3d_get_info(&info)) return false;
    generation = info.transport_generation;
    pixels = (uint32_t)width * height;
    if (!gpu_has_required_caps() || !width || !height) {
        if (g_gpu.ready) gpu_release_resources();
        return false;
    }
    if (g_gpu.ready && g_gpu.generation == generation &&
        g_gpu.width == width && g_gpu.height == height) return true;

    gpu_release_resources();
    g_gpu.generation = generation;
    g_gpu.width = width;
    g_gpu.height = height;
    if (!gpu_ensure_buffer(&g_gpu.scene, &g_gpu.scene_pixels, pixels) ||
        !gpu_ensure_buffer(&g_gpu.overlay_pixels, &g_gpu.overlay_capacity,
                           pixels) ||
        !gpu_create_surface(width, height, GFX3D_FORMAT_XRGB8888,
            GFX3D_SURFACE_TEXTURE | GFX3D_SURFACE_DYNAMIC,
            &g_gpu.background) ||
        !gpu_create_surface(width, height, GFX3D_FORMAT_ARGB8888,
            GFX3D_SURFACE_RENDER_TARGET | GFX3D_SURFACE_TEXTURE |
            GFX3D_SURFACE_WINDOW,
            &g_gpu.target) ||
        !gpu_create_surface(width, height, GFX3D_FORMAT_ARGB8888,
            GFX3D_SURFACE_TEXTURE | GFX3D_SURFACE_DYNAMIC,
            &g_gpu.overlay) ||
        !gpu_create_surface(GPU_GLYPH_ATLAS_W, GPU_GLYPH_ATLAS_H,
            GFX3D_FORMAT_ARGB8888, GFX3D_SURFACE_TEXTURE,
            &g_gpu.glyph_atlas) ||
        !gpu_build_glyph_atlas()) {
        gpu_release_resources();
        return false;
    }
    g_gpu.ready = true;
    return true;
}

bool gui_gpu_compositor_enabled(void) {
    const gfx_info_t *info = gfx_get_info();
    if (!info || info->bpp != 32U || !gfx3d_available()) {
        if (g_gpu.ready) gpu_release_resources();
        return false;
    }
    return gpu_prepare(info->width, info->height);
}

void gui_gpu_compositor_begin_frame(gui_desktop_t *desktop,
                                    const gui_surface_t *surface) {
    uint32_t pixels;
    if (!desktop || !surface || !surface->pixels ||
        !gpu_prepare(surface->width, surface->height)) {
        g_gpu.frame_active = false;
        return;
    }
    pixels = (uint32_t)surface->width * surface->height;
    kmemset(g_gpu.scene, 0, pixels * sizeof(uint32_t));
    for (uint32_t i = 0U; i < GPU_COMP_MAX_WINDOWS; i++)
        g_gpu.windows[i].seen = false;
    g_gpu.frame_active = true;
}

void gui_gpu_compositor_capture_background(const gui_surface_t *surface) {
    uint32_t *dst;
    if (!g_gpu.frame_active || !surface || !surface->pixels) return;
    for (uint32_t y = 0U; y < surface->height; y++) {
        dst = g_gpu.scene + y * surface->width;
        for (uint32_t x = 0U; x < surface->width; x++)
            dst[x] = 0xFF000000U |
                surface->pixels[y * surface->pitch + x];
    }
    if (!gpu_upload_full(g_gpu.background, g_gpu.scene,
                         surface->width, surface->height))
        g_gpu.frame_active = false;
}

static gpu_window_surface_t *gpu_find_window_slot(gui_window_t *window) {
    gpu_window_surface_t *free_slot = NULL;
    for (uint32_t i = 0U; i < GPU_COMP_MAX_WINDOWS; i++) {
        if (g_gpu.windows[i].window == window) return &g_gpu.windows[i];
        if (!g_gpu.windows[i].window && !free_slot) free_slot = &g_gpu.windows[i];
    }
    return free_slot;
}

void gui_gpu_compositor_capture_window(gui_window_t *window,
                                       const gui_surface_t *surface) {
    gpu_window_surface_t *slot;
    uint32_t pixels;
    if (!g_gpu.frame_active || !window || !window->visible ||
        !surface || !surface->pixels || window->bounds.w <= 0 ||
        window->bounds.h <= 0 || window->bounds.w > 65535 ||
        window->bounds.h > 65535) return;
    slot = gpu_find_window_slot(window);
    if (!slot) return;
    if (!slot->surface || slot->width != (uint16_t)window->bounds.w ||
        slot->height != (uint16_t)window->bounds.h) {
        gpu_destroy_handle(&slot->surface);
        slot->width = (uint16_t)window->bounds.w;
        slot->height = (uint16_t)window->bounds.h;
        if (!gpu_create_surface(slot->width, slot->height,
                GFX3D_FORMAT_ARGB8888,
                GFX3D_SURFACE_TEXTURE | GFX3D_SURFACE_DYNAMIC |
                GFX3D_SURFACE_WINDOW, &slot->surface)) {
            slot->window = NULL;
            return;
        }
    }
    slot->window = window;
    slot->seen = true;
    pixels = (uint32_t)slot->width * slot->height;
    if (!gpu_ensure_buffer(&g_gpu.scratch, &g_gpu.scratch_pixels, pixels)) {
        g_gpu.frame_active = false;
        return;
    }
    kmemset(g_gpu.scratch, 0, pixels * sizeof(uint32_t));
    for (uint32_t ly = 0U; ly < slot->height; ly++) {
        int sy = window->bounds.y + (int)ly;
        if (sy < 0 || sy >= surface->height) continue;
        for (uint32_t lx = 0U; lx < slot->width; lx++) {
            int sx = window->bounds.x + (int)lx;
            uint32_t pixel;
            if (sx < 0 || sx >= surface->width) continue;
            pixel = 0xFF000000U |
                surface->pixels[(uint32_t)sy * surface->pitch + (uint32_t)sx];
            g_gpu.scratch[ly * slot->width + lx] = pixel;
            g_gpu.scene[(uint32_t)sy * g_gpu.width + (uint32_t)sx] = pixel;
        }
    }
    if (!gpu_upload_full(slot->surface, g_gpu.scratch,
                         slot->width, slot->height))
        g_gpu.frame_active = false;
}

static bool gpu_composite(gfx3d_surface_handle_t source,
                          gfx3d_surface_handle_t destination,
                          gfx_rect_t source_rect,
                          gfx3d_transform2d_t transform,
                          gfx_rect_t clip, uint8_t opacity,
                          uint32_t modulation, gfx3d_filter_t filter,
                          uint32_t *fence_out) {
    gfx3d_composite_t operation;
    operation.source = source_rect;
    operation.transform = transform;
    operation.clip = clip;
    operation.opacity = opacity;
    operation.filter = filter;
    operation.source_premultiplied = false;
    operation.modulation_color = modulation;
    return gfx3d_surface_composite(source, destination, &operation, fence_out);
}

static bool gpu_draw_text(gfx3d_surface_handle_t destination,
                          int x, int y, const char *text,
                          uint32_t rgb, gfx_rect_t clip) {
    gfx3d_transform2d_t transform = {1.0f, 0.0f, 0.0f,
                                   0.0f, 1.0f, 0.0f};
    while (text && *text) {
        uint8_t c = (uint8_t)*text++;
        gfx_rect_t source = {(c & 15U) * 8, (c >> 4U) * 8, 8, 8};
        transform.tx = (float)x;
        transform.ty = (float)y;
        if (!gpu_composite(g_gpu.glyph_atlas, destination, source,
                           transform, clip, 255U,
                           0xFF000000U | (rgb & 0x00FFFFFFU),
                           GFX3D_FILTER_NEAREST, NULL)) return false;
        x += 7;
    }
    return true;
}

static bool gpu_draw_window_title(gui_window_t *window) {
    gui_rect_t frame;
    gui_rect_t minimize;
    gfx_rect_t clip;
    uint32_t title_bg, title_text;
    int title_left, title_right, title_w, title_x;
    if (!window || window->borderless || !window->visible) return true;
    frame = window->bounds;
    title_bg = window->focused ? window->title_color : GPU_CDE_INACTIVE_TITLE;
    title_text = (((title_bg >> 16) & 0xFFU) +
                  ((title_bg >> 8) & 0xFFU) +
                  (title_bg & 0xFFU)) < 400U ? 0x00FFFFFFU : 0x001A1716U;
    title_left = frame.x + GUI_BORDER_SIZE + 21;
    minimize = gui_window_minimize_button_rect(window);
    title_right = gui_setup_mode()
        ? frame.x + frame.w - GUI_BORDER_SIZE - 4
        : minimize.x - 5;
    if (title_right < title_left) title_right = title_left;
    clip.x = title_left;
    clip.y = frame.y + 5;
    clip.w = title_right - title_left;
    clip.h = GUI_TITLEBAR_HEIGHT - 8;
    title_w = (int)gui_font_text_width(window->title);
    title_x = frame.x + (frame.w - title_w) / 2;
    if (title_x < title_left) title_x = title_left;
    if (title_x + title_w > title_right) title_x = title_right - title_w;
    if (title_x < title_left) title_x = title_left;
    return gpu_draw_text(g_gpu.target, title_x, frame.y + 8,
                         window->title, title_text, clip);
}

static bool gpu_build_overlay(const gui_surface_t *surface) {
    uint32_t pixels;
    if (!surface || !surface->pixels) return false;
    pixels = (uint32_t)surface->width * surface->height;
    for (uint32_t y = 0U; y < surface->height; y++) {
        for (uint32_t x = 0U; x < surface->width; x++) {
            uint32_t final_pixel = 0xFF000000U |
                surface->pixels[y * surface->pitch + x];
            uint32_t scene_pixel = g_gpu.scene[y * surface->width + x];
            g_gpu.overlay_pixels[y * surface->width + x] =
                final_pixel == scene_pixel ? 0U : final_pixel;
        }
    }
    return gpu_upload_full(g_gpu.overlay, g_gpu.overlay_pixels,
                           surface->width, surface->height) && pixels != 0U;
}

bool gui_gpu_compositor_present(gui_desktop_t *desktop,
                                const gui_surface_t *final_surface) {
    gfx_rect_t full;
    gfx3d_transform2d_t identity = {1.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f};
    uint32_t fence = 0U;
    if (!g_gpu.frame_active || !desktop || !final_surface ||
        !gpu_build_overlay(final_surface)) return false;
    full = (gfx_rect_t){0, 0, g_gpu.width, g_gpu.height};
    if (!gfx3d_begin(g_gpu.target, 0xFF000000U, 0.0f,
                     GFX3D_DRAW_CLEAR_COLOR) ||
        !gpu_composite(g_gpu.background, g_gpu.target, full, identity,
                       full, 255U, 0xFFFFFFFFU, GFX3D_FILTER_NEAREST, NULL))
        return false;

    for (gui_window_t *window = desktop->first_window;
         window; window = window->next) {
        gpu_window_surface_t *slot = gpu_find_window_slot(window);
        gfx3d_transform2d_t transform = identity;
        gfx_rect_t source;
        if (!slot || slot->window != window || !slot->seen ||
            !slot->surface || !window->visible) continue;
        source = (gfx_rect_t){0, 0, slot->width, slot->height};
        transform.tx = (float)window->bounds.x;
        transform.ty = (float)window->bounds.y;
        if (!gpu_composite(slot->surface, g_gpu.target, source, transform,
                           full, 255U, 0xFFFFFFFFU,
                           GFX3D_FILTER_NEAREST, NULL) ||
            !gpu_draw_window_title(window)) return false;

        /* The 3D viewport is a separate GPU layer. It is inserted after the
         * window's software client and before later windows, preserving the
         * normal z-order without ever reading its pixels back to the CPU. */
        if (window->gpu_view_visible && window->gpu_view_surface) {
            gfx_rect_t viewport_source = {0, 0,
                window->gpu_view_width, window->gpu_view_height};
            gfx_rect_t viewport_clip;
            gfx3d_transform2d_t viewport_transform = identity;
            gui_rect_t clipped;
            gui_rect_t gpu_rect = window->gpu_view_rect;
            if (gui_rect_intersect(window->bounds, gpu_rect, &clipped) &&
                gui_rect_intersect(clipped,
                    (gui_rect_t){0, 0, g_gpu.width, g_gpu.height}, &clipped)) {
                viewport_clip = (gfx_rect_t){clipped.x, clipped.y,
                                             clipped.w, clipped.h};
                viewport_transform.tx = (float)gpu_rect.x;
                viewport_transform.ty = (float)gpu_rect.y;
                viewport_transform.m00 = (float)gpu_rect.w /
                                         window->gpu_view_width;
                viewport_transform.m11 = (float)gpu_rect.h /
                                         window->gpu_view_height;
                if (!gpu_composite(window->gpu_view_surface, g_gpu.target,
                                   viewport_source, viewport_transform,
                                   viewport_clip, 255U, 0xFFFFFFFFU,
                                   GFX3D_FILTER_LINEAR, NULL)) return false;
            }
        }
    }
    if (!gpu_composite(g_gpu.overlay, g_gpu.target, full, identity,
                       full, 255U, 0xFFFFFFFFU, GFX3D_FILTER_NEAREST, NULL) ||
        !gfx3d_end(g_gpu.target, &fence) ||
        !gfx3d_surface_present(g_gpu.target, &full, &fence)) return false;
    if (fence && !gfx3d_wait_fence(fence)) return false;

    for (uint32_t i = 0U; i < GPU_COMP_MAX_WINDOWS; i++) {
        if (g_gpu.windows[i].window && !g_gpu.windows[i].seen) {
            gpu_destroy_handle(&g_gpu.windows[i].surface);
            g_gpu.windows[i].window = NULL;
            g_gpu.windows[i].width = 0U;
            g_gpu.windows[i].height = 0U;
        }
    }
    g_gpu.frame_active = false;
    return true;
}

void gui_gpu_compositor_shutdown(void) {
    gpu_release_resources();
}
