#include "win32.h"
#include "resources.h"
#include "../../gui/gui.h"
#include "../include/memory.h"

#define MEMDC_BASE 0x78000000U
#define BITMAP_BASE 0x79000000U
#define MAX_MEMDCS 16U
#define MAX_BITMAPS 32U
#define SRCCOPY 0x00CC0020U
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

typedef struct { bool used; void *target; void *selected; uint32_t text,bk,text_align; int bk_mode,x,y; } memdc_t;
typedef struct { bool used; int width,height; uint32_t *pixels; bool owned; } bitmap_t;
static memdc_t memdcs[MAX_MEMDCS];
static bitmap_t bitmaps[MAX_BITMAPS];
static uint32_t text_color=0x00101010U,background_color=0x00FFFFFFU;
static int background_mode=2,current_x,current_y;
static uint32_t text_align;
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
static memdc_t *memdc_from(void*h){uint32_t v=(uint32_t)(uintptr_t)h;if(v<MEMDC_BASE||v>=MEMDC_BASE+MAX_MEMDCS)return NULL;v-=MEMDC_BASE;return memdcs[v].used?&memdcs[v]:NULL;}
static bitmap_t *bitmap_from(void*h){uint32_t v=(uint32_t)(uintptr_t)h;if(v<BITMAP_BASE||v>=BITMAP_BASE+MAX_BITMAPS)return NULL;v-=BITMAP_BASE;return bitmaps[v].used?&bitmaps[v]:NULL;}
static void *bitmap_alloc(int w,int h){if(w<=0||h<=0||w>2048||h>2048)return NULL;for(uint32_t i=0;i<MAX_BITMAPS;i++)if(!bitmaps[i].used){bitmap_t*b=&bitmaps[i];kmemset(b,0,sizeof(*b));b->pixels=(uint32_t*)kzalloc((size_t)w*(size_t)h*sizeof(uint32_t));if(!b->pixels)return NULL;b->used=true;b->owned=true;b->width=w;b->height=h;return(void*)(uintptr_t)(BITMAP_BASE+i);}return NULL;}
static uint16_t rd16(const uint8_t*p){return(uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void *decode_dib_resource(void*resource){const uint8_t*d=(const uint8_t*)win32_resource_lock(resource);uint32_t size=win32_resource_size(NULL,resource);if(!d||size<40U||rd32(d)<40U)return NULL;int w=(int)rd32(d+4),h=(int)rd32(d+8);uint16_t planes=rd16(d+12),bpp=rd16(d+14);uint32_t comp=rd32(d+16),colors=rd32(d+32);if(w<=0||h==0||planes!=1||comp!=0U)return NULL;bool top=h<0;if(h<0)h=-h;void*handle=bitmap_alloc(w,h);bitmap_t*b=bitmap_from(handle);if(!b)return NULL;uint32_t palette_count=bpp<=8U?(colors?colors:(1U<<bpp)):0U;uint32_t off=rd32(d)+palette_count*4U;if(off>=size){return handle;}uint32_t stride=((uint32_t)w*bpp+31U)/32U*4U;for(int y=0;y<h;y++){uint32_t sy=top?(uint32_t)y:(uint32_t)(h-1-y);const uint8_t*row=d+off+sy*stride;if(row>=d+size)break;for(int x=0;x<w;x++){uint32_t c=0;if(bpp==32U)c=rd32(row+x*4U)&0x00FFFFFFU;else if(bpp==24U){const uint8_t*p=row+x*3U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==8U){uint8_t idx=row[x];const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==4U){uint8_t q=row[x/2];uint8_t idx=(x&1)?(q&15U):(q>>4);const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}else if(bpp==1U){uint8_t idx=(row[x/8]>>(7-(x&7)))&1U;const uint8_t*p=d+rd32(d)+idx*4U;c=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);}b->pixels[y*w+x]=c;}}return handle;}
static void *WIN32_API gdi_GetDC(void *hwnd){return hwnd;}
static int WIN32_API gdi_ReleaseDC(void *hwnd UNUSED,void *dc UNUSED){return 1;}
static void *WIN32_API gdi_BeginPaint(void *hwnd,void *paint){
    win32_gdi_begin(hwnd);
    text_color=0x00101010U;background_color=0x00FFFFFFU;background_mode=2;
    text_align=0U;current_x=0;current_y=0;
    window_pen=(void *)(uintptr_t)(OBJ_STOCK_TAG+BLACK_PEN);
    window_brush=(void *)(uintptr_t)(OBJ_STOCK_TAG+WHITE_BRUSH);
    window_font=(void *)(uintptr_t)(OBJ_STOCK_TAG+17U);
    if(paint){kmemset(paint,0,64);*(void**)paint=hwnd;}
    return hwnd;
}
static int WIN32_API gdi_EndPaint(void *hwnd UNUSED,const void *paint UNUSED){return 1;}
static void *WIN32_API gdi_CreateCompatibleDC(void*dc){for(uint32_t i=0;i<MAX_MEMDCS;i++)if(!memdcs[i].used){memdc_t*m=&memdcs[i];kmemset(m,0,sizeof(*m));m->used=true;m->target=dc;m->text=text_color;m->bk=background_color;m->text_align=text_align;m->bk_mode=background_mode;return(void*)(uintptr_t)(MEMDC_BASE+i);}return NULL;}
static int WIN32_API gdi_DeleteDC(void*dc){memdc_t*m=memdc_from(dc);if(!m)return 0;kmemset(m,0,sizeof(*m));return 1;}
static void *WIN32_API gdi_CreateCompatibleBitmap(void*dc UNUSED,int w,int h){return bitmap_alloc(w,h);}
static uint32_t WIN32_API gdi_SetTextColor(void *dc,uint32_t color){memdc_t*m=memdc_from(dc);uint32_t old=m?m->text:text_color;if(m)m->text=color&0xFFFFFFU;else text_color=color&0xFFFFFFU;return old;}
static uint32_t WIN32_API gdi_SetBkColor(void *dc,uint32_t color){memdc_t*m=memdc_from(dc);uint32_t old=m?m->bk:background_color;if(m)m->bk=color&0xFFFFFFU;else background_color=color&0xFFFFFFU;return old;}
static uint32_t WIN32_API gdi_SetTextAlign(void *dc,uint32_t align){
    if(align&~TA_VALID_MASK)return GDI_ERROR;
    memdc_t*m=memdc_from(dc);
    uint32_t old=m?m->text_align:text_align;
    if(m)m->text_align=align;else text_align=align;
    return old;
}
static int WIN32_API gdi_TextOutA(void *dc,int x,int y,const char*text,int length){
    char copy[96];
    if(!text||length<0)return 0;
    if(length>94)length=94;
    kmemcpy(copy,text,(size_t)length);copy[length]='\0';
    memdc_t*m=memdc_from(dc);
    uint32_t align=m?m->text_align:text_align;
    int *cur_x=m?&m->x:&current_x;
    int *cur_y=m?&m->y:&current_y;
    int width=(int)gui_font_text_width(copy);
    int height=12;
    if(align&TA_UPDATECP){x=*cur_x;y=*cur_y;}
    if((align&TA_CENTER)==TA_CENTER)x-=width/2;
    else if(align&TA_RIGHT)x-=width;
    if((align&TA_BASELINE)==TA_BASELINE)y-=10;
    else if(align&TA_BOTTOM)y-=height;
    if(m){
        bitmap_t*b=bitmap_from(m->selected);if(!b)return 0;
        for(int i=0;i<length;i++)for(int yy=0;yy<10;yy++)for(int xx=0;xx<6;xx++)if((i+xx+yy)&1){
            int px=x+i*8+xx,py=y+yy;
            if(px>=0&&py>=0&&px<b->width&&py<b->height)b->pixels[py*b->width+px]=m->text;
        }
    }else{
        if(background_mode==2)
            win32_gdi_fill_rect(dc,x,y,x+width,y+height,background_color);
        if(!win32_gdi_text(dc,x,y,copy,text_color))return 0;
    }
    if(align&TA_UPDATECP)*cur_x+=width;
    return 1;
}
static int WIN32_API gdi_DrawTextA(void *dc,const char*text,int length,int32_t*rect,uint32_t format UNUSED){if(!rect||!text)return 0;if(length<0)length=(int)kstrlen(text);return gdi_TextOutA(dc,rect[0],rect[1],text,length)?16:0;}
static int WIN32_API gdi_MoveToEx(void *dc,int x,int y,int32_t*old){memdc_t*m=memdc_from(dc);if(old){old[0]=m?m->x:current_x;old[1]=m?m->y:current_y;}if(m){m->x=x;m->y=y;}else{current_x=x;current_y=y;}return 1;}
static int draw_line_bitmap(bitmap_t*b,int x0,int y0,int x1,int y1,uint32_t c){if(!b)return 0;int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=-(y1>y0?y1-y0:y0-y1),sy=y0<y1?1:-1,err=dx+dy;for(;;){if(x0>=0&&y0>=0&&x0<b->width&&y0<b->height)b->pixels[y0*b->width+x0]=c;if(x0==x1&&y0==y1)break;int e2=2*err;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}return 1;}
static int WIN32_API gdi_LineTo(void *dc,int x,int y){
    memdc_t*m=memdc_from(dc);
    if(m){int ok=draw_line_bitmap(bitmap_from(m->selected),m->x,m->y,x,y,m->text);m->x=x;m->y=y;return ok;}
    uint32_t color=0;bool is_null=false;
    int ok=object_color(window_pen,true,&color,&is_null)?(is_null?1:win32_gdi_line(dc,current_x,current_y,x,y,color)):0;
    current_x=x;current_y=y;return ok;
}
static int WIN32_API gdi_Rectangle(void *dc,int l,int t,int r,int b){
    memdc_t*m=memdc_from(dc);
    if(m){bitmap_t*bm=bitmap_from(m->selected);draw_line_bitmap(bm,l,t,r-1,t,m->text);draw_line_bitmap(bm,r-1,t,r-1,b-1,m->text);draw_line_bitmap(bm,r-1,b-1,l,b-1,m->text);return draw_line_bitmap(bm,l,b-1,l,t,m->text);}
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
static void *WIN32_API gdi_CreatePen(int style UNUSED,int width UNUSED,uint32_t color){return(void*)(uintptr_t)(OBJ_PEN_TAG|(color&0xFFFFFFU));}
static void *WIN32_API gdi_CreateSolidBrush(uint32_t color){return(void*)(uintptr_t)(OBJ_BRUSH_TAG|(color&0xFFFFFFU));}
static void *WIN32_API gdi_SelectObject(void *dc,void*object){
    memdc_t*m=memdc_from(dc);
    if(m){void*old=m->selected;if(!bitmap_from(object)){void*decoded=decode_dib_resource(object);if(decoded)object=decoded;}m->selected=object;return old;}
    uint32_t color;bool is_null;void *old;
    if(object_color(object,true,&color,&is_null)){old=window_pen;window_pen=object;return old;}
    if(object_color(object,false,&color,&is_null)){old=window_brush;window_brush=object;return old;}
    old=window_font;window_font=object;return old;
}
static int WIN32_API gdi_DeleteObject(void*object){bitmap_t*b=bitmap_from(object);if(!b)return 1;if(b->owned)kfree(b->pixels);kmemset(b,0,sizeof(*b));return 1;}
static void *WIN32_API gdi_GetStockObject(int object){return(void*)(uintptr_t)(OBJ_STOCK_TAG+(uint32_t)object);}
static void *WIN32_API gdi_CreateFontA(int h UNUSED,int w UNUSED,int e UNUSED,int o UNUSED,int weight UNUSED,uint32_t i UNUSED,uint32_t u UNUSED,uint32_t s UNUSED,uint32_t cs UNUSED,uint32_t op UNUSED,uint32_t cp UNUSED,uint32_t q UNUSED,uint32_t p UNUSED,const char*f UNUSED){return(void*)(uintptr_t)(OBJ_STOCK_TAG+0x100U);}
static int WIN32_API gdi_GetTextExtentPoint32A(void *dc UNUSED,const char *text,int length,int32_t *size){char copy[128];if(!text||!size||length<0)return 0;if(length>126)length=126;kmemcpy(copy,text,(size_t)length);copy[length]='\0';size[0]=(int32_t)gui_font_text_width(copy);size[1]=12;return 1;}
typedef struct{int32_t height,ascent,descent,internal_leading,external_leading,ave_width,max_width,weight,overhang,aspect_x,aspect_y;uint8_t first_char,last_char,default_char,break_char,italic,underlined,struck_out,pitch_family,char_set;uint8_t pad[3];}textmetric_a_t;
static int WIN32_API gdi_GetTextMetricsA(void *dc UNUSED,textmetric_a_t *tm){if(!tm)return 0;kmemset(tm,0,sizeof(*tm));tm->height=12;tm->ascent=10;tm->descent=2;tm->ave_width=8;tm->max_width=8;tm->weight=400;tm->first_char=32;tm->last_char=126;tm->default_char='?';tm->break_char=' ';tm->pitch_family=1;return 1;}
static int WIN32_API gdi_GetDeviceCaps(void *dc UNUSED,int index){gui_desktop_t*d=gui_get_desktop();switch(index){case 8:return d?d->surface.width:800;case 10:return d?d->surface.height:600;case 12:return 32;case 14:return 1;case 24:return -1;case 88:case 90:return 96;default:return 0;}}

typedef struct {
    int32_t height,width,escapement,orientation,weight;
    uint8_t italic,underline,strikeout,charset;
    uint8_t out_precision,clip_precision,quality,pitch_family;
    char face_name[32];
} logfont_a_t;
static int WIN32_API gdi_SetMapMode(void *dc UNUSED,int mode){return mode?1:0;}
static int WIN32_API gdi_GetCharWidthA(void *dc UNUSED,uint32_t first,uint32_t last,int32_t *widths){if(!widths||last<first)return 0;for(uint32_t c=first;c<=last;c++){widths[c-first]=8;if(c==0xFFFFFFFFU)break;}return 1;}
static int WIN32_API gdi_GetTextFaceA(void *dc UNUSED,int count,char *face){const char*name="System";int len=(int)kstrlen(name);if(!face||count<=0)return len+1;if(len>=count)len=count-1;kmemcpy(face,name,(size_t)len);face[len]='\0';return len;}
static void *WIN32_API gdi_CreateFontIndirectA(const logfont_a_t *font){if(!font)return NULL;return gdi_CreateFontA(font->height,font->width,font->escapement,font->orientation,font->weight,font->italic,font->underline,font->strikeout,font->charset,font->out_precision,font->clip_precision,font->quality,font->pitch_family,font->face_name);}
static int WIN32_API gdi_SetAbortProc(void *dc UNUSED,void *proc UNUSED){return 1;}
static int WIN32_API gdi_StartDocA(void *dc UNUSED,const void *doc UNUSED){return -1;}
static int WIN32_API gdi_StartPage(void *dc UNUSED){return -1;}
static int WIN32_API gdi_EndPage(void *dc UNUSED){return -1;}
static int WIN32_API gdi_EndDoc(void *dc UNUSED){return -1;}
static int WIN32_API gdi_AbortDoc(void *dc UNUSED){return -1;}

static uint32_t WIN32_API gdi_SetPixel(void *dc,int x,int y,uint32_t color){memdc_t*m=memdc_from(dc);if(m){bitmap_t*b=bitmap_from(m->selected);if(!b||x<0||y<0||x>=b->width||y>=b->height)return 0xFFFFFFFFU;b->pixels[y*b->width+x]=color&0xFFFFFFU;return color&0xFFFFFFU;}return win32_gdi_line(dc,x,y,x,y,color&0xFFFFFFU)?(color&0xFFFFFFU):0xFFFFFFFFU;}
static int WIN32_API gdi_Polyline(void *dc,const int32_t *points,int count){if(!points||count<2)return 0;for(int i=1;i<count;i++){gdi_MoveToEx(dc,points[(i-1)*2],points[(i-1)*2+1],NULL);if(!gdi_LineTo(dc,points[i*2],points[i*2+1]))return 0;}return 1;}
static int WIN32_API gdi_BitBlt(void*dst,int dx,int dy,int w,int h,void*src,int sx,int sy,uint32_t rop){if(rop!=SRCCOPY||w<=0||h<=0)return 0;memdc_t*sm=memdc_from(src);bitmap_t*sb=sm?bitmap_from(sm->selected):NULL;if(!sb)return 0;if(sx<0||sy<0||sx+w>sb->width||sy+h>sb->height)return 0;memdc_t*dm=memdc_from(dst);if(dm){bitmap_t*db=bitmap_from(dm->selected);if(!db)return 0;for(int y=0;y<h;y++)for(int x=0;x<w;x++)if(dx+x>=0&&dy+y>=0&&dx+x<db->width&&dy+y<db->height)db->pixels[(dy+y)*db->width+dx+x]=sb->pixels[(sy+y)*sb->width+sx+x];return 1;}return win32_gdi_blit(dst,dx,dy,w,h,sb->pixels,sb->width,sx,sy);}
static int WIN32_API gdi_StretchBlt(void*dst,int dx,int dy,int dw,int dh,void*src,int sx,int sy,int sw,int sh,uint32_t rop){if(rop!=SRCCOPY||dw<=0||dh<=0||sw<=0||sh<=0)return 0;memdc_t*sm=memdc_from(src);bitmap_t*sb=sm?bitmap_from(sm->selected):NULL;if(!sb)return 0;void*tmp=bitmap_alloc(dw,dh);bitmap_t*tb=bitmap_from(tmp);if(!tb)return 0;for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){int px=sx+x*sw/dw,py=sy+y*sh/dh;if(px>=0&&py>=0&&px<sb->width&&py<sb->height)tb->pixels[y*dw+x]=sb->pixels[py*sb->width+px];}memdc_t fake={true,NULL,tmp,0,0,0,0,0,0};memdc_t*slot=NULL;for(uint32_t i=0;i<MAX_MEMDCS;i++)if(!memdcs[i].used){memdcs[i]=fake;slot=&memdcs[i];src=(void*)(uintptr_t)(MEMDC_BASE+i);break;}int ok=slot?gdi_BitBlt(dst,dx,dy,dw,dh,src,0,0,rop):0;if(slot)kmemset(slot,0,sizeof(*slot));gdi_DeleteObject(tmp);return ok;}
uint32_t win32_gdi32_resolve(const char*name){
#define G(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&gdi_##api
G(GetDC);G(ReleaseDC);G(BeginPaint);G(EndPaint);G(CreateCompatibleDC);G(DeleteDC);G(CreateCompatibleBitmap);G(BitBlt);G(StretchBlt);G(SetTextColor);G(SetTextAlign);G(SetBkColor);G(SetBkMode);G(TextOutA);G(DrawTextA);G(MoveToEx);G(LineTo);G(Rectangle);G(Ellipse);G(RoundRect);G(CreatePen);G(CreateSolidBrush);G(CreateFontA);G(CreateFontIndirectA);G(SelectObject);G(DeleteObject);G(GetStockObject);G(GetTextExtentPoint32A);G(GetTextMetricsA);G(GetDeviceCaps);G(GetCharWidthA);G(GetTextFaceA);G(SetMapMode);G(SetAbortProc);G(StartDocA);G(StartPage);G(EndPage);G(EndDoc);G(AbortDoc);G(SetPixel);G(Polyline);
#undef G
return 0;}
