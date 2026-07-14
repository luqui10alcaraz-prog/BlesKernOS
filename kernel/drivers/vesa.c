#include "../include/vesa.h"
#include "../include/driver.h"

static bool g_has_vesa = false;
static bool g_bga_probe_done = false;
static bool g_bga_available = false;
static uint16_t g_bga_max_width = 0;
static uint16_t g_bga_max_height = 0;
static uint8_t g_bga_max_bpp = 0;
static bool g_page_flip_enabled = false;
static uint8_t g_visible_page = 0;

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF
#define VBE_DISPI_INDEX_ID      0x00
#define VBE_DISPI_INDEX_XRES    0x01
#define VBE_DISPI_INDEX_YRES    0x02
#define VBE_DISPI_INDEX_BPP     0x03
#define VBE_DISPI_INDEX_ENABLE  0x04
#define VBE_DISPI_INDEX_VIRT_W  0x06
#define VBE_DISPI_INDEX_VIRT_H  0x07
#define VBE_DISPI_INDEX_X_OFF   0x08
#define VBE_DISPI_INDEX_Y_OFF   0x09

#define VBE_DISPI_ID0           0xB0C0
#define VBE_DISPI_ID5           0xB0C5
#define VBE_DISPI_DISABLED      0x0000
#define VBE_DISPI_ENABLED       0x0001
#define VBE_DISPI_GETCAPS       0x0002
#define VBE_DISPI_LFB_ENABLED   0x0040

static inline void vesa_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void vesa_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t vesa_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t bga_read(uint16_t index) {
    vesa_outw(VBE_DISPI_IOPORT_INDEX, index);
    return vesa_inw(VBE_DISPI_IOPORT_DATA);
}

static void bga_write(uint16_t index, uint16_t value) {
    vesa_outw(VBE_DISPI_IOPORT_INDEX, index);
    vesa_outw(VBE_DISPI_IOPORT_DATA, value);
}

static void vesa_probe_runtime_bga(void) {
    uint16_t id;
    uint16_t saved_enable;

    if (g_bga_probe_done) return;
    g_bga_probe_done = true;

    id = bga_read(VBE_DISPI_INDEX_ID);
    if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5) return;

    saved_enable = bga_read(VBE_DISPI_INDEX_ENABLE);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_GETCAPS);
    g_bga_max_width = bga_read(VBE_DISPI_INDEX_XRES);
    g_bga_max_height = bga_read(VBE_DISPI_INDEX_YRES);
    g_bga_max_bpp = (uint8_t)bga_read(VBE_DISPI_INDEX_BPP);
    bga_write(VBE_DISPI_INDEX_ENABLE, saved_enable);

    if (g_bga_max_width < 320 || g_bga_max_height < 200 ||
        g_bga_max_bpp < 8) {
        g_bga_max_width = 0;
        g_bga_max_height = 0;
        g_bga_max_bpp = 0;
        return;
    }

    g_bga_available = true;
}

static uint8_t bootinfo_read8(uint32_t offset) {
    uint8_t value;
    volatile uint8_t *p = (volatile uint8_t *)(VESA_BOOTINFO_ADDR + offset);
    __asm__ volatile ("movb (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static uint16_t bootinfo_read16(uint32_t offset) {
    uint16_t value;
    volatile uint16_t *p = (volatile uint16_t *)(VESA_BOOTINFO_ADDR + offset);
    __asm__ volatile ("movw (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static uint32_t bootinfo_read32(uint32_t offset) {
    uint32_t value;
    volatile uint32_t *p = (volatile uint32_t *)(VESA_BOOTINFO_ADDR + offset);
    __asm__ volatile ("movl (%1), %0" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static uint16_t rgb_to_565(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb & 0xFF);
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3));
}

static uint32_t rgb_from_565(uint16_t v) {
    uint8_t r = (uint8_t)((v >> 11) & 0x1F);
    uint8_t g = (uint8_t)((v >> 5) & 0x3F);
    uint8_t b = (uint8_t)(v & 0x1F);
    r = (uint8_t)((r << 3) | (r >> 2));
    g = (uint8_t)((g << 2) | (g >> 4));
    b = (uint8_t)((b << 3) | (b >> 2));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint8_t rgb_to_332(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xE0);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xE0);
    uint8_t b = (uint8_t)(rgb & 0xC0);
    return (uint8_t)(r | (g >> 3) | (b >> 6));
}

static uint32_t rgb_from_332(uint8_t v) {
    uint8_t r = (uint8_t)(v & 0xE0);
    uint8_t g = (uint8_t)((v & 0x1C) << 3);
    uint8_t b = (uint8_t)((v & 0x03) << 6);
    r |= (uint8_t)(r >> 3) | (uint8_t)(r >> 6);
    g |= (uint8_t)(g >> 3) | (uint8_t)(g >> 6);
    b |= (uint8_t)(b >> 2) | (uint8_t)(b >> 4) | (uint8_t)(b >> 6);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void vesa_set_palette_332(void) {
    vesa_outb(0x3C8, 0);
    for (uint16_t i = 0; i < 256; i++) {
        uint8_t r = (uint8_t)(i & 0xE0);
        uint8_t g = (uint8_t)((i & 0x1C) << 3);
        uint8_t b = (uint8_t)((i & 0x03) << 6);
        r |= (uint8_t)(r >> 3) | (uint8_t)(r >> 6);
        g |= (uint8_t)(g >> 3) | (uint8_t)(g >> 6);
        b |= (uint8_t)(b >> 2) | (uint8_t)(b >> 4) | (uint8_t)(b >> 6);
        vesa_outb(0x3C9, (uint8_t)(r >> 2));
        vesa_outb(0x3C9, (uint8_t)(g >> 2));
        vesa_outb(0x3C9, (uint8_t)(b >> 2));
    }
}

bool vesa_attach_lfb(gfx_info_t *info, uint32_t framebuffer, uint16_t width, uint16_t height, uint16_t pitch, uint8_t bpp) {
    if (!info || !framebuffer || !width || !height || !pitch) return false;
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return false;
    if (pitch < (uint16_t)(width * ((bpp + 7) / 8))) return false;

    info->mode = GFX_MODE_VESA_LFB;
    info->framebuffer = framebuffer;
    info->width = width;
    info->height = height;
    info->pitch = pitch;
    info->bpp = bpp;
    g_has_vesa = true;
    g_page_flip_enabled = false;
    g_visible_page = 0;
    if (bpp == 8) vesa_set_palette_332();
    return true;
}

bool vesa_init_from_bootinfo(gfx_info_t *info) {
    uint32_t magic = bootinfo_read32(0);
    uint32_t framebuffer = bootinfo_read32(4);
    uint16_t width = bootinfo_read16(8);
    uint16_t height = bootinfo_read16(10);
    uint16_t pitch = bootinfo_read16(12);
    uint8_t bpp = bootinfo_read8(14);

    if (magic != VESA_BOOTINFO_MAGIC) return false;
    return vesa_attach_lfb(info, framebuffer, width, height, pitch, bpp);
}

bool vesa_has_lfb(void) {
    return g_has_vesa;
}

bool vesa_can_change_mode(void) {
    vesa_probe_runtime_bga();
    return g_bga_available;
}

bool vesa_list_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                     uint32_t *count, uint8_t preferred_bpp) {
    static const gfx_display_mode_t candidates[] = {
        {320, 200, 0},
        {320, 240, 0},
        {400, 300, 0},
        {512, 384, 0},
        {640, 480, 0},
        {720, 400, 0},
        {800, 600, 0},
        {848, 480, 0},
        {960, 540, 0},
        {1024, 768, 0},
        {1152, 864, 0},
        {1280, 720, 0},
        {1280, 800, 0},
        {1280, 1024, 0},
        {1366, 768, 0},
        {1400, 1050, 0},
        {1440, 900, 0},
        {1600, 900, 0},
        {1600, 1200, 0},
        {1680, 1050, 0},
        {1920, 1080, 0},
        {1920, 1200, 0},
    };
    uint32_t written = 0;

    if (!count) return false;
    *count = 0;
    vesa_probe_runtime_bga();
    if (!g_bga_available || !modes || max_modes == 0) return false;
    if (preferred_bpp != 8 && preferred_bpp != 16 &&
        preferred_bpp != 24 && preferred_bpp != 32)
        return false;
    if (preferred_bpp > g_bga_max_bpp) return false;

    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (written >= max_modes) break;
        if (candidates[i].width > g_bga_max_width ||
            candidates[i].height > g_bga_max_height)
            continue;
        modes[written] = candidates[i];
        modes[written].bpp = preferred_bpp;
        written++;
    }
    *count = written;
    return written != 0;
}

bool vesa_list_all_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                         uint32_t *count) {
    static const uint8_t bpps[] = {8, 16, 24, 32};
    static const gfx_display_mode_t candidates[] = {
        {320, 200, 0},
        {320, 240, 0},
        {400, 300, 0},
        {512, 384, 0},
        {640, 480, 0},
        {720, 400, 0},
        {800, 600, 0},
        {848, 480, 0},
        {960, 540, 0},
        {1024, 768, 0},
        {1152, 864, 0},
        {1280, 720, 0},
        {1280, 800, 0},
        {1280, 1024, 0},
        {1366, 768, 0},
        {1400, 1050, 0},
        {1440, 900, 0},
        {1600, 900, 0},
        {1600, 1200, 0},
        {1680, 1050, 0},
        {1920, 1080, 0},
        {1920, 1200, 0},
    };
    uint32_t written = 0;

    if (!count) return false;
    *count = 0;
    vesa_probe_runtime_bga();
    if (!g_bga_available || !modes || max_modes == 0) return false;

    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (candidates[i].width > g_bga_max_width ||
            candidates[i].height > g_bga_max_height)
            continue;
        for (uint32_t bi = 0; bi < sizeof(bpps) / sizeof(bpps[0]); bi++) {
            if (written >= max_modes) break;
            if (bpps[bi] > g_bga_max_bpp) continue;
            modes[written] = candidates[i];
            modes[written].bpp = bpps[bi];
            written++;
        }
        if (written >= max_modes) break;
    }

    *count = written;
    return written != 0;
}

bool vesa_set_mode(gfx_info_t *info, uint16_t width, uint16_t height,
                   uint8_t bpp) {
    uint16_t actual_width;
    uint16_t actual_height;
    uint16_t virt_width;
    uint16_t pitch;
    uint8_t bytes_per_pixel;

    if (!info || !info->framebuffer || !width || !height) return false;
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return false;

    vesa_probe_runtime_bga();
    if (!g_bga_available) return false;
    if (width > g_bga_max_width || height > g_bga_max_height ||
        bpp > g_bga_max_bpp)
        return false;

    bytes_per_pixel = (uint8_t)((bpp + 7) / 8);
    if (bytes_per_pixel == 0) return false;

    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_VIRT_W, width);
    bga_write(VBE_DISPI_INDEX_VIRT_H, height);
    bga_write(VBE_DISPI_INDEX_X_OFF, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFF, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    actual_width = bga_read(VBE_DISPI_INDEX_XRES);
    actual_height = bga_read(VBE_DISPI_INDEX_YRES);
    if (actual_width != width || actual_height != height ||
        bga_read(VBE_DISPI_INDEX_BPP) != bpp)
        return false;

    virt_width = bga_read(VBE_DISPI_INDEX_VIRT_W);
    if (!virt_width) virt_width = width;
    pitch = (uint16_t)(virt_width * bytes_per_pixel);
    return vesa_attach_lfb(info, info->framebuffer, width, height, pitch, bpp);
}

bool vesa_enable_page_flip(const gfx_info_t *info) {
    uint32_t virtual_height;

    if (!info || info->mode != GFX_MODE_VESA_LFB || !info->framebuffer ||
        info->pitch != (uint16_t)(info->width * ((info->bpp + 7U) / 8U)))
        return false;
    vesa_probe_runtime_bga();
    if (!g_bga_available) return false;
    virtual_height = (uint32_t)info->height * 2U;
    if (virtual_height > 0xFFFFU) return false;

    bga_write(VBE_DISPI_INDEX_VIRT_W, info->width);
    bga_write(VBE_DISPI_INDEX_VIRT_H, (uint16_t)virtual_height);
    if (bga_read(VBE_DISPI_INDEX_VIRT_H) < virtual_height) return false;
    bga_write(VBE_DISPI_INDEX_X_OFF, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFF, 0);
    g_visible_page = 0;
    g_page_flip_enabled = true;
    return true;
}

uint32_t vesa_page_flip_draw_buffer(const gfx_info_t *info) {
    uint32_t hidden_page;

    if (!g_page_flip_enabled || !info) return 0U;
    hidden_page = g_visible_page ? 0U : 1U;
    return info->framebuffer + hidden_page * (uint32_t)info->pitch *
           (uint32_t)info->height;
}

bool vesa_page_flip_commit(const gfx_info_t *info) {
    uint16_t next_y;

    if (!g_page_flip_enabled || !info) return false;
    g_visible_page ^= 1U;
    next_y = g_visible_page ? info->height : 0U;
    bga_write(VBE_DISPI_INDEX_Y_OFF, next_y);
    return bga_read(VBE_DISPI_INDEX_Y_OFF) == next_y;
}

void vesa_putpixel_rgb(const gfx_info_t *info, int x, int y, uint32_t rgb) {
    uint8_t *where;
    if (!info || info->mode != GFX_MODE_VESA_LFB) return;
    if (x < 0 || y < 0 || x >= info->width || y >= info->height) return;

    where = (uint8_t *)(info->framebuffer + (uint32_t)y * info->pitch +
                        ((uint32_t)x * ((info->bpp + 7) / 8)));
    if (info->bpp == 8) {
        *where = rgb_to_332(rgb);
    } else if (info->bpp == 16) {
        *((uint16_t *)where) = rgb_to_565(rgb);
    } else {
        where[0] = (uint8_t)(rgb & 0xFF);
        where[1] = (uint8_t)((rgb >> 8) & 0xFF);
        where[2] = (uint8_t)((rgb >> 16) & 0xFF);
        if (info->bpp == 32) where[3] = 0;
    }
}

uint32_t vesa_getpixel_rgb(const gfx_info_t *info, int x, int y) {
    uint8_t *where;
    if (!info || info->mode != GFX_MODE_VESA_LFB) return 0;
    if (x < 0 || y < 0 || x >= info->width || y >= info->height) return 0;

    where = (uint8_t *)(info->framebuffer + (uint32_t)y * info->pitch +
                        ((uint32_t)x * ((info->bpp + 7) / 8)));
    if (info->bpp == 8) return rgb_from_332(*where);
    if (info->bpp == 16) return rgb_from_565(*((uint16_t *)where));
    return ((uint32_t)where[2] << 16) | ((uint32_t)where[1] << 8) | where[0];
}

void vesa_fill_rect_rgb(const gfx_info_t *info, int x, int y, int w, int h, uint32_t rgb) {
    if (!info || w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) vesa_putpixel_rgb(info, x + col, y + row, rgb);
    }
}

void vesa_clear_rgb(const gfx_info_t *info, uint32_t rgb) {
    if (!info) return;
    vesa_fill_rect_rgb(info, 0, 0, info->width, info->height, rgb);
}

static bool vesa_driver_init(void) {
    static const vesa_driver_ops_t ops = {
        .init_from_bootinfo = vesa_init_from_bootinfo,
        .attach_lfb = vesa_attach_lfb,
        .has_lfb = vesa_has_lfb,
        .can_change_mode = vesa_can_change_mode,
        .list_modes = vesa_list_modes,
        .list_all_modes = vesa_list_all_modes,
        .set_mode = vesa_set_mode,
        .enable_page_flip = vesa_enable_page_flip,
        .page_flip_draw_buffer = vesa_page_flip_draw_buffer,
        .page_flip_commit = vesa_page_flip_commit,
        .clear_rgb = vesa_clear_rgb,
        .putpixel_rgb = vesa_putpixel_rgb,
        .getpixel_rgb = vesa_getpixel_rgb,
        .fill_rect_rgb = vesa_fill_rect_rgb
    };
    return vesa_register_driver(&ops);
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "vesa",
        "Framebuffer lineal VESA y Bochs VBE",
        vesa_driver_init,
        NULL
    };
    return &module;
}
