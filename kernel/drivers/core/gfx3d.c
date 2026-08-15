#include "../../include/gfx3d_driver.h"
#include "../../include/memory.h"
#include "../../include/task.h"
#include "../../include/klock.h"

#define GFX3D_MAX_DRIVERS 4U

static const gfx3d_driver_ops_t *g_drivers[GFX3D_MAX_DRIVERS];
static uint32_t g_driver_count;
static const gfx3d_driver_ops_t *g_active;
static gfx3d_info_t g_info;
static kmutex_t g_gfx3d_call_lock = KMUTEX_INITIALIZER;

/* Mode switches and 3D command submission share driver-owned surfaces. The
 * lock is local to GFX3D: filesystem, networking and CPU rendering continue
 * on other cores while one task submits or resets the device. */
static void gfx3d_call_lock(void) {
    kmutex_lock(&g_gfx3d_call_lock);
}

static void gfx3d_call_unlock(void) {
    kmutex_unlock(&g_gfx3d_call_lock);
}

static bool gfx3d_try_activate(void) {
    const gfx3d_driver_ops_t *best = NULL;
    gfx3d_info_t best_info;
    if (g_active) {
        gfx3d_info_t current;
        kmemset(&current, 0, sizeof(current));
        if (g_active->probe && g_active->probe(&current)) {
            g_info = current;
            return true;
        }
        if (g_active->reset) g_active->reset();
        g_active = NULL;
        kmemset(&g_info, 0, sizeof(g_info));
    }
    for (uint32_t i = 0; i < g_driver_count; i++) {
        gfx3d_info_t candidate;
        const gfx3d_driver_ops_t *ops = g_drivers[i];
        kmemset(&candidate, 0, sizeof(candidate));
        if (!ops || !ops->probe || !ops->probe(&candidate)) continue;
        if (!best || ops->priority > best->priority) {
            best = ops;
            best_info = candidate;
        }
    }
    if (!best) return false;
    g_active = best;
    g_info = best_info;
    return true;
}

bool gfx3d_register_driver(const gfx3d_driver_ops_t *ops) {
    bool result = false;
    gfx3d_call_lock();
    if (!ops || ops->abi_version != BK_GFX3D_DRIVER_ABI_VERSION ||
        ops->descriptor_size != sizeof(*ops) || !ops->name ||
        !ops->probe || !ops->surface_create || !ops->surface_destroy ||
        !ops->surface_upload || !ops->surface_present ||
        !ops->draw_triangles || !ops->selftest ||
        g_driver_count >= GFX3D_MAX_DRIVERS) goto out;
    for (uint32_t i = 0; i < g_driver_count; i++)
        if (g_drivers[i] == ops) {
            result = true;
            goto out;
        }
    g_drivers[g_driver_count++] = ops;
    (void)gfx3d_try_activate();
    result = true;
out:
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_available(void) {
    bool result;
    gfx3d_call_lock();
    result = gfx3d_try_activate();
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_get_info(gfx3d_info_t *info) {
    bool result = false;
    gfx3d_call_lock();
    if (!gfx3d_try_activate()) {
        if (info) kmemset(info, 0, sizeof(*info));
        goto out;
    }
    if (info) *info = g_info;
    result = true;
out:
    gfx3d_call_unlock();
    return result;
}

const char *gfx3d_driver_name(void) {
    const char *name;
    gfx3d_call_lock();
    if (gfx3d_try_activate() && g_active && g_active->name)
        name = g_active->name;
    else
        name = g_driver_count && g_drivers[0] ? g_drivers[0]->name : "ninguno";
    gfx3d_call_unlock();
    return name;
}

uint32_t gfx3d_capabilities(void) {
    uint32_t capabilities;
    gfx3d_call_lock();
    capabilities = gfx3d_try_activate() ? g_info.capabilities : 0U;
    gfx3d_call_unlock();
    return capabilities;
}

bool gfx3d_surface_create(const gfx3d_surface_desc_t *desc,
                          gfx3d_surface_handle_t *handle_out) {
    bool result;
    if (handle_out) *handle_out = GFX3D_SURFACE_INVALID;
    gfx3d_call_lock();
    result = desc && handle_out && gfx3d_try_activate() &&
             g_active->surface_create(desc, handle_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_destroy(gfx3d_surface_handle_t handle) {
    bool result;
    gfx3d_call_lock();
    result = handle && gfx3d_try_activate() &&
             g_active->surface_destroy(handle);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_upload(gfx3d_surface_handle_t handle,
                          const uint32_t *pixels, uint32_t source_pitch,
                          const gfx_rect_t *rect) {
    bool result;
    gfx3d_call_lock();
    result = handle && pixels && gfx3d_try_activate() &&
             g_active->surface_upload(handle, pixels, source_pitch, rect);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_upload_region(gfx3d_surface_handle_t handle,
                                 const uint32_t *pixels,
                                 uint32_t source_pitch,
                                 uint32_t destination_x,
                                 uint32_t destination_y,
                                 uint32_t width, uint32_t height) {
    bool result;
    gfx_rect_t destination;
    gfx3d_call_lock();
    if (!handle || !pixels || !source_pitch || !width || !height ||
        !gfx3d_try_activate()) {
        result = false;
    } else if (g_active->surface_upload_region) {
        result = g_active->surface_upload_region(handle, pixels, source_pitch,
            destination_x, destination_y, width, height);
    } else if (destination_x == 0U && destination_y == 0U &&
               g_active->surface_upload) {
        destination = (gfx_rect_t){0, 0, (int32_t)width, (int32_t)height};
        result = g_active->surface_upload(handle, pixels, source_pitch,
                                           &destination);
    } else {
        result = false;
    }
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_download(gfx3d_surface_handle_t handle,
                            uint32_t *pixels, uint32_t destination_pitch,
                            const gfx_rect_t *rect) {
    bool result;
    gfx3d_call_lock();
    result = handle && pixels && gfx3d_try_activate() &&
             g_active->surface_download &&
             g_active->surface_download(handle, pixels, destination_pitch,
                                        rect);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_clear(gfx3d_surface_handle_t handle, uint32_t color,
                         uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = handle && gfx3d_try_activate() && g_active->surface_clear &&
             g_active->surface_clear(handle, color, fence_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_composite(gfx3d_surface_handle_t source,
                             gfx3d_surface_handle_t destination,
                             const gfx3d_composite_t *operation,
                             uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = source && destination && operation && gfx3d_try_activate() &&
             g_active->surface_composite &&
             g_active->surface_composite(source, destination, operation,
                                         fence_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_surface_present(gfx3d_surface_handle_t handle,
                           const gfx_rect_t *rect, uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = handle && gfx3d_try_activate() &&
             g_active->surface_present(handle, rect, fence_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_depth_upload(gfx3d_surface_handle_t target,
                        const uint16_t *depth, uint32_t source_pitch,
                        const gfx_rect_t *rect) {
    bool result;
    gfx3d_call_lock();
    result = target && depth && source_pitch && gfx3d_try_activate() &&
             g_active->depth_upload &&
             g_active->depth_upload(target, depth, source_pitch, rect);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_depth_download(gfx3d_surface_handle_t target,
                          uint16_t *depth, uint32_t destination_pitch,
                          const gfx_rect_t *rect) {
    bool result;
    gfx3d_call_lock();
    result = target && depth && destination_pitch && gfx3d_try_activate() &&
             g_active->depth_download &&
             g_active->depth_download(target, depth, destination_pitch, rect);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_begin(gfx3d_surface_handle_t target, uint32_t clear_color,
                 float clear_depth, uint32_t flags) {
    bool result;
    gfx3d_call_lock();
    result = target && gfx3d_try_activate() && g_active->begin &&
             g_active->begin(target, clear_color, clear_depth, flags);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_draw_triangles(gfx3d_surface_handle_t target,
                          gfx3d_surface_handle_t texture,
                          const gfx3d_vertex_t *vertices,
                          uint32_t vertex_count, uint32_t flags,
                          uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = target && vertices && vertex_count >= 3U &&
             gfx3d_try_activate() &&
             g_active->draw_triangles(target, texture, vertices, vertex_count,
                                      flags, fence_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_end(gfx3d_surface_handle_t target, uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = target && gfx3d_try_activate() && g_active->end &&
             g_active->end(target, fence_out);
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_wait_fence(uint32_t fence) {
    bool result;
    gfx3d_call_lock();
    result = !fence || (gfx3d_try_activate() && g_active->wait_fence &&
                        g_active->wait_fence(fence));
    gfx3d_call_unlock();
    return result;
}

bool gfx3d_selftest(uint32_t *fence_out) {
    bool result;
    if (fence_out) *fence_out = 0U;
    gfx3d_call_lock();
    result = gfx3d_try_activate() && g_active->selftest(fence_out);
    gfx3d_call_unlock();
    return result;
}

void gfx3d_reset(void) {
    gfx3d_call_lock();
    if (g_active && g_active->reset) g_active->reset();
    g_active = NULL;
    kmemset(&g_info, 0, sizeof(g_info));
    gfx3d_call_unlock();
}
