/*
 * BlesKernOS experimental fixed-function 3D driver for ATI Rage Mobility M3
 * (Rage 128 LF, PCI 1002:4C46).
 *
 * The module is intentionally separate from ATIR128.DVR.  The 2D module owns
 * display setup, scanout, cursor and normal GUI acceleration.  This extension
 * only claims the GFX3D ABI when the ATI backend is already active in a
 * 32-bpp linear mode.  It uses the Rage 128 Concurrent Command Engine (CCE)
 * in 192-DWORD programmed-I/O mode, so no AGP/GART or DMA ring is required.
 *
 * Implemented first stage:
 *   - off-screen ARGB/XRGB render targets in local VRAM;
 *   - transformed triangle lists with Gouraud diffuse color;
 *   - source-alpha blending, Z16 and one ARGB8888 texture unit;
 *   - synchronous upload/download/present and TinyGL integration;
 *   - bounded waits, smoke test and automatic shutdown on a CCE timeout.
 *
 * Register values and packet layout follow ATI's Rage 128 register/software
 * development manuals and the MIT-licensed Linux/X.Org r128 implementations.
 */

#include "../../include/types.h"
#include "../../include/memory.h"
#include "../../include/driver.h"
#include "../../include/gfx.h"
#include "../../include/gfx3d.h"
#include "../../include/gfx3d_driver.h"
#include "../../include/pci.h"
#include "../../include/rage128_engine.h"
#include "ati_rage128_cce_ucode.h"
#include "../../stdio.h"

#define ATI_VENDOR_ID                   0x1002U
#define ATI_RAGE128_LF_DEVICE_ID        0x4C46U

#define R1283D_MAX_SURFACES             16U
#define R1283D_PAGE_SIZE                4096U
#define R1283D_MAX_PAGES                2048U
#define R1283D_PAGE_WORDS               (R1283D_MAX_PAGES / 32U)
#define R1283D_TOP_RESERVE              (64U * 1024U)
#define R1283D_TIMEOUT                  3000000U
#define R1283D_MAX_VERTICES             384U
#define R1283D_PIO_FIFO_DWORDS          192U
#define R1283D_EXPERIMENTAL_Z16          1U

#define R1283D_BASE_CAPABILITIES \
    (GFX3D_CAP_FIXED_FUNCTION | GFX3D_CAP_RENDER_TARGETS | \
     GFX3D_CAP_VERTEX_BUFFERS | GFX3D_CAP_PRESENT | \
     GFX3D_CAP_ALPHA_BLEND | GFX3D_CAP_TEXTURES | GFX3D_CAP_TINYGL | \
     GFX3D_CAP_DEPTH_FUNCS | GFX3D_CAP_BLEND_ADDITIVE | \
     GFX3D_CAP_TEXTURE_REGION_UPLOAD)
#if R1283D_EXPERIMENTAL_Z16
#define R1283D_CAPABILITIES \
    (R1283D_BASE_CAPABILITIES | GFX3D_CAP_DEPTH_BUFFER | \
     GFX3D_CAP_DEPTH_SURFACE_IO)
#else
#define R1283D_CAPABILITIES R1283D_BASE_CAPABILITIES
#endif

#define ALIGN_UP(v, a)                  (((v) + ((a) - 1U)) & ~((a) - 1U))
#define ALIGN_DOWN(v, a)                ((v) & ~((a) - 1U))

/* Core, reset, cache and display-related registers. */
#define R128_CLOCK_CNTL_INDEX           0x0008U
#define R128_CLOCK_CNTL_DATA            0x000CU
#define R128_MCLK_CNTL                  0x000FU
#define R128_GEN_RESET_CNTL             0x00F0U
#define R128_CONFIG_MEMSIZE             0x00F8U
#define R128_PC_NGUI_CTLSTAT            0x0184U
#define R128_GUI_STAT                    0x1740U
#define R128_OV0_SCALE_CNTL             0x0420U
#define R128_SOFT_RESET_GUI             (1U << 0)
#define R128_FORCE_GCP                  (1U << 16)
#define R128_FORCE_PIPE3D_CP            (1U << 17)
#define R128_PLL_WR_EN                  (1U << 7)
#define R128_PC_FLUSH_ALL               0x000000FFU
#define R128_PC_BUSY                    (1U << 31)
#define R128_GUI_FIFOCNT_MASK           0x00000FFFU
#define R128_GUI_ACTIVE                 (1U << 31)

/* CCE / PM4. */
#define R128_PM4_BUFFER_CNTL            0x0704U
#define R128_PM4_BUFFER_DL_RPTR         0x0710U
#define R128_PM4_BUFFER_DL_WPTR         0x0714U
#define R128_PM4_VC_FPU_SETUP          0x071CU
#define R128_PM4_STAT                   0x07B8U
#define R128_PM4_MICROCODE_ADDR         0x07D4U
#define R128_PM4_MICROCODE_DATAH        0x07DCU
#define R128_PM4_MICROCODE_DATAL        0x07E0U
#define R128_PM4_BUFFER_ADDR            0x07F0U
#define R128_PM4_MICRO_CNTL             0x07FCU
#define R128_PM4_FIFO_DATA_EVEN         0x1000U
#define R128_PM4_FIFO_DATA_ODD          0x1004U
#define R128_PM4_NONPM4                 (0U << 28)
#define R128_PM4_192PIO                 (1U << 28)
#define R128_PM4_BUFFER_CNTL_NOUPDATE   (1U << 27)
#define R128_PM4_MICRO_FREERUN          (1U << 30)
#define R128_PM4_FIFOCNT_MASK           0x00000FFFU
#define R128_PM4_BUSY                   (1U << 16)
#define R128_PM4_GUI_ACTIVE             (1U << 31)

/* 3D state. */
#define R128_PC_GUI_CTLSTAT             0x1748U
#define R128_SCALE_3D_CNTL              0x1A00U
#define R128_SETUP_CNTL                 0x1BC4U
#define R128_WINDOW_XY_OFFSET           0x1BCCU
#define R128_DST_PITCH_OFFSET_C         0x1C80U
#define R128_DP_GUI_MASTER_CNTL_C       0x1C84U
#define R128_SC_TOP_LEFT_C              0x1C88U
#define R128_SC_BOTTOM_RIGHT_C          0x1C8CU
#define R128_Z_OFFSET_C                 0x1C90U
#define R128_Z_PITCH_C                  0x1C94U
#define R128_Z_STEN_CNTL_C              0x1C98U
#define R128_TEX_CNTL_C                 0x1C9CU
#define R128_MISC_3D_STATE_CNTL_REG     0x1CA0U
#define R128_CONSTANT_COLOR_C           0x1D34U
#define R128_PLANE_3D_MASK_C            0x1D44U
#define R128_PRIM_TEX_CNTL_C            0x1CB0U
#define R128_PRIM_TEXTURE_COMBINE_CNTL_C 0x1CB4U
#define R128_TEX_SIZE_PITCH_C           0x1CB8U
#define R128_PRIM_TEX_0_OFFSET_C        0x1CBCU

#define R128_PC_FLUSH_GUI               (3U << 0)
#define R128_SCALE_3D_TEXMAP_SHADE      (2U << 6)
#define R128_SCALE_PIX_REPLICATE        (1U << 8)
#define R128_TEX_CACHE_SPLIT            (1U << 9)
#define R128_ALPHA_COMB_ADD_CLAMP       (0U << 12)
#define R128_TEX_MAP_ALPHA_IN_TEXTURE   (1U << 30)
#define R128_TEX_CACHE_LINE_SIZE_4QW    (1U << 31)

#define R128_COLOR_GOURAUD              (4U << 3)
#define R128_PRIM_TYPE_TRI              (0U << 7)
#define R128_TEXTURE_ST_MULT_W          (0U << 9)
#define R128_STARTING_VERTEX_1          (1U << 14)
#define R128_ENDING_VERTEX_3            (3U << 16)
#define R128_SUB_PIX_4BITS              (1U << 19)
#define R128_SU_POLY_LINE_NOT_LAST      (1U << 18)

#define R128_FRONT_DIR_CCW              (1U << 0)
#define R128_BACKFACE_SOLID             (3U << 1)
#define R128_FRONTFACE_SOLID            (3U << 3)
#define R128_FPU_COLOR_GOURAUD          (2U << 5)
#define R128_FPU_SUB_PIX_4BITS          (1U << 7)
#define R128_FPU_MODE_3D                (1U << 8)
#define R128_TRAP_BITS_DISABLE          (1U << 9)
#define R128_FLAT_SHADE_VERTEX_OGL      (1U << 14)
#define R128_FPU_ROUND_TRUNCATE         (0U << 15)
#define R128_WM_SEL_8DW                 (0U << 16)

#define R128_GMC_DST_PITCH_OFFSET_CNTL  (1U << 1)
#define R128_GMC_BRUSH_SOLID_COLOR      (13U << 4)
#define R128_GMC_DST_32BPP              (6U << 8)
#define R128_GMC_SRC_DATATYPE_COLOR     (3U << 12)
#define R128_ROP3_S                     0x00CC0000U
#define R128_DP_SRC_SOURCE_MEMORY       (2U << 24)
#define R128_GMC_3D_FCN_EN              (1U << 27)
#define R128_GMC_CLR_CMP_CNTL_DIS       (1U << 28)
#define R128_GMC_AUX_CLIP_DIS           (1U << 29)
#define R128_GMC_WR_MSK_DIS             (1U << 30)

#define R128_Z_PIX_WIDTH_16             (0U << 1)
#define R128_Z_TEST_NEVER               (0U << 4)
#define R128_Z_TEST_LESS                (1U << 4)
#define R128_Z_TEST_LESSEQUAL           (2U << 4)
#define R128_Z_TEST_EQUAL               (3U << 4)
#define R128_Z_TEST_GREATEREQUAL        (4U << 4)
#define R128_Z_TEST_GREATER             (5U << 4)
#define R128_Z_TEST_NOTEQUAL            (6U << 4)
#define R128_Z_TEST_ALWAYS              (7U << 4)
#define R128_Z_ENABLE                   (1U << 0)
#define R128_Z_WRITE_ENABLE             (1U << 1)
#define R128_ALPHA_ENABLE               (1U << 9)
#define R128_TEXMAP_ENABLE              (1U << 4)
#define R128_TEX_CACHE_FLUSH            (1U << 23)

#define R128_MIN_BLEND_LINEAR           (1U << 1)
#define R128_MAG_BLEND_LINEAR           (1U << 4)
#define R128_MIP_MAP_DISABLE            (1U << 7)
#define R128_TEX_CLAMP_S_CLAMP          (2U << 8)
#define R128_TEX_WRAP_S                 (1U << 10)
#define R128_TEX_CLAMP_T_CLAMP          (2U << 11)
#define R128_TEX_WRAP_T                 (1U << 13)
#define R128_DATATYPE_ARGB8888          (6U << 16)
#define R128_COMB_MODULATE              (3U << 0)
#define R128_COLOR_FACTOR_TEX           (4U << 4)
#define R128_INPUT_FACTOR_INT_COLOR     (4U << 10)
#define R128_COMB_ALPHA_MODULATE        (3U << 14)
#define R128_ALPHA_FACTOR_TEX_ALPHA     (6U << 18)
#define R128_INP_FACTOR_A_INT_ALPHA     (2U << 25)

#define R128_MISC_SCALE_3D_TEXMAP_SHADE (2U << 8)
#define R128_MISC_SCALE_PIX_REPLICATE   (1U << 10)
#define R128_ALPHA_BLEND_ONE            1U
#define R128_ALPHA_BLEND_SRCALPHA       4U
#define R128_ALPHA_BLEND_INVSRCALPHA    5U
#define R128_ALPHA_BLEND_SRC_SHIFT      16U
#define R128_ALPHA_BLEND_DST_SHIFT      20U

/* CCE packets and transformed vertex layout. */
#define R128_CCE_PACKET0                0x00000000U
#define R128_CCE_PACKET2                0x80000000U
#define R128_CCE_PACKET3                0xC0000000U
#define R128_CCE_PACKET3_3D_RNDR_GEN_PRIM 0x00002500U
#define R128_CCE_VC_FRMT_RHW            0x00000001U
#define R128_CCE_VC_FRMT_DIFFUSE_ARGB   0x00000008U
#define R128_CCE_VC_FRMT_S_T            0x00000080U
#define R128_CCE_VC_CNTL_PRIM_TYPE_TRI_LIST 0x00000004U
#define R128_CCE_VC_CNTL_PRIM_WALK_RING 0x00000030U
#define R128_CCE_VC_CNTL_NUM_SHIFT      16U

#define CCE_PACKET0(reg, n) \
    (R128_CCE_PACKET0 | ((uint32_t)(n) << 16) | ((reg) >> 2))
#define CCE_PACKET3(pkt, n) \
    (R128_CCE_PACKET3 | (pkt) | ((uint32_t)(n) << 16))

typedef union {
    float f;
    uint32_t u;
} r1283d_float_bits_t;

typedef struct {
    bool used;
    uint8_t generation;
    uint16_t width;
    uint16_t height;
    gfx3d_format_t format;
    uint32_t flags;
    uint32_t bytes_per_pixel;
    uint32_t pitch;
    uint32_t size;
    uint32_t offset;
    uint32_t page_first;
    uint32_t page_count;
    uint32_t depth_offset;
    uint32_t depth_pitch;
    uint32_t depth_page_first;
    uint32_t depth_page_count;
} r1283d_surface_t;

typedef struct {
    const pci_device_t *pci;
    volatile uint8_t *mmio;
    volatile uint8_t *vram;
    uint32_t fb_bar;
    uint32_t mmio_bar;
    uint32_t vram_size;

    uint32_t arena_base;
    uint32_t arena_limit;
    uint32_t arena_pages;
    uint32_t page_bitmap[R1283D_PAGE_WORDS];

    r1283d_surface_t surfaces[R1283D_MAX_SURFACES];
    uint8_t generations[R1283D_MAX_SURFACES];

    gfx_info_t mode;
    bool detected;
    bool ready;
    bool disabled;
    bool ucode_loaded;
    bool smoke_tested;
    bool smoke_ok;
    bool cce_running;
    bool engine_acquired;
    bool overlay_saved;
    uint32_t saved_overlay_scale;
    uint32_t saved_pm4_buffer_cntl;
    uint32_t saved_pm4_micro_cntl;

    gfx3d_surface_handle_t current_target;
    uint32_t last_fence;
    uint32_t completed_fence;
    uint32_t stat_context_acquires;
    uint32_t stat_cce_starts;
    uint32_t stat_present_hw;
    uint32_t stat_present_cpu_calls;
    uint32_t stat_present_cpu_bytes;
    bool absence_reported;
} r1283d_state_t;

static r1283d_state_t g_r1283d;

static void r1283d_disable_after_hang(const char *where);
static void r1283d_perf_report(const char *reason);

static uint32_t r1283d_read(uint32_t reg) {
    volatile uint32_t *p = (volatile uint32_t *)(g_r1283d.mmio + reg);
    uint32_t value = *p;
    __asm__ volatile ("" ::: "memory");
    return value;
}

static void r1283d_write(uint32_t reg, uint32_t value) {
    volatile uint32_t *p = (volatile uint32_t *)(g_r1283d.mmio + reg);
    *p = value;
    __asm__ volatile ("" ::: "memory");
}

static void r1283d_write8(uint32_t reg, uint8_t value) {
    volatile uint8_t *p = g_r1283d.mmio + reg;
    *p = value;
    __asm__ volatile ("" ::: "memory");
}

static uint32_t r1283d_pll_read(uint8_t index) {
    r1283d_write8(R128_CLOCK_CNTL_INDEX, (uint8_t)(index & 0x3FU));
    return r1283d_read(R128_CLOCK_CNTL_DATA);
}

static void r1283d_pll_write(uint8_t index, uint32_t value) {
    r1283d_write8(R128_CLOCK_CNTL_INDEX,
                  (uint8_t)((index & 0x3FU) | R128_PLL_WR_EN));
    r1283d_write(R128_CLOCK_CNTL_DATA, value);
}

static uint32_t r1283d_float_word(float value) {
    r1283d_float_bits_t bits;
    bits.f = value;
    return bits.u;
}

static bool r1283d_detect_pci(void) {
    if (g_r1283d.detected) return true;
    for (uint32_t i = 0; i < pci_device_count(); i++) {
        const pci_device_t *dev = pci_device_at(i);
        uint32_t fb, mmio;
        if (!dev || dev->vendor_id != ATI_VENDOR_ID ||
            dev->device_id != ATI_RAGE128_LF_DEVICE_ID) continue;
        fb = dev->bars[0] & ~0xFU;
        mmio = dev->bars[2] & ~0xFU;
        if (!fb || !mmio || (dev->bars[0] & 1U) || (dev->bars[2] & 1U))
            continue;
        if (!pci_enable_command(dev, PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER))
            return false;
        g_r1283d.pci = dev;
        g_r1283d.fb_bar = fb;
        g_r1283d.mmio_bar = mmio;
        g_r1283d.mmio = (volatile uint8_t *)(uintptr_t)mmio;
        g_r1283d.vram = (volatile uint8_t *)(uintptr_t)fb;
        g_r1283d.vram_size = r1283d_read(R128_CONFIG_MEMSIZE) & 0x03FFFFFFU;
        if (g_r1283d.vram_size < (2U * 1024U * 1024U) ||
            g_r1283d.vram_size > (128U * 1024U * 1024U))
            g_r1283d.vram_size = 8U * 1024U * 1024U;
        g_r1283d.detected = true;
        kprintf("[ATIR1283D:TRACE] PCI 1002:4C46 bdf=%u:%u.%u "
                "FB=0x%x MMIO=0x%x VRAM=%u KiB\n",
                dev->bus, dev->slot, dev->function, fb, mmio,
                g_r1283d.vram_size / 1024U);
        return true;
    }
    return false;
}

static bool r1283d_flush_pixel_cache(void) {
    uint32_t cache = r1283d_read(R128_PC_NGUI_CTLSTAT);
    r1283d_write(R128_PC_NGUI_CTLSTAT, cache | R128_PC_FLUSH_ALL);
    for (uint32_t i = 0; i < R1283D_TIMEOUT; i++)
        if (!(r1283d_read(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY))
            return true;
    return false;
}

/* The 2D GUI FIFO and the PM4 CCE FIFO are different engines.  Linux's
 * r128 DRM waits on GUI_STAT before loading/starting the CCE, then on
 * PM4_STAT after commands have been submitted.  Mixing both checks made the
 * first revision reject a perfectly idle chip while PM4 was still disabled. */
static bool r1283d_wait_2d_idle(void) {
    for (uint32_t i = 0; i < R1283D_TIMEOUT; i++) {
        uint32_t stat = r1283d_read(R128_GUI_STAT);
        if ((stat & R128_GUI_FIFOCNT_MASK) >= 64U &&
            !(stat & R128_GUI_ACTIVE))
            return r1283d_flush_pixel_cache();
    }
    return false;
}

static bool r1283d_wait_cce_idle(void) {
    for (uint32_t i = 0; i < R1283D_TIMEOUT; i++) {
        uint32_t stat = r1283d_read(R128_PM4_STAT);
        if ((stat & R128_PM4_FIFOCNT_MASK) >= R1283D_PIO_FIFO_DWORDS &&
            !(stat & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE)))
            return r1283d_flush_pixel_cache();
    }
    return false;
}

static bool r1283d_wait_pio_slots(uint32_t slots) {
    for (uint32_t i = 0; i < R1283D_TIMEOUT; i++)
        if ((r1283d_read(R128_PM4_STAT) & R128_PM4_FIFOCNT_MASK) >= slots)
            return true;
    return false;
}

static void r1283d_engine_reset(void) {
    uint32_t clock_index;
    uint32_t mclk;
    uint32_t reset;

    if (!g_r1283d.mmio) return;
    clock_index = r1283d_read(R128_CLOCK_CNTL_INDEX);
    mclk = r1283d_pll_read(R128_MCLK_CNTL);
    r1283d_pll_write(R128_MCLK_CNTL,
                     mclk | R128_FORCE_GCP | R128_FORCE_PIPE3D_CP);
    reset = r1283d_read(R128_GEN_RESET_CNTL);
    r1283d_write(R128_GEN_RESET_CNTL, reset | R128_SOFT_RESET_GUI);
    (void)r1283d_read(R128_GEN_RESET_CNTL);
    r1283d_write(R128_GEN_RESET_CNTL, reset & ~R128_SOFT_RESET_GUI);
    (void)r1283d_read(R128_GEN_RESET_CNTL);
    r1283d_pll_write(R128_MCLK_CNTL, mclk);
    r1283d_write(R128_CLOCK_CNTL_INDEX, clock_index);
    r1283d_write(R128_GEN_RESET_CNTL, reset);
    r1283d_write(R128_PM4_BUFFER_DL_WPTR, 0U);
    r1283d_write(R128_PM4_BUFFER_DL_RPTR, 0U);
    g_r1283d.ucode_loaded = false;
}

static bool r1283d_load_microcode(void) {
    if (g_r1283d.ucode_loaded) return true;
    if (!r1283d_wait_2d_idle()) {
        r1283d_engine_reset();
        if (!r1283d_wait_2d_idle()) return false;
    }
    r1283d_write(R128_PM4_MICRO_CNTL, 0U);
    r1283d_write(R128_PM4_BUFFER_CNTL,
                 R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
    r1283d_write(R128_PM4_MICROCODE_ADDR, 0U);
    for (uint32_t i = 0; i < 256U; i++) {
        r1283d_write(R128_PM4_MICROCODE_DATAH,
                     r128_cce_microcode[i * 2U]);
        r1283d_write(R128_PM4_MICROCODE_DATAL,
                     r128_cce_microcode[i * 2U + 1U]);
    }
    g_r1283d.ucode_loaded = true;
    return true;
}

static bool r1283d_cce_active_hw(void) {
    uint32_t buffer;
    uint32_t micro;
    if (!g_r1283d.mmio) return false;
    buffer = r1283d_read(R128_PM4_BUFFER_CNTL);
    micro = r1283d_read(R128_PM4_MICRO_CNTL);
    return (buffer & (3U << 28)) == R128_PM4_192PIO &&
           (micro & R128_PM4_MICRO_FREERUN) != 0U;
}

static void r1283d_perf_report(const char *reason) {
    kprintf("[ATIR1283D:PERF] %s CCE=%s acquire=%u CCEstart=%u "
            "presentHW=%u presentCPU=%u CPUbytes=%u fence=%u/%u\n",
            reason ? reason : "estado",
            r1283d_cce_active_hw() ? "activo" : "detenido",
            g_r1283d.stat_context_acquires, g_r1283d.stat_cce_starts,
            g_r1283d.stat_present_hw, g_r1283d.stat_present_cpu_calls,
            g_r1283d.stat_present_cpu_bytes,
            g_r1283d.completed_fence, g_r1283d.last_fence);
}

static bool r1283d_cce_start(void) {
    if (g_r1283d.cce_running && r1283d_cce_active_hw()) return true;
    g_r1283d.cce_running = false;
    if (!r1283d_load_microcode() || !r1283d_wait_2d_idle()) return false;
    g_r1283d.saved_pm4_buffer_cntl = r1283d_read(R128_PM4_BUFFER_CNTL);
    g_r1283d.saved_pm4_micro_cntl = r1283d_read(R128_PM4_MICRO_CNTL);
    r1283d_write(R128_PM4_BUFFER_CNTL,
                 R128_PM4_192PIO | R128_PM4_BUFFER_CNTL_NOUPDATE);
    (void)r1283d_read(R128_PM4_BUFFER_ADDR);
    r1283d_write(R128_PM4_MICRO_CNTL, R128_PM4_MICRO_FREERUN);
    if (!r1283d_wait_pio_slots(2U)) return false;
    g_r1283d.cce_running = true;
    g_r1283d.stat_cce_starts++;
    return true;
}

static void r1283d_cce_stop(void) {
    if (!g_r1283d.mmio) return;
    if (!g_r1283d.cce_running && !r1283d_cce_active_hw()) return;
    r1283d_write(R128_PM4_MICRO_CNTL, 0U);
    r1283d_write(R128_PM4_BUFFER_CNTL,
                 R128_PM4_NONPM4 | R128_PM4_BUFFER_CNTL_NOUPDATE);
    g_r1283d.cce_running = false;
}

static bool r1283d_engine_enter(bool *acquired_here) {
    if (acquired_here) *acquired_here = false;
    if (g_r1283d.engine_acquired) return true;
    if (!r128_engine_acquire_3d()) return false;
    g_r1283d.engine_acquired = true;
    g_r1283d.stat_context_acquires++;
    if (acquired_here) *acquired_here = true;
    return true;
}

static void r1283d_engine_leave(bool acquired_here) {
    if (!acquired_here || !g_r1283d.engine_acquired) return;
    g_r1283d.engine_acquired = false;
    r128_engine_release_3d();
}

static bool r1283d_wait_for_cpu_vram(void) {
    if (!r1283d_cce_active_hw()) {
        g_r1283d.cce_running = false;
        return true;
    }
    if (!r1283d_wait_cce_idle()) return false;
    g_r1283d.completed_fence = g_r1283d.last_fence;
    return true;
}

static bool r1283d_submit_words(const uint32_t *words, uint32_t count) {
    uint32_t i = 0U;
    if (!words || !count) return false;
    while (i + 1U < count) {
        if (!r1283d_wait_pio_slots(2U)) return false;
        r1283d_write(R128_PM4_FIFO_DATA_EVEN, words[i++]);
        r1283d_write(R128_PM4_FIFO_DATA_ODD, words[i++]);
    }
    if (i < count) {
        if (!r1283d_wait_pio_slots(2U)) return false;
        r1283d_write(R128_PM4_FIFO_DATA_EVEN, words[i]);
        r1283d_write(R128_PM4_FIFO_DATA_ODD, R128_CCE_PACKET2);
    }
    return true;
}

static bool r1283d_page_used(uint32_t page) {
    return (g_r1283d.page_bitmap[page >> 5] &
            (1U << (page & 31U))) != 0U;
}

static void r1283d_page_set(uint32_t page, bool used) {
    uint32_t mask = 1U << (page & 31U);
    if (used) g_r1283d.page_bitmap[page >> 5] |= mask;
    else g_r1283d.page_bitmap[page >> 5] &= ~mask;
}

static bool r1283d_alloc_pages(uint32_t bytes, uint32_t *first_out,
                               uint32_t *count_out, uint32_t *offset_out) {
    uint32_t pages;
    if (!bytes || !first_out || !count_out || !offset_out) return false;
    pages = ALIGN_UP(bytes, R1283D_PAGE_SIZE) / R1283D_PAGE_SIZE;
    if (!pages || pages > g_r1283d.arena_pages) return false;
    for (uint32_t first = 0; first + pages <= g_r1283d.arena_pages; first++) {
        uint32_t n;
        for (n = 0; n < pages; n++)
            if (r1283d_page_used(first + n)) break;
        if (n != pages) {
            first += n;
            continue;
        }
        for (n = 0; n < pages; n++) r1283d_page_set(first + n, true);
        *first_out = first;
        *count_out = pages;
        *offset_out = g_r1283d.arena_base + first * R1283D_PAGE_SIZE;
        return true;
    }
    return false;
}

static void r1283d_free_pages(uint32_t first, uint32_t count) {
    if (first >= g_r1283d.arena_pages ||
        count > g_r1283d.arena_pages - first) return;
    for (uint32_t i = 0; i < count; i++) r1283d_page_set(first + i, false);
}

static uint32_t r1283d_handle(uint32_t index, uint8_t generation) {
    return ((uint32_t)generation << 8) | (index + 1U);
}

static r1283d_surface_t *r1283d_surface_from_handle(
    gfx3d_surface_handle_t handle) {
    uint32_t index;
    uint8_t generation;
    if (!handle) return NULL;
    index = (handle & 0xFFU);
    if (!index || index > R1283D_MAX_SURFACES) return NULL;
    index--;
    generation = (uint8_t)(handle >> 8);
    if (!g_r1283d.surfaces[index].used ||
        g_r1283d.surfaces[index].generation != generation) return NULL;
    return &g_r1283d.surfaces[index];
}

static uint32_t r1283d_bpp(gfx3d_format_t format) {
#if R1283D_EXPERIMENTAL_Z16
    if (format == GFX3D_FORMAT_Z16) return 2U;
#endif
    if (format == GFX3D_FORMAT_XRGB8888 ||
        format == GFX3D_FORMAT_ARGB8888) return 4U;
    return 0U;
}

static bool r1283d_create_surface(const gfx3d_surface_desc_t *desc,
                                  gfx3d_surface_handle_t *handle_out) {
    uint32_t bpp, pitch, size;
    if (handle_out) *handle_out = GFX3D_SURFACE_INVALID;
    if (!g_r1283d.ready || !desc || !handle_out ||
        !desc->width || !desc->height) return false;
    bpp = r1283d_bpp(desc->format);
    if (!bpp) return false;
    pitch = ALIGN_UP((uint32_t)desc->width * bpp, 32U);
    size = pitch * (uint32_t)desc->height;
    if (size / pitch != desc->height) return false;

    for (uint32_t i = 0; i < R1283D_MAX_SURFACES; i++) {
        r1283d_surface_t *surface;
        uint32_t first, pages, offset;
        uint8_t generation;
        if (g_r1283d.surfaces[i].used) continue;
        if (!r1283d_alloc_pages(size, &first, &pages, &offset)) return false;
        generation = (uint8_t)(g_r1283d.generations[i] + 1U);
        if (!generation) generation = 1U;
        g_r1283d.generations[i] = generation;
        surface = &g_r1283d.surfaces[i];
        kmemset(surface, 0, sizeof(*surface));
        surface->used = true;
        surface->generation = generation;
        surface->width = desc->width;
        surface->height = desc->height;
        surface->format = desc->format;
        surface->flags = desc->flags;
        surface->bytes_per_pixel = bpp;
        surface->pitch = pitch;
        surface->size = size;
        surface->offset = offset;
        surface->page_first = first;
        surface->page_count = pages;
        *handle_out = r1283d_handle(i, generation);
        return true;
    }
    return false;
}

static bool r1283d_destroy_surface(gfx3d_surface_handle_t handle) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    if (!surface) return false;
    if (g_r1283d.current_target == handle) {
        if (g_r1283d.cce_running && !r1283d_wait_cce_idle())
            return false;
        g_r1283d.completed_fence = g_r1283d.last_fence;
        g_r1283d.current_target = GFX3D_SURFACE_INVALID;
        if (g_r1283d.engine_acquired) {
            g_r1283d.engine_acquired = false;
            r128_engine_release_3d();
        }
    }
    r1283d_free_pages(surface->page_first, surface->page_count);
    if (surface->depth_page_count)
        r1283d_free_pages(surface->depth_page_first,
                          surface->depth_page_count);
    kmemset(surface, 0, sizeof(*surface));
    return true;
}

static bool r1283d_rect_for_surface(const r1283d_surface_t *surface,
                                    const gfx_rect_t *requested,
                                    gfx_rect_t *result) {
    gfx_rect_t rect;
    if (!surface || !result) return false;
    if (requested) rect = *requested;
    else rect = (gfx_rect_t){0, 0, surface->width, surface->height};
    if (rect.x < 0) {
        rect.w += rect.x;
        rect.x = 0;
    }
    if (rect.y < 0) {
        rect.h += rect.y;
        rect.y = 0;
    }
    if (rect.x >= surface->width || rect.y >= surface->height ||
        rect.w <= 0 || rect.h <= 0) return false;
    if (rect.w > (int32_t)surface->width - rect.x)
        rect.w = (int32_t)surface->width - rect.x;
    if (rect.h > (int32_t)surface->height - rect.y)
        rect.h = (int32_t)surface->height - rect.y;
    *result = rect;
    return true;
}

static bool r1283d_upload_surface_locked(gfx3d_surface_handle_t handle,
                                  const uint32_t *pixels,
                                  uint32_t source_pitch,
                                  const gfx_rect_t *requested) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    gfx_rect_t rect;
    if (!surface || !pixels || surface->bytes_per_pixel != 4U ||
        !source_pitch || !r1283d_rect_for_surface(surface, requested, &rect))
        return false;
    for (int32_t y = 0; y < rect.h; y++) {
        const uint8_t *src = (const uint8_t *)(pixels +
            (uint32_t)(rect.y + y) * source_pitch + (uint32_t)rect.x);
        volatile uint8_t *dst = g_r1283d.vram + surface->offset +
            (uint32_t)(rect.y + y) * surface->pitch +
            (uint32_t)rect.x * 4U;
        kmemcpy((void *)dst, src, (uint32_t)rect.w * 4U);
    }
    return true;
}

static bool r1283d_download_surface_locked(gfx3d_surface_handle_t handle,
                                    uint32_t *pixels,
                                    uint32_t destination_pitch,
                                    const gfx_rect_t *requested) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    gfx_rect_t rect;
    if (!surface || !pixels || surface->bytes_per_pixel != 4U ||
        !destination_pitch ||
        !r1283d_rect_for_surface(surface, requested, &rect)) return false;
    for (int32_t y = 0; y < rect.h; y++) {
        const volatile uint8_t *src = g_r1283d.vram + surface->offset +
            (uint32_t)(rect.y + y) * surface->pitch +
            (uint32_t)rect.x * 4U;
        uint8_t *dst = (uint8_t *)(pixels +
            (uint32_t)(rect.y + y) * destination_pitch + (uint32_t)rect.x);
        kmemcpy(dst, (const void *)src, (uint32_t)rect.w * 4U);
    }
    return true;
}

static bool r1283d_clear_surface_locked(gfx3d_surface_handle_t handle,
                                 uint32_t color, uint32_t *fence_out) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    if (fence_out) *fence_out = 0U;
    if (!surface) return false;
    if (surface->bytes_per_pixel == 4U) {
        for (uint32_t y = 0; y < surface->height; y++) {
            volatile uint32_t *row = (volatile uint32_t *)
                (g_r1283d.vram + surface->offset + y * surface->pitch);
            for (uint32_t x = 0; x < surface->width; x++) row[x] = color;
        }
    } else if (surface->bytes_per_pixel == 2U) {
        uint16_t value = (uint16_t)color;
        for (uint32_t y = 0; y < surface->height; y++) {
            volatile uint16_t *row = (volatile uint16_t *)
                (g_r1283d.vram + surface->offset + y * surface->pitch);
            for (uint32_t x = 0; x < surface->width; x++) row[x] = value;
        }
    } else {
        return false;
    }
    g_r1283d.last_fence++;
    if (!g_r1283d.last_fence) g_r1283d.last_fence = 1U;
    if (fence_out) *fence_out = g_r1283d.last_fence;
    return true;
}

static bool r1283d_upload_surface(gfx3d_surface_handle_t handle,
                                  const uint32_t *pixels,
                                  uint32_t source_pitch,
                                  const gfx_rect_t *requested) {
    bool acquired_here;
    bool ok;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    ok = r1283d_wait_for_cpu_vram() &&
         r1283d_upload_surface_locked(handle, pixels, source_pitch, requested);
    r1283d_engine_leave(acquired_here);
    return ok;
}

static bool r1283d_upload_surface_region(gfx3d_surface_handle_t handle,
                                          const uint32_t *pixels,
                                          uint32_t source_pitch,
                                          uint32_t destination_x,
                                          uint32_t destination_y,
                                          uint32_t width, uint32_t height) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    bool acquired_here, ok = false;
    if (!surface || !pixels || surface->bytes_per_pixel != 4U ||
        !source_pitch || source_pitch < width || !width || !height ||
        destination_x + width > surface->width ||
        destination_y + height > surface->height)
        return false;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    if (r1283d_wait_for_cpu_vram()) {
        for (uint32_t y = 0; y < height; y++) {
            volatile uint8_t *dst = g_r1283d.vram + surface->offset +
                (destination_y + y) * surface->pitch + destination_x * 4U;
            const uint8_t *src = (const uint8_t *)(pixels + y * source_pitch);
            kmemcpy((void *)dst, src, width * 4U);
        }
        ok = true;
    }
    r1283d_engine_leave(acquired_here);
    return ok;
}

static bool r1283d_download_surface(gfx3d_surface_handle_t handle,
                                    uint32_t *pixels,
                                    uint32_t destination_pitch,
                                    const gfx_rect_t *requested) {
    bool acquired_here;
    bool ok;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    ok = r1283d_wait_for_cpu_vram() &&
         r1283d_download_surface_locked(handle, pixels,
                                        destination_pitch, requested);
    r1283d_engine_leave(acquired_here);
    return ok;
}

#if R1283D_EXPERIMENTAL_Z16
static bool r1283d_ensure_depth(r1283d_surface_t *target);
#endif

static bool r1283d_depth_transfer(gfx3d_surface_handle_t handle,
                                     uint16_t *depth,
                                     uint32_t pitch,
                                     const gfx_rect_t *requested,
                                     bool upload) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    gfx_rect_t rect;
    bool acquired_here, ok = false;
    if (!surface || !depth || !pitch ||
        !r1283d_rect_for_surface(surface, requested, &rect) ||
        pitch < surface->width)
        return false;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    if (r1283d_wait_for_cpu_vram() && r1283d_ensure_depth(surface)) {
        for (int32_t y = 0; y < rect.h; y++) {
            volatile uint16_t *gpu_row = (volatile uint16_t *)(
                g_r1283d.vram + surface->depth_offset +
                (uint32_t)(rect.y + y) * surface->depth_pitch) + rect.x;
            uint16_t *cpu_row = depth +
                (uint32_t)(rect.y + y) * pitch + (uint32_t)rect.x;
            if (upload)
                kmemcpy((void *)gpu_row, cpu_row,
                        (uint32_t)rect.w * sizeof(uint16_t));
            else
                kmemcpy(cpu_row, (const void *)gpu_row,
                        (uint32_t)rect.w * sizeof(uint16_t));
        }
        ok = true;
    }
    r1283d_engine_leave(acquired_here);
    return ok;
}

static bool r1283d_depth_upload(gfx3d_surface_handle_t handle,
                                const uint16_t *depth, uint32_t pitch,
                                const gfx_rect_t *rect) {
    return r1283d_depth_transfer(handle, (uint16_t *)depth, pitch, rect, true);
}

static bool r1283d_depth_download(gfx3d_surface_handle_t handle,
                                  uint16_t *depth, uint32_t pitch,
                                  const gfx_rect_t *rect) {
    return r1283d_depth_transfer(handle, depth, pitch, rect, false);
}

static bool r1283d_clear_surface(gfx3d_surface_handle_t handle,
                                 uint32_t color, uint32_t *fence_out) {
    bool acquired_here;
    bool ok;
    if (fence_out) *fence_out = 0U;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    ok = r1283d_wait_for_cpu_vram() &&
         r1283d_clear_surface_locked(handle, color, fence_out);
    if (ok) g_r1283d.completed_fence = g_r1283d.last_fence;
    r1283d_engine_leave(acquired_here);
    return ok;
}

#if R1283D_EXPERIMENTAL_Z16
static bool r1283d_ensure_depth(r1283d_surface_t *target) {
    uint32_t pitch, size;
    if (!target || target->bytes_per_pixel != 4U) return false;
    if (target->depth_page_count) return true;
    pitch = ALIGN_UP((uint32_t)target->width * 2U, 32U);
    size = pitch * (uint32_t)target->height;
    if (!r1283d_alloc_pages(size, &target->depth_page_first,
                            &target->depth_page_count,
                            &target->depth_offset)) return false;
    target->depth_pitch = pitch;
    return true;
}

static void r1283d_clear_depth(r1283d_surface_t *target, float depth) {
    uint16_t value;
    if (!target || !target->depth_page_count) return;
    if (depth <= 0.0f) value = 0U;
    else if (depth >= 1.0f) value = 0xFFFFU;
    else value = (uint16_t)(depth * 65535.0f + 0.5f);
    for (uint32_t y = 0; y < target->height; y++) {
        volatile uint16_t *row = (volatile uint16_t *)
            (g_r1283d.vram + target->depth_offset +
             y * target->depth_pitch);
        for (uint32_t x = 0; x < target->width; x++) row[x] = value;
    }
}

#endif

static bool r1283d_begin_render(gfx3d_surface_handle_t target_handle,
                                uint32_t clear_color, float clear_depth,
                                uint32_t flags) {
    r1283d_surface_t *target = r1283d_surface_from_handle(target_handle);
    bool acquired_here = false;

    if (!g_r1283d.ready || !target || g_r1283d.engine_acquired ||
        g_r1283d.current_target != GFX3D_SURFACE_INVALID ||
        target->bytes_per_pixel != 4U ||
        !(target->flags & GFX3D_SURFACE_RENDER_TARGET)) return false;
    if (flags & (GFX3D_DRAW_DEPTH_TEST | GFX3D_DRAW_DEPTH_WRITE |
                 GFX3D_DRAW_CLEAR_DEPTH)) {
#if R1283D_EXPERIMENTAL_Z16
        if (!r1283d_ensure_depth(target)) return false;
#else
        return false;
#endif
    }

    /* Keep the shared owner for the complete begin/draw/end sequence.  This
     * lets CCE commands remain queued without a GUI operation changing PM4
     * underneath them. */
    if (!r1283d_engine_enter(&acquired_here) || !acquired_here)
        return false;
    if (!r1283d_wait_for_cpu_vram()) {
        r1283d_engine_leave(true);
        return false;
    }
    if ((flags & GFX3D_DRAW_CLEAR_COLOR) &&
        !r1283d_clear_surface_locked(target_handle, clear_color, NULL)) {
        r1283d_engine_leave(true);
        return false;
    }
    if (flags & GFX3D_DRAW_CLEAR_COLOR)
        g_r1283d.completed_fence = g_r1283d.last_fence;
#if R1283D_EXPERIMENTAL_Z16
    if (flags & GFX3D_DRAW_CLEAR_DEPTH)
        r1283d_clear_depth(target, clear_depth);
#else
    (void)clear_depth;
#endif
    g_r1283d.current_target = target_handle;
    if (!r1283d_cce_start()) {
        g_r1283d.current_target = GFX3D_SURFACE_INVALID;
        r1283d_disable_after_hang("begin_render/cce_start");
        return false;
    }
    return true;
}

static uint32_t r1283d_pitch_offset(uint32_t pitch_bytes,
                                    uint32_t offset_bytes,
                                    uint32_t bytes_per_pixel) {
    uint32_t pitch_pixels = pitch_bytes / bytes_per_pixel;
    return ((pitch_pixels / 8U) << 21) | (offset_bytes >> 5);
}

static bool r1283d_emit_reg(uint32_t reg, uint32_t value) {
    uint32_t pair[2];
    pair[0] = CCE_PACKET0(reg, 0U);
    pair[1] = value;
    return r1283d_submit_words(pair, 2U);
}

static bool r1283d_power_of_two_log(uint32_t value, uint32_t *log_out) {
    uint32_t log = 0U;
    if (!value || (value & (value - 1U))) return false;
    while (value > 1U) {
        value >>= 1;
        log++;
    }
    if (log > 10U) return false;
    if (log_out) *log_out = log;
    return true;
}

static bool r1283d_valid_texture(const r1283d_surface_t *texture) {
    uint32_t ignored;
    return texture && (texture->flags & GFX3D_SURFACE_TEXTURE) &&
           texture->format == GFX3D_FORMAT_ARGB8888 &&
           texture->width >= 8U && texture->height >= 8U &&
           texture->pitch == (uint32_t)texture->width * 4U &&
           r1283d_power_of_two_log(texture->width, &ignored) &&
           r1283d_power_of_two_log(texture->height, &ignored);
}

static uint32_t r1283d_depth_compare(uint32_t flags) {
    switch (gfx3d_draw_depth_func(flags)) {
    case GFX3D_DEPTH_NEVER: return R128_Z_TEST_NEVER;
    case GFX3D_DEPTH_LESS: return R128_Z_TEST_LESS;
    case GFX3D_DEPTH_EQUAL: return R128_Z_TEST_EQUAL;
    case GFX3D_DEPTH_LEQUAL: return R128_Z_TEST_LESSEQUAL;
    case GFX3D_DEPTH_GREATER: return R128_Z_TEST_GREATER;
    case GFX3D_DEPTH_NOTEQUAL: return R128_Z_TEST_NOTEQUAL;
    case GFX3D_DEPTH_GEQUAL: return R128_Z_TEST_GREATEREQUAL;
    default: return R128_Z_TEST_ALWAYS;
    }
}

static bool r1283d_program_state(r1283d_surface_t *target,
                                 r1283d_surface_t *texture,
                                 uint32_t flags) {
    uint32_t tex_cntl = R128_TEX_CACHE_FLUSH;
    uint32_t z_cntl = R128_Z_PIX_WIDTH_16 | R128_Z_TEST_ALWAYS;
    uint32_t blend;
    uint32_t setup;
    uint32_t fpu;
    uint32_t gmc;
    uint32_t prim_tex = R128_MIP_MAP_DISABLE | R128_DATATYPE_ARGB8888;
    uint32_t combine = R128_COMB_MODULATE | R128_COLOR_FACTOR_TEX |
                       R128_INPUT_FACTOR_INT_COLOR |
                       R128_COMB_ALPHA_MODULATE |
                       R128_ALPHA_FACTOR_TEX_ALPHA |
                       R128_INP_FACTOR_A_INT_ALPHA;
    uint32_t size_pitch = 0U;

    if (flags & GFX3D_DRAW_DEPTH_TEST) {
        tex_cntl |= R128_Z_ENABLE;
        z_cntl = R128_Z_PIX_WIDTH_16 | r1283d_depth_compare(flags);
    }
    if (flags & GFX3D_DRAW_DEPTH_WRITE) tex_cntl |= R128_Z_WRITE_ENABLE;
    if (flags & GFX3D_DRAW_BLEND) {
        tex_cntl |= R128_ALPHA_ENABLE;
        blend = (R128_ALPHA_BLEND_SRCALPHA << R128_ALPHA_BLEND_SRC_SHIFT) |
                (((flags & GFX3D_DRAW_BLEND_ADDITIVE) ?
                  R128_ALPHA_BLEND_ONE : R128_ALPHA_BLEND_INVSRCALPHA)
                 << R128_ALPHA_BLEND_DST_SHIFT);
    } else {
        blend = (R128_ALPHA_BLEND_ONE << R128_ALPHA_BLEND_SRC_SHIFT);
    }
    if (texture) {
        uint32_t log_width, log_height, log_size;
        if (!r1283d_valid_texture(texture) ||
            !r1283d_power_of_two_log(texture->width, &log_width) ||
            !r1283d_power_of_two_log(texture->height, &log_height))
            return false;
        log_size = log_width > log_height ? log_width : log_height;
        tex_cntl |= R128_TEXMAP_ENABLE;
        if (flags & GFX3D_DRAW_LINEAR)
            prim_tex |= R128_MIN_BLEND_LINEAR | R128_MAG_BLEND_LINEAR;
        prim_tex |= (flags & GFX3D_DRAW_REPEAT_U) ?
            R128_TEX_WRAP_S : R128_TEX_CLAMP_S_CLAMP;
        prim_tex |= (flags & GFX3D_DRAW_REPEAT_V) ?
            R128_TEX_WRAP_T : R128_TEX_CLAMP_T_CLAMP;
        size_pitch = log_width | (log_size << 4) |
                     (log_height << 8) | (log_size << 12);
    }

    setup = R128_COLOR_GOURAUD | R128_PRIM_TYPE_TRI |
            R128_TEXTURE_ST_MULT_W | R128_STARTING_VERTEX_1 |
            R128_ENDING_VERTEX_3 | R128_SU_POLY_LINE_NOT_LAST |
            R128_SUB_PIX_4BITS;
    fpu = R128_FRONT_DIR_CCW | R128_BACKFACE_SOLID |
          R128_FRONTFACE_SOLID | R128_FPU_COLOR_GOURAUD |
          R128_FPU_SUB_PIX_4BITS | R128_FPU_MODE_3D |
          R128_TRAP_BITS_DISABLE | R128_FLAT_SHADE_VERTEX_OGL |
          R128_FPU_ROUND_TRUNCATE | R128_WM_SEL_8DW;
    gmc = R128_GMC_DST_PITCH_OFFSET_CNTL |
          R128_GMC_BRUSH_SOLID_COLOR | R128_GMC_DST_32BPP |
          R128_GMC_SRC_DATATYPE_COLOR | R128_ROP3_S |
          R128_DP_SRC_SOURCE_MEMORY | R128_GMC_3D_FCN_EN |
          R128_GMC_CLR_CMP_CNTL_DIS | R128_GMC_AUX_CLIP_DIS |
          R128_GMC_WR_MSK_DIS;

    return
        r1283d_emit_reg(R128_SCALE_3D_CNTL,
            R128_SCALE_3D_TEXMAP_SHADE | R128_SCALE_PIX_REPLICATE |
            R128_TEX_CACHE_SPLIT | R128_TEX_MAP_ALPHA_IN_TEXTURE |
            R128_TEX_CACHE_LINE_SIZE_4QW) &&
        r1283d_emit_reg(R128_SETUP_CNTL, setup) &&
        r1283d_emit_reg(R128_PM4_VC_FPU_SETUP, fpu) &&
        r1283d_emit_reg(R128_PLANE_3D_MASK_C, 0xFFFFFFFFU) &&
        r1283d_emit_reg(R128_CONSTANT_COLOR_C, 0xFFFFFFFFU) &&
        r1283d_emit_reg(R128_WINDOW_XY_OFFSET, 0U) &&
        r1283d_emit_reg(R128_DST_PITCH_OFFSET_C,
            r1283d_pitch_offset(target->pitch, target->offset, 4U)) &&
        r1283d_emit_reg(R128_DP_GUI_MASTER_CNTL_C, gmc) &&
        r1283d_emit_reg(R128_SC_TOP_LEFT_C, 0U) &&
        r1283d_emit_reg(R128_SC_BOTTOM_RIGHT_C,
            ((uint32_t)(target->height - 1U) << 16) |
            (uint32_t)(target->width - 1U)) &&
        r1283d_emit_reg(R128_Z_OFFSET_C,
            target->depth_page_count ? (target->depth_offset >> 5) : 0U) &&
        r1283d_emit_reg(R128_Z_PITCH_C,
            target->depth_page_count ?
                ((target->depth_pitch / 2U) / 8U) : 0U) &&
        r1283d_emit_reg(R128_Z_STEN_CNTL_C, z_cntl) &&
        r1283d_emit_reg(R128_MISC_3D_STATE_CNTL_REG,
            R128_MISC_SCALE_3D_TEXMAP_SHADE |
            R128_MISC_SCALE_PIX_REPLICATE |
            R128_ALPHA_COMB_ADD_CLAMP | blend) &&
        r1283d_emit_reg(R128_TEX_CNTL_C, tex_cntl) &&
        r1283d_emit_reg(R128_PRIM_TEX_CNTL_C, prim_tex) &&
        r1283d_emit_reg(R128_PRIM_TEXTURE_COMBINE_CNTL_C, combine) &&
        r1283d_emit_reg(R128_TEX_SIZE_PITCH_C, size_pitch) &&
        r1283d_emit_reg(R128_PRIM_TEX_0_OFFSET_C,
            texture ? texture->offset : 0U) &&
        r1283d_emit_reg(R128_PC_GUI_CTLSTAT, R128_PC_FLUSH_GUI);
}

static bool r1283d_submit_vertices(const gfx3d_vertex_t *vertices,
                                   uint32_t vertex_count, bool textured) {
    uint32_t total_words;
    uint32_t stride = textured ? 7U : 5U;
    uint32_t *words;
    uint32_t pos = 0U;
    bool ok;
    if (!vertices || !vertex_count || vertex_count > R1283D_MAX_VERTICES)
        return false;
    total_words = 3U + vertex_count * stride;
    words = (uint32_t *)kmalloc((total_words + 1U) * sizeof(uint32_t));
    if (!words) return false;
    words[pos++] = CCE_PACKET3(R128_CCE_PACKET3_3D_RNDR_GEN_PRIM,
                               1U + vertex_count * stride);
    words[pos++] = R128_CCE_VC_FRMT_RHW |
                   R128_CCE_VC_FRMT_DIFFUSE_ARGB |
                   (textured ? R128_CCE_VC_FRMT_S_T : 0U);
    words[pos++] = R128_CCE_VC_CNTL_PRIM_TYPE_TRI_LIST |
                   R128_CCE_VC_CNTL_PRIM_WALK_RING |
                   (vertex_count << R128_CCE_VC_CNTL_NUM_SHIFT);
    for (uint32_t i = 0; i < vertex_count; i++) {
        words[pos++] = r1283d_float_word(vertices[i].x);
        words[pos++] = r1283d_float_word(vertices[i].y + 0.125f);
        words[pos++] = r1283d_float_word(vertices[i].z);
        words[pos++] = r1283d_float_word(vertices[i].rhw);
        words[pos++] = vertices[i].color;
        if (textured) {
            words[pos++] = r1283d_float_word(vertices[i].u);
            words[pos++] = r1283d_float_word(vertices[i].v);
        }
    }
    if (pos & 1U) words[pos++] = R128_CCE_PACKET2;
    ok = r1283d_submit_words(words, pos);
    kfree(words);
    return ok;
}

static void r1283d_disable_after_hang(const char *where) {
    uint32_t stat = g_r1283d.mmio ? r1283d_read(R128_PM4_STAT) : 0U;
    kprintf("[ATIR1283D.DVR] TIMEOUT en %s PM4_STAT=0x%x; "
            "se desactiva 3D y TinyGL vuelve a CPU\n",
            where ? where : "desconocido", stat);
    r1283d_cce_stop();
    r1283d_engine_reset();
    g_r1283d.disabled = true;
    g_r1283d.ready = false;
    g_r1283d.current_target = GFX3D_SURFACE_INVALID;
    if (g_r1283d.engine_acquired) {
        g_r1283d.engine_acquired = false;
        r128_engine_release_3d();
    }
    r128_engine_report("timeout 3D");
}

static bool r1283d_draw_triangles(gfx3d_surface_handle_t target_handle,
                                  gfx3d_surface_handle_t texture,
                                  const gfx3d_vertex_t *vertices,
                                  uint32_t vertex_count, uint32_t flags,
                                  uint32_t *fence_out) {
    r1283d_surface_t *target = r1283d_surface_from_handle(target_handle);
    r1283d_surface_t *texture_surface =
        r1283d_surface_from_handle(texture);
    bool textured = (flags & GFX3D_DRAW_TEXTURED) != 0U;
    bool ok;
    if (fence_out) *fence_out = 0U;
    if (!g_r1283d.ready || !g_r1283d.engine_acquired || !target ||
        !vertices || vertex_count < 3U || (vertex_count % 3U) != 0U ||
        vertex_count > R1283D_MAX_VERTICES ||
        g_r1283d.current_target != target_handle) return false;
    if (textured != (texture != GFX3D_SURFACE_INVALID) ||
        (textured && !r1283d_valid_texture(texture_surface))) return false;
    if (flags & (GFX3D_DRAW_DEPTH_TEST | GFX3D_DRAW_DEPTH_WRITE)) {
#if R1283D_EXPERIMENTAL_Z16
        if (!r1283d_ensure_depth(target)) return false;
#else
        return false;
#endif
    }

    if (!g_r1283d.cce_running && !r1283d_cce_start()) {
        r1283d_disable_after_hang("cce_start");
        return false;
    }
    ok = r1283d_program_state(target, texture_surface, flags) &&
         r1283d_submit_vertices(vertices, vertex_count, textured);
    if (!ok) {
        r1283d_disable_after_hang("draw_triangles");
        return false;
    }
    g_r1283d.last_fence++;
    if (!g_r1283d.last_fence) g_r1283d.last_fence = 1U;
    if (fence_out) *fence_out = g_r1283d.last_fence;
    return true;
}

static bool r1283d_end_render(gfx3d_surface_handle_t target,
                              uint32_t *fence_out) {
    if (fence_out) *fence_out = 0U;
    if (!g_r1283d.ready || !g_r1283d.engine_acquired ||
        target != g_r1283d.current_target ||
        !r1283d_surface_from_handle(target)) return false;
    if (!r1283d_wait_cce_idle()) {
        r1283d_disable_after_hang("end_render");
        return false;
    }

    /* Do not stop CCE here.  It remains available for the next 3D frame and
     * the shared 2D owner stops it only when a GUI operation really needs the
     * engine. */
    g_r1283d.completed_fence = g_r1283d.last_fence;
    g_r1283d.current_target = GFX3D_SURFACE_INVALID;
    if (fence_out) *fence_out = g_r1283d.last_fence;
    g_r1283d.engine_acquired = false;
    r128_engine_release_3d();
    return true;
}

static bool r1283d_present_surface(gfx3d_surface_handle_t handle,
                                   const gfx_rect_t *requested,
                                   uint32_t *fence_out) {
    r1283d_surface_t *surface = r1283d_surface_from_handle(handle);
    const gfx_info_t *info = gfx_get_info();
    gfx_rect_t rect;
    uint32_t fb_delta;
    uint32_t bytes;

    if (fence_out) *fence_out = 0U;
    if (!surface || !info || g_r1283d.engine_acquired ||
        g_r1283d.current_target != GFX3D_SURFACE_INVALID ||
        info->bpp != 32U || info->framebuffer < g_r1283d.fb_bar ||
        !r1283d_rect_for_surface(surface, requested, &rect))
        return false;
    if (rect.x >= info->width || rect.y >= info->height) return false;
    if (rect.w > (int32_t)info->width - rect.x)
        rect.w = (int32_t)info->width - rect.x;
    if (rect.h > (int32_t)info->height - rect.y)
        rect.h = (int32_t)info->height - rect.y;
    if (rect.w <= 0 || rect.h <= 0) return false;

    /* Preferred path: the 2D driver owns scanout and performs the copy inside
     * VRAM.  The coordinator waits for 3D before changing PM4. */
    if (r128_engine_present_vram32(surface->offset, surface->pitch,
                                   rect.x, rect.y, rect.x, rect.y,
                                   rect.w, rect.h)) {
        g_r1283d.stat_present_hw++;
        if ((g_r1283d.stat_present_hw +
             g_r1283d.stat_present_cpu_calls) % 120U == 0U)
            r1283d_perf_report("periodico");
        g_r1283d.last_fence++;
        if (!g_r1283d.last_fence) g_r1283d.last_fence = 1U;
        g_r1283d.completed_fence = g_r1283d.last_fence;
        if (fence_out) *fence_out = g_r1283d.last_fence;
        return true;
    }

    /* Diagnostic fallback only.  It is explicitly counted as CPU traffic and
     * still acquires the 2D owner so it cannot race a CCE submission. */
    if (!r128_engine_acquire_2d()) return false;
    fb_delta = info->framebuffer - g_r1283d.fb_bar;
    for (int32_t y = 0; y < rect.h; y++) {
        const volatile uint8_t *src = g_r1283d.vram + surface->offset +
            (uint32_t)(rect.y + y) * surface->pitch +
            (uint32_t)rect.x * 4U;
        volatile uint8_t *dst = g_r1283d.vram + fb_delta +
            (uint32_t)(rect.y + y) * info->pitch +
            (uint32_t)rect.x * 4U;
        kmemcpy((void *)dst, (const void *)src, (uint32_t)rect.w * 4U);
    }
    r128_engine_release_2d();
    bytes = (uint32_t)rect.w * (uint32_t)rect.h * 4U;
    g_r1283d.stat_present_cpu_calls++;
    g_r1283d.stat_present_cpu_bytes += bytes;
    if ((g_r1283d.stat_present_hw +
         g_r1283d.stat_present_cpu_calls) % 120U == 0U)
        r1283d_perf_report("periodico");
    g_r1283d.last_fence++;
    if (!g_r1283d.last_fence) g_r1283d.last_fence = 1U;
    g_r1283d.completed_fence = g_r1283d.last_fence;
    if (fence_out) *fence_out = g_r1283d.last_fence;
    return true;
}

static bool r1283d_composite_surface(gfx3d_surface_handle_t source,
                                     gfx3d_surface_handle_t destination,
                                     const gfx3d_composite_t *operation,
                                     uint32_t *fence_out) {
    (void)source;
    (void)destination;
    (void)operation;
    if (fence_out) *fence_out = 0U;
    return false;
}

static bool r1283d_wait_fence(uint32_t fence) {
    bool acquired_here;
    bool ok;

    if (!fence) return true;
    if (!g_r1283d.ready || fence > g_r1283d.last_fence) return false;
    if (fence <= g_r1283d.completed_fence) return true;
    if (!r1283d_engine_enter(&acquired_here)) return false;
    ok = r1283d_wait_for_cpu_vram();
    r1283d_engine_leave(acquired_here);
    return ok && fence <= g_r1283d.completed_fence;
}

static bool r1283d_internal_smoke_test(void) {
    gfx3d_surface_desc_t desc;
    gfx3d_surface_handle_t handle = GFX3D_SURFACE_INVALID;
    gfx3d_surface_handle_t texture = GFX3D_SURFACE_INVALID;
    gfx3d_vertex_t vertices[3];
    uint32_t checker[64];
    r1283d_surface_t *surface;
    uint32_t clear = 0xFF102030U;
    uint32_t changed = 0U;
    bool ok;

    desc.width = 64U;
    desc.height = 64U;
    desc.format = GFX3D_FORMAT_ARGB8888;
    desc.flags = GFX3D_SURFACE_RENDER_TARGET | GFX3D_SURFACE_DYNAMIC;
    if (!r1283d_create_surface(&desc, &handle)) return false;
    desc.width = 8U;
    desc.height = 8U;
    desc.format = GFX3D_FORMAT_ARGB8888;
    desc.flags = GFX3D_SURFACE_TEXTURE | GFX3D_SURFACE_DYNAMIC;
    if (!r1283d_create_surface(&desc, &texture)) {
        (void)r1283d_destroy_surface(handle);
        return false;
    }
    for (uint32_t i=0U;i<64U;i++)
        checker[i]=((i+(i>>3))&1U)?0xFFFFFFFFU:0xFF4080FFU;
    if (!r1283d_upload_surface(texture,checker,8U,NULL)) {
        (void)r1283d_destroy_surface(texture);
        (void)r1283d_destroy_surface(handle);
        return false;
    }
    vertices[0] = (gfx3d_vertex_t){32.0f, 8.0f, 0.5f, 1.0f,
                                   0xFFFF0000U, 0.0f, 0.0f};
    vertices[1] = (gfx3d_vertex_t){8.0f, 56.0f, 0.5f, 1.0f,
                                   0xFF00FF00U, 0.0f, 0.0f};
    vertices[2] = (gfx3d_vertex_t){56.0f, 56.0f, 0.5f, 1.0f,
                                   0xFF0000FFU, 0.0f, 0.0f};
    ok = r1283d_begin_render(handle, clear, 0.0f,
                             GFX3D_DRAW_CLEAR_COLOR |
                             GFX3D_DRAW_CLEAR_DEPTH |
                             GFX3D_DRAW_DEPTH_TEST |
                             GFX3D_DRAW_DEPTH_WRITE) &&
         r1283d_draw_triangles(handle, texture, vertices, 3U,
                               GFX3D_DRAW_TEXTURED |
                               GFX3D_DRAW_LINEAR |
                               GFX3D_DRAW_DEPTH_TEST |
                               GFX3D_DRAW_DEPTH_WRITE |
                               GFX3D_DRAW_REVERSED_DEPTH, NULL) &&
         r1283d_end_render(handle, NULL);
    surface = r1283d_surface_from_handle(handle);
    if (ok && surface) {
        for (uint32_t y = 16U; y < 56U && !changed; y++) {
            const volatile uint32_t *row = (const volatile uint32_t *)
                (g_r1283d.vram + surface->offset + y * surface->pitch);
            for (uint32_t x = 12U; x < 52U; x++) {
                if (row[x] != clear) {
                    changed = row[x];
                    break;
                }
            }
        }
    }
    (void)r1283d_destroy_surface(texture);
    (void)r1283d_destroy_surface(handle);
    kprintf("[ATIR1283D:TRACE] selftest CCE+Z16+textura %s sample=0x%x\n",
            ok && changed ? "OK" : "FAIL", changed);
    return ok && changed;
}

static bool r1283d_run_selftest(uint32_t *fence_out) {
    bool ok;
    if (fence_out) *fence_out = 0U;
    if (!g_r1283d.ready) return false;
    ok = r1283d_internal_smoke_test();
    if (ok && fence_out) *fence_out = g_r1283d.last_fence;
    return ok;
}

static void r1283d_release(bool restore_overlay) {
    bool acquired_here = false;
    bool owns_engine = g_r1283d.engine_acquired;

    if (g_r1283d.mmio && !owns_engine)
        owns_engine = r1283d_engine_enter(&acquired_here);
    if (g_r1283d.mmio && owns_engine) {
        if (r1283d_cce_active_hw()) (void)r1283d_wait_cce_idle();
        g_r1283d.completed_fence = g_r1283d.last_fence;
        r1283d_cce_stop();
        if (restore_overlay && g_r1283d.overlay_saved)
            r1283d_write(R128_OV0_SCALE_CNTL,
                         g_r1283d.saved_overlay_scale);
    }
    if (g_r1283d.engine_acquired) {
        g_r1283d.engine_acquired = false;
        r128_engine_release_3d();
    }
    (void)acquired_here;
    for (uint32_t i = 0; i < R1283D_MAX_SURFACES; i++)
        kmemset(&g_r1283d.surfaces[i], 0,
                sizeof(g_r1283d.surfaces[i]));
    kmemset(g_r1283d.page_bitmap, 0, sizeof(g_r1283d.page_bitmap));
    if (g_r1283d.stat_context_acquires || g_r1283d.stat_cce_starts ||
        g_r1283d.stat_present_hw || g_r1283d.stat_present_cpu_calls)
        r1283d_perf_report("release");
    g_r1283d.ready = false;
    g_r1283d.current_target = GFX3D_SURFACE_INVALID;
}

static void r1283d_reset_driver(void) {
    bool was_detected = g_r1283d.detected;
    const pci_device_t *pci = g_r1283d.pci;
    volatile uint8_t *mmio = g_r1283d.mmio;
    volatile uint8_t *vram = g_r1283d.vram;
    uint32_t fb = g_r1283d.fb_bar;
    uint32_t mmio_bar = g_r1283d.mmio_bar;
    uint32_t vram_size = g_r1283d.vram_size;
    r1283d_release(true);
    kmemset(&g_r1283d, 0, sizeof(g_r1283d));
    g_r1283d.detected = was_detected;
    g_r1283d.pci = pci;
    g_r1283d.mmio = mmio;
    g_r1283d.vram = vram;
    g_r1283d.fb_bar = fb;
    g_r1283d.mmio_bar = mmio_bar;
    g_r1283d.vram_size = vram_size;
}

static bool r1283d_prepare_for_mode(const gfx_info_t *info) {
    uint32_t fb_delta;
    uint32_t visible_end;
    uint32_t reserved_end;
    uint32_t limit;
    bool acquired_here;
    bool microcode_ok;

    if (!info || info->mode != GFX_MODE_ATI_RAGE128 || info->bpp != 32U ||
        !info->width || !info->height || info->pitch < info->width * 4U ||
        info->framebuffer < g_r1283d.fb_bar ||
        info->framebuffer >= g_r1283d.fb_bar + g_r1283d.vram_size)
        return false;

    fb_delta = info->framebuffer - g_r1283d.fb_bar;
    visible_end = ALIGN_UP(fb_delta +
        (uint32_t)info->pitch * info->height, R1283D_PAGE_SIZE);
    reserved_end = r128_engine_reserved_vram_end();
    if (reserved_end > visible_end)
        visible_end = ALIGN_UP(reserved_end, R1283D_PAGE_SIZE);
    limit = ALIGN_DOWN(g_r1283d.vram_size - R1283D_TOP_RESERVE,
                       R1283D_PAGE_SIZE);
    if (visible_end >= limit ||
        (limit - visible_end) / R1283D_PAGE_SIZE > R1283D_MAX_PAGES)
        return false;

    g_r1283d.mode = *info;
    g_r1283d.arena_base = visible_end;
    g_r1283d.arena_limit = limit;
    g_r1283d.arena_pages = (limit - visible_end) / R1283D_PAGE_SIZE;
    kmemset(g_r1283d.page_bitmap, 0, sizeof(g_r1283d.page_bitmap));
    kmemset(g_r1283d.surfaces, 0, sizeof(g_r1283d.surfaces));

    if (!r1283d_engine_enter(&acquired_here)) return false;
    if (!g_r1283d.overlay_saved) {
        g_r1283d.saved_overlay_scale = r1283d_read(R128_OV0_SCALE_CNTL);
        g_r1283d.overlay_saved = true;
    }
    r1283d_write(R128_OV0_SCALE_CNTL, 0U);
    microcode_ok = r1283d_load_microcode();
    r1283d_engine_leave(acquired_here);
    if (!microcode_ok) return false;

    g_r1283d.ready = true;
    g_r1283d.disabled = false;
    g_r1283d.smoke_tested = true;
    g_r1283d.smoke_ok = r1283d_internal_smoke_test();
    if (!g_r1283d.smoke_ok) {
        g_r1283d.ready = false;
        g_r1283d.disabled = true;
        return false;
    }
    kprintf("[ATIR1283D.DVR] CCE 192PIO activo, VRAM 3D=0x%x..0x%x "
            "(%u KiB), reserva2D=0x%x, modo=%ux%ux32\n",
            g_r1283d.arena_base, g_r1283d.arena_limit,
            (g_r1283d.arena_limit - g_r1283d.arena_base) / 1024U,
            reserved_end, info->width, info->height);
    kprintf("[ATIR1283D.DVR] propietario 2D/3D compartido; present VRAM->VRAM; "
            "CCE persistente entre frames\n");
    return true;
}

static bool r1283d_probe(gfx3d_info_t *info_out) {
    const gfx_info_t *info;
    if (info_out) kmemset(info_out, 0, sizeof(*info_out));
    if (g_r1283d.disabled || !r1283d_detect_pci()) return false;
    info = gfx_get_info();
    if (!info || info->mode != GFX_MODE_ATI_RAGE128 || info->bpp != 32U) {
        if (info && info->mode == GFX_MODE_ATI_RAGE128 &&
            !g_r1283d.absence_reported) {
            kprintf("[ATIR1283D.DVR] Rage 128 activa en %u bpp; "
                    "3D requiere scanout de 32 bpp\n", info->bpp);
            g_r1283d.absence_reported = true;
        }
        return false;
    }
    g_r1283d.absence_reported = false;
    if (g_r1283d.ready &&
        (g_r1283d.mode.framebuffer != info->framebuffer ||
         g_r1283d.mode.width != info->width ||
         g_r1283d.mode.height != info->height ||
         g_r1283d.mode.pitch != info->pitch)) {
        r1283d_release(false);
    }
    if (!g_r1283d.ready && !r1283d_prepare_for_mode(info)) return false;
    if (info_out) {
        info_out->available = true;
        info_out->driver_name = "ati_rage128_3d";
        info_out->transport_name = "CCE PM4 192PIO + owner compartido";
        info_out->capabilities = R1283D_CAPABILITIES;
        info_out->host_hw_version = 0x00000128U;
        info_out->guest_hw_version = 1U;
        info_out->transport_generation = 1U;
    }
    return true;
}

static bool ati_rage128_3d_driver_init(void) {
    static const gfx3d_driver_ops_t ops = {
        BK_GFX3D_DRIVER_ABI_VERSION,
        sizeof(gfx3d_driver_ops_t),
        "ati_rage128_3d",
        240U,
        R1283D_CAPABILITIES,
        r1283d_probe,
        r1283d_reset_driver,
        r1283d_create_surface,
        r1283d_destroy_surface,
        r1283d_upload_surface,
        r1283d_download_surface,
        r1283d_clear_surface,
        r1283d_composite_surface,
        r1283d_present_surface,
        r1283d_begin_render,
        r1283d_draw_triangles,
        r1283d_end_render,
        r1283d_wait_fence,
        r1283d_run_selftest,
        r1283d_upload_surface_region,
        r1283d_depth_upload,
        r1283d_depth_download
    };
    if (!gfx3d_register_driver(&ops)) return false;
    kprintf("[ATIR1283D.DVR] extension registrada; espera ATIR128 32bpp\n");
    return true;
}

static void ati_rage128_3d_driver_shutdown(void) {
    r1283d_release(true);
    r128_engine_report("shutdown 3D");
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "ati_rage128_3d",
        "ATI Rage 128 CCE 3D: Gouraud, Z16, textura ARGB, alfa y TinyGL",
        ati_rage128_3d_driver_init,
        ati_rage128_3d_driver_shutdown
    };
    return &module;
}
