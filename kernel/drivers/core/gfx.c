#include "../../include/gfx.h"
#include "../../include/gfx_driver.h"
#include "../../include/gfx3d.h"
#include "../../include/gfx_vga.h"
#include "../../include/vesa.h"
#include "../../include/memory.h"
#include "../../include/compat_mode.h"
#include "../../include/vga.h"

static gfx_info_t g_gfx;
static const gfx_driver_ops_t *g_registered_driver;
static const gfx_driver_ops_t *g_active_driver;
static uint32_t g_last_fence;
static uint32_t g_driver_generation = 1U;

typedef struct {
    uint32_t hw_fill;
    uint32_t sw_fill;
    uint32_t hw_blit;
    uint32_t sw_blit;
    uint32_t present_calls;
    uint32_t present_cpu_bytes;
    uint32_t driver_failures;
} gfx_perf_stats_t;

static gfx_perf_stats_t g_gfx_perf;

static void gfx_perf_reset(void) {
    g_gfx_perf = (gfx_perf_stats_t){0};
}

static void gfx_advance_driver_generation(void) {
    g_driver_generation++;
    if (!g_driver_generation) g_driver_generation = 1U;
}

static uint16_t gfx_bootinfo_read16(uint32_t offset) {
    return mm_boot_vesa_read16(offset);
}

static uint32_t gfx_bootinfo_read32(uint32_t offset) {
    return mm_boot_vesa_read32(offset);
}

static uint32_t gfx_palette_rgb(uint8_t color) {
    static const uint32_t palette[16] = {
        0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
        0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
        0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
        0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF
    };
    return palette[color & 0x0F];
}

static uint8_t gfx_rgb_to_332(uint32_t rgb) {
    return (uint8_t)(((rgb >> 16) & 0xE0U) |
                     ((rgb >> 11) & 0x1CU) |
                     ((rgb >> 6) & 0x03U));
}

static const uint8_t font8x8_digits[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
};

static const uint8_t font8x8_upper[26][8] = {
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
};

static const uint8_t *glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return font8x8_upper[c - 'A'];
    if (c >= '0' && c <= '9') return font8x8_digits[c - '0'];
    switch (c) {
        case ' ': { static const uint8_t g[8] = {0,0,0,0,0,0,0,0}; return g; }
        case '.': { static const uint8_t g[8] = {0,0,0,0,0,0x18,0x18,0}; return g; }
        case ':': { static const uint8_t g[8] = {0,0x18,0x18,0,0,0x18,0x18,0}; return g; }
        case '/': { static const uint8_t g[8] = {0x06,0x0C,0x18,0x30,0x60,0,0,0}; return g; }
        case '-': { static const uint8_t g[8] = {0,0,0,0x7E,0,0,0,0}; return g; }
        default: { static const uint8_t g[8] = {0x7E,0x42,0x0C,0x18,0x18,0,0x18,0}; return g; }
    }
}

static void gfx_clip_rect(int *x, int *y, int *w, int *h) {
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > g_gfx.width) *w = g_gfx.width - *x;
    if (*y + *h > g_gfx.height) *h = g_gfx.height - *y;
}

static void gfx_disable_active_driver(void) {
    bool had_driver = g_active_driver != NULL;
    gfx3d_reset();
    if (g_active_driver && g_active_driver->disable)
        g_active_driver->disable();
    g_active_driver = NULL;
    g_last_fence = 0;
    gfx_perf_reset();
    if (had_driver) gfx_advance_driver_generation();
}

bool gfx_register_driver(const gfx_driver_ops_t *ops) {
    if (!ops) {
        kprintf("[GFX:TRACE] REGISTER FAIL descriptor=NULL\n");
        return false;
    }
    kprintf("[GFX:TRACE] REGISTER request ops=%x abi=%u size=%u name_ptr=%x priority=%u caps=%x\n",
            (uint32_t)(uintptr_t)ops, ops->abi_version, ops->descriptor_size,
            (uint32_t)(uintptr_t)ops->name, ops->priority, ops->capabilities);
    if (ops->abi_version != BK_GFX_DRIVER_ABI_VERSION) {
        kprintf("[GFX:TRACE] REGISTER FAIL ABI got=%u expected=%u\n",
                ops->abi_version, BK_GFX_DRIVER_ABI_VERSION);
        return false;
    }
    if (ops->descriptor_size != sizeof(*ops)) {
        kprintf("[GFX:TRACE] REGISTER FAIL size got=%u expected=%u\n",
                ops->descriptor_size, (uint32_t)sizeof(*ops));
        return false;
    }
    if (!ops->name || !ops->activate || !ops->list_modes || !ops->set_mode ||
        !ops->present_buffer || !ops->update_rect || !ops->flush ||
        !ops->wait_fence) {
        kprintf("[GFX:TRACE] REGISTER FAIL callbacks name=%x activate=%x list=%x set=%x present=%x update=%x flush=%x wait=%x\n",
                (uint32_t)(uintptr_t)ops->name,
                (uint32_t)(uintptr_t)ops->activate,
                (uint32_t)(uintptr_t)ops->list_modes,
                (uint32_t)(uintptr_t)ops->set_mode,
                (uint32_t)(uintptr_t)ops->present_buffer,
                (uint32_t)(uintptr_t)ops->update_rect,
                (uint32_t)(uintptr_t)ops->flush,
                (uint32_t)(uintptr_t)ops->wait_fence);
        return false;
    }
    if (!g_registered_driver || ops->priority > g_registered_driver->priority) {
        kprintf("[GFX:TRACE] REGISTER SELECT name=%s priority=%u previous=%s previous_priority=%u\n",
                ops->name, ops->priority,
                g_registered_driver ? g_registered_driver->name : "(ninguno)",
                g_registered_driver ? g_registered_driver->priority : 0U);
        g_registered_driver = ops;
    } else {
        kprintf("[GFX:TRACE] REGISTER KEEP name=%s priority=%u selected=%s selected_priority=%u\n",
                ops->name, ops->priority, g_registered_driver->name,
                g_registered_driver->priority);
    }
    return true;
}

void gfx_init(void) {
    uint32_t magic;

    g_gfx.mode = GFX_MODE_TEXT;
    g_gfx.framebuffer = 0x000B8000;
    g_gfx.width = 80;
    g_gfx.height = 25;
    g_gfx.pitch = 160;
    g_gfx.bpp = 16;

    magic = gfx_bootinfo_read32(0);
    kprintf("[GFX:TRACE] INIT boot_magic=%x registered=%s ops=%x\n", magic,
            g_registered_driver ? g_registered_driver->name : "(ninguno)",
            (uint32_t)(uintptr_t)g_registered_driver);

    if (magic == VGA_BOOTINFO_MAGIC) {
        uint32_t requested = gfx_bootinfo_read32(4);
        gfx_disable_active_driver();
        if (requested == VGA_BOOTINFO_13H) {
            if (!gfx_vga_set_mode13h(&g_gfx)) gfx_vga_set_text_mode(&g_gfx);
            return;
        }
        if (requested == VGA_BOOTINFO_12H) {
            if (!gfx_vga_set_mode12h(&g_gfx)) gfx_vga_set_text_mode(&g_gfx);
            return;
        }
        gfx_vga_set_text_mode(&g_gfx);
        return;
    }

    if (magic == VESA_BOOTINFO_MAGIC && g_registered_driver) {
        uint16_t preferred_width = gfx_bootinfo_read16(8);
        uint16_t preferred_height = gfx_bootinfo_read16(10);
        /* Elegir el escritorio amplio antes de activar el controlador. De
           este modo SVGA/VirtIO crean directamente el scanout definitivo y
           la GUI nunca nace en 640x480 para cambiar en mitad del arranque. */
        if (compat_mode_prefer_800x600()) {
            preferred_width = 800U;
            preferred_height = 600U;
        }
        kprintf("[GFX:TRACE] ACTIVATE call name=%s preferred=%ux%u\n",
                g_registered_driver->name, (uint32_t)preferred_width,
                (uint32_t)preferred_height);
        if (g_registered_driver->activate(&g_gfx, preferred_width,
                                          preferred_height)) {
            g_active_driver = g_registered_driver;
            gfx_perf_reset();
            gfx_advance_driver_generation();
            kprintf("[GFX:TRACE] ACTIVATE OK name=%s mode=%ux%ux%u pitch=%u fb=%x\n",
                    g_active_driver->name, (uint32_t)g_gfx.width,
                    (uint32_t)g_gfx.height, (uint32_t)g_gfx.bpp,
                    (uint32_t)g_gfx.pitch, g_gfx.framebuffer);
            return;
        }
        kprintf("[GFX:TRACE] ACTIVATE FAIL name=%s; fallback VESA\n",
                g_registered_driver->name);
    } else if (magic != VESA_BOOTINFO_MAGIC) {
        kprintf("[GFX:TRACE] no VESA bootinfo; magic=%x\n", magic);
    } else {
        kprintf("[GFX:TRACE] VESA bootinfo presente pero no hay driver registrado\n");
    }

    g_active_driver = NULL;
    if (vesa_init_from_bootinfo(&g_gfx))
        kprintf("[GFX:TRACE] FALLBACK VESA OK mode=%ux%ux%u pitch=%u fb=%x\n",
                (uint32_t)g_gfx.width, (uint32_t)g_gfx.height,
                (uint32_t)g_gfx.bpp, (uint32_t)g_gfx.pitch, g_gfx.framebuffer);
    else
        kprintf("[GFX:TRACE] FALLBACK VESA FAIL\n");
}

video_type_t gfx_detect_video_type(void) {
    return (video_type_t)(mm_boot_equipment_word() & 0x30U);
}

const char *gfx_video_type_name(video_type_t type) {
    switch (type) {
        case VIDEO_TYPE_COLOUR: return "colour";
        case VIDEO_TYPE_MONOCHROME: return "monochrome";
        case VIDEO_TYPE_NONE: return "none";
        default: return "unknown";
    }
}

const gfx_info_t *gfx_get_info(void) { return &g_gfx; }
const char *gfx_driver_name(void) {
    if (g_active_driver) return g_active_driver->name;
    return g_gfx.mode == GFX_MODE_VESA_LFB ? "vesa" : "gfx/vga";
}
uint32_t gfx_driver_capabilities(void) {
    if (!g_active_driver) return 0U;
    if (g_active_driver->get_capabilities)
        return g_active_driver->get_capabilities();
    return g_active_driver->capabilities;
}
uint32_t gfx_driver_generation(void) { return g_driver_generation; }

bool gfx_set_text_mode(void) {
    gfx_disable_active_driver();
    return gfx_vga_set_text_mode(&g_gfx);
}
bool gfx_set_mode13h(void) {
    gfx_disable_active_driver();
    return gfx_vga_set_mode13h(&g_gfx);
}
bool gfx_set_mode12h(void) {
    gfx_disable_active_driver();
    return gfx_vga_set_mode12h(&g_gfx);
}

bool gfx_attach_vesa_lfb(uint32_t framebuffer, uint16_t width,
                         uint16_t height, uint16_t pitch, uint8_t bpp) {
    gfx_disable_active_driver();
    return vesa_attach_lfb(&g_gfx, framebuffer, width, height, pitch, bpp);
}
bool gfx_has_vesa_lfb(void) { return vesa_has_lfb(); }
bool gfx_is_linear_framebuffer(void) {
    return g_gfx.mode == GFX_MODE_VESA_LFB || g_active_driver != NULL;
}
bool gfx_can_change_mode(void) {
    /* BlesKernOS 0.8 keeps one display geometry for the lifetime of the GUI.
       Replacing the scanout/backbuffer under live windows was the source of
       disappearing Deskbar/windows and half-applied resolution changes. */
    return false;
}

bool gfx_list_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                            uint32_t *count) {
    (void)modes;
    (void)max_modes;
    if (count) *count = 0U;
    return false;
}
bool gfx_list_all_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                                uint32_t *count) {
    (void)modes;
    (void)max_modes;
    if (count) *count = 0U;
    return false;
}
bool gfx_set_display_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    /* Public/runtime mode changes are intentionally disabled.  A request for
       the already-active mode is a harmless no-op so old software that merely
       reasserts the current mode keeps working. */
    if (width == g_gfx.width && height == g_gfx.height &&
        (!bpp || bpp == g_gfx.bpp))
        return true;
    kprintf("[GFX:MODE] cambio en caliente bloqueado: %ux%ux%u; activo=%ux%ux%u\n",
            (uint32_t)width, (uint32_t)height, (uint32_t)bpp,
            (uint32_t)g_gfx.width, (uint32_t)g_gfx.height,
            (uint32_t)g_gfx.bpp);
    return false;
}

bool gfx_enable_page_flip(void) {
    return !g_active_driver && g_gfx.mode == GFX_MODE_VESA_LFB &&
           vesa_enable_page_flip(&g_gfx);
}
uint32_t gfx_page_flip_draw_buffer(void) {
    if (g_active_driver || g_gfx.mode != GFX_MODE_VESA_LFB) return 0U;
    return vesa_page_flip_draw_buffer(&g_gfx);
}
bool gfx_page_flip_commit(void) {
    return !g_active_driver && g_gfx.mode == GFX_MODE_VESA_LFB &&
           vesa_page_flip_commit(&g_gfx);
}

bool gfx_present_buffer(const uint32_t *pixels, uint32_t source_pitch,
                        const gfx_rect_t *rects, uint32_t rect_count,
                        uint32_t *fence_out) {
    uint32_t fence = 0;
    uint32_t caps;
    bool ok;
    if (!g_active_driver || !pixels || !rects || !rect_count) return false;
    ok = g_active_driver->present_buffer(&g_gfx, pixels, source_pitch,
                                         rects, rect_count, &fence);
    if (ok) {
        caps = gfx_driver_capabilities();
        if (fence) g_last_fence = fence;
        g_gfx_perf.present_calls++;
        if (caps & GFX_CAP_PRESENT_VRAM_BLIT)
            g_gfx_perf.hw_blit += rect_count;
        if (caps & GFX_CAP_PRESENT_CPU_COPY) {
            for (uint32_t i = 0; i < rect_count; i++) {
                if (rects[i].w > 0 && rects[i].h > 0)
                    g_gfx_perf.present_cpu_bytes +=
                        (uint32_t)rects[i].w * (uint32_t)rects[i].h *
                        (g_gfx.bpp == 8U ? 1U : 4U);
            }
        }
    } else {
        g_gfx_perf.driver_failures++;
    }
    if (fence_out) *fence_out = fence;
    return ok;
}

bool gfx_present_rect(int x, int y, int w, int h) {
    if (!g_active_driver) return true;
    return g_active_driver->update_rect(x, y, w, h);
}
bool gfx_flush(void) {
    uint32_t fence = 0;
    if (!g_active_driver) return true;
    if (!g_active_driver->flush(&fence)) {
        g_gfx_perf.driver_failures++;
        return false;
    }
    if (fence) g_last_fence = fence;
    return true;
}
uint32_t gfx_last_fence(void) { return g_last_fence; }
bool gfx_wait_fence(uint32_t fence) {
    if (!g_active_driver || !fence) return true;
    return g_active_driver->wait_fence(fence);
}

static bool gfx_clip_blit_args(int *src_x, int *src_y,
                               int *dst_x, int *dst_y,
                               int *w, int *h) {
    if (!src_x || !src_y || !dst_x || !dst_y || !w || !h ||
        *w <= 0 || *h <= 0)
        return false;
    if (*src_x < 0) { int d = -*src_x; *src_x = 0; *dst_x += d; *w -= d; }
    if (*src_y < 0) { int d = -*src_y; *src_y = 0; *dst_y += d; *h -= d; }
    if (*dst_x < 0) { int d = -*dst_x; *dst_x = 0; *src_x += d; *w -= d; }
    if (*dst_y < 0) { int d = -*dst_y; *dst_y = 0; *src_y += d; *h -= d; }
    if (*src_x + *w > g_gfx.width) *w = g_gfx.width - *src_x;
    if (*dst_x + *w > g_gfx.width) *w = g_gfx.width - *dst_x;
    if (*src_y + *h > g_gfx.height) *h = g_gfx.height - *src_y;
    if (*dst_y + *h > g_gfx.height) *h = g_gfx.height - *dst_y;
    return *w > 0 && *h > 0;
}

static void gfx_memmove_fast(void *destination, const void *source,
                             size_t bytes) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (!bytes || dst == src) return;

    if (dst < src || dst >= src + bytes) {
        if ((((uintptr_t)dst | (uintptr_t)src | bytes) & 3U) == 0U) {
            uint32_t *d32 = (uint32_t *)(void *)dst;
            const uint32_t *s32 = (const uint32_t *)(const void *)src;
            for (size_t i = 0; i < bytes / 4U; i++) d32[i] = s32[i];
        } else {
            for (size_t i = 0; i < bytes; i++) dst[i] = src[i];
        }
        return;
    }

    if ((((uintptr_t)dst | (uintptr_t)src | bytes) & 3U) == 0U) {
        uint32_t *d32 = (uint32_t *)(void *)dst;
        const uint32_t *s32 = (const uint32_t *)(const void *)src;
        size_t count = bytes / 4U;
        while (count--) d32[count] = s32[count];
    } else {
        while (bytes--) dst[bytes] = src[bytes];
    }
}

static uint32_t gfx_apply_rop(uint32_t source, uint32_t destination,
                              gfx_rop_t rop, uint32_t mask) {
    source &= mask;
    destination &= mask;
    switch (rop) {
        case GFX_ROP_XOR: return (source ^ destination) & mask;
        case GFX_ROP_AND: return (source & destination) & mask;
        case GFX_ROP_OR: return (source | destination) & mask;
        case GFX_ROP_INVERT: return (~destination) & mask;
        case GFX_ROP_COPY:
        default: return source;
    }
}

static bool gfx_software_bitblt(int src_x, int src_y, int dst_x, int dst_y,
                                int w, int h, gfx_rop_t rop) {
    int row_start;
    int row_end;
    int row_step;

    if (!gfx_clip_blit_args(&src_x, &src_y, &dst_x, &dst_y, &w, &h) ||
        (g_gfx.bpp != 8U && g_gfx.bpp != 32U))
        return false;

    row_start = dst_y > src_y ? h - 1 : 0;
    row_end = dst_y > src_y ? -1 : h;
    row_step = dst_y > src_y ? -1 : 1;

    for (int row = row_start; row != row_end; row += row_step) {
        if (g_gfx.bpp == 8U) {
            uint8_t *src = (uint8_t *)(uintptr_t)
                (g_gfx.framebuffer + (uint32_t)(src_y + row) * g_gfx.pitch) + src_x;
            uint8_t *dst = (uint8_t *)(uintptr_t)
                (g_gfx.framebuffer + (uint32_t)(dst_y + row) * g_gfx.pitch) + dst_x;
            if (rop == GFX_ROP_COPY) {
                gfx_memmove_fast(dst, src, (size_t)w);
            } else if (dst_x > src_x && dst_x < src_x + w) {
                for (int col = w - 1; col >= 0; col--)
                    dst[col] = (uint8_t)gfx_apply_rop(src[col], dst[col], rop,
                                                      0xFFU);
            } else {
                for (int col = 0; col < w; col++)
                    dst[col] = (uint8_t)gfx_apply_rop(src[col], dst[col], rop,
                                                      0xFFU);
            }
        } else {
            uint32_t *src = (uint32_t *)(uintptr_t)
                (g_gfx.framebuffer + (uint32_t)(src_y + row) * g_gfx.pitch) + src_x;
            uint32_t *dst = (uint32_t *)(uintptr_t)
                (g_gfx.framebuffer + (uint32_t)(dst_y + row) * g_gfx.pitch) + dst_x;
            if (rop == GFX_ROP_COPY) {
                gfx_memmove_fast(dst, src, (size_t)w * sizeof(uint32_t));
            } else if (dst_x > src_x && dst_x < src_x + w) {
                for (int col = w - 1; col >= 0; col--)
                    dst[col] = gfx_apply_rop(src[col], dst[col], rop,
                                             0x00FFFFFFU);
            } else {
                for (int col = 0; col < w; col++)
                    dst[col] = gfx_apply_rop(src[col], dst[col], rop,
                                             0x00FFFFFFU);
            }
        }
    }
    __asm__ volatile ("" ::: "memory");
    return true;
}

bool gfx_bitblt(int src_x, int src_y, int dst_x, int dst_y,
                int w, int h, gfx_rop_t rop, uint32_t *fence_out) {
    uint32_t fence = 0;
    bool ok;
    uint32_t caps = gfx_driver_capabilities();
    if (g_active_driver && g_active_driver->bitblt) {
        ok = g_active_driver->bitblt(&g_gfx, src_x, src_y, dst_x, dst_y,
                                     w, h, rop, &fence);
        if (ok) {
            bool hardware = rop == GFX_ROP_COPY
                ? (caps & GFX_CAP_BITBLT_COPY_HW) != 0U
                : (caps & GFX_CAP_BITBLT_ROP_HW) != 0U;
            if (hardware) g_gfx_perf.hw_blit++;
            else g_gfx_perf.sw_blit++;
            if (fence) g_last_fence = fence;
            if (fence_out) *fence_out = fence;
            return true;
        }
        g_gfx_perf.driver_failures++;
    }

    /* El driver rechazó el comando o desactivó su motor. Sincronizar antes
     * de tocar VRAM y ejecutar un fallback explícito, visible en estadísticas. */
    (void)gfx_flush();
    ok = gfx_software_bitblt(src_x, src_y, dst_x, dst_y, w, h, rop);
    if (ok) g_gfx_perf.sw_blit++;
    if (fence_out) *fence_out = fence;
    return ok;
}
bool gfx_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    return gfx_bitblt(src_x, src_y, dst_x, dst_y, w, h,
                      GFX_ROP_COPY, NULL);
}

bool gfx_cursor_supported(void) {
    return g_active_driver &&
           (gfx_driver_capabilities() & GFX_CAP_HW_CURSOR) &&
           g_active_driver->cursor_define &&
           g_active_driver->cursor_move && g_active_driver->cursor_show;
}
bool gfx_cursor_define(const uint32_t *argb, uint16_t width, uint16_t height,
                       uint16_t hot_x, uint16_t hot_y) {
    return gfx_cursor_supported() &&
           g_active_driver->cursor_define(argb, width, height, hot_x, hot_y);
}
bool gfx_cursor_move(int x, int y) {
    return gfx_cursor_supported() && g_active_driver->cursor_move(x, y);
}
bool gfx_cursor_show(bool visible) {
    return gfx_cursor_supported() && g_active_driver->cursor_show(visible);
}

bool gfx_surface_create(uint16_t width, uint16_t height,
                        gfx_surface_handle_t *handle_out) {
    return g_active_driver && g_active_driver->surface_create &&
           g_active_driver->surface_create(width, height, handle_out);
}
bool gfx_surface_destroy(gfx_surface_handle_t handle) {
    return g_active_driver && g_active_driver->surface_destroy &&
           g_active_driver->surface_destroy(handle);
}
bool gfx_surface_map(gfx_surface_handle_t handle, uint32_t **pixels_out,
                     uint32_t *pitch_out) {
    uintptr_t address;
    if (!pixels_out || !pitch_out || !g_active_driver || g_gfx.bpp != 32U ||
        !(gfx_driver_capabilities() & GFX_CAP_SURFACE_CPU_MAP) ||
        (g_gfx.pitch & 3U))
        return false;
    address = (uintptr_t)g_gfx.framebuffer + (uintptr_t)handle;
    if (address < (uintptr_t)g_gfx.framebuffer) return false;
    *pixels_out = (uint32_t *)address;
    *pitch_out = (uint32_t)g_gfx.pitch / sizeof(uint32_t);
    return true;
}
bool gfx_surface_upload(gfx_surface_handle_t handle, const uint32_t *pixels,
                        uint32_t source_pitch, const gfx_rect_t *rect) {
    return g_active_driver && g_active_driver->surface_upload &&
           g_active_driver->surface_upload(handle, pixels, source_pitch, rect);
}
bool gfx_surface_blit(gfx_surface_handle_t handle, int src_x, int src_y,
                      int dst_x, int dst_y, int w, int h,
                      uint32_t *fence_out) {
    uint32_t fence = 0;
    bool ok;
    if (!g_active_driver || !g_active_driver->surface_blit) return false;
    ok = g_active_driver->surface_blit(&g_gfx, handle, src_x, src_y,
                                       dst_x, dst_y, w, h, &fence);
    if (ok && fence) g_last_fence = fence;
    if (fence_out) *fence_out = fence;
    return ok;
}

bool gfx_overlay_supported(void) {
    return g_active_driver &&
           (gfx_driver_capabilities() & GFX_CAP_VIDEO_OVERLAY) &&
           g_active_driver->overlay_put && g_active_driver->overlay_stop;
}

bool gfx_overlay_put(const void *pixels, uint32_t source_pitch,
                     uint16_t source_width, uint16_t source_height,
                     gfx_overlay_format_t format,
                     int dst_x, int dst_y, int dst_w, int dst_h) {
    return gfx_overlay_supported() && pixels &&
           g_active_driver->overlay_put(&g_gfx, pixels, source_pitch,
                                        source_width, source_height, format,
                                        dst_x, dst_y, dst_w, dst_h);
}

bool gfx_overlay_stop(void) {
    return gfx_overlay_supported() && g_active_driver->overlay_stop();
}

void gfx_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    gfx_vga_set_palette_color(index, r, g, b);
}
void gfx_set_default_palette(void) { gfx_vga_set_default_palette(); }

static void gfx_linear_putpixel_rgb(int x, int y, uint32_t rgb) {
    volatile uint32_t *pixel = (volatile uint32_t *)(uintptr_t)
        (g_gfx.framebuffer + (uint32_t)y * g_gfx.pitch + (uint32_t)x * 4U);
    *pixel = rgb & 0x00FFFFFFU;
}
static uint32_t gfx_linear_getpixel_rgb(int x, int y) {
    volatile uint32_t *pixel = (volatile uint32_t *)(uintptr_t)
        (g_gfx.framebuffer + (uint32_t)y * g_gfx.pitch + (uint32_t)x * 4U);
    return *pixel & 0x00FFFFFFU;
}

void gfx_clear(uint8_t color) { gfx_clear_rgb(gfx_palette_rgb(color)); }
void gfx_clear_rgb(uint32_t rgb) {
    gfx_fill_rect_rgb(0, 0, g_gfx.width, g_gfx.height, rgb);
}
void gfx_putpixel(int x, int y, uint8_t color) {
    gfx_putpixel_rgb(x, y, gfx_palette_rgb(color));
}
void gfx_putpixel_rgb(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) return;
    if (g_active_driver && (g_gfx.bpp == 8U || g_gfx.bpp == 32U)) {
        (void)gfx_flush();
        if (g_gfx.bpp == 32U) {
            gfx_linear_putpixel_rgb(x, y, rgb);
        } else {
            volatile uint8_t *pixel = (volatile uint8_t *)(uintptr_t)
                (g_gfx.framebuffer + (uint32_t)y * g_gfx.pitch + (uint32_t)x);
            *pixel = gfx_rgb_to_332(rgb);
        }
        (void)g_active_driver->update_rect(x, y, 1, 1);
    } else if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        vesa_putpixel_rgb(&g_gfx, x, y, rgb);
    } else {
        gfx_vga_putpixel(&g_gfx, x, y, gfx_vga_rgb_to_color(rgb));
    }
}
uint32_t gfx_getpixel_rgb(int x, int y) {
    if (x < 0 || y < 0 || x >= g_gfx.width || y >= g_gfx.height) return 0;
    if (g_active_driver && g_gfx.bpp == 32) {
        (void)gfx_flush();
        return gfx_linear_getpixel_rgb(x, y);
    }
    if (g_gfx.mode != GFX_MODE_VESA_LFB) return 0;
    return vesa_getpixel_rgb(&g_gfx, x, y);
}
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
    gfx_fill_rect_rgb(x, y, w, h, gfx_palette_rgb(color));
}
void gfx_fill_rect_rgb(int x, int y, int w, int h, uint32_t rgb) {
    uint32_t fence = 0;
    uint32_t caps = gfx_driver_capabilities();
    bool driver_attempted;
    if (w <= 0 || h <= 0) return;
    gfx_clip_rect(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    driver_attempted = g_active_driver && g_active_driver->fill_rect;
    if (driver_attempted &&
        g_active_driver->fill_rect(&g_gfx, x, y, w, h, rgb, &fence)) {
        if (fence) g_last_fence = fence;
        if (caps & GFX_CAP_FILL_HW) g_gfx_perf.hw_fill++;
        else g_gfx_perf.sw_fill++;
    } else if (g_gfx.mode == GFX_MODE_VESA_LFB) {
        if (driver_attempted) g_gfx_perf.driver_failures++;
        g_gfx_perf.sw_fill++;
        vesa_fill_rect_rgb(&g_gfx, x, y, w, h, rgb);
    } else if (g_active_driver && (g_gfx.bpp == 8U || g_gfx.bpp == 32U)) {
        if (driver_attempted) g_gfx_perf.driver_failures++;
        (void)gfx_flush();
        g_gfx_perf.sw_fill++;
        if (g_gfx.bpp == 32U) {
            for (int row = 0; row < h; row++) {
                uint32_t *dst = (uint32_t *)(uintptr_t)
                    (g_gfx.framebuffer + (uint32_t)(y + row) * g_gfx.pitch) + x;
                for (int col = 0; col < w; col++) dst[col] = rgb;
            }
        } else {
            uint8_t native = gfx_rgb_to_332(rgb);
            for (int row = 0; row < h; row++) {
                uint8_t *dst = (uint8_t *)(uintptr_t)
                    (g_gfx.framebuffer + (uint32_t)(y + row) * g_gfx.pitch) + x;
                for (int col = 0; col < w; col++) dst[col] = native;
            }
        }
        (void)g_active_driver->update_rect(x, y, w, h);
    } else {
        gfx_vga_fill_rect(&g_gfx, x, y, w, h, gfx_vga_rgb_to_color(rgb));
    }
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        int e2;
        gfx_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    (void)gfx_flush();
}

void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg, bool fill_bg) {
    const uint8_t *glyph = glyph_for(c);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool on = (glyph[row] & (0x80 >> col)) != 0;
            if (on) gfx_putpixel(x + col, y + row, fg);
            else if (fill_bg) gfx_putpixel(x + col, y + row, bg);
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg, bool fill_bg) {
    while (s && *s) {
        gfx_draw_char(x, y, *s++, fg, bg, fill_bg);
        x += 8;
    }
    (void)gfx_flush();
}


void gfx_demo(void) {
    gfx_clear(1);
    gfx_fill_rect(20, 20, 280, 160, 3);
    gfx_fill_rect(28, 28, 264, 144, 0);
    gfx_draw_line(0, 0, 319, 199, 12);
    gfx_draw_line(319, 0, 0, 199, 10);
    gfx_fill_rect(48, 64, 224, 56, 9);
    gfx_draw_string(64, 80, "BLESKERNOS VGA", 15, 9, false);
    gfx_draw_string(64, 96, "MODE 13H 320X200", 14, 9, false);
}
