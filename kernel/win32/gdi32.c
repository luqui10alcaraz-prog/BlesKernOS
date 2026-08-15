#include "win32.h"
#include "resources.h"
#include "../../gui/gui.h"
#include "../include/memory.h"
#include "../include/task.h"

#define MEMDC_BASE 0x78000000U
#define BITMAP_BASE 0x79000000U
#define DISPLAY_DC_HANDLE 0x7A000000U
#define MAX_MEMDCS 16U
#define MAX_BITMAPS 32U
#define FONT_BASE 0x7B000000U
#define MAX_FONTS 128U
#define SRCCOPY 0x00CC0020U
#define SRCAND 0x008800C6U
#define SRCPAINT 0x00EE0086U
#define SRCINVERT 0x00660046U
#define NOTSRCCOPY 0x00330008U
#define BI_RGB 0U
#define DIB_RGB_COLORS 0U
#define PATCOPY 0x00F00021U
#define BLACKNESS 0x00000042U
#define WHITENESS 0x00FF0062U
#define TA_UPDATECP 0x0001U
#define TA_RIGHT 0x0002U
#define TA_CENTER 0x0006U
#define TA_BOTTOM 0x0008U
#define TA_BASELINE 0x0018U
#define TA_RTLREADING 0x0100U
#define TA_VALID_MASK 0x011FU
#define GDI_ERROR 0xFFFFFFFFU
#define OBJ_PEN_TAG 0x73000000U
#define OBJ_BRUSH_TAG 0x74000000U
#define OBJ_STOCK_TAG 0x75000000U
#define OBJ_TAG_MASK 0xFF000000U
#define WHITE_BRUSH 0U
#define LTGRAY_BRUSH 1U
#define GRAY_BRUSH 2U
#define DKGRAY_BRUSH 3U
#define BLACK_BRUSH 4U
#define NULL_BRUSH 5U
#define WHITE_PEN 6U
#define BLACK_PEN 7U
#define NULL_PEN 8U
#define DC_BRUSH 18U
#define DC_PEN 19U

typedef struct {
    int map_mode;
    int32_t window_org_x,window_org_y,viewport_org_x,viewport_org_y;
    int32_t window_ext_x,window_ext_y,viewport_ext_x,viewport_ext_y;
} gdi_map_state_t;
typedef struct { void *selected,*font; uint32_t text,bk,text_align; int bk_mode,x,y; gdi_map_state_t map; } gdi_saved_state_t;
typedef struct { bool used; void *target; void *selected,*font; uint32_t text,bk,text_align; int bk_mode,x,y; gdi_map_state_t map; gdi_saved_state_t saved[8]; uint8_t saved_count; } memdc_t;
typedef struct { bool used; int width,height; uint32_t *pixels; bool owned; uint8_t *dib_bits; uint32_t dib_stride; uint16_t dib_bpp; bool dib_top_down; bool dib_owned; } bitmap_t;
/* BLES_WINE_FONT_CATALOG_FIX_20260723
 * Catalogo logico compatible con nombres clasicos de Win9x.
 * No incorpora fuentes propietarias: todas las caras se rasterizan con el
 * backend bitmap interno, pero conservan familia, pitch y metricas coherentes. */
typedef enum {
    BK_FONT_SANS = 0,
    BK_FONT_SERIF = 1,
    BK_FONT_MONO = 2,
    BK_FONT_SYSTEM = 3
} bk_font_family_t;

typedef struct {
    const char *alias;
    const char *canonical;
    bk_font_family_t family;
    uint8_t default_height;
    uint8_t average_width;
} bk_font_catalog_entry_t;

static const bk_font_catalog_entry_t bk_font_catalog[] = {
    {"SYSTEM",          "System",          BK_FONT_SYSTEM,  8, 7},
    {"SYSTEM_FIXED_FONT","Fixedsys",       BK_FONT_MONO,    8, 8},
    {"MS SANS SERIF",   "MS Sans Serif",   BK_FONT_SANS,    8, 6},
    {"MS SHELL DLG",    "MS Sans Serif",   BK_FONT_SANS,    8, 6},
    {"MS SHELL DLG 2",  "Tahoma",          BK_FONT_SANS,    8, 6},
    {"HELV",            "Helv",            BK_FONT_SANS,    8, 6},
    {"HELVETICA",       "Helvetica",       BK_FONT_SANS,    8, 6},
    {"ARIAL",           "Arial",           BK_FONT_SANS,    9, 6},
    {"TAHOMA",          "Tahoma",          BK_FONT_SANS,    8, 6},
    {"VERDANA",         "Verdana",         BK_FONT_SANS,    9, 7},
    {"SANS SERIF",      "MS Sans Serif",   BK_FONT_SANS,    8, 6},
    {"TIMES",           "Times New Roman", BK_FONT_SERIF,  10, 6},
    {"TIMES NEW ROMAN", "Times New Roman", BK_FONT_SERIF,  10, 6},
    {"ROMAN",           "Times New Roman", BK_FONT_SERIF,  10, 6},
    {"SERIF",           "Times New Roman", BK_FONT_SERIF,  10, 6},
    {"COURIER",         "Courier New",     BK_FONT_MONO,    9, 8},
    {"COURIER NEW",     "Courier New",     BK_FONT_MONO,    9, 8},
    {"FIXEDSYS",        "Fixedsys",        BK_FONT_MONO,    8, 8},
    {"TERMINAL",        "Terminal",        BK_FONT_MONO,    8, 8},
    {"MODERN",          "Courier New",     BK_FONT_MONO,    9, 8}
};

typedef struct {
    bool used, bold, italic, monospace, underline, strikeout;
    uint32_t owner_process_id;
    int16_t pixel_height, requested_width;
    uint16_t weight;
    uint8_t charset, average_width;
    bk_font_family_t family;
    char face[32];
} logical_font_t;
static memdc_t memdcs[MAX_MEMDCS];
static bitmap_t bitmaps[MAX_BITMAPS];
static logical_font_t logical_fonts[MAX_FONTS];
static uint32_t text_color=0x00101010U,background_color=0x00FFFFFFU;
static int background_mode=2,current_x,current_y;
static uint32_t text_align;
static gdi_map_state_t window_map={1,0,0,0,0,1,1,1,1};
static void *window_pen=(void *)(uintptr_t)(OBJ_STOCK_TAG + BLACK_PEN);
static void *window_brush=(void *)(uintptr_t)(OBJ_STOCK_TAG + WHITE_BRUSH);
static void *window_font=(void *)(uintptr_t)(OBJ_STOCK_TAG + 17U);

static uint32_t stock_color(uint32_t index) {
    switch (index) {
        case WHITE_BRUSH: case WHITE_PEN: return 0x00FFFFFFU;
        case LTGRAY_BRUSH: return 0x00D8D8D8U;
        case GRAY_BRUSH: return 0x00808080U;
        case DKGRAY_BRUSH: return 0x00404040U;
        case BLACK_BRUSH: case BLACK_PEN: return 0x00000000U;
        default: return 0x00000000U;
    }
}
static bool object_color(void *object, bool pen, uint32_t *color, bool *is_null) {
    uint32_t value=(uint32_t)(uintptr_t)object,tag=value&OBJ_TAG_MASK,index=value&0x00FFFFFFU;
    if(is_null)*is_null=false;
    if(tag==(pen?OBJ_PEN_TAG:OBJ_BRUSH_TAG)){if(color)*color=index;return true;}
    if(tag!=OBJ_STOCK_TAG)return false;
    if(pen){
        if(index==NULL_PEN){if(is_null)*is_null=true;return true;}
        if(index==WHITE_PEN||index==BLACK_PEN||index==DC_PEN){if(color)*color=stock_color(index==DC_PEN?BLACK_PEN:index);return true;}
    }else{
        if(index==NULL_BRUSH){if(is_null)*is_null=true;return true;}
        if(index<=BLACK_BRUSH||index==DC_BRUSH){if(color)*color=stock_color(index==DC_BRUSH?WHITE_BRUSH:index);return true;}
    }
    return false;
}
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
/* WIN32_GDI_CREATEDCA */
static uint8_t gdi_upper_ascii(uint8_t value) {
    return value >= 'a' && value <= 'z'
        ? (uint8_t)(value - ('a' - 'A')) : value;
}

static bool gdi_equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;

    while (*left && *right) {
        if (gdi_upper_ascii((uint8_t)*left) !=
            gdi_upper_ascii((uint8_t)*right))
            return false;
        left++;
        right++;
    }

    return *left == *right;
}

static void *WIN32_API gdi_CreateDCA(const char *driver,
                                      const char *device,
                                      const char *output,
                                      const void *initialization) {
    (void)output;
    (void)initialization;

    if ((!driver && !device) ||
        gdi_equal_ci(driver, "DISPLAY") ||
        gdi_equal_ci(device, "DISPLAY")) {
        return (void *)(uintptr_t)DISPLAY_DC_HANDLE;
    }

    return NULL;
}

static memdc_t *memdc_from(void*h){uint32_t v=(uint32_t)(uintptr_t)h;if(v<MEMDC_BASE||v>=MEMDC_BASE+MAX_MEMDCS)return NULL;v-=MEMDC_BASE;return memdcs[v].used?&memdcs[v]:NULL;}
static bitmap_t *bitmap_from(void*h){uint32_t v=(uint32_t)(uintptr_t)h;if(v<BITMAP_BASE||v>=BITMAP_BASE+MAX_BITMAPS)return NULL;v-=BITMAP_BASE;return bitmaps[v].used?&bitmaps[v]:NULL;}
/* BLES_WINE_DIALOG_UI_PERF_FIX_20260723
 * A compact logical-font table is enough for Win9x programs that use
 * CreateFontIndirect + SelectObject + text metrics. */
static logical_font_t *font_from(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (value < FONT_BASE || value >= FONT_BASE + MAX_FONTS) return NULL;
    value -= FONT_BASE;
    return logical_fonts[value].used ? &logical_fonts[value] : NULL;
}
static bool font_name_has(const char *name, const char *needle) {
    if (!name || !needle) return false;
    while (*name) {
        const char *a = name, *b = needle;
        while (*a && *b && gdi_upper_ascii((uint8_t)*a) ==
                           gdi_upper_ascii((uint8_t)*b)) { a++; b++; }
        if (!*b) return true;
        name++;
    }
    return false;
}
static void gdi_copy_face(char dst[32], const char *src) {
    uint32_t i=0;
    if (!src) src="System";
    while (i<31U && src[i]) { dst[i]=src[i]; i++; }
    dst[i]='\0';
}
static const bk_font_catalog_entry_t *gdi_font_catalog_find(const char *face) {
    if (!face || !*face) face="SYSTEM";
    for (uint32_t i=0;i<sizeof(bk_font_catalog)/sizeof(bk_font_catalog[0]);i++)
        if (gdi_equal_ci(face,bk_font_catalog[i].alias)) return &bk_font_catalog[i];
    /* Windows hace font substitution. Conservamos nombres desconocidos como
       sans para que una aplicacion no falle solo por no tener la cara exacta. */
    if (font_name_has(face,"COURIER") || font_name_has(face,"FIXED") ||
        font_name_has(face,"TERMINAL") || font_name_has(face,"MONO"))
        return &bk_font_catalog[16];
    if (font_name_has(face,"TIMES") || font_name_has(face,"ROMAN") ||
        font_name_has(face,"SERIF")) return &bk_font_catalog[11];
    return &bk_font_catalog[2];
}
static void font_values(void *handle, int *height, bool *bold,
                        bool *italic, bool *monospace) {
    logical_font_t *font = font_from(handle);
    int h = 8;
    bool b = false, i = false, m = false;
    if (font) {
        h = font->pixel_height;
        b = font->bold;
        i = font->italic;
        m = font->monospace;
    } else {
        uint32_t value = (uint32_t)(uintptr_t)handle;
        uint32_t tag = value & OBJ_TAG_MASK;
        uint32_t index = value & 0x00FFFFFFU;
        if (tag == OBJ_STOCK_TAG) {
            h = (index == 13U || index == 17U) ? 8 : 10;
            m = index == 10U || index == 11U || index == 15U || index == 16U;
        }
    }
    if (h < 8) h = 8;
    if (h > 32) h = 32;
    if (height) *height = h;
    if (bold) *bold = b;
    if (italic) *italic = i;
    if (monospace) *monospace = m;
}
bool win32_gdi_font_query(void *font, int *height, bool *bold,
                          bool *italic, bool *monospace) {
    if (!font && !height && !bold && !italic && !monospace) return false;
    font_values(font, height, bold, italic, monospace);
    return font_from(font) != NULL ||
           (((uint32_t)(uintptr_t)font & OBJ_TAG_MASK) == OBJ_STOCK_TAG);
}
static void *font_for_dc(void *dc) {
    memdc_t *memory = memdc_from(dc);
    return memory ? memory->font : window_font;
}
static int text_width_for_font(void *font, const char *text, uint32_t length) {
    int h; bool b, i, m; logical_font_t *logical=font_from(font);
    (void)i;
    font_values(font, &h, &b, &i, &m);
    int width=(int)gui_font_text_width_px(text, length, h, m, b);
    if (logical && logical->requested_width>0 && length)
        width=(int)logical->requested_width*(int)length+(b?(int)length:0);
    else if (logical && logical->family==BK_FONT_SERIF && length)
        width += (int)(length/8U); /* leve separacion de familia Roman */
    return width;
}
void win32_gdi_cleanup_process(uint32_t pid) {
    for (uint32_t i = 0; i < MAX_FONTS; i++)
        if (logical_fonts[i].used && logical_fonts[i].owner_process_id == pid)
            kmemset(&logical_fonts[i], 0, sizeof(logical_fonts[i]));
}
static void *bitmap_alloc(int w,int h){if(w<=0||h<=0||w>2048||h>2048)return NULL;for(uint32_t i=0;i<MAX_BITMAPS;i++)if(!bitmaps[i].used){bitmap_t*b=&bitmaps[i];kmemset(b,0,sizeof(*b));b->pixels=(uint32_t*)kzalloc((size_t)w*(size_t)h*sizeof(uint32_t));if(!b->pixels)return NULL;b->used=true;b->owned=true;b->width=w;b->height=h;return(void*)(uintptr_t)(BITMAP_BASE+i);}return NULL;}
static uint16_t rd16(const uint8_t*p);
static uint32_t rd32(const uint8_t*p);
static uint32_t dib_stride_for(int width, uint16_t bpp) {
    return (((uint32_t)width * bpp + 31U) / 32U) * 4U;
}
static void bitmap_sync_from_dib(bitmap_t *b) {
    if (!b || !b->dib_bits || !b->pixels) return;
    for (int y = 0; y < b->height; y++) {
        int sy = b->dib_top_down ? y : (b->height - 1 - y);
        const uint8_t *row = b->dib_bits + (uint32_t)sy * b->dib_stride;
        for (int x = 0; x < b->width; x++) {
            uint32_t c = 0U;
            if (b->dib_bpp == 32U) c = rd32(row + (uint32_t)x * 4U) & 0x00FFFFFFU;
            else if (b->dib_bpp == 24U) {
                const uint8_t *q = row + (uint32_t)x * 3U;
                c = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16);
            } else if (b->dib_bpp == 16U) {
                uint16_t q = rd16(row + (uint32_t)x * 2U);
                c = ((uint32_t)(q & 31U) << 3) |
                    ((uint32_t)((q >> 5) & 31U) << 11) |
                    ((uint32_t)((q >> 10) & 31U) << 19);
            }
            b->pixels[(uint32_t)y * (uint32_t)b->width + (uint32_t)x] = c;
        }
    }
}
static void bitmap_sync_to_dib(bitmap_t *b) {
    if (!b || !b->dib_bits || !b->pixels) return;
    for (int y = 0; y < b->height; y++) {
        int dy = b->dib_top_down ? y : (b->height - 1 - y);
        uint8_t *row = b->dib_bits + (uint32_t)dy * b->dib_stride;
        for (int x = 0; x < b->width; x++) {
            uint32_t c = b->pixels[(uint32_t)y * (uint32_t)b->width + (uint32_t)x];
            if (b->dib_bpp == 32U) *(uint32_t *)(row + (uint32_t)x * 4U) = c;
            else if (b->dib_bpp == 24U) {
                uint8_t *q = row + (uint32_t)x * 3U;
                q[0] = (uint8_t)c; q[1] = (uint8_t)(c >> 8); q[2] = (uint8_t)(c >> 16);
            } else if (b->dib_bpp == 16U) {
                uint16_t q = (uint16_t)(((c >> 3) & 31U) |
                    (((c >> 11) & 31U) << 5) | (((c >> 19) & 31U) << 10));
                *(uint16_t *)(row + (uint32_t)x * 2U) = q;
            }
        }
    }
}
static uint16_t rd16(const uint8_t*p){return(uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void *decode_dib_resource(void*resource){const uint8_t*d=(const uint8_t*)win32_resource_lock(resource);uint32_t size=win32_resource_size(NULL,resource);if(!d||size<40U||rd32(d)<40U)return NULL;int w=(int)rd32(d+4),h=(int)rd32(d+8);uint16_t planes=rd16(d+12),bpp=rd16(d+14);uint32_t comp=rd32(d+16),colors=rd32(d+32);if(w<=0||h==0||planes!=1||comp!=0U)return NULL;bool top=h<0;if(h<0)h=-h;void*handle=bitmap_alloc(w,h);bitmap_t*b=bitmap_from(handle);if(!b)return NULL;uint32_t palette_count=bpp<=8U?(colors?colors:(1U<<bpp)):0U;uint32_t off=rd32(d)+palette_count*4U;if(off>=size){return handle;}uint32_t stride=((uint32_t)w*bpp+31U)/32U*4U;for(int y=0;y<h;y++){uint32_t sy=top?(uint32_t)y:(uint32_t)(h-1-y);const uint8_t*row=d+off+sy*stride;if(row>=d+size)break;for(int x=0;x<w;x++){uint32_t c=0;if(bpp==32U)c=rd32(row+x*4U)&0x00FFFFFFU;else if(bpp==24U){const uint8_t*p=row+x*3U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==8U){uint8_t idx=row[x];const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==4U){uint8_t q=row[x/2];uint8_t idx=(x&1)?(q&15U):(q>>4);const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==1U){uint8_t idx=(row[x/8]>>(7-(x&7)))&1U;const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}b->pixels[y*w+x]=c;}}return handle;}

/*
 * LoadBitmap returns an HBITMAP, not the HRSRC used to locate its RT_BITMAP.
 * Keeping that conversion at the USER32/GDI boundary also makes GetObject and
 * SelectObject observe the same object instead of decoding two unrelated
 * temporary bitmaps.
 */
void *win32_gdi_bitmap_from_resource(void *resource) {
    if (!resource) return NULL;
    if (bitmap_from(resource)) return resource;
    return decode_dib_resource(resource);
}
bool win32_gdi_bitmap_query(void *handle, int *width, int *height,
                            const uint32_t **pixels) {
    bitmap_t *bitmap = bitmap_from(handle);
    if (!bitmap) return false;
    bitmap_sync_from_dib(bitmap);
    if (width) *width = bitmap->width;
    if (height) *height = bitmap->height;
    if (pixels) *pixels = bitmap->pixels;
    return true;
}
static void *WIN32_API gdi_GetDC(void *hwnd){return hwnd;}
static int WIN32_API gdi_ReleaseDC(void *hwnd UNUSED,void *dc UNUSED){return 1;}
static void *WIN32_API gdi_BeginPaint(void *hwnd,void *paint){
    win32_gdi_begin(hwnd);
    text_color=0x00101010U;background_color=0x00FFFFFFU;background_mode=2;
    text_align=0U;current_x=0;current_y=0;
    window_map=(gdi_map_state_t){1,0,0,0,0,1,1,1,1};
    window_pen=(void *)(uintptr_t)(OBJ_STOCK_TAG+BLACK_PEN);
    window_brush=(void *)(uintptr_t)(OBJ_STOCK_TAG+WHITE_BRUSH);
    window_font=(void *)(uintptr_t)(OBJ_STOCK_TAG+17U);
    if(paint){kmemset(paint,0,64);*(void**)paint=hwnd;}
    return hwnd;
}
static int WIN32_API gdi_EndPaint(void *hwnd UNUSED,const void *paint UNUSED){return 1;}
static void *WIN32_API gdi_CreateCompatibleDC(void*dc){for(uint32_t i=0;i<MAX_MEMDCS;i++)if(!memdcs[i].used){memdc_t*m=&memdcs[i];kmemset(m,0,sizeof(*m));m->used=true;m->target=dc;m->text=text_color;m->bk=background_color;m->text_align=text_align;m->bk_mode=background_mode;m->font=window_font;m->map=(gdi_map_state_t){1,0,0,0,0,1,1,1,1};return(void*)(uintptr_t)(MEMDC_BASE+i);}return NULL;}
static int WIN32_API gdi_DeleteDC(void *dc) {
    memdc_t *memory;

    if ((uint32_t)(uintptr_t)dc == DISPLAY_DC_HANDLE) return 1;

    memory = memdc_from(dc);
    if (!memory) return 0;
    kmemset(memory, 0, sizeof(*memory));
    return 1;
}
static void *WIN32_API gdi_CreateCompatibleBitmap(void*dc UNUSED,int w,int h){return bitmap_alloc(w,h);}
static uint32_t WIN32_API gdi_SetTextColor(void *dc,uint32_t color){memdc_t*m=memdc_from(dc);uint32_t old=m?m->text:text_color;if(m)m->text=color&0xFFFFFFU;else text_color=color&0xFFFFFFU;return old;}
static uint32_t WIN32_API gdi_SetBkColor(void *dc,uint32_t color){memdc_t*m=memdc_from(dc);uint32_t old=m?m->bk:background_color;if(m)m->bk=color&0xFFFFFFU;else background_color=color&0xFFFFFFU;return old;}
static uint32_t WIN32_API gdi_GetTextColor(void *dc){memdc_t*m=memdc_from(dc);return m?m->text:text_color;}
static uint32_t WIN32_API gdi_GetBkColor(void *dc){memdc_t*m=memdc_from(dc);return m?m->bk:background_color;}
static uint32_t WIN32_API gdi_SetTextAlign(void *dc,uint32_t align){
    if(align&~TA_VALID_MASK)return GDI_ERROR;
    memdc_t*m=memdc_from(dc);
    uint32_t old=m?m->text_align:text_align;
    if(m)m->text_align=align;else text_align=align;
    return old;
}
static int WIN32_API gdi_TextOutA(void *dc,int x,int y,const char*text,int length){
    char copy[256];int font_height;bool bold,italic,monospace;
    if(!text||length<0)return 0;
    if(length>254)length=254;
    kmemcpy(copy,text,(size_t)length);copy[length]='\0';
    memdc_t*m=memdc_from(dc);uint32_t align=m?m->text_align:text_align;
    int *cur_x=m?&m->x:&current_x;int *cur_y=m?&m->y:&current_y;
    void *selected_font=m?m->font:window_font;
    font_values(selected_font,&font_height,&bold,&italic,&monospace);
    int width=(int)gui_font_text_width_px(copy,(uint32_t)length,font_height,monospace,bold);
    int height=font_height;
    if(align&TA_UPDATECP){x=*cur_x;y=*cur_y;}
    if((align&TA_CENTER)==TA_CENTER)x-=width/2;else if(align&TA_RIGHT)x-=width;
    if((align&TA_BASELINE)==TA_BASELINE)y-=(height>2?height-2:height);
    else if(align&TA_BOTTOM)y-=height;
    if(m){
        bitmap_t*b=bitmap_from(m->selected);if(!b)return 0;
        /* Memory-DC glyph rasterisation remains deliberately simple, but it
         * keeps the selected bitmap and font as separate GDI objects. */
        for(int n=0;n<length;n++)for(int yy=0;yy<font_height&&yy<12;yy++)
            for(int xx=0;xx<6;xx++)if((n+xx+yy)&1){int px=x+n*8+xx,py=y+yy;
                if(px>=0&&py>=0&&px<b->width&&py<b->height)b->pixels[py*b->width+px]=m->text;}
        bitmap_sync_to_dib(b);
    }else{
        if(background_mode==2)win32_gdi_fill_rect(dc,x,y,x+width,y+height,background_color);
        if(!win32_gdi_text_ex(dc,x,y,copy,text_color,font_height,bold,italic,monospace))return 0;
    }
    if(align&TA_UPDATECP)*cur_x+=width;
    return 1;
}

static int WIN32_API gdi_DrawTextA(void *dc,const char*text,int length,int32_t*rect,uint32_t format UNUSED){if(!rect||!text)return 0;if(length<0)length=(int)kstrlen(text);return gdi_TextOutA(dc,rect[0],rect[1],text,length)?16:0;}
static int WIN32_API gdi_MoveToEx(void *dc,int x,int y,int32_t*old){memdc_t*m=memdc_from(dc);if(old){old[0]=m?m->x:current_x;old[1]=m?m->y:current_y;}if(m){m->x=x;m->y=y;}else{current_x=x;current_y=y;}return 1;}
static int draw_line_bitmap(bitmap_t*b,int x0,int y0,int x1,int y1,uint32_t c){if(!b)return 0;int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;for(;;){if(x0>=0&&y0>=0&&x0<b->width&&y0<b->height)b->pixels[y0*b->width+x0]=c;if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}return 1;}
static int WIN32_API gdi_LineTo(void *dc,int x,int y){
    memdc_t*m=memdc_from(dc);
    if(m){bitmap_t*b=bitmap_from(m->selected);int ok=draw_line_bitmap(b,m->x,m->y,x,y,m->text);m->x=x;m->y=y;if(ok)bitmap_sync_to_dib(b);return ok;}
    uint32_t color=0;bool is_null=false;
    int ok=object_color(window_pen,true,&color,&is_null)?(is_null?1:win32_gdi_line(dc,current_x,current_y,x,y,color)):0;
    current_x=x;current_y=y;return ok;
}
static int WIN32_API gdi_Rectangle(void *dc,int l,int t,int r,int b){
    memdc_t*m=memdc_from(dc);
    if(m){bitmap_t*bm=bitmap_from(m->selected);int ok;draw_line_bitmap(bm,l,t,r-1,t,m->text);draw_line_bitmap(bm,r-1,t,r-1,b-1,m->text);draw_line_bitmap(bm,r-1,b-1,l,b-1,m->text);ok=draw_line_bitmap(bm,l,b-1,l,t,m->text);if(ok)bitmap_sync_to_dib(bm);return ok;}
    uint32_t pen_color=0,brush_color=0;bool null_pen=false,null_brush=false;int ok=1;
    if(object_color(window_brush,false,&brush_color,&null_brush)&&!null_brush&&r-l>2&&b-t>2)
        ok=win32_gdi_fill_rect(dc,l+1,t+1,r-1,b-1,brush_color);
    if(object_color(window_pen,true,&pen_color,&null_pen)&&!null_pen)
        ok=win32_gdi_rect(dc,l,t,r,b,pen_color)&&ok;
    return ok;
}
static int WIN32_API gdi_Ellipse(void *dc,int l,int t,int r,int b){return gdi_Rectangle(dc,l,t,r,b);}
static int WIN32_API gdi_RoundRect(void *dc,int l,int t,int r,int b,int ew UNUSED,int eh UNUSED){return gdi_Rectangle(dc,l,t,r,b);}
static int WIN32_API gdi_SetBkMode(void *dc,int mode){memdc_t*m=memdc_from(dc);int old=m?m->bk_mode:background_mode;if(m)m->bk_mode=mode;else background_mode=mode;return old;}
static int WIN32_API gdi_GetBkMode(void *dc){memdc_t*m=memdc_from(dc);return m?m->bk_mode:background_mode;}
static void *WIN32_API gdi_CreatePen(int style UNUSED,int width UNUSED,uint32_t color){return(void*)(uintptr_t)(OBJ_PEN_TAG|(color&0xFFFFFFU));}
static void *WIN32_API gdi_CreateSolidBrush(uint32_t color){return(void*)(uintptr_t)(OBJ_BRUSH_TAG|(color&0xFFFFFFU));}
static void *WIN32_API gdi_CreateHatchBrush(int hatch UNUSED,uint32_t color){return gdi_CreateSolidBrush(color);}
static void *WIN32_API gdi_CreateBrushIndirect(const uint32_t *brush){return brush?gdi_CreateSolidBrush(brush[1]):NULL;}
static void *WIN32_API gdi_SelectObject(void *dc,void*object){
    memdc_t*m=memdc_from(dc);
    logical_font_t *font = font_from(object);
    uint32_t value=(uint32_t)(uintptr_t)object;
    bool stock_font=((value&OBJ_TAG_MASK)==OBJ_STOCK_TAG &&
                     (value&0x00FFFFFFU)>=10U);
    if(m){
        if(font||stock_font){void*old=m->font;m->font=object;return old;}
        void*old=m->selected;
        if(!bitmap_from(object)){void*decoded=win32_gdi_bitmap_from_resource(object);if(decoded)object=decoded;}
        m->selected=object;return old;
    }
    uint32_t color;bool is_null;void *old;
    if(object_color(object,true,&color,&is_null)){old=window_pen;window_pen=object;return old;}
    if(object_color(object,false,&color,&is_null)){old=window_brush;window_brush=object;return old;}
    old=window_font;window_font=object;return old;
}
static int WIN32_API gdi_DeleteObject(void*object){
    logical_font_t *font=font_from(object);
    if(font){kmemset(font,0,sizeof(*font));return 1;}
    bitmap_t*b=bitmap_from(object);if(!b)return 1;
    if(b->owned)kfree(b->pixels);
    if(b->dib_owned&&b->dib_bits)kfree(b->dib_bits);
    kmemset(b,0,sizeof(*b));return 1;
}


static void *WIN32_API gdi_GetStockObject(int object){return(void*)(uintptr_t)(OBJ_STOCK_TAG+(uint32_t)object);}
static void *WIN32_API gdi_GetCurrentObject(void *dc,uint32_t type){memdc_t*m=memdc_from(dc);if(type==6U)return m?m->font:window_font;return m?m->selected:window_font;}
static int WIN32_API gdi_GetObjectA(void*object,int count,void*out){bitmap_t*b=bitmap_from(object);if(!out||count<=0)return 0;if(b){uint8_t raw[24];kmemset(raw,0,sizeof(raw));*(int32_t*)(raw+4)=b->width;*(int32_t*)(raw+8)=b->height;*(int32_t*)(raw+12)=b->width*4;*(uint16_t*)(raw+16)=1;*(uint16_t*)(raw+18)=32;*(void**)(raw+20)=b->pixels;if(count>(int)sizeof(raw))count=sizeof(raw);kmemcpy(out,raw,(size_t)count);return count;}return 0;}
static void *WIN32_API gdi_CreateBitmap(int width,int height,uint32_t planes,uint32_t bits,const void*pixels){void*h=bitmap_alloc(width,height);bitmap_t*b=bitmap_from(h);if(!b)return NULL;if(pixels&&planes==1U&&bits==32U)kmemcpy(b->pixels,pixels,(size_t)width*(size_t)height*4U);return h;}
static void *WIN32_API gdi_CreateFontA(int h,int w,int e UNUSED,int o UNUSED,int weight,uint32_t italic,uint32_t u,uint32_t strike,uint32_t cs,uint32_t op UNUSED,uint32_t cp UNUSED,uint32_t q UNUSED,uint32_t p,const char*f){
    const bk_font_catalog_entry_t *entry=gdi_font_catalog_find(f);
    int pixel_height=h<0?-h:h;
    if(pixel_height<=0)pixel_height=entry->default_height;
    /* BLES_WINE_FONT_VISIBLE_ROLLBACK_20260723
     * Conservar la altura solicitada por la plantilla del dialogo. */
    if(pixel_height<8)pixel_height=8;
    if(pixel_height>32)pixel_height=32;
    for(uint32_t n=0;n<MAX_FONTS;n++)if(!logical_fonts[n].used){
        logical_font_t*font=&logical_fonts[n];kmemset(font,0,sizeof(*font));
        font->used=true;font->owner_process_id=task_current_process_id();font->pixel_height=(int16_t)pixel_height;
        font->requested_width=(int16_t)(w<0?-w:w);font->weight=(uint16_t)(weight>0?weight:400);
        font->bold=font->weight>=600U;font->italic=italic!=0U;
        font->underline=u!=0U;font->strikeout=strike!=0U;font->charset=(uint8_t)cs;
        font->family=entry->family;font->average_width=entry->average_width;
        font->monospace=entry->family==BK_FONT_MONO || (p&1U)!=0U;
        /* GetTextFace devuelve el nombre pedido si es reconocido; para aliases
           de shell devuelve la cara canonica que Windows seleccionaria. */
        if (f && *f && !gdi_equal_ci(f,"MS SHELL DLG") && !gdi_equal_ci(f,"MS SHELL DLG 2"))
            gdi_copy_face(font->face,f);
        else gdi_copy_face(font->face,entry->canonical);
        return(void*)(uintptr_t)(FONT_BASE+n);
    }
    return NULL;
}
void *win32_gdi_create_font_internal(int pixel_height, int weight,
                                     bool italic, bool monospace,
                                     const char *face) {
    return gdi_CreateFontA(-pixel_height, 0, 0, 0, weight,
        italic ? 1U : 0U, 0U, 0U, 0U, 0U, 0U, 0U,
        monospace ? 1U : 0U, face);
}
static int WIN32_API gdi_GetTextExtentPoint32A(void *dc,const char *text,int length,int32_t *size){
    char copy[256];int h;bool b,i,m;
    if(!text||!size||length<0)return 0;
    if(length>254)length=254;
    kmemcpy(copy,text,(size_t)length);copy[length]='\0';
    font_values(font_for_dc(dc),&h,&b,&i,&m);
    size[0]=(int32_t)gui_font_text_width_px(copy,(uint32_t)length,h,m,b);
    size[1]=h;return 1;
}
static int WIN32_API gdi_StretchBlt(void*dst,int dx,int dy,int dw,int dh,void*src,int sx,int sy,int sw,int sh,uint32_t rop);

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t size_image;
    int32_t x_pels_per_meter;
    int32_t y_pels_per_meter;
    uint32_t colors_used;
    uint32_t colors_important;
} bitmap_info_header_t;

static void *WIN32_API gdi_CreateDIBSection(void *dc UNUSED, const void *info_raw,
                                             uint32_t usage UNUSED, void **bits,
                                             void *section UNUSED,
                                             uint32_t offset UNUSED) {
    const bitmap_info_header_t *info = (const bitmap_info_header_t *)info_raw;
    int height;
    uint32_t stride, bytes;
    void *handle;
    bitmap_t *bitmap;
    if (bits) *bits = NULL;
    if (!info || info->size < sizeof(*info) || info->width <= 0 ||
        info->height == 0 || info->planes != 1U || info->compression != BI_RGB ||
        (info->bit_count != 16U && info->bit_count != 24U && info->bit_count != 32U))
        return NULL;
    height = info->height < 0 ? -info->height : info->height;
    stride = dib_stride_for(info->width, info->bit_count);
    if ((uint32_t)height > 0xFFFFFFFFU / stride) return NULL;
    bytes = stride * (uint32_t)height;
    handle = bitmap_alloc(info->width, height);
    bitmap = bitmap_from(handle);
    if (!bitmap) return NULL;
    bitmap->dib_bits = (uint8_t *)kzalloc(bytes ? bytes : 1U);
    if (!bitmap->dib_bits) { gdi_DeleteObject(handle); return NULL; }
    bitmap->dib_owned = true;
    bitmap->dib_stride = stride;
    bitmap->dib_bpp = info->bit_count;
    bitmap->dib_top_down = info->height < 0;
    if (bits) *bits = bitmap->dib_bits;
    return handle;
}

static int gdi_copy_scanlines_to_bitmap(bitmap_t *bitmap, uint32_t start,
                                         uint32_t lines, const void *bits,
                                         const bitmap_info_header_t *info) {
    uint32_t stride;
    if (!bitmap || !bits || !info || start >= (uint32_t)bitmap->height) return 0;
    if (lines > (uint32_t)bitmap->height - start) lines = (uint32_t)bitmap->height - start;
    stride = dib_stride_for(info->width, info->bit_count);
    for (uint32_t row = 0; row < lines; row++) {
        uint32_t logical = start + row;
        uint32_t src_row = info->height < 0 ? row : lines - 1U - row;
        const uint8_t *src = (const uint8_t *)bits + src_row * stride;
        for (int x = 0; x < bitmap->width && x < info->width; x++) {
            uint32_t c = 0U;
            if (info->bit_count == 32U) c = rd32(src + (uint32_t)x * 4U) & 0xFFFFFFU;
            else if (info->bit_count == 24U) {
                const uint8_t *q = src + (uint32_t)x * 3U;
                c = q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16);
            } else if (info->bit_count == 16U) {
                uint16_t q = rd16(src + (uint32_t)x * 2U);
                c = ((uint32_t)(q & 31U) << 3) | ((uint32_t)((q >> 5) & 31U) << 11) |
                    ((uint32_t)((q >> 10) & 31U) << 19);
            }
            bitmap->pixels[logical * (uint32_t)bitmap->width + (uint32_t)x] = c;
        }
    }
    bitmap_sync_to_dib(bitmap);
    return (int)lines;
}

static void *WIN32_API gdi_CreateDIBitmap(void *dc UNUSED,
                                           const bitmap_info_header_t *header,
                                           uint32_t init, const void *bits,
                                           const void *info UNUSED,
                                           uint32_t usage UNUSED) {
    void *handle;
    bitmap_t *bitmap;
    int height;
    if (!header || header->width <= 0 || header->height == 0) return NULL;
    height = header->height < 0 ? -header->height : header->height;
    handle = bitmap_alloc(header->width, height);
    bitmap = bitmap_from(handle);
    if (!bitmap) return NULL;
    if ((init & 4U) && bits) (void)gdi_copy_scanlines_to_bitmap(bitmap, 0U,
        (uint32_t)height, bits, header);
    return handle;
}

static int WIN32_API gdi_SetDIBits(void *dc UNUSED, void *bitmap_handle,
                                    uint32_t start, uint32_t lines,
                                    const void *bits, const void *info_raw,
                                    uint32_t usage UNUSED) {
    return gdi_copy_scanlines_to_bitmap(bitmap_from(bitmap_handle), start, lines,
        bits, (const bitmap_info_header_t *)info_raw);
}

static int WIN32_API gdi_GetDIBits(void *dc UNUSED, void *bitmap_handle,
                                    uint32_t start, uint32_t lines, void *bits,
                                    void *info_raw, uint32_t usage UNUSED) {
    bitmap_t *bitmap = bitmap_from(bitmap_handle);
    bitmap_info_header_t *info = (bitmap_info_header_t *)info_raw;
    uint32_t stride;
    if (!bitmap || !info || info->size < sizeof(*info)) return 0;
    if (!info->width) info->width = bitmap->width;
    if (!info->height) info->height = bitmap->height;
    if (!info->planes) info->planes = 1U;
    if (!info->bit_count) info->bit_count = 32U;
    info->compression = BI_RGB;
    stride = dib_stride_for(bitmap->width, info->bit_count);
    info->size_image = stride * (uint32_t)bitmap->height;
    if (!bits) return (int)bitmap->height;
    if (start >= (uint32_t)bitmap->height) return 0;
    if (lines > (uint32_t)bitmap->height - start) lines = (uint32_t)bitmap->height - start;
    for (uint32_t row = 0; row < lines; row++) {
        uint32_t logical = start + row;
        uint32_t dst_row = info->height < 0 ? row : lines - 1U - row;
        uint8_t *dst = (uint8_t *)bits + dst_row * stride;
        for (int x = 0; x < bitmap->width; x++) {
            uint32_t c = bitmap->pixels[logical * (uint32_t)bitmap->width + (uint32_t)x];
            if (info->bit_count == 32U) *(uint32_t *)(dst + (uint32_t)x * 4U) = c;
            else if (info->bit_count == 24U) {
                uint8_t *q = dst + (uint32_t)x * 3U; q[0]=(uint8_t)c;q[1]=(uint8_t)(c>>8);q[2]=(uint8_t)(c>>16);
            }
        }
    }
    return (int)lines;
}

static int WIN32_API gdi_StretchDIBits(void *dc, int dx, int dy, int dw, int dh,
                                        int sx, int sy, int sw, int sh,
                                        const void *bits, const void *info_raw,
                                        uint32_t usage UNUSED, uint32_t rop) {
    const bitmap_info_header_t *info = (const bitmap_info_header_t *)info_raw;
    void *bitmap_handle, *source_dc;
    bitmap_t *bitmap;
    int height, result;
    if (!info || !bits || info->width <= 0 || info->height == 0) return 0;
    height = info->height < 0 ? -info->height : info->height;
    bitmap_handle = bitmap_alloc(info->width, height);
    bitmap = bitmap_from(bitmap_handle);
    if (!bitmap) return 0;
    (void)gdi_copy_scanlines_to_bitmap(bitmap, 0U, (uint32_t)height, bits, info);
    source_dc = gdi_CreateCompatibleDC(dc);
    if (!source_dc) { gdi_DeleteObject(bitmap_handle); return 0; }
    gdi_SelectObject(source_dc, bitmap_handle);
    result = gdi_StretchBlt(dc, dx, dy, dw, dh, source_dc, sx, sy, sw, sh, rop);
    gdi_DeleteDC(source_dc); gdi_DeleteObject(bitmap_handle);
    return result ? sh : 0;
}

static int WIN32_API gdi_SetDIBitsToDevice(void *dc, int dx, int dy,
                                            uint32_t width, uint32_t height,
                                            int sx, int sy, uint32_t start,
                                            uint32_t lines, const void *bits,
                                            const void *info, uint32_t usage) {
    (void)start;
    if (lines < height) height = lines;
    return gdi_StretchDIBits(dc, dx, dy, (int)width, (int)height, sx, sy,
        (int)width, (int)height, bits, info, usage, SRCCOPY);
}

typedef struct{int32_t height,ascent,descent,internal_leading,external_leading,ave_width,max_width,weight,overhang,aspect_x,aspect_y;uint8_t first_char,last_char,default_char,break_char,italic,underlined,struck_out,pitch_family,char_set;uint8_t pad[3];}textmetric_a_t;
static int WIN32_API gdi_GetTextMetricsA(void *dc,textmetric_a_t *tm){int h;bool b,i,m;logical_font_t*fnt=font_from(font_for_dc(dc));if(!tm)return 0;font_values(font_for_dc(dc),&h,&b,&i,&m);kmemset(tm,0,sizeof(*tm));tm->height=h;tm->ascent=h>2?h-2:h;tm->descent=h>=2?2:0;tm->ave_width=fnt&&fnt->average_width?((int)fnt->average_width*h+4)/8:text_width_for_font(font_for_dc(dc),"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52U)/52;if(fnt&&fnt->requested_width>0)tm->ave_width=fnt->requested_width;if(tm->ave_width<1)tm->ave_width=1;tm->max_width=tm->ave_width+(b?2:1);tm->weight=fnt?fnt->weight:(b?700:400);tm->first_char=32;tm->last_char=255;tm->default_char='?';tm->break_char=' ';tm->italic=i;tm->underlined=fnt&&fnt->underline;tm->struck_out=fnt&&fnt->strikeout;tm->char_set=fnt?fnt->charset:0;tm->pitch_family=(uint8_t)(m?1:(fnt&&fnt->family==BK_FONT_SERIF?0x12:0x22));return 1;}
static int WIN32_API gdi_GetDeviceCaps(void *dc UNUSED,int index){gui_desktop_t*d=gui_get_desktop();switch(index){case 8:return d?d->surface.width:800;case 10:return d?d->surface.height:600;case 12:return 32;case 14:return 1;case 24:return -1;case 88:case 90:return 96;default:return 0;}}

typedef struct {
    int32_t height,width,escapement,orientation,weight;
    uint8_t italic,underline,strikeout,charset;
    uint8_t out_precision,clip_precision,quality,pitch_family;
    char face_name[32];
} logfont_a_t;
static gdi_map_state_t *gdi_map_from_dc(void *dc){memdc_t*m=memdc_from(dc);return m?&m->map:&window_map;}
static int32_t gdi_muldiv_round(int32_t value,int32_t numerator,int32_t denominator){int64_t product=(int64_t)value*(int64_t)numerator;if(!denominator)return 0;if(product>=0)product+=denominator>0?denominator/2:-(denominator/2);else product-=denominator>0?denominator/2:-(denominator/2);return(int32_t)(product/denominator);}
static int WIN32_API gdi_SetMapMode(void *dc,int mode){gdi_map_state_t*s=gdi_map_from_dc(dc);int old=s->map_mode;if(mode<1||mode>8)return 0;s->map_mode=mode;s->window_org_x=s->window_org_y=s->viewport_org_x=s->viewport_org_y=0;switch(mode){case 1:s->window_ext_x=s->window_ext_y=s->viewport_ext_x=s->viewport_ext_y=1;break;case 2:s->window_ext_x=s->window_ext_y=254;s->viewport_ext_x=96;s->viewport_ext_y=-96;break;case 3:s->window_ext_x=s->window_ext_y=2540;s->viewport_ext_x=96;s->viewport_ext_y=-96;break;case 4:s->window_ext_x=s->window_ext_y=100;s->viewport_ext_x=96;s->viewport_ext_y=-96;break;case 5:s->window_ext_x=s->window_ext_y=1000;s->viewport_ext_x=96;s->viewport_ext_y=-96;break;case 6:s->window_ext_x=s->window_ext_y=1440;s->viewport_ext_x=96;s->viewport_ext_y=-96;break;default:if(!s->window_ext_x)s->window_ext_x=1;if(!s->window_ext_y)s->window_ext_y=1;if(!s->viewport_ext_x)s->viewport_ext_x=1;if(!s->viewport_ext_y)s->viewport_ext_y=1;break;}return old;}
static int WIN32_API gdi_GetMapMode(void *dc){return gdi_map_from_dc(dc)->map_mode;}
static int gdi_set_pair(int32_t*a,int32_t*b,int32_t x,int32_t y,int32_t*old){if(old){old[0]=*a;old[1]=*b;}*a=x;*b=y;return 1;}
static int WIN32_API gdi_SetWindowOrgEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);return gdi_set_pair(&s->window_org_x,&s->window_org_y,x,y,old);}
static int WIN32_API gdi_SetViewportOrgEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);return gdi_set_pair(&s->viewport_org_x,&s->viewport_org_y,x,y,old);}
static int WIN32_API gdi_OffsetWindowOrgEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(old){old[0]=s->window_org_x;old[1]=s->window_org_y;}s->window_org_x+=x;s->window_org_y+=y;return 1;}
static int WIN32_API gdi_OffsetViewportOrgEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(old){old[0]=s->viewport_org_x;old[1]=s->viewport_org_y;}s->viewport_org_x+=x;s->viewport_org_y+=y;return 1;}
static int WIN32_API gdi_SetWindowExtEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!x||!y)return 0;return gdi_set_pair(&s->window_ext_x,&s->window_ext_y,x,y,old);}
static int WIN32_API gdi_SetViewportExtEx(void*dc,int x,int y,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!x||!y)return 0;return gdi_set_pair(&s->viewport_ext_x,&s->viewport_ext_y,x,y,old);}
static int WIN32_API gdi_ScaleWindowExtEx(void*dc,int xn,int xd,int yn,int yd,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!xd||!yd)return 0;if(old){old[0]=s->window_ext_x;old[1]=s->window_ext_y;}s->window_ext_x=gdi_muldiv_round(s->window_ext_x,xn,xd);s->window_ext_y=gdi_muldiv_round(s->window_ext_y,yn,yd);return s->window_ext_x&&s->window_ext_y;}
static int WIN32_API gdi_ScaleViewportExtEx(void*dc,int xn,int xd,int yn,int yd,int32_t*old){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!xd||!yd)return 0;if(old){old[0]=s->viewport_ext_x;old[1]=s->viewport_ext_y;}s->viewport_ext_x=gdi_muldiv_round(s->viewport_ext_x,xn,xd);s->viewport_ext_y=gdi_muldiv_round(s->viewport_ext_y,yn,yd);return s->viewport_ext_x&&s->viewport_ext_y;}
static int WIN32_API gdi_LPtoDP(void*dc,int32_t*points,int count){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!points||count<0||!s->window_ext_x||!s->window_ext_y)return 0;for(int i=0;i<count;i++){points[i*2]=s->viewport_org_x+gdi_muldiv_round(points[i*2]-s->window_org_x,s->viewport_ext_x,s->window_ext_x);points[i*2+1]=s->viewport_org_y+gdi_muldiv_round(points[i*2+1]-s->window_org_y,s->viewport_ext_y,s->window_ext_y);}return 1;}
static int WIN32_API gdi_DPtoLP(void*dc,int32_t*points,int count){gdi_map_state_t*s=gdi_map_from_dc(dc);if(!points||count<0||!s->viewport_ext_x||!s->viewport_ext_y)return 0;for(int i=0;i<count;i++){points[i*2]=s->window_org_x+gdi_muldiv_round(points[i*2]-s->viewport_org_x,s->window_ext_x,s->viewport_ext_x);points[i*2+1]=s->window_org_y+gdi_muldiv_round(points[i*2+1]-s->viewport_org_y,s->window_ext_y,s->viewport_ext_y);}return 1;}
static int WIN32_API gdi_GetCharWidthA(void *dc,uint32_t first,uint32_t last,int32_t *widths){
    char glyph[2]={'\0','\0'};void*font=font_for_dc(dc);
    if(!widths||last<first)return 0;
    for(uint32_t c=first;c<=last;c++){glyph[0]=(char)c;widths[c-first]=text_width_for_font(font,glyph,1U);if(c==0xFFFFFFFFU)break;}return 1;
}
static int WIN32_API gdi_GetTextFaceA(void *dc,int count,char *face){
    logical_font_t*font=font_from(font_for_dc(dc));const char*name=font?font->face:"System";
    int len=(int)kstrlen(name);if(!face||count<=0)return len+1;if(len>=count)len=count-1;
    kmemcpy(face,name,(size_t)len);face[len]='\0';return len;
}
static void *WIN32_API gdi_CreateFontIndirectA(const logfont_a_t *font){if(!font)return NULL;return gdi_CreateFontA(font->height,font->width,font->escapement,font->orientation,font->weight,font->italic,font->underline,font->strikeout,font->charset,font->out_precision,font->clip_precision,font->quality,font->pitch_family,font->face_name);}
static int WIN32_API gdi_SetAbortProc(void *dc UNUSED,void *proc UNUSED){return 1;}
static int WIN32_API gdi_StartDocA(void *dc UNUSED,const void *doc UNUSED){return -1;}
static int WIN32_API gdi_StartPage(void *dc UNUSED){return -1;}
static int WIN32_API gdi_EndPage(void *dc UNUSED){return -1;}
static int WIN32_API gdi_EndDoc(void *dc UNUSED){return -1;}
static int WIN32_API gdi_AbortDoc(void *dc UNUSED){return -1;}

static uint32_t WIN32_API gdi_SetPixel(void *dc,int x,int y,uint32_t color){memdc_t*m=memdc_from(dc);if(m){bitmap_t*b=bitmap_from(m->selected);if(!b||x<0||y<0||x>=b->width||y>=b->height)return 0xFFFFFFFFU;b->pixels[y*b->width+x]=color&0xFFFFFFU;bitmap_sync_to_dib(b);return color&0xFFFFFFU;}return win32_gdi_line(dc,x,y,x,y,color&0xFFFFFFU)?(color&0xFFFFFFU):0xFFFFFFFFU;}
static uint32_t WIN32_API gdi_GetPixel(void *dc,int x,int y){memdc_t*m=memdc_from(dc);bitmap_t*b=m?bitmap_from(m->selected):NULL;if(!b||x<0||y<0||x>=b->width||y>=b->height)return 0xFFFFFFFFU;bitmap_sync_from_dib(b);return b->pixels[y*b->width+x]&0xFFFFFFU;}
static int WIN32_API gdi_PatBlt(void*dc,int x,int y,int width,int height,uint32_t rop){uint32_t color;if(width<=0||height<=0)return 0;color=rop==BLACKNESS?0U:(rop==WHITENESS?0xFFFFFFU:0xFFFFFFU);if(rop!=BLACKNESS&&rop!=WHITENESS&&rop!=PATCOPY)return 0;memdc_t*m=memdc_from(dc);if(m){bitmap_t*b=bitmap_from(m->selected);if(!b)return 0;for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++)if(x+xx>=0&&y+yy>=0&&x+xx<b->width&&y+yy<b->height)b->pixels[(y+yy)*b->width+x+xx]=color;bitmap_sync_to_dib(b);return 1;}return win32_gdi_fill_rect(dc,x,y,x+width,y+height,color);}
static int WIN32_API gdi_SaveDC(void*dc){memdc_t*m=memdc_from(dc);if(!m||m->saved_count>=8U)return 0;gdi_saved_state_t*s=&m->saved[m->saved_count++];s->selected=m->selected;s->font=m->font;s->text=m->text;s->bk=m->bk;s->text_align=m->text_align;s->bk_mode=m->bk_mode;s->x=m->x;s->y=m->y;s->map=m->map;return m->saved_count;}
static int WIN32_API gdi_RestoreDC(void*dc,int saved){memdc_t*m=memdc_from(dc);uint32_t index;if(!m||!m->saved_count||saved==0)return 0;if(saved<0){int target=(int)m->saved_count+saved;if(target<0)return 0;index=(uint32_t)target;}else{if((uint32_t)saved>m->saved_count)return 0;index=(uint32_t)saved-1U;}gdi_saved_state_t*s=&m->saved[index];m->selected=s->selected;m->font=s->font;m->text=s->text;m->bk=s->bk;m->text_align=s->text_align;m->bk_mode=s->bk_mode;m->x=s->x;m->y=s->y;m->map=s->map;m->saved_count=(uint8_t)index;return 1;}
static int WIN32_API gdi_Polyline(void *dc,const int32_t *points,int count){if(!points||count<2)return 0;for(int i=1;i<count;i++){gdi_MoveToEx(dc,points[(i-1)*2],points[(i-1)*2+1],NULL);if(!gdi_LineTo(dc,points[i*2],points[i*2+1]))return 0;}return 1;}
static int WIN32_API gdi_BitBlt(void*dst,int dx,int dy,int w,int h,void*src,int sx,int sy,uint32_t rop){if((rop!=SRCCOPY&&rop!=SRCAND&&rop!=SRCPAINT&&rop!=SRCINVERT&&rop!=NOTSRCCOPY)||w<=0||h<=0)return 0;memdc_t*sm=memdc_from(src);bitmap_t*sb=sm?bitmap_from(sm->selected):NULL;if(!sb)return 0;bitmap_sync_from_dib(sb);if(sx<0||sy<0||sx+w>sb->width||sy+h>sb->height)return 0;memdc_t*dm=memdc_from(dst);if(dm){bitmap_t*db=bitmap_from(dm->selected);if(!db)return 0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(dx+x>=0&&dy+y>=0&&dx+x<db->width&&dy+y<db->height){uint32_t sv=sb->pixels[(sy+y)*sb->width+sx+x],*dv=&db->pixels[(dy+y)*db->width+dx+x];if(rop==SRCCOPY)*dv=sv;else if(rop==SRCAND)*dv&=sv;else if(rop==SRCPAINT)*dv|=sv;else if(rop==SRCINVERT)*dv^=sv;else *dv=(~sv)&0xFFFFFFU;}bitmap_sync_to_dib(db);return 1;}return win32_gdi_blit(dst,dx,dy,w,h,sb->pixels,sb->width,sx,sy);}
static int WIN32_API gdi_StretchBlt(void*dst,int dx,int dy,int dw,int dh,void*src,int sx,int sy,int sw,int sh,uint32_t rop){if(rop!=SRCCOPY||dw<=0||dh<=0||sw<=0||sh<=0)return 0;memdc_t*sm=memdc_from(src);bitmap_t*sb=sm?bitmap_from(sm->selected):NULL;if(!sb)return 0;void*tmp=bitmap_alloc(dw,dh);bitmap_t*tb=bitmap_from(tmp);if(!tb)return 0;for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){int px=sx+x*sw/dw,py=sy+y*sh/dh;if(px>=0&&py>=0&&px<sb->width&&py<sb->height)tb->pixels[y*dw+x]=sb->pixels[py*sb->width+px];}memdc_t fake={.used=true,.target=NULL,.selected=tmp,.map={1,0,0,0,0,1,1,1,1}};memdc_t*slot=NULL;for(uint32_t i=0;i<MAX_MEMDCS;i++)if(!memdcs[i].used){memdcs[i]=fake;slot=&memdcs[i];src=(void*)(uintptr_t)(MEMDC_BASE+i);break;}int ok=slot?gdi_BitBlt(dst,dx,dy,dw,dh,src,0,0,rop):0;if(slot)kmemset(slot,0,sizeof(*slot));gdi_DeleteObject(tmp);return ok;}
uint32_t win32_gdi32_resolve(const char*name){
#define G(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&gdi_##api
G(GetDC);G(ReleaseDC);G(BeginPaint);G(EndPaint);G(CreateDCA);G(CreateCompatibleDC);G(DeleteDC);G(CreateCompatibleBitmap);G(CreateBitmap);G(CreateDIBSection);G(CreateDIBitmap);G(GetDIBits);G(SetDIBits);G(SetDIBitsToDevice);G(StretchDIBits);G(BitBlt);G(StretchBlt);G(PatBlt);G(SetTextColor);G(GetTextColor);G(SetTextAlign);G(SetBkColor);G(GetBkColor);G(SetBkMode);G(GetBkMode);G(TextOutA);G(DrawTextA);G(MoveToEx);G(LineTo);G(Rectangle);G(Ellipse);G(RoundRect);G(CreatePen);G(CreateSolidBrush);G(CreateHatchBrush);G(CreateBrushIndirect);G(CreateFontA);G(CreateFontIndirectA);G(SelectObject);G(DeleteObject);G(GetStockObject);G(GetCurrentObject);G(GetObjectA);G(GetTextExtentPoint32A);G(GetTextMetricsA);G(GetDeviceCaps);G(GetCharWidthA);G(GetTextFaceA);G(SetMapMode);G(GetMapMode);G(LPtoDP);G(DPtoLP);G(SetWindowOrgEx);G(SetViewportOrgEx);G(OffsetWindowOrgEx);G(OffsetViewportOrgEx);G(SetWindowExtEx);G(SetViewportExtEx);G(ScaleWindowExtEx);G(ScaleViewportExtEx);G(SetAbortProc);G(StartDocA);G(StartPage);G(EndPage);G(EndDoc);G(AbortDoc);G(SetPixel);G(GetPixel);G(Polyline);G(SaveDC);G(RestoreDC);
#undef G
return 0;}
