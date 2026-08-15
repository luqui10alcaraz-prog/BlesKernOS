/*
 * Intel 852GME + GMA 9xx conservative display driver.
 *
 * The video BIOS remains responsible for clocks, pipes and panel fitting.
 * BlesKernOS adopts the active display plane, including its hardware stride
 * and aperture offset.  Indexed, RGB565 and RGB888 modes are presented by a
 * safe CPU conversion; the legacy XY blitter is restricted to later 32-bpp
 * chips and enabled only after an off-screen color-BLT smoke test succeeds.
 */

#include "../../include/types.h"
#include "../../include/memory.h"
#include "../../include/driver.h"
#include "../../include/gfx.h"
#include "../../include/gfx_driver.h"
#include "../../include/pci.h"
#include "../../include/vesa.h"
#include "../../stdio.h"

#define INTEL_VENDOR_ID             0x8086U
#define GMA_DSPA_CNTR               0x70180U
#define GMA_DSPA_BASE               0x70184U
#define GMA_DSPA_STRIDE             0x70188U
#define GMA_DSPB_CNTR               0x71180U
#define GMA_DSPB_BASE               0x71184U
#define GMA_DSPB_STRIDE             0x71188U
#define GMA_DISPLAY_PLANE_ENABLED   (1U << 31)
#define GMA_DISPLAY_PIPE_B          (1U << 24)
#define GMA_DISPLAY_FORMAT_MASK     (0xFU << 26)
#define GMA_DISPLAY_FORMAT_SHIFT    26U
#define GMA_FORMAT_INDEXED8         2U
#define GMA_FORMAT_BGRA555          3U
#define GMA_FORMAT_BGRX555          4U
#define GMA_FORMAT_BGRX565          5U
#define GMA_FORMAT_BGRX888          6U
#define GMA_FORMAT_BGRA888          7U
#define GMA_RING_BASE               0x02000U
#define GMA_RING_TAIL               (GMA_RING_BASE + 0x30U)
#define GMA_RING_HEAD               (GMA_RING_BASE + 0x34U)
#define GMA_RING_START              (GMA_RING_BASE + 0x38U)
#define GMA_RING_CTL                (GMA_RING_BASE + 0x3CU)
#define GMA_HWS_PGA                 0x02080U
#define GMA_RING_VALID              0x00000001U
#define GMA_RING_WAIT               (1U << 11)
#define GMA_RING_ADDR_MASK          0x001FFFFCU
#define GMA_RING_SIZE               (64U * 1024U)
#define GMA_RING_GUARD              64U
#define GMA_TIMEOUT                 3000000U

#define GMA_MI_NOOP                 0x00000000U
#define GMA_MI_FLUSH                (0x04U << 23)
#define GMA_XY_COLOR_BLT            ((2U << 29) | (0x50U << 22) | 4U)
#define GMA_XY_SRC_COPY_BLT         ((2U << 29) | (0x53U << 22) | 6U)
#define GMA_BLT_WRITE_ALPHA         (1U << 21)
#define GMA_BLT_WRITE_RGB           (1U << 20)
#define GMA_BLT_DEPTH_32            (3U << 24)
#define GMA_ROP_COPY                (0xCCU << 16)

#define ALIGN_UP(v,a)               (((v)+((a)-1U))&~((a)-1U))

typedef struct {
    uint16_t id;
    uint8_t generation;
    const char *name;
} gma_device_t;

static const gma_device_t gma_devices[] = {
    {0x2582U,3U,"915G/GMA 900"}, {0x2592U,3U,"915GM/GMA 900"},
    {0x2772U,3U,"945G/GMA 950"}, {0x27A2U,3U,"945GM/GMA 950"},
    {0x27AEU,3U,"945GME/GMA 950"}, {0x2972U,4U,"946GZ/GMA 3000"},
    {0x2982U,4U,"G35/GMA X3500"}, {0x2992U,4U,"Q965/GMA 3000"},
    {0x29A2U,4U,"G965/GMA X3000"}, {0x29B2U,4U,"Q35/GMA 3100"},
    {0x29C2U,4U,"G33/GMA 3100"}, {0x29D2U,4U,"Q33/GMA 3100"},
    {0x2A02U,4U,"GM965/GMA X3100"}, {0x2A12U,4U,"GME965/GMA X3100"},
};

typedef struct {
    const pci_device_t *pci;
    const gma_device_t *chip;
    volatile uint8_t *mmio;
    volatile uint8_t *aperture;
    uint32_t aperture_base;
    uint32_t aperture_size;
    uint32_t fb_offset;
    uint32_t ring_offset;
    volatile uint32_t *ring;
    uint32_t ring_tail;
    void *hws_raw;
    uint32_t *hws;
    bool active;
    bool accel;
    bool saved;
    uint32_t saved_tail, saved_head, saved_start, saved_ctl, saved_hws;
    gfx_info_t mode;
    uint32_t fence;
    uint8_t scanout_format;
} gma_state_t;

static gma_state_t g_gma;

static uint32_t gma_read(uint32_t reg) {
    volatile uint32_t *p=(volatile uint32_t *)(g_gma.mmio+reg);
    uint32_t value=*p; __asm__ volatile("":::"memory"); return value;
}
static void gma_write(uint32_t reg,uint32_t value) {
    volatile uint32_t *p=(volatile uint32_t *)(g_gma.mmio+reg);
    *p=value; __asm__ volatile("":::"memory");
}

static const gma_device_t *gma_chip(uint16_t id) {
    for(uint32_t i=0;i<sizeof(gma_devices)/sizeof(gma_devices[0]);i++)
        if(gma_devices[i].id==id)return &gma_devices[i];
    return NULL;
}

static bool gma_find(void) {
    pci_bar_info_t mmio,aperture;
    if (g_gma.pci && g_gma.mmio && g_gma.aperture) return true;
    for(uint32_t i=0;i<pci_device_count();i++) {
        const pci_device_t *dev=pci_device_at(i);
        const gma_device_t *chip;
        if(!dev||dev->vendor_id!=INTEL_VENDOR_ID||
           !(chip=gma_chip(dev->device_id)))continue;
        /* i830/852 (Gen2) expone GMADR en BAR0 y MMADR en BAR1. Desde
         * i915 se usa MMIO en BAR0 y la apertura en BAR2. Confundir ambos
         * diseños dejaba al 852GME sin driver justo en el equipo real. */
        uint8_t mmio_bar = chip->generation == 2U ? 1U : 0U;
        uint8_t aperture_bar = chip->generation == 2U ? 0U : 2U;
        if(!pci_get_bar_info(dev,mmio_bar,&mmio)||mmio.is_io||
           !pci_get_bar_info(dev,aperture_bar,&aperture)||aperture.is_io||
           !mmio.base||!aperture.base||mmio.size<0x10000U||
           aperture.size<4U*1024U*1024U)continue;
        if(!pci_enable_command(dev,PCI_COMMAND_MEMORY|PCI_COMMAND_BUSMASTER))
            continue;
        g_gma.pci=dev;g_gma.chip=chip;
        g_gma.mmio=(volatile uint8_t *)(uintptr_t)mmio.base;
        g_gma.aperture=(volatile uint8_t *)(uintptr_t)aperture.base;
        g_gma.aperture_base=aperture.base;g_gma.aperture_size=aperture.size;
        return true;
    }
    return false;
}

static bool gma_wait_idle(void) {
    for(uint32_t i=0;i<GMA_TIMEOUT;i++) {
        uint32_t head=gma_read(GMA_RING_HEAD)&GMA_RING_ADDR_MASK;
        uint32_t tail=gma_read(GMA_RING_TAIL)&GMA_RING_ADDR_MASK;
        if(head==tail && !(gma_read(GMA_RING_CTL)&GMA_RING_WAIT))return true;
    }
    return false;
}

static bool gma_ring_space(uint32_t bytes) {
    for(uint32_t i=0;i<GMA_TIMEOUT;i++) {
        uint32_t head=gma_read(GMA_RING_HEAD)&(GMA_RING_SIZE-1U);
        uint32_t tail=g_gma.ring_tail;
        uint32_t space=(head-tail-GMA_RING_GUARD)&(GMA_RING_SIZE-1U);
        if(space>=bytes)return true;
    }
    return false;
}

static bool gma_emit(const uint32_t *words,uint32_t count) {
    if(!g_gma.accel||!words||!count||!gma_ring_space(count*4U))return false;
    for(uint32_t i=0;i<count;i++) {
        g_gma.ring[g_gma.ring_tail>>2]=words[i];
        g_gma.ring_tail=(g_gma.ring_tail+4U)&(GMA_RING_SIZE-1U);
    }
    __asm__ volatile("":::"memory");
    gma_write(GMA_RING_TAIL,g_gma.ring_tail);
    return true;
}

static void gma_restore_ring(void) {
    if(g_gma.saved) {
        gma_write(GMA_RING_CTL,0U);(void)gma_read(GMA_RING_CTL);
        gma_write(GMA_RING_TAIL,g_gma.saved_tail);
        gma_write(GMA_RING_HEAD,g_gma.saved_head);
        gma_write(GMA_RING_START,g_gma.saved_start);
        gma_write(GMA_HWS_PGA,g_gma.saved_hws);
        gma_write(GMA_RING_CTL,g_gma.saved_ctl);
        g_gma.saved=false;
    }
}

static void gma_stop_accel(const char *where) {
    if(g_gma.accel)
        kprintf("[INTELGMA.DVR] timeout/fallo en %s; 2D vuelve a CPU\n",where);
    g_gma.accel=false;
    gma_restore_ring();
}

static bool gma_mapped_pages_distinct(uint32_t offset) {
    volatile uint32_t *a,*b;uint32_t olda,oldb;bool ok;
    if(offset+8192U>g_gma.aperture_size)return false;
    a=(volatile uint32_t *)(g_gma.aperture+offset);
    b=(volatile uint32_t *)(g_gma.aperture+offset+4096U);
    olda=*a;oldb=*b;*a=0x13579BDFU;*b=0x2468ACE0U;
    ok=*a==0x13579BDFU&&*b==0x2468ACE0U;
    *a=olda;*b=oldb;return ok;
}

static bool gma_color_blt(uint32_t offset,uint32_t pitch,int x,int y,int w,int h,
                          uint32_t color) {
    uint32_t cmd[8];
    cmd[0]=GMA_XY_COLOR_BLT|GMA_BLT_WRITE_ALPHA|GMA_BLT_WRITE_RGB;
    cmd[1]=GMA_BLT_DEPTH_32|GMA_ROP_COPY|(pitch&0xFFFFU);
    cmd[2]=((uint32_t)y<<16)|(uint32_t)x;
    cmd[3]=((uint32_t)(y+h)<<16)|(uint32_t)(x+w);
    cmd[4]=offset;cmd[5]=color;cmd[6]=GMA_MI_FLUSH;cmd[7]=GMA_MI_NOOP;
    return gma_emit(cmd,8U);
}

static bool gma_copy_blt(uint32_t offset,uint32_t pitch,int sx,int sy,int dx,
                         int dy,int w,int h) {
    uint32_t cmd[10];
    cmd[0]=GMA_XY_SRC_COPY_BLT|GMA_BLT_WRITE_ALPHA|GMA_BLT_WRITE_RGB;
    cmd[1]=GMA_BLT_DEPTH_32|GMA_ROP_COPY|(pitch&0xFFFFU);
    cmd[2]=((uint32_t)dy<<16)|(uint32_t)dx;
    cmd[3]=((uint32_t)(dy+h)<<16)|(uint32_t)(dx+w);
    cmd[4]=offset;cmd[5]=((uint32_t)sy<<16)|(uint32_t)sx;
    cmd[6]=pitch;cmd[7]=offset;cmd[8]=GMA_MI_FLUSH;cmd[9]=GMA_MI_NOOP;
    return gma_emit(cmd,10U);
}

static bool gma_init_ring(void) {
    uint32_t visible_end,scratch,old;
    visible_end=g_gma.fb_offset+(uint32_t)g_gma.mode.pitch*g_gma.mode.height;
    g_gma.ring_offset=ALIGN_UP(visible_end,4096U);
    scratch=g_gma.ring_offset+GMA_RING_SIZE;
    if(scratch+8192U>g_gma.aperture_size||
       !gma_mapped_pages_distinct(g_gma.ring_offset)||
       !gma_mapped_pages_distinct(scratch))return false;
    g_gma.hws_raw=kmalloc(8192U);
    if(!g_gma.hws_raw)return false;
    g_gma.hws=(uint32_t *)(uintptr_t)ALIGN_UP((uint32_t)(uintptr_t)g_gma.hws_raw,4096U);
    kmemset(g_gma.hws,0,4096U);
    g_gma.saved_tail=gma_read(GMA_RING_TAIL);g_gma.saved_head=gma_read(GMA_RING_HEAD);
    g_gma.saved_start=gma_read(GMA_RING_START);g_gma.saved_ctl=gma_read(GMA_RING_CTL);
    g_gma.saved_hws=gma_read(GMA_HWS_PGA);g_gma.saved=true;
    if((g_gma.saved_ctl&GMA_RING_VALID)&&!gma_wait_idle()) { gma_stop_accel("ring previo");return false; }
    gma_write(GMA_RING_CTL,0U);
    g_gma.ring=(volatile uint32_t *)(g_gma.aperture+g_gma.ring_offset);
    for(uint32_t i=0;i<GMA_RING_SIZE/4U;i++)g_gma.ring[i]=GMA_MI_NOOP;
    gma_write(GMA_HWS_PGA,(uint32_t)(uintptr_t)g_gma.hws);
    gma_write(GMA_RING_START,g_gma.ring_offset);
    gma_write(GMA_RING_HEAD,0U);gma_write(GMA_RING_TAIL,0U);
    gma_write(GMA_RING_CTL,(GMA_RING_SIZE-4096U)|GMA_RING_VALID);
    g_gma.ring_tail=0U;g_gma.accel=true;
    old=*(volatile uint32_t *)(g_gma.aperture+scratch);
    if(!gma_color_blt(scratch,32U,0,0,8,8,0xFF39A7E0U)||!gma_wait_idle()||
       *(volatile uint32_t *)(g_gma.aperture+scratch)!=0xFF39A7E0U) {
        *(volatile uint32_t *)(g_gma.aperture+scratch)=old;
        gma_stop_accel("autoprueba XY_COLOR_BLT");return false;
    }
    *(volatile uint32_t *)(g_gma.aperture+scratch)=old;
    kprintf("[INTELGMA.DVR] ring 64KiB + XY blitter OK, GGTT=0x%x\n",
            g_gma.ring_offset);
    return true;
}

static bool gma_clip(const gfx_info_t *info,int *x,int *y,int *w,int *h) {
    if(!info||!x||!y||!w||!h||*w<=0||*h<=0)return false;
    if(*x<0){*w+=*x;*x=0;}if(*y<0){*h+=*y;*y=0;}
    if(*x+*w>info->width)*w=info->width-*x;
    if(*y+*h>info->height)*h=info->height-*y;
    return *w>0&&*h>0;
}

static uint8_t gma_rgb332(uint32_t rgb) {
    return (uint8_t)((rgb & 0x00E00000U) >> 16U |
                     (rgb & 0x0000E000U) >> 11U |
                     (rgb & 0x000000C0U) >> 6U);
}

static uint16_t gma_rgb565(uint32_t rgb) {
    return (uint16_t)(((rgb >> 8U) & 0xF800U) |
                      ((rgb >> 5U) & 0x07E0U) |
                      ((rgb >> 3U) & 0x001FU));
}

static uint16_t gma_rgb555(uint32_t rgb) {
    return (uint16_t)(((rgb >> 9U) & 0x7C00U) |
                      ((rgb >> 6U) & 0x03E0U) |
                      ((rgb >> 3U) & 0x001FU));
}

static uint8_t gma_format_bpp(uint32_t control) {
    uint8_t format = (uint8_t)((control & GMA_DISPLAY_FORMAT_MASK) >>
                               GMA_DISPLAY_FORMAT_SHIFT);
    switch (format) {
        case GMA_FORMAT_INDEXED8: return 8U;
        case GMA_FORMAT_BGRA555:
        case GMA_FORMAT_BGRX555:
        case GMA_FORMAT_BGRX565: return 16U;
        case GMA_FORMAT_BGRX888:
        case GMA_FORMAT_BGRA888: return 32U;
        default: return 0U;
    }
}

static bool gma_adopt_display_plane(gfx_info_t *boot) {
    uint32_t control_a;
    uint32_t control_b;
    uint32_t base_register;
    uint32_t stride_register;
    uint32_t active_control;
    uint32_t stride;
    uint32_t plane_base;
    uint32_t bytes_per_pixel;
    uint32_t minimum_pitch;
    uint32_t vbe_framebuffer;
    uint16_t vbe_pitch;
    char selected_plane;
    uint64_t visible_end;

    if (!boot || !g_gma.mmio || !g_gma.aperture) return false;
    vbe_framebuffer = boot->framebuffer;
    vbe_pitch = boot->pitch;
    bytes_per_pixel = (boot->bpp + 7U) / 8U;
    if (!bytes_per_pixel) return false;
    minimum_pitch = (uint32_t)boot->width * bytes_per_pixel;

    /* El LVDS de 852 sólo puede salir por pipe B. El BIOS puede conectar a
     * ese pipe el plano A o B, por lo que se prefiere un plano habilitado y
     * dirigido a B antes de recurrir a cualquier otro plano habilitado. */
    control_a = gma_read(GMA_DSPA_CNTR);
    control_b = gma_read(GMA_DSPB_CNTR);
    base_register = GMA_DSPA_BASE;
    stride_register = GMA_DSPA_STRIDE;
    active_control = control_a;
    selected_plane = 'A';
    if ((control_b & (GMA_DISPLAY_PLANE_ENABLED | GMA_DISPLAY_PIPE_B)) ==
            (GMA_DISPLAY_PLANE_ENABLED | GMA_DISPLAY_PIPE_B) ||
        (!(control_a & GMA_DISPLAY_PLANE_ENABLED) &&
         (control_b & GMA_DISPLAY_PLANE_ENABLED))) {
        base_register = GMA_DSPB_BASE;
        stride_register = GMA_DSPB_STRIDE;
        active_control = control_b;
        selected_plane = 'B';
    }

    if (!(active_control & GMA_DISPLAY_PLANE_ENABLED)) return false;
    boot->bpp = gma_format_bpp(active_control);
    if (!boot->bpp) return false;
    g_gma.scanout_format = (uint8_t)((active_control &
        GMA_DISPLAY_FORMAT_MASK) >> GMA_DISPLAY_FORMAT_SHIFT);

    bytes_per_pixel = (boot->bpp + 7U) / 8U;
    minimum_pitch = (uint32_t)boot->width * bytes_per_pixel;

    /* En i830/852 el BIOS puede mantener el plano LCD con un stride alineado
     * distinto de BytesPerScanLine de VBE. Es el valor que realmente consume
     * el scanout y por tanto debe gobernar cada fila escrita por CPU. */
    stride = gma_read(stride_register) & 0x0000FFC0U;
    if (stride >= minimum_pitch && stride <= 32768U)
        boot->pitch = (uint16_t)stride;

    /* En i8xx/i9xx pre-965 DSPADDR es la direccion grafica completa. Linux
     * tampoco la enmascara en Gen2/Gen3: los bits bajos pueden representar
     * un offset lineal valido y borrarlos desplaza toda la imagen. */
    plane_base = gma_read(base_register);
    kprintf("[INTELGMA:SCANOUT] A=%x B=%x plano=%c formato=%u "
            "addr=%x stride=%u VBE_fb=%x VBE_pitch=%u\n",
            control_a,control_b,selected_plane,g_gma.scanout_format,
            plane_base,stride,vbe_framebuffer,vbe_pitch);
    if (plane_base < g_gma.aperture_size) {
        visible_end = (uint64_t)plane_base +
                      (uint64_t)boot->pitch * boot->height;
        if (visible_end <= g_gma.aperture_size) {
            boot->framebuffer = g_gma.aperture_base + plane_base;
            g_gma.fb_offset = plane_base;
            return true;
        }
    }

    if (boot->framebuffer < g_gma.aperture_base ||
        boot->framebuffer >= g_gma.aperture_base + g_gma.aperture_size)
        return false;
    g_gma.fb_offset = boot->framebuffer - g_gma.aperture_base;
    visible_end = (uint64_t)g_gma.fb_offset +
                  (uint64_t)boot->pitch * boot->height;
    return visible_end <= g_gma.aperture_size;
}

static bool gma_activate(gfx_info_t *info,uint16_t pw,uint16_t ph) {
    gfx_info_t boot;
    (void)pw;(void)ph;
    if(!gma_find()||!vesa_init_from_bootinfo(&boot)||
       !gma_adopt_display_plane(&boot)||
       !vesa_attach_lfb(&boot,boot.framebuffer,boot.width,boot.height,
                        boot.pitch,boot.bpp))return false;
    g_gma.mode=boot;g_gma.mode.mode=GFX_MODE_INTEL_GMA9XX;g_gma.active=true;
    if(g_gma.chip->generation>=3U&&boot.bpp==32U)(void)gma_init_ring();
    if(info)*info=g_gma.mode;
    kprintf("[INTELGMA.DVR] %s Gen%u %ux%ux%u pitch=%u fb=%x, aceleracion=%s\n",
            g_gma.chip->name,g_gma.chip->generation,boot.width,boot.height,
            boot.bpp,boot.pitch,boot.framebuffer,
            g_gma.accel?"XY BLT":"CPU segura");
    return true;
}

static uint32_t gma_caps(void) {
    uint32_t c=GFX_CAP_PRESENT_BUFFER|GFX_CAP_DIRTY_RECTS;
    if(g_gma.accel)c|=GFX_CAP_FILL|GFX_CAP_BITBLT|GFX_CAP_BITBLT_COPY_HW|GFX_CAP_FENCE;
    return g_gma.active?c:0U;
}
static void gma_disable(void) {
    if(g_gma.accel)(void)gma_wait_idle();
    g_gma.accel=false;
    gma_restore_ring();
    if(g_gma.hws_raw)kfree(g_gma.hws_raw);
    g_gma.hws_raw=NULL;g_gma.hws=NULL;
    g_gma.active=false;
}
static bool gma_list(gfx_display_mode_t *m,uint32_t max,uint32_t *count) {
    if(count)*count=g_gma.active?1U:0U;
    if(m&&max&&g_gma.active)m[0]=(gfx_display_mode_t){g_gma.mode.width,g_gma.mode.height,g_gma.mode.bpp};
    return g_gma.active;
}
static bool gma_set_mode(gfx_info_t *info,uint16_t w,uint16_t h,uint8_t bpp) {
    if(!g_gma.active||w!=g_gma.mode.width||h!=g_gma.mode.height||bpp!=g_gma.mode.bpp)return false;
    if(info)*info=g_gma.mode;
    return true;
}
static bool gma_present(const gfx_info_t *info,const uint32_t *pixels,uint32_t sp,
                        const gfx_rect_t *rects,uint32_t n,uint32_t *fence) {
    if(fence)*fence=0U;
    if(!g_gma.active||!info||!pixels||sp<info->width)return false;
    if(!rects||!n){gfx_rect_t full={0,0,info->width,info->height};rects=&full;n=1U;}
    for(uint32_t i=0;i<n;i++){int x=rects[i].x,y=rects[i].y,w=rects[i].w,h=rects[i].h;
        if(!gma_clip(info,&x,&y,&w,&h))continue;
        for(int row=0;row<h;row++) {
            const uint32_t *src=pixels+(uint32_t)(y+row)*sp+(uint32_t)x;
            uint8_t *dst=(uint8_t *)(uintptr_t)(info->framebuffer+
                (uint32_t)(y+row)*info->pitch+
                (uint32_t)x*((info->bpp+7U)/8U));
            if(info->bpp==32U)kmemcpy(dst,src,(uint32_t)w*4U);
            else for(int col=0;col<w;col++) {
                uint32_t rgb=src[col]&0x00FFFFFFU;
                if(info->bpp==8U)dst[col]=gma_rgb332(rgb);
                else if(info->bpp==16U)((uint16_t *)dst)[col]=
                    (g_gma.scanout_format==GMA_FORMAT_BGRA555||
                     g_gma.scanout_format==GMA_FORMAT_BGRX555)
                    ?gma_rgb555(rgb):gma_rgb565(rgb);
                else {
                    uint8_t *p=dst+(uint32_t)col*3U;
                    p[0]=(uint8_t)rgb;p[1]=(uint8_t)(rgb>>8U);
                    p[2]=(uint8_t)(rgb>>16U);
                }
            }
        }
    }return true;
}
static bool gma_update(int x,int y,int w,int h){(void)x;(void)y;(void)w;(void)h;return g_gma.active;}
static bool gma_flush(uint32_t *f){if(f)*f=0U;return !g_gma.accel||gma_wait_idle();}
static bool gma_wait(uint32_t f){(void)f;return !g_gma.accel||gma_wait_idle();}
static bool gma_fill(const gfx_info_t *info,int x,int y,int w,int h,uint32_t rgb,uint32_t *f) {
    if(f)*f=0U;
    if(!g_gma.accel||!gma_clip(info,&x,&y,&w,&h))return false;
    if(!gma_color_blt(g_gma.fb_offset,info->pitch,x,y,w,h,0xFF000000U|(rgb&0xFFFFFFU))){gma_stop_accel("fill");return false;}
    g_gma.fence++;if(!g_gma.fence)g_gma.fence=1U;if(f)*f=g_gma.fence;return true;
}
static bool gma_blt(const gfx_info_t *info,int sx,int sy,int dx,int dy,int w,int h,gfx_rop_t rop,uint32_t *f) {
    if(f)*f=0U;
    if(!g_gma.accel||!info||rop!=GFX_ROP_COPY||w<=0||h<=0)return false;
    if(sx<0||sy<0||dx<0||dy<0||sx+w>info->width||dx+w>info->width||sy+h>info->height||dy+h>info->height)return false;
    /* Reverse-overlap copies need a directional command variant; use CPU fallback. */
    if((dy>sy&&dy<sy+h)||(dy==sy&&dx>sx&&dx<sx+w))return false;
    if(!gma_copy_blt(g_gma.fb_offset,info->pitch,sx,sy,dx,dy,w,h)){gma_stop_accel("bitblt");return false;}
    g_gma.fence++;if(!g_gma.fence)g_gma.fence=1U;if(f)*f=g_gma.fence;return true;
}

static bool gma_driver_init(void) {
    static const gfx_driver_ops_t ops={BK_GFX_DRIVER_ABI_VERSION,sizeof(gfx_driver_ops_t),
        "intel_gma9xx",235U,GFX_CAP_PRESENT_BUFFER|GFX_CAP_DIRTY_RECTS|
        GFX_CAP_FILL|GFX_CAP_BITBLT|GFX_CAP_BITBLT_COPY_HW|GFX_CAP_FENCE,
        gma_activate,gma_caps,gma_disable,gma_list,gma_set_mode,gma_present,
        gma_update,gma_flush,gma_wait,gma_fill,gma_blt,
        NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    kmemset(&g_gma,0,sizeof(g_gma));
    if(!gma_find()){kprintf("[INTELGMA.DVR] Intel GMA 915/945/965 no detectada\n");return false;}
    return gfx_register_driver(&ops);
}
static void gma_driver_shutdown(void){gma_disable();}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module={BK_DRIVER_ABI_VERSION,sizeof(bk_driver_module_t),
        "intel_gma9xx","Intel GMA 9xx: scanout real + ring XY BLT opcional",
        gma_driver_init,gma_driver_shutdown};return &module;
}
