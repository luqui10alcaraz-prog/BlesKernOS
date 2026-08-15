#ifndef GFX_H
#define GFX_H

#include "types.h"

typedef enum {
    VIDEO_TYPE_NONE = 0x00,
    VIDEO_TYPE_COLOUR = 0x20,
    VIDEO_TYPE_MONOCHROME = 0x30,
} video_type_t;

typedef enum {
    GFX_MODE_TEXT = 0,
    GFX_MODE_VGA_13H = 1,
    GFX_MODE_VGA_12H = 2,
    GFX_MODE_VESA_LFB = 3,
    GFX_MODE_VMWARE_SVGA = 4,
    GFX_MODE_VIRTIO_GPU = 5,
    GFX_MODE_ATI_RAGE128 = 6,
    GFX_MODE_INTEL_GMA9XX = 7,
} gfx_mode_t;

typedef struct {
    gfx_mode_t mode;
    uint32_t framebuffer;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t bpp;
} gfx_info_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
} gfx_display_mode_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} gfx_rect_t;

typedef enum {
    GFX_ROP_COPY = 0,
    GFX_ROP_XOR,
    GFX_ROP_AND,
    GFX_ROP_OR,
    GFX_ROP_INVERT,
} gfx_rop_t;

typedef uint32_t gfx_surface_handle_t;
#define GFX_SURFACE_INVALID 0U

typedef enum {
    GFX_OVERLAY_YUY2 = 0,
    GFX_OVERLAY_UYVY = 1,
} gfx_overlay_format_t;

/* Capacidades publicadas por el backend gráfico activo. */
#define GFX_CAP_PRESENT_BUFFER   (1U << 0)
#define GFX_CAP_DIRTY_RECTS      (1U << 1)
#define GFX_CAP_HW_CURSOR        (1U << 2)
#define GFX_CAP_FENCE            (1U << 3)
#define GFX_CAP_OFFSCREEN_VRAM   (1U << 4)
#define GFX_CAP_GMR              (1U << 5)
#define GFX_CAP_BITBLT           (1U << 6)
#define GFX_CAP_FILL             (1U << 7)
#define GFX_CAP_BITBLT_COPY_HW    (1U << 8)
#define GFX_CAP_BITBLT_ROP_HW     (1U << 9)
#define GFX_CAP_SURFACE_BLIT_HW    (1U << 10)
#define GFX_CAP_VIDEO_OVERLAY      (1U << 11)
/* El callback existe y la operación la ejecuta realmente el motor 2D. */
#define GFX_CAP_FILL_HW             (1U << 12)
/* present_buffer sube píxeles desde RAM mediante la CPU. No debe mostrarse
 * como presentación acelerada aunque el mismo backend tenga BitBlt/Fill HW. */
#define GFX_CAP_PRESENT_CPU_COPY    (1U << 13)
/* surface_create devuelve un desplazamiento mapeable por CPU respecto del
 * framebuffer visible. Solo debe anunciarse si ese mapa es apropiado para
 * rasterización frecuente; MMIO/VRAM sin write-combining no cumple esto. */
#define GFX_CAP_SURFACE_CPU_MAP      (1U << 14)
/* La transferencia final al scanout usa BitBlt VRAM -> VRAM. El backend puede
 * anunciar también PRESENT_CPU_COPY cuando antes debe subir dirty rects desde
 * un backbuffer normal en RAM hacia una superficie staging en VRAM. */
#define GFX_CAP_PRESENT_VRAM_BLIT    (1U << 15)

void gfx_init(void);
video_type_t gfx_detect_video_type(void);
const char *gfx_video_type_name(video_type_t type);
const gfx_info_t *gfx_get_info(void);
const char *gfx_driver_name(void);
uint32_t gfx_driver_capabilities(void);
/* Cambia cada vez que se activa, desactiva o reconfigura el backend. Los
 * consumidores que almacenan recursos del dispositivo (por ejemplo el
 * cursor) deben volver a crearlos cuando cambia esta generación. */
uint32_t gfx_driver_generation(void);
bool gfx_set_text_mode(void);
bool gfx_set_mode13h(void);
bool gfx_set_mode12h(void);
bool gfx_attach_vesa_lfb(uint32_t framebuffer, uint16_t width, uint16_t height, uint16_t pitch, uint8_t bpp);
bool gfx_has_vesa_lfb(void);
bool gfx_is_linear_framebuffer(void);
bool gfx_can_change_mode(void);
bool gfx_list_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                            uint32_t *count);
bool gfx_list_all_display_modes(gfx_display_mode_t *modes, uint32_t max_modes,
                                uint32_t *count);
bool gfx_set_display_mode(uint16_t width, uint16_t height, uint8_t bpp);
bool gfx_enable_page_flip(void);
uint32_t gfx_page_flip_draw_buffer(void);
bool gfx_page_flip_commit(void);

/* Presentación del back buffer y regiones sucias. source_pitch se expresa en
 * píxeles de 32 bits. El backend puede copiar a VRAM o usar GMR/DMA. */
bool gfx_present_buffer(const uint32_t *pixels, uint32_t source_pitch,
                        const gfx_rect_t *rects, uint32_t rect_count,
                        uint32_t *fence_out);
bool gfx_present_rect(int x, int y, int w, int h);
bool gfx_flush(void);
uint32_t gfx_last_fence(void);
bool gfx_wait_fence(uint32_t fence);

/* BitBlt visible-a-visible. COPY usa aceleración cuando existe; el resto tiene
 * fallback software para mantener una API completa y determinista. */
bool gfx_bitblt(int src_x, int src_y, int dst_x, int dst_y,
                int w, int h, gfx_rop_t rop, uint32_t *fence_out);
bool gfx_copy_rect(int src_x, int src_y, int dst_x, int dst_y, int w, int h);

/* Cursor hardware. Los píxeles son ARGB8888; el driver puede convertirlos al
 * formato monocromo/clásico que anuncie el dispositivo. */
bool gfx_cursor_supported(void);
bool gfx_cursor_define(const uint32_t *argb, uint16_t width, uint16_t height,
                       uint16_t hot_x, uint16_t hot_y);
bool gfx_cursor_move(int x, int y);
bool gfx_cursor_show(bool visible);

/* Superficies fuera de pantalla. En SVGA-II se reservan en VRAM libre; el
 * blit a pantalla usa aceleración si el host la ofrece y fallback si no. */
bool gfx_surface_create(uint16_t width, uint16_t height,
                        gfx_surface_handle_t *handle_out);
bool gfx_surface_destroy(gfx_surface_handle_t handle);
/* Solo disponible con GFX_CAP_SURFACE_CPU_MAP. El handle es un desplazamiento
 * respecto del framebuffer visible y el pitch se devuelve en pixeles. */
bool gfx_surface_map(gfx_surface_handle_t handle, uint32_t **pixels_out,
                     uint32_t *pitch_out);
bool gfx_surface_upload(gfx_surface_handle_t handle, const uint32_t *pixels,
                        uint32_t source_pitch, const gfx_rect_t *rect);
bool gfx_surface_blit(gfx_surface_handle_t handle, int src_x, int src_y,
                      int dst_x, int dst_y, int w, int h,
                      uint32_t *fence_out);

/* Overlay de video por hardware. El buffer fuente usa YUY2 o UYVY empaquetado
 * (dos bytes por píxel) y puede escalarse al rectángulo de destino. */
bool gfx_overlay_supported(void);
bool gfx_overlay_put(const void *pixels, uint32_t source_pitch,
                     uint16_t source_width, uint16_t source_height,
                     gfx_overlay_format_t format,
                     int dst_x, int dst_y, int dst_w, int dst_h);
bool gfx_overlay_stop(void);

void gfx_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void gfx_set_default_palette(void);
void gfx_clear(uint8_t color);
void gfx_clear_rgb(uint32_t rgb);
void gfx_putpixel(int x, int y, uint8_t color);
void gfx_putpixel_rgb(int x, int y, uint32_t rgb);
uint32_t gfx_getpixel_rgb(int x, int y);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fill_rect_rgb(int x, int y, int w, int h, uint32_t rgb);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint8_t color);
void gfx_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg, bool fill_bg);
void gfx_draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg, bool fill_bg);
void gfx_demo(void);

#endif
