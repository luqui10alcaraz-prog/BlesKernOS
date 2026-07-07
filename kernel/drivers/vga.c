#include "../include/gfx_vga.h"
#include "../include/memory.h"
#include "../include/pic.h"

#define VGA_AC_INDEX       0x3C0
#define VGA_MISC_WRITE     0x3C2
#define VGA_DAC_MASK       0x3C6
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA       0x3C9
#define VGA_SEQ_INDEX      0x3C4
#define VGA_GC_INDEX       0x3CE
#define VGA_CRTC_INDEX     0x3D4
#define VGA_CRTC_DATA      0x3D5
#define VGA_INPUT_STATUS   0x3DA

#define VGA_TEXT_FB        0x000B8000
#define VGA_GRAPHICS_FB    0x000A0000
#define VGA_12H_WIDTH      640
#define VGA_12H_HEIGHT     480
#define VGA_12H_PITCH      80

static const uint8_t mode13h_regs[] = {
    0x63,
    0x03, 0x01, 0x0F, 0x00, 0x0E,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

static const uint8_t mode12h_regs[] = {
    0xE3,
    0x03, 0x01, 0x0F, 0x00, 0x06,
    0x5F, 0x4F, 0x4F, 0x83, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x01, 0x00, 0x0F, 0x00, 0x00
};

static const uint8_t mode3_regs[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x02,
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static void vga_write_indexed(uint16_t index_port, uint8_t index, uint8_t value) {
    outb(index_port, index);
    outb((uint16_t)(index_port + 1), value);
}

uint8_t gfx_vga_rgb_to_color(uint32_t rgb) {
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(rgb & 0xFF);
    uint8_t color = 0;
    if (r > 85) color |= 4;
    if (g > 85) color |= 2;
    if (b > 85) color |= 1;
    if (r > 170 || g > 170 || b > 170) color |= 8;
    return color;
}

static void vga_write_regs(const uint8_t *regs) {
    uint32_t i;
    outb(VGA_MISC_WRITE, *regs++);

    for (i = 0; i < 5; i++) vga_write_indexed(VGA_SEQ_INDEX, (uint8_t)i, *regs++);

    vga_write_indexed(VGA_CRTC_INDEX, 0x03, (uint8_t)(inb(VGA_CRTC_DATA) | 0x80));
    vga_write_indexed(VGA_CRTC_INDEX, 0x11, (uint8_t)(inb(VGA_CRTC_DATA) & ~0x80));
    for (i = 0; i < 25; i++) {
        uint8_t value = *regs++;
        if (i == 0x03) value |= 0x80;
        if (i == 0x11) value &= (uint8_t)~0x80;
        vga_write_indexed(VGA_CRTC_INDEX, (uint8_t)i, value);
    }

    for (i = 0; i < 9; i++) vga_write_indexed(VGA_GC_INDEX, (uint8_t)i, *regs++);

    for (i = 0; i < 21; i++) {
        (void)inb(VGA_INPUT_STATUS);
        outb(VGA_AC_INDEX, (uint8_t)i);
        outb(VGA_AC_INDEX, *regs++);
    }
    (void)inb(VGA_INPUT_STATUS);
    outb(VGA_AC_INDEX, 0x20);
}

static void vga12_putpixel(int x, int y, uint8_t color) {
    volatile uint8_t *addr;
    uint8_t mask;
    if (x < 0 || y < 0 || x >= VGA_12H_WIDTH || y >= VGA_12H_HEIGHT) return;

    addr = (volatile uint8_t *)(VGA_GRAPHICS_FB + (uint32_t)y * VGA_12H_PITCH + (uint32_t)(x >> 3));
    mask = (uint8_t)(0x80 >> (x & 7));
    vga_write_indexed(VGA_GC_INDEX, 0x00, (uint8_t)(color & 0x0F));
    vga_write_indexed(VGA_GC_INDEX, 0x01, 0x0F);
    vga_write_indexed(VGA_GC_INDEX, 0x03, 0x00);
    vga_write_indexed(VGA_GC_INDEX, 0x05, 0x00);
    vga_write_indexed(VGA_GC_INDEX, 0x08, mask);
    (void)*addr;
    *addr = 0xFF;
    vga_write_indexed(VGA_GC_INDEX, 0x08, 0xFF);
}

static void vga12_clear(uint8_t color) {
    for (uint8_t plane = 0; plane < 4; plane++) {
        vga_write_indexed(VGA_SEQ_INDEX, 0x02, (uint8_t)(1 << plane));
        kmemset((void *)VGA_GRAPHICS_FB, (color & (1 << plane)) ? 0xFF : 0x00, VGA_12H_PITCH * VGA_12H_HEIGHT);
    }
    vga_write_indexed(VGA_SEQ_INDEX, 0x02, 0x0F);
}

bool gfx_vga_set_text_mode(gfx_info_t *info) {
    if (!info) return false;
    vga_write_regs(mode3_regs);
    info->mode = GFX_MODE_TEXT;
    info->framebuffer = VGA_TEXT_FB;
    info->width = 80;
    info->height = 25;
    info->pitch = 160;
    info->bpp = 16;
    return true;
}

bool gfx_vga_set_mode13h(gfx_info_t *info) {
    if (!info) return false;
    vga_write_regs(mode13h_regs);
    info->mode = GFX_MODE_VGA_13H;
    info->framebuffer = VGA_GRAPHICS_FB;
    info->width = 320;
    info->height = 200;
    info->pitch = 320;
    info->bpp = 8;
    gfx_vga_set_default_palette();
    gfx_vga_clear(info, 0);
    return true;
}

bool gfx_vga_set_mode12h(gfx_info_t *info) {
    if (!info) return false;
    vga_write_regs(mode12h_regs);
    info->mode = GFX_MODE_VGA_12H;
    info->framebuffer = VGA_GRAPHICS_FB;
    info->width = VGA_12H_WIDTH;
    info->height = VGA_12H_HEIGHT;
    info->pitch = VGA_12H_PITCH;
    info->bpp = 4;
    gfx_vga_set_default_palette();
    gfx_vga_clear(info, 0);
    return true;
}

void gfx_vga_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(VGA_DAC_MASK, 0xFF);
    outb(VGA_DAC_WRITE_INDEX, index);
    outb(VGA_DAC_DATA, (uint8_t)(r >> 2));
    outb(VGA_DAC_DATA, (uint8_t)(g >> 2));
    outb(VGA_DAC_DATA, (uint8_t)(b >> 2));
}

void gfx_vga_set_default_palette(void) {
    static const uint8_t ega[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},
        {170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},
        {255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    for (uint8_t i = 0; i < 16; i++) gfx_vga_set_palette_color(i, ega[i][0], ega[i][1], ega[i][2]);
    for (uint16_t i = 16; i < 256; i++) {
        uint8_t v = (uint8_t)i;
        gfx_vga_set_palette_color((uint8_t)i, v, v, v);
    }
}

void gfx_vga_clear(const gfx_info_t *info, uint8_t color) {
    if (!info) return;
    if (info->mode == GFX_MODE_VGA_13H) {
        kmemset((void *)VGA_GRAPHICS_FB, color, (size_t)info->pitch * info->height);
    } else if (info->mode == GFX_MODE_VGA_12H) {
        vga12_clear(color);
    }
}

void gfx_vga_putpixel(const gfx_info_t *info, int x, int y, uint8_t color) {
    volatile uint8_t *fb = (volatile uint8_t *)VGA_GRAPHICS_FB;
    if (!info) return;
    if (x < 0 || y < 0 || x >= info->width || y >= info->height) return;
    if (info->mode == GFX_MODE_VGA_13H) {
        fb[(uint32_t)y * info->pitch + (uint32_t)x] = color;
    } else if (info->mode == GFX_MODE_VGA_12H) {
        vga12_putpixel(x, y, color);
    }
}

void gfx_vga_fill_rect(const gfx_info_t *info, int x, int y, int w, int h, uint8_t color) {
    if (!info || w <= 0 || h <= 0) return;
    if (info->mode == GFX_MODE_VGA_13H) {
        for (int row = 0; row < h; row++) {
            kmemset((void *)(VGA_GRAPHICS_FB + (uint32_t)(y + row) * info->pitch + (uint32_t)x), color, (size_t)w);
        }
    } else if (info->mode == GFX_MODE_VGA_12H) {
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) gfx_vga_putpixel(info, x + col, y + row, color);
        }
    }
}
