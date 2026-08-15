#include "../../include/vmware_svga.h"
#include "../../include/pci.h"
#include "../../include/pic.h"
#include "../../include/vga.h"

/* VMware SVGA-II register I/O ports, relative to PCI BAR0. */
#define SVGA_PORT_INDEX       0
#define SVGA_PORT_VALUE       1

/* Device protocol IDs. */
#define SVGA_ID_0             0x90000000U
#define SVGA_ID_1             0x90000001U
#define SVGA_ID_2             0x90000002U

/* Device registers. */
#define SVGA_REG_ID               0
#define SVGA_REG_ENABLE           1
#define SVGA_REG_WIDTH            2
#define SVGA_REG_HEIGHT           3
#define SVGA_REG_MAX_WIDTH        4
#define SVGA_REG_MAX_HEIGHT       5
#define SVGA_REG_BITS_PER_PIXEL   7
#define SVGA_REG_BYTES_PER_LINE  12
#define SVGA_REG_FB_START        13
#define SVGA_REG_FB_OFFSET       14
#define SVGA_REG_VRAM_SIZE       15
#define SVGA_REG_FB_SIZE         16
#define SVGA_REG_CAPABILITIES    17
#define SVGA_REG_MEM_START       18
#define SVGA_REG_MEM_SIZE        19
#define SVGA_REG_CONFIG_DONE     20
#define SVGA_REG_SYNC            21
#define SVGA_REG_BUSY            22
#define SVGA_REG_MEM_REGS        30

/* Original SVGA-II capabilities. */
#define SVGA_CAP_RECT_FILL       (1U << 0)
#define SVGA_CAP_RECT_COPY       (1U << 1)
#define SVGA_CAP_EXTENDED_FIFO   (1U << 15)

/* FIFO registers (32-bit words at the beginning of FIFO memory). */
#define SVGA_FIFO_MIN             0
#define SVGA_FIFO_MAX             1
#define SVGA_FIFO_NEXT_CMD        2
#define SVGA_FIFO_STOP            3
#define SVGA_FIFO_CAPABILITIES    4

#define SVGA_FIFO_CAP_ACCELFRONT (1U << 1)
#define SVGA_FIFO_LEGACY_REGS     4U
#define SVGA_FIFO_EXT_REGS        293U
#define SVGA_FIFO_MIN_QUEUE       (10U * 1024U)

/* FIFO commands used by the 2D path. */
#define SVGA_CMD_UPDATE           1U
#define SVGA_CMD_RECT_FILL        2U
#define SVGA_CMD_RECT_COPY        3U
#define SVGA_CMD_FRONT_ROP_FILL  29U
#define SVGA_ROP_COPY             3U

#define SVGA_SYNC_SPIN_LIMIT 10000000U

typedef struct {
    bool active;
    uint16_t io_base;
    uint32_t protocol_id;
    uint32_t capabilities;
    uint32_t fifo_capabilities;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t fb_start;
    uint32_t fb_offset;
    uint32_t fb_size;
    uint32_t vram_size;
    uint32_t fifo_start;
    uint32_t fifo_size;
    volatile uint32_t *fifo;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t framebuffer;
    bool dirty;
    int dirty_x1;
    int dirty_y1;
    int dirty_x2;
    int dirty_y2;
} vmware_svga_state_t;

static vmware_svga_state_t g_svga;

static uint32_t svga_read_reg(uint32_t reg) {
    outl((uint16_t)(g_svga.io_base + SVGA_PORT_INDEX), reg);
    return inl((uint16_t)(g_svga.io_base + SVGA_PORT_VALUE));
}

static void svga_write_reg(uint32_t reg, uint32_t value) {
    outl((uint16_t)(g_svga.io_base + SVGA_PORT_INDEX), reg);
    outl((uint16_t)(g_svga.io_base + SVGA_PORT_VALUE), value);
}

static bool svga_sync_fifo(void) {
    uint32_t spins = 0;

    if (!g_svga.fifo) return false;
    __asm__ volatile ("" : : : "memory");
    svga_write_reg(SVGA_REG_SYNC, 1);
    while (svga_read_reg(SVGA_REG_BUSY)) {
        if (++spins >= SVGA_SYNC_SPIN_LIMIT) return false;
        __asm__ volatile ("pause");
    }
    return true;
}

static uint32_t svga_fifo_free_bytes(void) {
    uint32_t min;
    uint32_t max;
    uint32_t next;
    uint32_t stop;

    if (!g_svga.fifo) return 0;
    min = g_svga.fifo[SVGA_FIFO_MIN];
    max = g_svga.fifo[SVGA_FIFO_MAX];
    next = g_svga.fifo[SVGA_FIFO_NEXT_CMD];
    stop = g_svga.fifo[SVGA_FIFO_STOP];
    if (min >= max || next < min || next >= max || stop < min || stop >= max)
        return 0;

    if (next >= stop)
        return (max - next) + (stop - min) - 4U;
    return stop - next - 4U;
}

static bool svga_fifo_emit(const uint32_t *words, uint32_t count) {
    uint32_t bytes;
    uint32_t next;
    uint32_t min;
    uint32_t max;

    if (!g_svga.active || !g_svga.fifo || !words || !count) return false;
    bytes = count * 4U;
    min = g_svga.fifo[SVGA_FIFO_MIN];
    max = g_svga.fifo[SVGA_FIFO_MAX];
    if (min >= max || bytes >= max - min) return false;

    if (svga_fifo_free_bytes() < bytes) {
        if (!svga_sync_fifo() || svga_fifo_free_bytes() < bytes) return false;
    }

    next = g_svga.fifo[SVGA_FIFO_NEXT_CMD];
    for (uint32_t i = 0; i < count; i++) {
        g_svga.fifo[next >> 2] = words[i];
        next += 4U;
        if (next >= max) next = min;
    }
    __asm__ volatile ("" : : : "memory");
    g_svga.fifo[SVGA_FIFO_NEXT_CMD] = next;
    return true;
}

static void svga_mark_dirty(int x, int y, int w, int h) {
    int x2;
    int y2;

    if (w <= 0 || h <= 0) return;
    x2 = x + w;
    y2 = y + h;
    if (!g_svga.dirty) {
        g_svga.dirty = true;
        g_svga.dirty_x1 = x;
        g_svga.dirty_y1 = y;
        g_svga.dirty_x2 = x2;
        g_svga.dirty_y2 = y2;
        return;
    }
    if (x < g_svga.dirty_x1) g_svga.dirty_x1 = x;
    if (y < g_svga.dirty_y1) g_svga.dirty_y1 = y;
    if (x2 > g_svga.dirty_x2) g_svga.dirty_x2 = x2;
    if (y2 > g_svga.dirty_y2) g_svga.dirty_y2 = y2;
}

static bool svga_clip_rect(int *x, int *y, int *w, int *h) {
    if (!x || !y || !w || !h || *w <= 0 || *h <= 0) return false;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x >= (int)g_svga.width || *y >= (int)g_svga.height) return false;
    if (*x + *w > (int)g_svga.width) *w = (int)g_svga.width - *x;
    if (*y + *h > (int)g_svga.height) *h = (int)g_svga.height - *y;
    return *w > 0 && *h > 0;
}

static bool svga_emit_update(int x, int y, int w, int h) {
    uint32_t command[5];

    if (!svga_clip_rect(&x, &y, &w, &h)) return false;
    command[0] = SVGA_CMD_UPDATE;
    command[1] = (uint32_t)x;
    command[2] = (uint32_t)y;
    command[3] = (uint32_t)w;
    command[4] = (uint32_t)h;
    return svga_fifo_emit(command, 5);
}

static bool svga_negotiate_id(void) {
    static const uint32_t ids[] = {SVGA_ID_2, SVGA_ID_1, SVGA_ID_0};

    for (uint32_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        svga_write_reg(SVGA_REG_ID, ids[i]);
        if (svga_read_reg(SVGA_REG_ID) == ids[i]) {
            g_svga.protocol_id = ids[i];
            return true;
        }
    }
    return false;
}

static bool svga_init_fifo(void) {
    uint32_t register_count;
    uint32_t minimum;

    if (!g_svga.fifo_start || g_svga.fifo_size < SVGA_FIFO_MIN_QUEUE + 16U)
        return false;

    g_svga.fifo = (volatile uint32_t *)(uintptr_t)g_svga.fifo_start;
    svga_write_reg(SVGA_REG_CONFIG_DONE, 0);
    register_count = svga_read_reg(SVGA_REG_MEM_REGS);
    if (register_count < SVGA_FIFO_LEGACY_REGS ||
        register_count > g_svga.fifo_size / 4U) {
        register_count = (g_svga.capabilities & SVGA_CAP_EXTENDED_FIFO)
            ? SVGA_FIFO_EXT_REGS : SVGA_FIFO_LEGACY_REGS;
    }
    minimum = register_count * 4U;
    if (minimum + SVGA_FIFO_MIN_QUEUE > g_svga.fifo_size) {
        minimum = SVGA_FIFO_LEGACY_REGS * 4U;
        if (minimum + SVGA_FIFO_MIN_QUEUE > g_svga.fifo_size) return false;
    }

    g_svga.fifo[SVGA_FIFO_MIN] = minimum;
    g_svga.fifo[SVGA_FIFO_MAX] = g_svga.fifo_size & ~3U;
    g_svga.fifo[SVGA_FIFO_NEXT_CMD] = minimum;
    g_svga.fifo[SVGA_FIFO_STOP] = minimum;
    __asm__ volatile ("" : : : "memory");
    svga_write_reg(SVGA_REG_CONFIG_DONE, 1);

    g_svga.fifo_capabilities = 0;
    if ((g_svga.capabilities & SVGA_CAP_EXTENDED_FIFO) && minimum >= 20U)
        g_svga.fifo_capabilities = g_svga.fifo[SVGA_FIFO_CAPABILITIES];
    return true;
}

static bool svga_mode_fits(uint32_t width, uint32_t height) {
    uint64_t bytes;
    uint32_t available;

    if (!width || !height || width > g_svga.max_width ||
        height > g_svga.max_height) return false;
    bytes = (uint64_t)width * (uint64_t)height * 4U;
    available = g_svga.fb_size ? g_svga.fb_size : g_svga.vram_size;
    if (!available) return true;
    if (g_svga.fb_offset >= available) return false;
    return bytes <= (uint64_t)(available - g_svga.fb_offset);
}

static bool svga_set_mode_internal(gfx_info_t *info, uint16_t width,
                                   uint16_t height) {
    uint32_t actual_width;
    uint32_t actual_height;
    uint32_t actual_bpp;
    uint32_t pitch;
    uint32_t offset;
    uint32_t available;
    uint64_t required;

    if (!info || !svga_mode_fits(width, height)) return false;

    svga_write_reg(SVGA_REG_ENABLE, 0);
    svga_write_reg(SVGA_REG_WIDTH, width);
    svga_write_reg(SVGA_REG_HEIGHT, height);
    svga_write_reg(SVGA_REG_BITS_PER_PIXEL, 32);
    svga_write_reg(SVGA_REG_ENABLE, 1);

    actual_width = svga_read_reg(SVGA_REG_WIDTH);
    actual_height = svga_read_reg(SVGA_REG_HEIGHT);
    actual_bpp = svga_read_reg(SVGA_REG_BITS_PER_PIXEL);
    pitch = svga_read_reg(SVGA_REG_BYTES_PER_LINE);
    offset = svga_read_reg(SVGA_REG_FB_OFFSET);
    if (!pitch) pitch = actual_width * 4U;

    if (actual_width != width || actual_height != height || actual_bpp != 32 ||
        pitch > 0xFFFFU || offset > 0xFFFFFFFFU - g_svga.fb_start)
        return false;

    available = g_svga.fb_size ? g_svga.fb_size : g_svga.vram_size;
    required = (uint64_t)pitch * actual_height;
    if (available && (offset >= available ||
        required > (uint64_t)(available - offset))) return false;

    g_svga.width = actual_width;
    g_svga.height = actual_height;
    g_svga.pitch = pitch;
    g_svga.fb_offset = offset;
    g_svga.framebuffer = g_svga.fb_start + offset;
    g_svga.dirty = false;

    info->mode = GFX_MODE_VMWARE_SVGA;
    info->framebuffer = g_svga.framebuffer;
    info->width = (uint16_t)actual_width;
    info->height = (uint16_t)actual_height;
    info->pitch = (uint16_t)pitch;
    info->bpp = 32;
    return true;
}

static const pci_device_t *svga_find_device(void) {
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t *dev = pci_device_at(i);
        if (dev && dev->vendor_id == VMWARE_SVGA_VENDOR_ID &&
            dev->device_id == VMWARE_SVGA_DEVICE_ID) return dev;
    }
    return NULL;
}

bool vmware_svga_init(gfx_info_t *info, uint16_t preferred_width,
                      uint16_t preferred_height) {
    const pci_device_t *dev;
    uint32_t io_base;
    uint32_t fb_bar_base;
    uint32_t fifo_bar_base;
    uint32_t width;
    uint32_t height;

    if (!info) return false;
    if (g_svga.active) {
        info->mode = GFX_MODE_VMWARE_SVGA;
        info->framebuffer = g_svga.framebuffer;
        info->width = (uint16_t)g_svga.width;
        info->height = (uint16_t)g_svga.height;
        info->pitch = (uint16_t)g_svga.pitch;
        info->bpp = 32;
        return true;
    }

    dev = svga_find_device();
    if (!dev || !(dev->bars[0] & 1U)) return false;
    io_base = dev->bars[0] & 0xFFFFFFFCU;
    fb_bar_base = (dev->bars[1] & 1U) ? 0U :
                  (dev->bars[1] & 0xFFFFFFF0U);
    fifo_bar_base = (dev->bars[2] & 1U) ? 0U :
                    (dev->bars[2] & 0xFFFFFFF0U);
    if (!io_base || io_base > 0xFFF0U) return false;

    g_svga = (vmware_svga_state_t){0};
    g_svga.io_base = (uint16_t)io_base;
    if (!pci_enable_command(dev, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                           PCI_COMMAND_BUSMASTER)) return false;
    if (!svga_negotiate_id()) return false;

    g_svga.capabilities = svga_read_reg(SVGA_REG_CAPABILITIES);
    g_svga.max_width = svga_read_reg(SVGA_REG_MAX_WIDTH);
    g_svga.max_height = svga_read_reg(SVGA_REG_MAX_HEIGHT);
    g_svga.fb_start = svga_read_reg(SVGA_REG_FB_START);
    g_svga.fb_offset = svga_read_reg(SVGA_REG_FB_OFFSET);
    g_svga.fb_size = svga_read_reg(SVGA_REG_FB_SIZE);
    g_svga.vram_size = svga_read_reg(SVGA_REG_VRAM_SIZE);
    g_svga.fifo_start = svga_read_reg(SVGA_REG_MEM_START);
    g_svga.fifo_size = svga_read_reg(SVGA_REG_MEM_SIZE);

    if (!g_svga.fb_start) g_svga.fb_start = fb_bar_base;
    if (!g_svga.fifo_start) g_svga.fifo_start = fifo_bar_base;

    if (!g_svga.max_width || !g_svga.max_height || !g_svga.fb_start ||
        !svga_init_fifo()) {
        svga_write_reg(SVGA_REG_ENABLE, 0);
        return false;
    }

    g_svga.active = true;
    width = preferred_width ? preferred_width : 800U;
    height = preferred_height ? preferred_height : 600U;
    if (!svga_mode_fits(width, height)) {
        width = g_svga.max_width >= 800U ? 800U : g_svga.max_width;
        height = g_svga.max_height >= 600U ? 600U : g_svga.max_height;
    }
    if (!svga_set_mode_internal(info, (uint16_t)width, (uint16_t)height)) {
        g_svga.active = false;
        svga_write_reg(SVGA_REG_CONFIG_DONE, 0);
        svga_write_reg(SVGA_REG_ENABLE, 0);
        return false;
    }

    kprintf("[SVGA] VMware SVGA-II ID=0x%x IO=0x%x FB=0x%x FIFO=0x%x\n",
            g_svga.protocol_id, g_svga.io_base, g_svga.framebuffer,
            g_svga.fifo_start);
    kprintf("[SVGA] %ux%ux32 pitch=%u caps=0x%x fifo_caps=0x%x\n",
            g_svga.width, g_svga.height, g_svga.pitch,
            g_svga.capabilities, g_svga.fifo_capabilities);
    vmware_svga_clear_rgb(info, 0);
    return true;
}

bool vmware_svga_active(void) {
    return g_svga.active;
}

void vmware_svga_disable(void) {
    if (!g_svga.active) return;
    (void)vmware_svga_flush();
    svga_write_reg(SVGA_REG_CONFIG_DONE, 0);
    svga_write_reg(SVGA_REG_ENABLE, 0);
    g_svga.active = false;
    g_svga.dirty = false;
}

bool vmware_svga_list_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                            uint32_t *count) {
    static const gfx_display_mode_t candidates[] = {
        {640, 480, 32}, {800, 600, 32}, {1024, 768, 32},
        {1152, 864, 32}, {1280, 720, 32}, {1280, 800, 32},
        {1280, 1024, 32}, {1366, 768, 32}, {1440, 900, 32},
        {1600, 900, 32}, {1680, 1050, 32}, {1920, 1080, 32}
    };
    uint32_t found = 0;

    if (!count) return false;
    *count = 0;
    if (!g_svga.active || !modes || max_modes == 0) return false;
    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (found >= max_modes) break;
        if (!svga_mode_fits(candidates[i].width, candidates[i].height))
            continue;
        modes[found++] = candidates[i];
    }
    *count = found;
    return found != 0;
}

bool vmware_svga_set_mode(gfx_info_t *info, uint16_t width, uint16_t height,
                          uint8_t bpp) {
    uint16_t old_width;
    uint16_t old_height;

    if (!g_svga.active || !info || bpp != 32) return false;
    old_width = info->width;
    old_height = info->height;
    if (!vmware_svga_flush()) return false;
    if (!svga_set_mode_internal(info, width, height)) {
        (void)svga_set_mode_internal(info, old_width, old_height);
        return false;
    }
    vmware_svga_clear_rgb(info, 0);
    return true;
}

bool vmware_svga_update_rect(int x, int y, int w, int h) {
    if (!g_svga.active) return false;
    return svga_emit_update(x, y, w, h);
}

bool vmware_svga_flush(void) {
    if (!g_svga.active) return false;
    if (g_svga.dirty) {
        int x = g_svga.dirty_x1;
        int y = g_svga.dirty_y1;
        int w = g_svga.dirty_x2 - x;
        int h = g_svga.dirty_y2 - y;
        g_svga.dirty = false;
        if (!svga_emit_update(x, y, w, h)) return false;
    }
    return svga_sync_fifo();
}

void vmware_svga_putpixel_rgb(const gfx_info_t *info, int x, int y,
                              uint32_t rgb) {
    volatile uint32_t *pixel;

    if (!g_svga.active || !info || info->mode != GFX_MODE_VMWARE_SVGA ||
        x < 0 || y < 0 || x >= info->width || y >= info->height) return;
    pixel = (volatile uint32_t *)(uintptr_t)
        (info->framebuffer + (uint32_t)y * info->pitch + (uint32_t)x * 4U);
    *pixel = rgb & 0x00FFFFFFU;
    svga_mark_dirty(x, y, 1, 1);
}

uint32_t vmware_svga_getpixel_rgb(const gfx_info_t *info, int x, int y) {
    volatile uint32_t *pixel;

    if (!g_svga.active || !info || info->mode != GFX_MODE_VMWARE_SVGA ||
        x < 0 || y < 0 || x >= info->width || y >= info->height) return 0;
    pixel = (volatile uint32_t *)(uintptr_t)
        (info->framebuffer + (uint32_t)y * info->pitch + (uint32_t)x * 4U);
    return *pixel & 0x00FFFFFFU;
}

void vmware_svga_fill_rect_rgb(const gfx_info_t *info, int x, int y,
                               int w, int h, uint32_t rgb) {
    if (!g_svga.active || !info || info->mode != GFX_MODE_VMWARE_SVGA ||
        !svga_clip_rect(&x, &y, &w, &h)) return;
    rgb &= 0x00FFFFFFU;

    /* QEMU expone los bits RECT_FILL/RECT_COPY del dispositivo legado, pero
     * su FIFO VMware SVGA-II no acepta necesariamente los opcodes 2/3. En ese
     * caso terminaba con "Unknown command 0x02". UPDATE (opcode 1) si forma
     * parte del FIFO base: escribir el LFB y anunciar el rectangulo funciona
     * tanto en QEMU/WHPX como en VMware real. */
    for (int row = 0; row < h; row++) {
        volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)
            (info->framebuffer + (uint32_t)(y + row) * info->pitch) + x;
        for (int col = 0; col < w; col++) dst[col] = rgb;
    }
    if (svga_emit_update(x, y, w, h)) (void)svga_sync_fifo();
}

void vmware_svga_clear_rgb(const gfx_info_t *info, uint32_t rgb) {
    if (!info) return;
    vmware_svga_fill_rect_rgb(info, 0, 0, info->width, info->height, rgb);
}

bool vmware_svga_copy_rect(const gfx_info_t *info, int src_x, int src_y,
                           int dst_x, int dst_y, int w, int h) {
    int row_start;
    int row_end;
    int row_step;

    if (!g_svga.active || !info || info->mode != GFX_MODE_VMWARE_SVGA ||
        w <= 0 || h <= 0) return false;

    if (src_x < 0) { int d = -src_x; src_x = 0; dst_x += d; w -= d; }
    if (src_y < 0) { int d = -src_y; src_y = 0; dst_y += d; h -= d; }
    if (dst_x < 0) { int d = -dst_x; dst_x = 0; src_x += d; w -= d; }
    if (dst_y < 0) { int d = -dst_y; dst_y = 0; src_y += d; h -= d; }
    if (src_x + w > info->width) w = info->width - src_x;
    if (dst_x + w > info->width) w = info->width - dst_x;
    if (src_y + h > info->height) h = info->height - src_y;
    if (dst_y + h > info->height) h = info->height - dst_y;
    if (w <= 0 || h <= 0) return false;

    if (dst_y > src_y) {
        row_start = h - 1;
        row_end = -1;
        row_step = -1;
    } else {
        row_start = 0;
        row_end = h;
        row_step = 1;
    }
    for (int row = row_start; row != row_end; row += row_step) {
        volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)
            (info->framebuffer + (uint32_t)(src_y + row) * info->pitch) + src_x;
        volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)
            (info->framebuffer + (uint32_t)(dst_y + row) * info->pitch) + dst_x;
        if (dst_x > src_x && dst_x < src_x + w) {
            for (int col = w - 1; col >= 0; col--) dst[col] = src[col];
        } else {
            for (int col = 0; col < w; col++) dst[col] = src[col];
        }
    }
    if (!svga_emit_update(dst_x, dst_y, w, h)) return false;
    return svga_sync_fifo();
}
