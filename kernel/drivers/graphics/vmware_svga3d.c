#include "../../include/driver.h"
#include "../../include/gfx3d_driver.h"
#include "../../include/svga3d_protocol.h"
#include "../../include/svga_transport.h"
#include "../../include/memory.h"
#include "../../stdio.h"

/*
 * VMware SVGA3D fixed-function extension.
 *
 * This is deliberately a second driver module. VMWARESVGA.DVR owns the PCI
 * device, FIFO and VRAM. SVGA3D.DVR consumes only the private transport
 * ABI and stays dormant when the host does not advertise SVGA_CAP_3D. Thus
 * QEMU keeps the mature 2D path while VMware Workstation/Fusion can enable 3D.
 */

#define SVGA3D_CONTEXT_ID              1U
#define SVGA3D_VERTEX_SURFACE_ID       1U
#define SVGA3D_FIRST_USER_SURFACE_ID  16U
#define SVGA3D_FIRST_DEPTH_SURFACE_ID 64U
#define SVGA3D_MAX_SURFACES           32U
#define SVGA3D_VERTEX_BUFFER_BYTES (128U * 1024U)
#define SVGA3D_VRAM_ALIGNMENT          64U
#define ALIGN_UP(v, a) (((v) + ((a) - 1U)) & ~((a) - 1U))
#define SVGA3D_MAX_DRAW_VERTICES \
    (SVGA3D_VERTEX_BUFFER_BYTES / (uint32_t)sizeof(gfx3d_vertex_t))

typedef struct {
    bool used;
    uint8_t generation;
    uint16_t width;
    uint16_t height;
    gfx3d_format_t format;
    uint32_t flags;
    uint32_t sid;
    uint32_t bytes_per_pixel;
    uint32_t pitch;
    uint32_t size;
    uint32_t vram_handle;
    uint32_t vram_offset;
    uint32_t staging_fence;
    bool staging_valid;

    /* ABI v3: persistent Z16 attachment owned by this render target. */
    bool depth_defined;
    uint32_t depth_sid;
    uint32_t depth_vram_handle;
    uint32_t depth_vram_offset;
    uint32_t depth_pitch;
    uint32_t depth_size;
    uint32_t depth_fence;
} svga3d_surface_t;

typedef struct {
    const bk_svga_transport_ops_t *transport;
    uint32_t transport_generation;
    bool ready;
    bool absence_reported;
    uint32_t host_hw_version;
    uint32_t guest_hw_version;

    uint32_t vertex_vram_handle;
    uint32_t vertex_vram_offset;
    uint32_t vertex_fence;
    bool context_defined;
    bool vertex_surface_defined;

    uint8_t surface_generations[SVGA3D_MAX_SURFACES];
    svga3d_surface_t surfaces[SVGA3D_MAX_SURFACES];
} svga3d_state_t;

static svga3d_state_t g_3d;

static bool svga3d_ensure_ready(void);

static bool transport_active(const bk_svga_transport_ops_t *transport) {
    return transport && transport->is_active &&
           transport->is_active(transport->context);
}

static bool transport_emit(const void *bytes, uint32_t byte_count) {
    return g_3d.transport && g_3d.transport->emit &&
           g_3d.transport->emit(g_3d.transport->context, bytes, byte_count);
}

static bool transport_submit(bool wait, uint32_t *fence_out) {
    return g_3d.transport && g_3d.transport->submit &&
           g_3d.transport->submit(g_3d.transport->context, wait, fence_out);
}

static bool transport_wait(uint32_t fence) {
    return !fence || (g_3d.transport && g_3d.transport->wait_fence &&
           g_3d.transport->wait_fence(g_3d.transport->context, fence));
}

static bool svga3d_emit(uint32_t command_id, const void *payload,
                        uint32_t payload_bytes, const void *tail,
                        uint32_t tail_bytes) {
    uint32_t total = (uint32_t)sizeof(bk_svga3d_header_t) +
                     payload_bytes + tail_bytes;
    uint32_t padded = ALIGN_UP(total, 4U);
    uint8_t *packet;
    bk_svga3d_header_t *header;
    bool ok;

    if (!g_3d.ready || (payload_bytes && !payload) ||
        (tail_bytes && !tail)) return false;
    packet = (uint8_t *)kmalloc(padded);
    if (!packet) return false;
    kmemset(packet, 0, padded);
    header = (bk_svga3d_header_t *)packet;
    header->id = command_id;
    header->size = payload_bytes + tail_bytes;
    if (payload_bytes)
        kmemcpy(packet + sizeof(*header), payload, payload_bytes);
    if (tail_bytes)
        kmemcpy(packet + sizeof(*header) + payload_bytes, tail, tail_bytes);
    ok = transport_emit(packet, padded);
    kfree(packet);
    return ok;
}

static uint32_t svga3d_bytes_per_pixel(gfx3d_format_t format) {
    switch (format) {
        case GFX3D_FORMAT_Z16: return 2U;
        case GFX3D_FORMAT_XRGB8888:
        case GFX3D_FORMAT_ARGB8888: return 4U;
        default: return 0U;
    }
}

static uint32_t svga3d_host_format(gfx3d_format_t format) {
    switch (format) {
        case GFX3D_FORMAT_XRGB8888: return SVGA3D_X8R8G8B8;
        case GFX3D_FORMAT_ARGB8888: return SVGA3D_A8R8G8B8;
        case GFX3D_FORMAT_Z16: return SVGA3D_Z_D16;
        default: return 0U;
    }
}

static uint32_t svga3d_surface_flags_from_desc(
    const gfx3d_surface_desc_t *desc) {
    uint64_t flags = 0U;
    if (desc->flags & GFX3D_SURFACE_DYNAMIC)
        flags |= SVGA3D_SURFACE_HINT_DYNAMIC;
    if (desc->flags & GFX3D_SURFACE_TEXTURE)
        flags |= SVGA3D_SURFACE_HINT_TEXTURE;
    if (desc->flags & GFX3D_SURFACE_RENDER_TARGET)
        flags |= SVGA3D_SURFACE_HINT_RENDERTARGET;
    if (desc->flags & GFX3D_SURFACE_DEPTH)
        flags |= SVGA3D_SURFACE_HINT_DEPTHSTENCIL;
    return (uint32_t)flags;
}

static bool svga3d_define_surface_id(uint32_t sid, uint32_t format,
                                     uint32_t flags, uint32_t width,
                                     uint32_t height) {
    bk_svga3d_define_surface_t command;
    bk_svga3d_size_t size;
    if (!sid || !format || !width || !height) return false;
    kmemset(&command, 0, sizeof(command));
    command.sid = sid;
    command.surface_flags_low = flags;
    command.format = format;
    command.face[0].num_mip_levels = 1U;
    size.width = width;
    size.height = height;
    size.depth = 1U;
    return svga3d_emit(SVGA_3D_CMD_SURFACE_DEFINE, &command,
                       sizeof(command), &size, sizeof(size));
}

static bool svga3d_destroy_surface_id(uint32_t sid) {
    bk_svga3d_destroy_surface_t command;
    if (!sid || sid == SVGA3D_INVALID_ID) return false;
    command.sid = sid;
    return svga3d_emit(SVGA_3D_CMD_SURFACE_DESTROY, &command,
                       sizeof(command), NULL, 0U);
}

static bool svga3d_define_context(void) {
    bk_svga3d_context_t command = {SVGA3D_CONTEXT_ID};
    return svga3d_emit(SVGA_3D_CMD_CONTEXT_DEFINE, &command,
                       sizeof(command), NULL, 0U);
}

static bool svga3d_destroy_context(void) {
    bk_svga3d_context_t command = {SVGA3D_CONTEXT_ID};
    return svga3d_emit(SVGA_3D_CMD_CONTEXT_DESTROY, &command,
                       sizeof(command), NULL, 0U);
}

static bool svga3d_surface_dma(uint32_t sid, uint32_t guest_offset,
                               uint32_t guest_pitch, uint32_t bpp,
                               uint32_t x, uint32_t y, uint32_t width,
                               uint32_t height, uint32_t transfer) {
    bk_svga3d_surface_dma_t command;
    bk_svga3d_copy_box_t box;

    if (!sid || !guest_pitch || !bpp || !width || !height) return false;
    kmemset(&command, 0, sizeof(command));
    kmemset(&box, 0, sizeof(box));
    command.guest.ptr.gmr_id = SVGA_GMR_FRAMEBUFFER;
    command.guest.ptr.offset = guest_offset;
    command.guest.pitch = guest_pitch;
    command.host.sid = sid;
    command.transfer = transfer;

    box.x = x;
    box.y = y;
    box.z = 0U;
    box.w = width;
    box.h = height;
    box.d = 1U;
    box.srcx = x;
    box.srcy = y;
    box.srcz = 0U;
    return svga3d_emit(SVGA_3D_CMD_SURFACE_DMA, &command,
                       sizeof(command), &box, sizeof(box));
}

static bool svga3d_set_render_target(uint32_t sid, uint32_t type) {
    bk_svga3d_set_render_target_t command;
    if (!sid) return false;
    command.cid = SVGA3D_CONTEXT_ID;
    command.type = type;
    command.target.sid = sid;
    command.target.face = 0U;
    command.target.mipmap = 0U;
    return svga3d_emit(SVGA_3D_CMD_SETRENDERTARGET, &command,
                       sizeof(command), NULL, 0U);
}

static bool svga3d_set_rect(uint32_t command_id, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height) {
    bk_svga3d_set_rect_t command;
    command.cid = SVGA3D_CONTEXT_ID;
    command.rect.x = x;
    command.rect.y = y;
    command.rect.w = width;
    command.rect.h = height;
    return svga3d_emit(command_id, &command, sizeof(command), NULL, 0U);
}

static bool svga3d_set_viewport(uint32_t width, uint32_t height) {
    return svga3d_set_rect(SVGA_3D_CMD_SETVIEWPORT, 0U, 0U, width, height) &&
           svga3d_set_rect(SVGA_3D_CMD_SETSCISSORRECT, 0U, 0U,
                           width, height);
}

static bool svga3d_set_scissor(gfx_rect_t clip, uint32_t width,
                               uint32_t height) {
    int32_t x = clip.x, y = clip.y, w = clip.w, h = clip.h;
    if (w <= 0 || h <= 0) return svga3d_set_viewport(width, height);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int32_t)width || y >= (int32_t)height) return false;
    if (x + w > (int32_t)width) w = (int32_t)width - x;
    if (y + h > (int32_t)height) h = (int32_t)height - y;
    if (w <= 0 || h <= 0) return false;
    return svga3d_set_rect(SVGA_3D_CMD_SETSCISSORRECT,
        (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

static uint32_t svga3d_depth_compare(uint32_t flags) {
    /* SVGA3D comparison values follow the D3D fixed-function encoding. */
    switch (gfx3d_draw_depth_func(flags)) {
    case GFX3D_DEPTH_NEVER: return 1U;
    case GFX3D_DEPTH_LESS: return 2U;
    case GFX3D_DEPTH_EQUAL: return 3U;
    case GFX3D_DEPTH_LEQUAL: return 4U;
    case GFX3D_DEPTH_GREATER: return 5U;
    case GFX3D_DEPTH_NOTEQUAL: return 6U;
    case GFX3D_DEPTH_GEQUAL: return 7U;
    default: return 8U;
    }
}

static bool svga3d_set_render_states(uint32_t flags) {
    bk_svga3d_set_render_state_t command;
    bk_svga3d_render_state_t states[11];
    uint32_t count = 0U;
#define ADD_STATE(name_, value_) do { \
        states[count].state = (name_); \
        states[count].value = (value_); \
        count++; \
    } while (0)
    command.cid = SVGA3D_CONTEXT_ID;
    ADD_STATE(SVGA3D_RS_ZENABLE,
              (flags & GFX3D_DRAW_DEPTH_TEST) ? 1U : 0U);
    ADD_STATE(SVGA3D_RS_ZWRITEENABLE,
              (flags & GFX3D_DRAW_DEPTH_WRITE) ? 1U : 0U);
    ADD_STATE(SVGA3D_RS_BLENDENABLE,
              (flags & GFX3D_DRAW_BLEND) ? 1U : 0U);
    ADD_STATE(SVGA3D_RS_LIGHTINGENABLE, 0U);
    ADD_STATE(SVGA3D_RS_SRCBLEND,
              (flags & GFX3D_DRAW_PREMULTIPLIED)
                ? SVGA3D_BLENDOP_ONE : SVGA3D_BLENDOP_SRCALPHA);
    ADD_STATE(SVGA3D_RS_DSTBLEND,
              (flags & GFX3D_DRAW_BLEND_ADDITIVE)
                ? SVGA3D_BLENDOP_ONE : SVGA3D_BLENDOP_INVSRCALPHA);
    ADD_STATE(SVGA3D_RS_BLENDEQUATION, SVGA3D_BLENDEQ_ADD);
    ADD_STATE(SVGA3D_RS_CULLMODE, SVGA3D_FACE_NONE);
    ADD_STATE(SVGA3D_RS_ZFUNC, svga3d_depth_compare(flags));
    ADD_STATE(SVGA3D_RS_COLORWRITEENABLE, SVGA3D_COLORWRITE_ALL);
    ADD_STATE(SVGA3D_RS_SCISSORTESTENABLE, 1U);
#undef ADD_STATE
    return svga3d_emit(SVGA_3D_CMD_SETRENDERSTATE, &command,
                       sizeof(command), states,
                       count * (uint32_t)sizeof(states[0]));
}

static bool svga3d_set_texture(uint32_t sid, uint32_t flags) {
    bk_svga3d_set_texture_state_t command;
    bk_svga3d_texture_state_t states[12];
    uint32_t count = 0U;
    uint32_t filter = (flags & GFX3D_DRAW_LINEAR)
        ? SVGA3D_TEX_FILTER_LINEAR : SVGA3D_TEX_FILTER_NEAREST;
#define ADD_TEX(stage_, name_, value_) do { \
        states[count].stage = (stage_); \
        states[count].name = (name_); \
        states[count].value = (value_); \
        count++; \
    } while (0)
    command.cid = SVGA3D_CONTEXT_ID;
    ADD_TEX(0U, SVGA3D_TS_BIND_TEXTURE, sid ? sid : SVGA3D_INVALID_ID);
    if (sid) {
        ADD_TEX(0U, SVGA3D_TS_COLOROP, SVGA3D_TC_MODULATE);
        ADD_TEX(0U, SVGA3D_TS_COLORARG1, SVGA3D_TA_TEXTURE);
        ADD_TEX(0U, SVGA3D_TS_COLORARG2, SVGA3D_TA_DIFFUSE);
        ADD_TEX(0U, SVGA3D_TS_ALPHAOP, SVGA3D_TC_MODULATE);
        ADD_TEX(0U, SVGA3D_TS_ALPHAARG1, SVGA3D_TA_TEXTURE);
        ADD_TEX(0U, SVGA3D_TS_ALPHAARG2, SVGA3D_TA_DIFFUSE);
        ADD_TEX(0U, SVGA3D_TS_ADDRESSU,
                (flags & GFX3D_DRAW_REPEAT_U)
                    ? SVGA3D_TEX_ADDRESS_WRAP : SVGA3D_TEX_ADDRESS_CLAMP);
        ADD_TEX(0U, SVGA3D_TS_ADDRESSV,
                (flags & GFX3D_DRAW_REPEAT_V)
                    ? SVGA3D_TEX_ADDRESS_WRAP : SVGA3D_TEX_ADDRESS_CLAMP);
        ADD_TEX(0U, SVGA3D_TS_MIPFILTER, SVGA3D_TEX_FILTER_NONE);
        ADD_TEX(0U, SVGA3D_TS_MAGFILTER, filter);
        ADD_TEX(0U, SVGA3D_TS_MINFILTER, filter);
    } else {
        ADD_TEX(0U, SVGA3D_TS_COLOROP, SVGA3D_TC_SELECTARG1);
        ADD_TEX(0U, SVGA3D_TS_COLORARG1, SVGA3D_TA_DIFFUSE);
        ADD_TEX(0U, SVGA3D_TS_ALPHAOP, SVGA3D_TC_SELECTARG1);
        ADD_TEX(0U, SVGA3D_TS_ALPHAARG1, SVGA3D_TA_DIFFUSE);
    }
#undef ADD_TEX
    return svga3d_emit(SVGA_3D_CMD_SETTEXTURESTATE, &command,
                       sizeof(command), states,
                       count * (uint32_t)sizeof(states[0]));
}

static bool svga3d_clear_current(uint32_t color, float depth,
                                 uint32_t flags, uint32_t width,
                                 uint32_t height) {
    bk_svga3d_clear_t command;
    bk_svga3d_rect_t rect;
    uint32_t clear = 0U;
    if (flags & GFX3D_DRAW_CLEAR_COLOR) clear |= SVGA3D_CLEAR_COLOR;
    if (flags & GFX3D_DRAW_CLEAR_DEPTH) clear |= SVGA3D_CLEAR_DEPTH;
    if (!clear) return true;
    command.cid = SVGA3D_CONTEXT_ID;
    command.clear_flags = clear;
    command.color = color;
    command.depth = depth;
    command.stencil = 0U;
    if (!width || !height) return false;
    rect.x = 0U;
    rect.y = 0U;
    rect.w = width;
    rect.h = height;
    return svga3d_emit(SVGA_3D_CMD_CLEAR, &command, sizeof(command),
                       &rect, sizeof(rect));
}

static bool svga3d_define_depth(svga3d_surface_t *target) {
    uint32_t pitch, size;
    if (!target || !(target->flags & GFX3D_SURFACE_RENDER_TARGET)) return false;
    if (target->depth_defined && target->depth_vram_handle) return true;
    pitch = ALIGN_UP((uint32_t)target->width * 2U, 4U);
    size = pitch * (uint32_t)target->height;
    if (!target->depth_sid) return false;
    if (!g_3d.transport->vram_allocate(g_3d.transport->context, size,
            SVGA3D_VRAM_ALIGNMENT, &target->depth_vram_handle,
            &target->depth_vram_offset)) return false;
    if (!svga3d_define_surface_id(target->depth_sid, SVGA3D_Z_D16,
            (uint32_t)SVGA3D_SURFACE_HINT_DEPTHSTENCIL,
            target->width, target->height)) {
        (void)g_3d.transport->vram_free(g_3d.transport->context,
                                        target->depth_vram_handle);
        target->depth_vram_handle = 0U;
        return false;
    }
    target->depth_defined = true;
    target->depth_pitch = pitch;
    target->depth_size = size;
    target->depth_fence = 0U;
    return true;
}

static gfx3d_surface_handle_t svga3d_make_handle(uint32_t slot) {
    return ((uint32_t)g_3d.surfaces[slot].generation << 8) | (slot + 1U);
}

static svga3d_surface_t *svga3d_surface_from_handle(
    gfx3d_surface_handle_t handle) {
    uint32_t slot = handle & 0xFFU;
    uint8_t generation = (uint8_t)(handle >> 8);
    if (!slot || slot > SVGA3D_MAX_SURFACES) return NULL;
    slot--;
    if (!g_3d.surfaces[slot].used ||
        g_3d.surfaces[slot].generation != generation) return NULL;
    return &g_3d.surfaces[slot];
}

static void svga3d_forget_state(void) {
    g_3d.ready = false;
    g_3d.host_hw_version = 0U;
    g_3d.guest_hw_version = 0U;
    g_3d.vertex_vram_handle = 0U;
    g_3d.vertex_vram_offset = 0U;
    g_3d.vertex_fence = 0U;
    g_3d.context_defined = false;
    g_3d.vertex_surface_defined = false;
    kmemset(g_3d.surfaces, 0, sizeof(g_3d.surfaces));
}

static void svga3d_release(bool emit_destroy) {
    const bk_svga_transport_ops_t *transport = g_3d.transport;
    bool same_generation = transport_active(transport) &&
        transport->generation(transport->context) ==
        g_3d.transport_generation;
    bool can_emit = emit_destroy && g_3d.ready && same_generation;

    if (can_emit) {
        for (uint32_t i = 0; i < SVGA3D_MAX_SURFACES; i++)
            if (g_3d.surfaces[i].used) {
                if (g_3d.surfaces[i].depth_defined)
                    (void)svga3d_destroy_surface_id(
                        g_3d.surfaces[i].depth_sid);
                (void)svga3d_destroy_surface_id(g_3d.surfaces[i].sid);
            }
        if (g_3d.vertex_surface_defined)
            (void)svga3d_destroy_surface_id(SVGA3D_VERTEX_SURFACE_ID);
        if (g_3d.context_defined)
            (void)svga3d_destroy_context();
        (void)transport_submit(true, NULL);
    }
    if (same_generation) {
        for (uint32_t i = 0; i < SVGA3D_MAX_SURFACES; i++)
            if (g_3d.surfaces[i].used) {
                if (g_3d.surfaces[i].vram_handle)
                    (void)transport->vram_free(transport->context,
                        g_3d.surfaces[i].vram_handle);
                if (g_3d.surfaces[i].depth_vram_handle)
                    (void)transport->vram_free(transport->context,
                        g_3d.surfaces[i].depth_vram_handle);
            }
        if (g_3d.vertex_vram_handle)
            (void)transport->vram_free(transport->context,
                                       g_3d.vertex_vram_handle);
    }
    svga3d_forget_state();
}

static bool svga3d_ensure_ready(void) {
    const bk_svga_transport_ops_t *transport = svga_transport_get();
    uint32_t generation, register_count, fifo_caps, host_version;

    if (!transport_active(transport)) {
        if (g_3d.ready) svga3d_release(false);
        g_3d.transport = transport;
        return false;
    }
    generation = transport->generation(transport->context);
    if (g_3d.transport != transport ||
        g_3d.transport_generation != generation) {
        if (g_3d.ready) svga3d_release(false);
        g_3d.transport = transport;
        g_3d.transport_generation = generation;
        g_3d.absence_reported = false;
    }
    if (g_3d.ready) return true;
    if (!(transport->device_capabilities(transport->context) & SVGA_CAP_3D)) {
        if (!g_3d.absence_reported) {
            kprintf("[SVGA3D.DVR] host sin SVGA3D; queda activo el driver 2D\n");
            g_3d.absence_reported = true;
        }
        return false;
    }

    register_count = transport->fifo_register_count(transport->context);
    if (register_count <= SVGA_FIFO_3D_HWVERSION) return false;
    fifo_caps = transport->fifo_capabilities(transport->context);
    host_version = 0U;
    if ((fifo_caps & SVGA_FIFO_CAP_3D_HWVERSION_REVISED) &&
        register_count > SVGA_FIFO_3D_HWVERSION_REVISED)
        host_version = transport->fifo_register_read(transport->context,
                         SVGA_FIFO_3D_HWVERSION_REVISED);
    if (!host_version)
        host_version = transport->fifo_register_read(transport->context,
                         SVGA_FIFO_3D_HWVERSION);
    if (host_version < SVGA3D_HWVERSION_WS65_B1) return false;

    /* VMWARESVGA.DVR advertised this version before CONFIG_DONE. */
    g_3d.host_hw_version = host_version;
    g_3d.guest_hw_version = SVGA3D_GUEST_HWVERSION;
    g_3d.ready = true; /* enables command emission during initialization */
    if (!transport->vram_allocate(transport->context,
            SVGA3D_VERTEX_BUFFER_BYTES, SVGA3D_VRAM_ALIGNMENT,
            &g_3d.vertex_vram_handle, &g_3d.vertex_vram_offset)) {
        svga3d_release(true);
        return false;
    }
    if (!svga3d_define_context()) {
        svga3d_release(true);
        return false;
    }
    g_3d.context_defined = true;
    if (!svga3d_define_surface_id(SVGA3D_VERTEX_SURFACE_ID, SVGA3D_BUFFER,
            (uint32_t)(SVGA3D_SURFACE_HINT_VERTEXBUFFER |
                       SVGA3D_SURFACE_HINT_DYNAMIC),
            SVGA3D_VERTEX_BUFFER_BYTES, 1U)) {
        svga3d_release(true);
        return false;
    }
    g_3d.vertex_surface_defined = true;
    if (!transport_submit(true, NULL)) {
        svga3d_release(true);
        return false;
    }
    kprintf("[SVGA3D.DVR] activo sobre %s; host=%x guest=%x\n",
            transport->name, host_version, g_3d.guest_hw_version);
    return true;
}

static bool svga3d_probe(gfx3d_info_t *info) {
    bool available = svga3d_ensure_ready();
    if (info) {
        info->available = available;
        info->driver_name = "vmware_svga3d";
        info->transport_name = g_3d.transport ? g_3d.transport->name
                                              : "ninguno";
        info->capabilities = available ?
            GFX3D_CAP_FIXED_FUNCTION | GFX3D_CAP_RENDER_TARGETS |
            GFX3D_CAP_VERTEX_BUFFERS | GFX3D_CAP_SURFACE_DMA |
            GFX3D_CAP_PRESENT | GFX3D_CAP_ALPHA_BLEND |
            GFX3D_CAP_TEXTURES | GFX3D_CAP_SCALE |
            GFX3D_CAP_TRANSFORM | GFX3D_CAP_WINDOW_SURFACES |
            GFX3D_CAP_GLYPH_ATLAS | GFX3D_CAP_TINYGL |
            GFX3D_CAP_DEPTH_BUFFER | GFX3D_CAP_DEPTH_SURFACE_IO |
            GFX3D_CAP_DEPTH_FUNCS | GFX3D_CAP_BLEND_ADDITIVE |
            GFX3D_CAP_TEXTURE_REGION_UPLOAD : 0U;
        info->host_hw_version = g_3d.host_hw_version;
        info->guest_hw_version = g_3d.guest_hw_version;
        info->transport_generation = g_3d.transport_generation;
    }
    return available;
}

static void svga3d_reset_driver(void) {
    svga3d_release(true);
}

static bool svga3d_create_surface(const gfx3d_surface_desc_t *desc,
                                  gfx3d_surface_handle_t *handle_out) {
    uint32_t slot, format, bpp, pitch, size;
    uint32_t vram_handle = 0U, vram_offset = 0U;
    svga3d_surface_t *surface;

    if (handle_out) *handle_out = GFX3D_SURFACE_INVALID;
    if (!svga3d_ensure_ready() || !desc || !handle_out ||
        !desc->width || !desc->height) return false;
    format = svga3d_host_format(desc->format);
    bpp = svga3d_bytes_per_pixel(desc->format);
    if (!format || !bpp) return false;
    for (slot = 0; slot < SVGA3D_MAX_SURFACES; slot++)
        if (!g_3d.surfaces[slot].used) break;
    if (slot == SVGA3D_MAX_SURFACES) return false;
    pitch = ALIGN_UP((uint32_t)desc->width * bpp, SVGA3D_VRAM_ALIGNMENT);
    size = ALIGN_UP(pitch * desc->height, SVGA3D_VRAM_ALIGNMENT);
    if (!g_3d.transport->vram_allocate(g_3d.transport->context, size,
            SVGA3D_VRAM_ALIGNMENT, &vram_handle, &vram_offset)) return false;

    surface = &g_3d.surfaces[slot];
    if (++g_3d.surface_generations[slot] == 0U)
        g_3d.surface_generations[slot] = 1U;
    kmemset(surface, 0, sizeof(*surface));
    surface->generation = g_3d.surface_generations[slot];
    surface->width = desc->width;
    surface->height = desc->height;
    surface->format = desc->format;
    surface->flags = desc->flags;
    surface->sid = SVGA3D_FIRST_USER_SURFACE_ID + slot;
    surface->depth_sid = SVGA3D_FIRST_DEPTH_SURFACE_ID + slot;
    surface->bytes_per_pixel = bpp;
    surface->pitch = pitch;
    surface->size = size;
    surface->vram_handle = vram_handle;
    surface->vram_offset = vram_offset;
    if (!svga3d_define_surface_id(surface->sid, format,
            svga3d_surface_flags_from_desc(desc),
            desc->width, desc->height)) {
        (void)g_3d.transport->vram_free(g_3d.transport->context, vram_handle);
        kmemset(surface, 0, sizeof(*surface));
        return false;
    }
    if (!transport_submit(true, NULL)) {
        (void)svga3d_destroy_surface_id(surface->sid);
        (void)transport_submit(true, NULL);
        (void)g_3d.transport->vram_free(g_3d.transport->context, vram_handle);
        kmemset(surface, 0, sizeof(*surface));
        return false;
    }
    surface->used = true;
    *handle_out = svga3d_make_handle(slot);
    return true;
}

static bool svga3d_destroy_surface(gfx3d_surface_handle_t handle) {
    svga3d_surface_t *surface;
    if (!svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface) return false;
    if ((surface->staging_fence && !transport_wait(surface->staging_fence)) ||
        (surface->depth_fence && !transport_wait(surface->depth_fence)))
        return false;
    if (surface->depth_defined &&
        !svga3d_destroy_surface_id(surface->depth_sid)) return false;
    if (!svga3d_destroy_surface_id(surface->sid) ||
        !transport_submit(true, NULL)) return false;
    (void)g_3d.transport->vram_free(g_3d.transport->context,
                                    surface->vram_handle);
    if (surface->depth_vram_handle)
        (void)g_3d.transport->vram_free(g_3d.transport->context,
                                        surface->depth_vram_handle);
    surface->used = false;
    return true;
}

static bool svga3d_clip_surface_rect(const svga3d_surface_t *surface,
                                     const gfx_rect_t *requested,
                                     gfx_rect_t *rect_out) {
    gfx_rect_t rect;
    if (!surface || !rect_out) return false;
    rect = requested ? *requested :
        (gfx_rect_t){0, 0, surface->width, surface->height};
    if (rect.x < 0) { rect.w += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.h += rect.y; rect.y = 0; }
    if (rect.x >= surface->width || rect.y >= surface->height) return false;
    if (rect.x + rect.w > surface->width) rect.w = surface->width - rect.x;
    if (rect.y + rect.h > surface->height) rect.h = surface->height - rect.y;
    if (rect.w <= 0 || rect.h <= 0) return false;
    *rect_out = rect;
    return true;
}

static bool svga3d_upload_surface(gfx3d_surface_handle_t handle,
                                  const uint32_t *pixels,
                                  uint32_t source_pitch,
                                  const gfx_rect_t *requested) {
    svga3d_surface_t *surface;
    gfx_rect_t rect;
    uint8_t *destination;
    uint32_t row_bytes, fence = 0U;

    if (!pixels || !source_pitch || !svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface || source_pitch < surface->width ||
        !svga3d_clip_surface_rect(surface, requested, &rect)) return false;
    if (surface->staging_fence && !transport_wait(surface->staging_fence))
        return false;
    destination = (uint8_t *)g_3d.transport->vram_pointer(
        g_3d.transport->context, surface->vram_offset, surface->size);
    if (!destination) return false;
    row_bytes = (uint32_t)rect.w * surface->bytes_per_pixel;
    for (int32_t row = 0; row < rect.h; row++) {
        const uint8_t *source_row = (const uint8_t *)pixels +
            ((uint32_t)(rect.y + row) * source_pitch + (uint32_t)rect.x) *
            surface->bytes_per_pixel;
        uint8_t *destination_row = destination +
            (uint32_t)(rect.y + row) * surface->pitch +
            (uint32_t)rect.x * surface->bytes_per_pixel;
        kmemcpy(destination_row, source_row, row_bytes);
    }
    if (!svga3d_surface_dma(surface->sid, surface->vram_offset,
            surface->pitch, surface->bytes_per_pixel,
            (uint32_t)rect.x, (uint32_t)rect.y,
            (uint32_t)rect.w, (uint32_t)rect.h,
            SVGA3D_WRITE_HOST_VRAM) ||
        !transport_submit(false, &fence)) return false;
    surface->staging_fence = fence;
    surface->staging_valid = true;
    return true;
}

static bool svga3d_upload_surface_region(gfx3d_surface_handle_t handle,
                                         const uint32_t *pixels,
                                         uint32_t source_pitch,
                                         uint32_t destination_x,
                                         uint32_t destination_y,
                                         uint32_t width, uint32_t height) {
    svga3d_surface_t *surface;
    uint8_t *destination;
    uint32_t fence = 0U;
    if (!pixels || !source_pitch || source_pitch < width || !width || !height ||
        !svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface || destination_x + width > surface->width ||
        destination_y + height > surface->height) return false;
    if (surface->staging_fence && !transport_wait(surface->staging_fence))
        return false;
    destination = (uint8_t *)g_3d.transport->vram_pointer(
        g_3d.transport->context, surface->vram_offset, surface->size);
    if (!destination) return false;
    for (uint32_t row = 0U; row < height; row++)
        kmemcpy(destination + (destination_y + row) * surface->pitch +
                    destination_x * surface->bytes_per_pixel,
                (const uint8_t *)pixels + row * source_pitch *
                    surface->bytes_per_pixel,
                width * surface->bytes_per_pixel);
    if (!svga3d_surface_dma(surface->sid, surface->vram_offset,
            surface->pitch, surface->bytes_per_pixel, destination_x,
            destination_y, width, height, SVGA3D_WRITE_HOST_VRAM) ||
        !transport_submit(false, &fence)) return false;
    surface->staging_fence = fence;
    surface->staging_valid = true;
    return true;
}

static bool svga3d_download_surface(gfx3d_surface_handle_t handle,
                                    uint32_t *pixels,
                                    uint32_t destination_pitch,
                                    const gfx_rect_t *requested) {
    svga3d_surface_t *surface;
    gfx_rect_t rect;
    uint8_t *source;
    uint32_t row_bytes;

    if (!pixels || !destination_pitch || !svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface || destination_pitch < surface->width ||
        !svga3d_clip_surface_rect(surface, requested, &rect)) return false;
    if (surface->staging_fence && !transport_wait(surface->staging_fence))
        return false;
    if (!svga3d_surface_dma(surface->sid, surface->vram_offset,
            surface->pitch, surface->bytes_per_pixel,
            (uint32_t)rect.x, (uint32_t)rect.y,
            (uint32_t)rect.w, (uint32_t)rect.h,
            SVGA3D_READ_HOST_VRAM) || !transport_submit(true, NULL))
        return false;
    source = (uint8_t *)g_3d.transport->vram_pointer(
        g_3d.transport->context, surface->vram_offset, surface->size);
    if (!source) return false;
    row_bytes = (uint32_t)rect.w * surface->bytes_per_pixel;
    for (int32_t row = 0; row < rect.h; row++) {
        const uint8_t *source_row = source +
            (uint32_t)(rect.y + row) * surface->pitch +
            (uint32_t)rect.x * surface->bytes_per_pixel;
        uint8_t *destination_row = (uint8_t *)pixels +
            ((uint32_t)(rect.y + row) * destination_pitch +
             (uint32_t)rect.x) * surface->bytes_per_pixel;
        kmemcpy(destination_row, source_row, row_bytes);
    }
    surface->staging_fence = 0U;
    surface->staging_valid = true;
    return true;
}

static bool svga3d_upload_vertices(const gfx3d_vertex_t *vertices,
                                   uint32_t vertex_count) {
    uint32_t bytes = vertex_count * (uint32_t)sizeof(*vertices);
    void *destination;
    if (!vertices || !vertex_count || bytes > SVGA3D_VERTEX_BUFFER_BYTES)
        return false;
    if (g_3d.vertex_fence && !transport_wait(g_3d.vertex_fence)) return false;
    destination = g_3d.transport->vram_pointer(g_3d.transport->context,
        g_3d.vertex_vram_offset, bytes);
    if (!destination) return false;
    kmemcpy(destination, vertices, bytes);
    return svga3d_surface_dma(SVGA3D_VERTEX_SURFACE_ID,
        g_3d.vertex_vram_offset, bytes, 1U, 0U, 0U, bytes, 1U,
        SVGA3D_WRITE_HOST_VRAM);
}

static bool svga3d_emit_draw(const gfx3d_vertex_t *vertices,
                             uint32_t vertex_count, uint32_t texture_sid,
                             uint32_t flags, uint32_t *fence_out) {
    bk_svga3d_draw_primitives_t command;
    struct PACKED {
        bk_svga3d_vertex_decl_t declarations[3];
        bk_svga3d_primitive_range_t range;
    } tail;
    const uint32_t declaration_count = 3U;
    uint32_t stride = sizeof(gfx3d_vertex_t);
    uint32_t fence = 0U;

    if (fence_out) *fence_out = 0U;
    if (!vertices || vertex_count < 3U || vertex_count % 3U ||
        vertex_count > SVGA3D_MAX_DRAW_VERTICES) return false;
    if (!svga3d_upload_vertices(vertices, vertex_count) ||
        !svga3d_set_render_states(flags) ||
        !svga3d_set_texture(texture_sid, flags)) return false;

    kmemset(&tail, 0, sizeof(tail));
    tail.declarations[0].identity.type = SVGA3D_DECLTYPE_FLOAT4;
    tail.declarations[0].identity.method = SVGA3D_DECLMETHOD_DEFAULT;
    tail.declarations[0].identity.usage = SVGA3D_DECLUSAGE_POSITIONT;
    tail.declarations[0].array.surface_id = SVGA3D_VERTEX_SURFACE_ID;
    tail.declarations[0].array.offset = 0U;
    tail.declarations[0].array.stride = stride;
    tail.declarations[0].range_hint.last = vertex_count - 1U;

    tail.declarations[1].identity.type = SVGA3D_DECLTYPE_D3DCOLOR;
    tail.declarations[1].identity.method = SVGA3D_DECLMETHOD_DEFAULT;
    tail.declarations[1].identity.usage = SVGA3D_DECLUSAGE_COLOR;
    tail.declarations[1].array.surface_id = SVGA3D_VERTEX_SURFACE_ID;
    tail.declarations[1].array.offset = 16U;
    tail.declarations[1].array.stride = stride;
    tail.declarations[1].range_hint.last = vertex_count - 1U;

    tail.declarations[2].identity.type = SVGA3D_DECLTYPE_FLOAT2;
    tail.declarations[2].identity.method = SVGA3D_DECLMETHOD_DEFAULT;
    tail.declarations[2].identity.usage = SVGA3D_DECLUSAGE_TEXCOORD;
    tail.declarations[2].array.surface_id = SVGA3D_VERTEX_SURFACE_ID;
    tail.declarations[2].array.offset = 20U;
    tail.declarations[2].array.stride = stride;
    tail.declarations[2].range_hint.last = vertex_count - 1U;
    tail.range.prim_type = SVGA3D_PRIMITIVE_TRIANGLELIST;
    tail.range.primitive_count = vertex_count / 3U;
    tail.range.index_array.surface_id = SVGA3D_INVALID_ID;

    command.cid = SVGA3D_CONTEXT_ID;
    command.num_vertex_decls = declaration_count;
    command.num_ranges = 1U;
    if (!svga3d_emit(SVGA_3D_CMD_DRAW_PRIMITIVES, &command,
            sizeof(command), &tail, (uint32_t)sizeof(tail)) ||
        !transport_submit(false, &fence)) return false;
    g_3d.vertex_fence = fence;
    if (fence_out) *fence_out = fence;
    return true;
}

static bool svga3d_depth_transfer(gfx3d_surface_handle_t target_handle,
                                    uint16_t *depth, uint32_t pitch,
                                    const gfx_rect_t *requested,
                                    bool upload) {
    svga3d_surface_t *target;
    gfx_rect_t rect;
    uint8_t *staging;
    uint32_t fence = 0U;
    if (!depth || !pitch || !svga3d_ensure_ready()) return false;
    target = svga3d_surface_from_handle(target_handle);
    if (!target || pitch < target->width ||
        !svga3d_clip_surface_rect(target, requested, &rect) ||
        !svga3d_define_depth(target)) return false;
    if (target->depth_fence && !transport_wait(target->depth_fence)) return false;
    staging = (uint8_t *)g_3d.transport->vram_pointer(g_3d.transport->context,
        target->depth_vram_offset, target->depth_size);
    if (!staging) return false;
    if (!upload) {
        if (!svga3d_surface_dma(target->depth_sid,
                target->depth_vram_offset, target->depth_pitch, 2U,
                (uint32_t)rect.x, (uint32_t)rect.y,
                (uint32_t)rect.w, (uint32_t)rect.h,
                SVGA3D_READ_HOST_VRAM) || !transport_submit(true, NULL))
            return false;
    }
    for (int32_t row = 0; row < rect.h; row++) {
        uint16_t *gpu = (uint16_t *)(staging +
            (uint32_t)(rect.y + row) * target->depth_pitch) + rect.x;
        uint16_t *cpu = depth + (uint32_t)(rect.y + row) * pitch + rect.x;
        if (upload) kmemcpy(gpu, cpu, (uint32_t)rect.w * 2U);
        else kmemcpy(cpu, gpu, (uint32_t)rect.w * 2U);
    }
    if (upload) {
        if (!svga3d_surface_dma(target->depth_sid,
                target->depth_vram_offset, target->depth_pitch, 2U,
                (uint32_t)rect.x, (uint32_t)rect.y,
                (uint32_t)rect.w, (uint32_t)rect.h,
                SVGA3D_WRITE_HOST_VRAM) ||
            !transport_submit(false, &fence)) return false;
        target->depth_fence = fence;
    }
    return true;
}

static bool svga3d_depth_upload(gfx3d_surface_handle_t target,
                                const uint16_t *depth, uint32_t pitch,
                                const gfx_rect_t *rect) {
    return svga3d_depth_transfer(target, (uint16_t *)depth, pitch, rect, true);
}

static bool svga3d_depth_download(gfx3d_surface_handle_t target,
                                  uint16_t *depth, uint32_t pitch,
                                  const gfx_rect_t *rect) {
    return svga3d_depth_transfer(target, depth, pitch, rect, false);
}

static bool svga3d_clear_surface(gfx3d_surface_handle_t handle,
                                 uint32_t color, uint32_t *fence_out) {
    svga3d_surface_t *surface;
    uint32_t fence = 0U;
    if (fence_out) *fence_out = 0U;
    if (!svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface ||
        !(surface->flags & GFX3D_SURFACE_RENDER_TARGET) ||
        !svga3d_set_render_target(surface->sid, SVGA3D_RT_COLOR0) ||
        !svga3d_set_viewport(surface->width, surface->height) ||
        !svga3d_set_render_states(0U) ||
        !svga3d_clear_current(color, 1.0f, GFX3D_DRAW_CLEAR_COLOR,
                              surface->width, surface->height) ||
        !transport_submit(false, &fence)) return false;
    surface->staging_valid = false;
    if (fence_out) *fence_out = fence;
    return true;
}

static uint8_t svga3d_u8_mul(uint8_t a, uint8_t b) {
    return (uint8_t)(((uint32_t)a * b + 127U) / 255U);
}

static uint32_t svga3d_vertex_color(uint32_t modulation, uint8_t opacity,
                                      bool premultiplied) {
    uint8_t a, r, g, b;
    if (!modulation) modulation = 0xFFFFFFFFU;
    a = svga3d_u8_mul((uint8_t)(modulation >> 24), opacity);
    r = (uint8_t)(modulation >> 16);
    g = (uint8_t)(modulation >> 8);
    b = (uint8_t)modulation;
    if (premultiplied) {
        r = svga3d_u8_mul(r, opacity);
        g = svga3d_u8_mul(g, opacity);
        b = svga3d_u8_mul(b, opacity);
    }
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | b;
}

static bool svga3d_composite_surface(gfx3d_surface_handle_t source_handle,
                                     gfx3d_surface_handle_t dest_handle,
                                     const gfx3d_composite_t *operation,
                                     uint32_t *fence_out) {
    svga3d_surface_t *source;
    svga3d_surface_t *destination;
    gfx3d_vertex_t vertices[6];
    gfx_rect_t src;
    float x0, y0, x1, y1, x2, y2, x3, y3;
    float u0, v0, u1, v1;
    uint32_t color, flags = GFX3D_DRAW_TEXTURED | GFX3D_DRAW_BLEND;

    if (fence_out) *fence_out = 0U;
    if (!operation || !svga3d_ensure_ready()) return false;
    source = svga3d_surface_from_handle(source_handle);
    destination = svga3d_surface_from_handle(dest_handle);
    if (!source || !destination ||
        !(source->flags & GFX3D_SURFACE_TEXTURE) ||
        !(destination->flags & GFX3D_SURFACE_RENDER_TARGET)) return false;
    src = operation->source;
    if (src.w <= 0 || src.h <= 0 || src.x < 0 || src.y < 0 ||
        src.x + src.w > source->width || src.y + src.h > source->height)
        return false;
    if (operation->filter == GFX3D_FILTER_LINEAR)
        flags |= GFX3D_DRAW_LINEAR;
    if (!svga3d_set_render_target(destination->sid, SVGA3D_RT_COLOR0) ||
        !svga3d_set_viewport(destination->width, destination->height) ||
        !svga3d_set_scissor(operation->clip, destination->width,
                            destination->height)) return false;

    x0 = operation->transform.tx;
    y0 = operation->transform.ty;
    x1 = operation->transform.m00 * src.w + operation->transform.tx;
    y1 = operation->transform.m10 * src.w + operation->transform.ty;
    x2 = operation->transform.m00 * src.w +
         operation->transform.m01 * src.h + operation->transform.tx;
    y2 = operation->transform.m10 * src.w +
         operation->transform.m11 * src.h + operation->transform.ty;
    x3 = operation->transform.m01 * src.h + operation->transform.tx;
    y3 = operation->transform.m11 * src.h + operation->transform.ty;
    u0 = (float)src.x / source->width;
    v0 = (float)src.y / source->height;
    u1 = (float)(src.x + src.w) / source->width;
    v1 = (float)(src.y + src.h) / source->height;
    if (operation->source_premultiplied)
        flags |= GFX3D_DRAW_PREMULTIPLIED;
    color = svga3d_vertex_color(operation->modulation_color,
                                operation->opacity,
                                operation->source_premultiplied);
#define SET_VERTEX(index_, px_, py_, pu_, pv_) do { \
        vertices[index_].x = (px_); vertices[index_].y = (py_); \
        vertices[index_].z = 0.0f; vertices[index_].rhw = 1.0f; \
        vertices[index_].color = color; \
        vertices[index_].u = (pu_); vertices[index_].v = (pv_); \
    } while (0)
    SET_VERTEX(0, x0, y0, u0, v0); SET_VERTEX(1, x1, y1, u1, v0);
    SET_VERTEX(2, x2, y2, u1, v1); SET_VERTEX(3, x0, y0, u0, v0);
    SET_VERTEX(4, x2, y2, u1, v1); SET_VERTEX(5, x3, y3, u0, v1);
#undef SET_VERTEX
    destination->staging_valid = false;
    return svga3d_emit_draw(vertices, 6U, source->sid, flags, fence_out);
}

static bool svga3d_present_surface(gfx3d_surface_handle_t handle,
                                   const gfx_rect_t *requested,
                                   uint32_t *fence_out) {
    svga3d_surface_t *surface;
    bk_svga3d_present_t command;
    bk_svga3d_copy_rect_t rect;
    gfx_rect_t clipped;
    uint32_t fence = 0U;

    if (fence_out) *fence_out = 0U;
    if (!svga3d_ensure_ready()) return false;
    surface = svga3d_surface_from_handle(handle);
    if (!surface ||
        !svga3d_clip_surface_rect(surface, requested, &clipped)) return false;
    command.sid = surface->sid;
    rect.x = (uint32_t)clipped.x;
    rect.y = (uint32_t)clipped.y;
    rect.w = (uint32_t)clipped.w;
    rect.h = (uint32_t)clipped.h;
    rect.srcx = rect.x;
    rect.srcy = rect.y;
    if (!svga3d_emit(SVGA_3D_CMD_PRESENT, &command, sizeof(command),
                     &rect, sizeof(rect)) ||
        !transport_submit(false, &fence)) return false;
    if (fence_out) *fence_out = fence;
    return true;
}

static bool svga3d_begin_render(gfx3d_surface_handle_t target_handle,
                                uint32_t clear_color, float clear_depth,
                                uint32_t flags) {
    svga3d_surface_t *target;
    if (!svga3d_ensure_ready()) return false;
    target = svga3d_surface_from_handle(target_handle);
    if (!target ||
        !(target->flags & GFX3D_SURFACE_RENDER_TARGET) ||
        !svga3d_set_render_target(target->sid, SVGA3D_RT_COLOR0) ||
        !svga3d_set_viewport(target->width, target->height)) return false;
    if (flags & (GFX3D_DRAW_DEPTH_TEST | GFX3D_DRAW_DEPTH_WRITE |
                 GFX3D_DRAW_CLEAR_DEPTH)) {
        if (!svga3d_define_depth(target) ||
            !svga3d_set_render_target(target->depth_sid,
                                      SVGA3D_RT_DEPTH)) return false;
    }
    if (!svga3d_set_render_states(flags) ||
        !svga3d_clear_current(clear_color, clear_depth, flags,
                              target->width, target->height)) return false;
    target->staging_valid = false;
    return true;
}

static bool svga3d_draw_triangles(gfx3d_surface_handle_t target_handle,
                                  gfx3d_surface_handle_t texture_handle,
                                  const gfx3d_vertex_t *vertices,
                                  uint32_t vertex_count, uint32_t flags,
                                  uint32_t *fence_out) {
    svga3d_surface_t *target;
    svga3d_surface_t *texture;
    uint32_t offset = 0U, last_fence = 0U;

    if (fence_out) *fence_out = 0U;
    if (!vertices || vertex_count < 3U || vertex_count % 3U ||
        !svga3d_ensure_ready()) return false;
    target = svga3d_surface_from_handle(target_handle);
    texture = texture_handle ? svga3d_surface_from_handle(texture_handle) : NULL;
    if (!target || (texture_handle && !texture)) return false;
    if (texture && !(texture->flags & GFX3D_SURFACE_TEXTURE)) return false;
    if (!svga3d_set_render_target(target->sid, SVGA3D_RT_COLOR0) ||
        !svga3d_set_viewport(target->width, target->height)) return false;
    while (offset < vertex_count) {
        uint32_t count = vertex_count - offset;
        if (count > SVGA3D_MAX_DRAW_VERTICES)
            count = SVGA3D_MAX_DRAW_VERTICES -
                    (SVGA3D_MAX_DRAW_VERTICES % 3U);
        if (!svga3d_emit_draw(vertices + offset, count,
                texture ? texture->sid : 0U,
                flags | (texture ? GFX3D_DRAW_TEXTURED : 0U),
                &last_fence)) return false;
        offset += count;
    }
    target->staging_valid = false;
    if (fence_out) *fence_out = last_fence;
    return true;
}

static bool svga3d_end_render(gfx3d_surface_handle_t target_handle,
                              uint32_t *fence_out) {
    svga3d_surface_t *target;
    if (fence_out) *fence_out = 0U;
    if (!svga3d_ensure_ready()) return false;
    target = svga3d_surface_from_handle(target_handle);
    if (!target) return false;
    target->staging_valid = false;
    return transport_submit(false, fence_out);
}

static bool svga3d_wait_driver_fence(uint32_t fence) {
    return svga3d_ensure_ready() && transport_wait(fence);
}

static bool svga3d_run_selftest(uint32_t *fence_out) {
    gfx3d_surface_desc_t desc;
    gfx3d_surface_handle_t surface = GFX3D_SURFACE_INVALID;
    gfx3d_vertex_t triangle[3];
    uint32_t width = 0U, height = 0U, fence = 0U;
    bool ok = false;

    if (fence_out) *fence_out = 0U;
    if (!svga3d_ensure_ready() ||
        !g_3d.transport->display_info(g_3d.transport->context,
                                     &width, &height, NULL, NULL) ||
        !width || !height || width > 65535U || height > 65535U) return false;
    desc.width = (uint16_t)width;
    desc.height = (uint16_t)height;
    desc.format = GFX3D_FORMAT_ARGB8888;
    desc.flags = GFX3D_SURFACE_RENDER_TARGET | GFX3D_SURFACE_TEXTURE |
                 GFX3D_SURFACE_DYNAMIC;
    if (!svga3d_create_surface(&desc, &surface)) return false;
    triangle[0] = (gfx3d_vertex_t){width * 0.50f, height * 0.15f,
        0.5f, 1.0f, 0xFFFF4040U, 0.0f, 0.0f};
    triangle[1] = (gfx3d_vertex_t){width * 0.20f, height * 0.80f,
        0.5f, 1.0f, 0xFF40FF40U, 0.0f, 0.0f};
    triangle[2] = (gfx3d_vertex_t){width * 0.80f, height * 0.80f,
        0.5f, 1.0f, 0xFF4080FFU, 0.0f, 0.0f};
    ok = svga3d_begin_render(surface, 0xFF203040U, 1.0f,
            GFX3D_DRAW_CLEAR_COLOR) &&
         svga3d_draw_triangles(surface, GFX3D_SURFACE_INVALID,
            triangle, 3U, 0U, NULL) &&
         svga3d_end_render(surface, NULL) &&
         svga3d_present_surface(surface, NULL, &fence);
    if (ok && fence) ok = transport_wait(fence);
    (void)svga3d_destroy_surface(surface);
    if (ok && fence_out) *fence_out = fence;
    return ok;
}

static bool vmware_svga3d_driver_init(void) {
    static const gfx3d_driver_ops_t ops = {
        BK_GFX3D_DRIVER_ABI_VERSION,
        sizeof(gfx3d_driver_ops_t),
        "vmware_svga3d",
        300U,
        GFX3D_CAP_FIXED_FUNCTION | GFX3D_CAP_RENDER_TARGETS |
        GFX3D_CAP_VERTEX_BUFFERS | GFX3D_CAP_SURFACE_DMA |
        GFX3D_CAP_PRESENT | GFX3D_CAP_ALPHA_BLEND |
        GFX3D_CAP_TEXTURES | GFX3D_CAP_SCALE | GFX3D_CAP_TRANSFORM |
        GFX3D_CAP_WINDOW_SURFACES | GFX3D_CAP_GLYPH_ATLAS |
        GFX3D_CAP_TINYGL | GFX3D_CAP_DEPTH_BUFFER |
        GFX3D_CAP_DEPTH_SURFACE_IO | GFX3D_CAP_DEPTH_FUNCS |
        GFX3D_CAP_BLEND_ADDITIVE | GFX3D_CAP_TEXTURE_REGION_UPLOAD,
        svga3d_probe,
        svga3d_reset_driver,
        svga3d_create_surface,
        svga3d_destroy_surface,
        svga3d_upload_surface,
        svga3d_download_surface,
        svga3d_clear_surface,
        svga3d_composite_surface,
        svga3d_present_surface,
        svga3d_begin_render,
        svga3d_draw_triangles,
        svga3d_end_render,
        svga3d_wait_driver_fence,
        svga3d_run_selftest,
        svga3d_upload_surface_region,
        svga3d_depth_upload,
        svga3d_depth_download
    };
    if (!gfx3d_register_driver(&ops)) return false;
    kprintf("[SVGA3D.DVR] extension 3D registrada; activacion por capacidades\n");
    return true;
}

static void vmware_svga3d_driver_shutdown(void) {
    svga3d_release(true);
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "vmware_svga3d",
        "VMware SVGA3D: surfaces, composición, alfa y TinyGL",
        vmware_svga3d_driver_init,
        vmware_svga3d_driver_shutdown
    };
    return &module;
}
