#include "win32.h"
#include "../include/memory.h"
#include "../include/sound.h"
#include "../include/task.h"
#include "../../gui/gui.h"
#include "../string.h"

#define S_OK 0U
#define E_POINTER 0x80004003U
#define E_NOINTERFACE 0x80004002U
#define E_NOTIMPL 0x80004001U
#define DDERR_INVALIDPARAMS 0x88760064U
#define DDERR_OUTOFMEMORY 0x8007000EU
#define DDERR_UNSUPPORTED 0x88760032U
#define DSERR_INVALIDPARAM 0x80070057U
#define DSERR_OUTOFMEMORY 0x8007000EU
#define DSERR_UNSUPPORTED 0x80004001U
#define DD_MAGIC 0x44445257U
#define DDS_MAGIC 0x44534643U
#define DDP_MAGIC 0x4450414CU
#define DS_MAGIC 0x44534E44U
#define DSB_MAGIC 0x44534246U
#define DDSD_CAPS 0x00000001U
#define DDSD_HEIGHT 0x00000002U
#define DDSD_WIDTH 0x00000004U
#define DDSD_PITCH 0x00000008U
#define DDSD_BACKBUFFERCOUNT 0x00000020U
#define DDSD_PIXELFORMAT 0x00001000U
#define DDSD_LPSURFACE 0x00000800U
#define DDSCAPS_COMPLEX 0x00000008U
#define DDSCAPS_FLIP 0x00000010U
#define DDSCAPS_BACKBUFFER 0x00000004U
#define DDSCAPS_PRIMARYSURFACE 0x00000200U
#define DDPF_PALETTEINDEXED8 0x00000020U
#define DDPF_RGB 0x00000040U
#define DDBLT_COLORFILL 0x00000400U
#define DSBCAPS_PRIMARYBUFFER 0x00000001U

typedef struct { int32_t left,top,right,bottom; } rect_t;
typedef struct { uint32_t size,flags,fourcc,rgb_bits,r_mask,g_mask,b_mask,a_mask; } pixel_format_t;
typedef struct { uint32_t caps; } caps_t;
typedef struct {
    uint32_t size,flags,height,width; int32_t pitch;
    uint32_t backbuffers,mipmaps,alpha,reserved; void *surface;
    uint32_t color_keys[8]; pixel_format_t format; caps_t caps;
} surface_desc_t;
typedef struct { uint8_t red,green,blue,flags; } palette_entry_t;
typedef struct { uint16_t tag,channels; uint32_t rate,average; uint16_t align,bits,size; } wave_format_t;
typedef struct { uint32_t size,flags,bytes,reserved; wave_format_t *format; uint32_t algorithm; } ds_buffer_desc_t;

typedef struct dd_object dd_object_t;
typedef struct dd_surface dd_surface_t;
typedef struct dd_palette dd_palette_t;
typedef struct ds_object ds_object_t;
typedef struct ds_buffer ds_buffer_t;
struct dd_object { void **vtable; uint32_t magic,refs,owner; void *window; uint32_t width,height,bpp; };
struct dd_palette { void **vtable; uint32_t magic,refs,owner; palette_entry_t entries[256]; };
struct dd_surface { void **vtable; uint32_t magic,refs,owner; dd_object_t *parent; dd_surface_t *attached; dd_palette_t *palette; uint32_t width,height,pitch,bpp,caps; uint8_t *pixels; uint32_t *present_pixels; };
struct ds_object { void **vtable; uint32_t magic,refs,owner; void *window; };
struct ds_buffer { void **vtable; uint32_t magic,refs,owner; ds_object_t *parent; wave_format_t format; uint8_t *data,*converted; uint32_t size,position,frequency; int32_t volume,pan; bool playing; };
static void *dx_objects[64];
static bool track_object(void*object){for(uint32_t i=0;i<64U;i++)if(!dx_objects[i]){dx_objects[i]=object;return true;}return false;}
static void untrack_object(void*object){for(uint32_t i=0;i<64U;i++)if(dx_objects[i]==object){dx_objects[i]=NULL;return;}}

static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint32_t bytes_per_pixel(uint32_t bpp){return bpp<=8U?1U:(bpp<=16U?2U:(bpp<=24U?3U:4U));}
static bool dd_valid(dd_object_t*o){return o&&o->magic==DD_MAGIC;}
static bool surface_valid(dd_surface_t*o){return o&&o->magic==DDS_MAGIC;}
static bool palette_valid(dd_palette_t*o){return o&&o->magic==DDP_MAGIC;}
static bool ds_valid(ds_object_t*o){return o&&o->magic==DS_MAGIC;}
static bool buffer_valid(ds_buffer_t*o){return o&&o->magic==DSB_MAGIC;}

static uint32_t WIN32_API com_query(void *object,const void*iid UNUSED,void**out){if(!object||!out)return E_POINTER;*out=object;(*(uint32_t*)((uint8_t*)object+8U))++;return S_OK;}
static uint32_t WIN32_API dd_addref(dd_object_t*o){return dd_valid(o)?++o->refs:0;}
static uint32_t WIN32_API dd_release(dd_object_t*o){uint32_t refs;if(!dd_valid(o))return 0;refs=--o->refs;if(!refs){untrack_object(o);o->magic=0;kfree(o);}return refs;}
static uint32_t WIN32_API surface_addref(dd_surface_t*s){return surface_valid(s)?++s->refs:0;}
static uint32_t WIN32_API palette_addref(dd_palette_t*p){return palette_valid(p)?++p->refs:0;}
static uint32_t WIN32_API ds_addref(ds_object_t*o){return ds_valid(o)?++o->refs:0;}
static uint32_t WIN32_API buffer_addref(ds_buffer_t*b){return buffer_valid(b)?++b->refs:0;}

static void surface_destroy(dd_surface_t*s){if(!surface_valid(s))return;untrack_object(s);if(s->attached){s->attached->attached=NULL;if(--s->attached->refs==0)surface_destroy(s->attached);}if(s->pixels)kfree(s->pixels);if(s->present_pixels)kfree(s->present_pixels);s->magic=0;kfree(s);}
static uint32_t WIN32_API surface_release(dd_surface_t*s){uint32_t refs;if(!surface_valid(s))return 0;refs=--s->refs;if(!refs)surface_destroy(s);return refs;}
static uint32_t WIN32_API palette_release(dd_palette_t*p){uint32_t refs;if(!palette_valid(p))return 0;refs=--p->refs;if(!refs){untrack_object(p);p->magic=0;kfree(p);}return refs;}
static uint32_t WIN32_API buffer_release(ds_buffer_t*b){uint32_t refs;if(!buffer_valid(b))return 0;refs=--b->refs;if(!refs){untrack_object(b);if(b->playing)sound_stop();if(b->data)kfree(b->data);if(b->converted)kfree(b->converted);b->magic=0;kfree(b);}return refs;}
static uint32_t WIN32_API ds_release(ds_object_t*o){uint32_t refs;if(!ds_valid(o))return 0;refs=--o->refs;if(!refs){untrack_object(o);o->magic=0;kfree(o);}return refs;}

static uint32_t surface_color(dd_surface_t*s,uint32_t x,uint32_t y){uint8_t*p=s->pixels+y*s->pitch+x*bytes_per_pixel(s->bpp);if(s->bpp<=8U){palette_entry_t e=s->palette?s->palette->entries[*p]:(palette_entry_t){*p,*p,*p,0};return((uint32_t)e.red<<16)|((uint32_t)e.green<<8)|e.blue;}if(s->bpp<=16U){uint16_t v=*(uint16_t*)p;return((uint32_t)((v>>11)&31U)*255U/31U<<16)|((uint32_t)((v>>5)&63U)*255U/63U<<8)|((uint32_t)(v&31U)*255U/31U);}if(s->bpp<=24U)return((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0];return*(uint32_t*)p&0xFFFFFFU;}
static uint32_t surface_present(dd_surface_t*s){if(!surface_valid(s)||!s->parent||!s->parent->window)return DDERR_INVALIDPARAMS;if(!s->present_pixels){s->present_pixels=(uint32_t*)kmalloc(s->width*s->height*4U);if(!s->present_pixels)return DDERR_OUTOFMEMORY;}for(uint32_t y=0;y<s->height;y++)for(uint32_t x=0;x<s->width;x++)s->present_pixels[y*s->width+x]=surface_color(s,x,y);return win32_directdraw_blit(s->parent->window,(int)s->width,(int)s->height,s->present_pixels)?S_OK:DDERR_UNSUPPORTED;}
static void fill_format(pixel_format_t*f,uint32_t bpp){kmemset(f,0,sizeof(*f));f->size=sizeof(*f);f->rgb_bits=bpp;if(bpp<=8U)f->flags=DDPF_RGB|DDPF_PALETTEINDEXED8;else{f->flags=DDPF_RGB;if(bpp==16U){f->r_mask=0xF800U;f->g_mask=0x07E0U;f->b_mask=0x001FU;}else{f->r_mask=0x00FF0000U;f->g_mask=0x0000FF00U;f->b_mask=0x000000FFU;}}}
static void fill_desc(dd_surface_t*s,surface_desc_t*d){uint32_t requested=d->size;kmemset(d,0,requested<sizeof(*d)?requested:sizeof(*d));d->size=requested;d->flags=DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PITCH|DDSD_PIXELFORMAT|DDSD_LPSURFACE;d->height=s->height;d->width=s->width;d->pitch=(int32_t)s->pitch;d->surface=s->pixels;d->caps.caps=s->caps;fill_format(&d->format,s->bpp);}

static void *surface_vtable[];
static void *palette_vtable[];
static dd_surface_t*surface_create(dd_object_t*parent,uint32_t width,uint32_t height,uint32_t bpp,uint32_t caps){dd_surface_t*s=(dd_surface_t*)kmalloc(sizeof(*s));uint32_t bytes;if(!s||!width||!height){if(s)kfree(s);return NULL;}kmemset(s,0,sizeof(*s));s->vtable=surface_vtable;s->magic=DDS_MAGIC;s->refs=1;s->owner=task_current_process_id();s->parent=parent;s->width=width;s->height=height;s->bpp=bpp;s->caps=caps;s->pitch=width*bytes_per_pixel(bpp);bytes=s->pitch*height;s->pixels=(uint8_t*)kmalloc(bytes);if(!s->pixels||!track_object(s)){if(s->pixels)kfree(s->pixels);kfree(s);return NULL;}kmemset(s->pixels,0,bytes);return s;}
static uint32_t WIN32_API dd_compact(dd_object_t*o UNUSED){return S_OK;}
static uint32_t WIN32_API dd_create_clipper(dd_object_t*o UNUSED,uint32_t flags UNUSED,void**out,void*outer UNUSED){if(out)*out=NULL;return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API dd_create_palette(dd_object_t*o,uint32_t flags UNUSED,const palette_entry_t*entries,dd_palette_t**out,void*outer UNUSED){dd_palette_t*p;if(!dd_valid(o)||!out)return DDERR_INVALIDPARAMS;p=(dd_palette_t*)kmalloc(sizeof(*p));if(!p)return DDERR_OUTOFMEMORY;kmemset(p,0,sizeof(*p));p->vtable=palette_vtable;p->magic=DDP_MAGIC;p->refs=1;p->owner=o->owner;if(!track_object(p)){kfree(p);return DDERR_OUTOFMEMORY;}if(entries)kmemcpy(p->entries,entries,sizeof(p->entries));*out=p;return S_OK;}
static uint32_t WIN32_API dd_create_surface(dd_object_t*o,surface_desc_t*d,dd_surface_t**out,void*outer UNUSED){uint32_t w,h,bpp,caps;dd_surface_t*s;if(!dd_valid(o)||!d||!out)return DDERR_INVALIDPARAMS;caps=d->caps.caps;w=(d->flags&DDSD_WIDTH)?d->width:o->width;h=(d->flags&DDSD_HEIGHT)?d->height:o->height;bpp=(d->flags&DDSD_PIXELFORMAT)&&d->format.rgb_bits?d->format.rgb_bits:o->bpp;s=surface_create(o,w,h,bpp,caps);if(!s)return DDERR_OUTOFMEMORY;if((caps&(DDSCAPS_COMPLEX|DDSCAPS_FLIP))&&d->backbuffers){s->attached=surface_create(o,w,h,bpp,DDSCAPS_BACKBUFFER);if(!s->attached){surface_destroy(s);return DDERR_OUTOFMEMORY;}}*out=s;return S_OK;}
static uint32_t WIN32_API dd_duplicate(dd_object_t*o,dd_surface_t*source,dd_surface_t**out){dd_surface_t*s;if(!dd_valid(o)||!surface_valid(source)||!out)return DDERR_INVALIDPARAMS;s=surface_create(o,source->width,source->height,source->bpp,source->caps);if(!s)return DDERR_OUTOFMEMORY;kmemcpy(s->pixels,source->pixels,source->pitch*source->height);*out=s;return S_OK;}
static uint32_t WIN32_API dd_enum_modes(dd_object_t*o UNUSED,uint32_t flags UNUSED,surface_desc_t*filter UNUSED,void*context UNUSED,void*callback UNUSED){return S_OK;}
static uint32_t WIN32_API dd_enum_surfaces(dd_object_t*o UNUSED,uint32_t flags UNUSED,surface_desc_t*filter UNUSED,void*context UNUSED,void*callback UNUSED){return S_OK;}
static uint32_t WIN32_API dd_ok0(dd_object_t*o UNUSED){return S_OK;}
static uint32_t WIN32_API dd_get_caps(dd_object_t*o UNUSED,void*driver,void*hel){if(driver)kmemset(driver,0,*(uint32_t*)driver);if(hel)kmemset(hel,0,*(uint32_t*)hel);return S_OK;}
static uint32_t WIN32_API dd_get_display(dd_object_t*o,surface_desc_t*d){if(!dd_valid(o)||!d)return DDERR_INVALIDPARAMS;dd_surface_t fake;kmemset(&fake,0,sizeof(fake));fake.width=o->width;fake.height=o->height;fake.pitch=o->width*bytes_per_pixel(o->bpp);fake.bpp=o->bpp;fake.caps=DDSCAPS_PRIMARYSURFACE;fill_desc(&fake,d);return S_OK;}
static uint32_t WIN32_API dd_get_fourcc(dd_object_t*o UNUSED,uint32_t*count,uint32_t*codes UNUSED){if(count)*count=0;return S_OK;}
static uint32_t WIN32_API dd_get_gdi(dd_object_t*o UNUSED,dd_surface_t**out){if(out)*out=NULL;return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API dd_get_frequency(dd_object_t*o UNUSED,uint32_t*out){if(out)*out=60;return out?S_OK:DDERR_INVALIDPARAMS;}
static uint32_t WIN32_API dd_get_scanline(dd_object_t*o UNUSED,uint32_t*out){if(out)*out=0;return out?S_OK:DDERR_INVALIDPARAMS;}
static uint32_t WIN32_API dd_get_vblank(dd_object_t*o UNUSED,int*out){if(out)*out=0;return out?S_OK:DDERR_INVALIDPARAMS;}
static uint32_t WIN32_API dd_initialize(dd_object_t*o UNUSED,const void*guid UNUSED){return S_OK;}
static uint32_t WIN32_API dd_set_cooperative(dd_object_t*o,void*window,uint32_t flags UNUSED){if(!dd_valid(o))return DDERR_INVALIDPARAMS;o->window=window;return S_OK;}
static uint32_t WIN32_API dd_set_mode(dd_object_t*o,uint32_t w,uint32_t h,uint32_t bpp){if(!dd_valid(o)||!w||!h||(bpp!=8U&&bpp!=16U&&bpp!=24U&&bpp!=32U))return DDERR_INVALIDPARAMS;o->width=w;o->height=h;o->bpp=bpp;return S_OK;}
static uint32_t WIN32_API dd_set_mode7(dd_object_t*o,uint32_t w,uint32_t h,uint32_t bpp,uint32_t refresh UNUSED,uint32_t flags UNUSED){return dd_set_mode(o,w,h,bpp);}
static uint32_t WIN32_API dd_wait_vblank(dd_object_t*o UNUSED,uint32_t flags UNUSED,void*event UNUSED){task_yield();return S_OK;}
static void *dd_vtable[]={com_query,dd_addref,dd_release,dd_compact,dd_create_clipper,dd_create_palette,dd_create_surface,dd_duplicate,dd_enum_modes,dd_enum_surfaces,dd_ok0,dd_get_caps,dd_get_display,dd_get_fourcc,dd_get_gdi,dd_get_frequency,dd_get_scanline,dd_get_vblank,dd_initialize,dd_ok0,dd_set_cooperative,dd_set_mode,dd_wait_vblank};
static uint32_t WIN32_API dd_available(dd_object_t*o UNUSED,const void*caps UNUSED,uint32_t*total,uint32_t*free_bytes){if(total)*total=8U*1024U*1024U;if(free_bytes)*free_bytes=6U*1024U*1024U;return S_OK;}
static uint32_t WIN32_API dd_surface_from_dc(dd_object_t*o UNUSED,void*dc UNUSED,dd_surface_t**out){if(out)*out=NULL;return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API dd_device_identifier(dd_object_t*o UNUSED,void*identifier,uint32_t flags UNUSED){if(!identifier)return DDERR_INVALIDPARAMS;kmemset(identifier,0,1068U);kstrcpy((char*)identifier,"BlesKernOS DirectDraw");kstrcpy((char*)identifier+512,"display");return S_OK;}
static uint32_t WIN32_API dd_start_mode_test(dd_object_t*o UNUSED,const void*modes UNUSED,uint32_t count UNUSED,uint32_t flags UNUSED){return S_OK;}
static uint32_t WIN32_API dd_evaluate_mode(dd_object_t*o UNUSED,uint32_t flags UNUSED,uint32_t*timeout){if(timeout)*timeout=0;return S_OK;}
static void *dd_vtable7[]={com_query,dd_addref,dd_release,dd_compact,dd_create_clipper,dd_create_palette,dd_create_surface,dd_duplicate,dd_enum_modes,dd_enum_surfaces,dd_ok0,dd_get_caps,dd_get_display,dd_get_fourcc,dd_get_gdi,dd_get_frequency,dd_get_scanline,dd_get_vblank,dd_initialize,dd_ok0,dd_set_cooperative,dd_set_mode7,dd_wait_vblank,dd_available,dd_surface_from_dc,dd_ok0,dd_ok0,dd_device_identifier,dd_start_mode_test,dd_evaluate_mode};

static uint32_t WIN32_API surface_add_attached(dd_surface_t*s,dd_surface_t*a){if(!surface_valid(s)||!surface_valid(a))return DDERR_INVALIDPARAMS;s->attached=a;a->refs++;return S_OK;}
static uint32_t WIN32_API surface_overlay_rect(dd_surface_t*s UNUSED,const rect_t*r UNUSED){return S_OK;}
static uint32_t WIN32_API surface_blt(dd_surface_t*d,const rect_t*dr,dd_surface_t*s,const rect_t*sr,uint32_t flags,const void*fx){uint32_t dl=0,dt=0,dw,dh,sl=0,st=0,sw,sh;if(!surface_valid(d))return DDERR_INVALIDPARAMS;dw=d->width;dh=d->height;if(dr){dl=dr->left;dt=dr->top;dw=dr->right-dr->left;dh=dr->bottom-dr->top;}if(flags&DDBLT_COLORFILL){uint32_t color=fx?*((const uint32_t*)fx+20):0;for(uint32_t y=0;y<dh&&dt+y<d->height;y++)for(uint32_t x=0;x<dw&&dl+x<d->width;x++){uint8_t*p=d->pixels+(dt+y)*d->pitch+(dl+x)*bytes_per_pixel(d->bpp);if(d->bpp<=8)*p=(uint8_t)color;else if(d->bpp<=16)*(uint16_t*)p=(uint16_t)color;else if(d->bpp<=24){p[0]=(uint8_t)color;p[1]=(uint8_t)(color>>8);p[2]=(uint8_t)(color>>16);}else*(uint32_t*)p=color;}return S_OK;}if(!surface_valid(s)||s->bpp!=d->bpp)return DDERR_UNSUPPORTED;sw=s->width;sh=s->height;if(sr){sl=sr->left;st=sr->top;sw=sr->right-sr->left;sh=sr->bottom-sr->top;}for(uint32_t y=0;y<dh&&dt+y<d->height;y++)for(uint32_t x=0;x<dw&&dl+x<d->width;x++){uint32_t sx=sl+x*sw/dw,sy=st+y*sh/dh;if(sx<s->width&&sy<s->height)kmemcpy(d->pixels+(dt+y)*d->pitch+(dl+x)*bytes_per_pixel(d->bpp),s->pixels+sy*s->pitch+sx*bytes_per_pixel(s->bpp),bytes_per_pixel(d->bpp));}if(d->caps&DDSCAPS_PRIMARYSURFACE)return surface_present(d);return S_OK;}
static uint32_t WIN32_API surface_blt_batch(dd_surface_t*s UNUSED,void*batch UNUSED,uint32_t count UNUSED,uint32_t flags UNUSED){return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API surface_blt_fast(dd_surface_t*d,uint32_t x,uint32_t y,dd_surface_t*s,const rect_t*r,uint32_t flags UNUSED){rect_t dr={(int32_t)x,(int32_t)y,(int32_t)(x+(r?(r->right-r->left):s->width)),(int32_t)(y+(r?(r->bottom-r->top):s->height))};return surface_blt(d,&dr,s,r,0,NULL);}
static uint32_t WIN32_API surface_delete_attached(dd_surface_t*s,uint32_t flags UNUSED,dd_surface_t*a){if(!surface_valid(s)||s->attached!=a)return DDERR_INVALIDPARAMS;s->attached=NULL;surface_release(a);return S_OK;}
static uint32_t WIN32_API surface_enum_attached(dd_surface_t*s,void*context,void*callback){if(surface_valid(s)&&s->attached&&callback){surface_desc_t d;d.size=sizeof(d);fill_desc(s->attached,&d);((uint32_t(WIN32_API*)(dd_surface_t*,surface_desc_t*,void*))callback)(s->attached,&d,context);}return S_OK;}
static uint32_t WIN32_API surface_enum_overlay(dd_surface_t*s UNUSED,uint32_t flags UNUSED,void*context UNUSED,void*callback UNUSED){return S_OK;}
static uint32_t WIN32_API surface_flip(dd_surface_t*s,dd_surface_t*target UNUSED,uint32_t flags UNUSED){uint8_t*swap;if(!surface_valid(s))return DDERR_INVALIDPARAMS;if(s->attached){swap=s->pixels;s->pixels=s->attached->pixels;s->attached->pixels=swap;}return surface_present(s);}
static uint32_t WIN32_API surface_get_attached(dd_surface_t*s,const caps_t*caps UNUSED,dd_surface_t**out){if(!surface_valid(s)||!out||!s->attached)return DDERR_INVALIDPARAMS;*out=s->attached;surface_addref(*out);return S_OK;}
static uint32_t WIN32_API surface_status(dd_surface_t*s UNUSED,uint32_t flags UNUSED){return S_OK;}
static uint32_t WIN32_API surface_get_caps(dd_surface_t*s,caps_t*c){if(!surface_valid(s)||!c)return DDERR_INVALIDPARAMS;c->caps=s->caps;return S_OK;}
static uint32_t WIN32_API surface_get_ptr(dd_surface_t*s,void**out){if(!surface_valid(s)||!out)return DDERR_INVALIDPARAMS;*out=NULL;return S_OK;}
static uint32_t WIN32_API surface_get_color(dd_surface_t*s,uint32_t flags UNUSED,void*out UNUSED){return surface_valid(s)?DDERR_UNSUPPORTED:DDERR_INVALIDPARAMS;}
static uint32_t WIN32_API surface_get_dc(dd_surface_t*s UNUSED,void**dc){if(dc)*dc=NULL;return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API surface_get_position(dd_surface_t*s UNUSED,int32_t*x,int32_t*y){if(x)*x=0;if(y)*y=0;return S_OK;}
static uint32_t WIN32_API surface_get_palette(dd_surface_t*s,dd_palette_t**out){if(!surface_valid(s)||!out||!s->palette)return DDERR_INVALIDPARAMS;*out=s->palette;palette_addref(*out);return S_OK;}
static uint32_t WIN32_API surface_get_format(dd_surface_t*s,pixel_format_t*f){if(!surface_valid(s)||!f)return DDERR_INVALIDPARAMS;fill_format(f,s->bpp);return S_OK;}
static uint32_t WIN32_API surface_get_desc(dd_surface_t*s,surface_desc_t*d){if(!surface_valid(s)||!d)return DDERR_INVALIDPARAMS;fill_desc(s,d);return S_OK;}
static uint32_t WIN32_API surface_initialize(dd_surface_t*s UNUSED,dd_object_t*d UNUSED,surface_desc_t*desc UNUSED){return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API surface_lock(dd_surface_t*s,const rect_t*r,surface_desc_t*d,uint32_t flags UNUSED,void*event UNUSED){if(!surface_valid(s)||!d)return DDERR_INVALIDPARAMS;fill_desc(s,d);if(r){d->width=r->right-r->left;d->height=r->bottom-r->top;d->surface=s->pixels+r->top*s->pitch+r->left*bytes_per_pixel(s->bpp);}return S_OK;}
static uint32_t WIN32_API surface_release_dc(dd_surface_t*s UNUSED,void*dc UNUSED){return S_OK;}
static uint32_t WIN32_API surface_restore(dd_surface_t*s UNUSED){return S_OK;}
static uint32_t WIN32_API surface_set_ptr(dd_surface_t*s UNUSED,void*p UNUSED){return S_OK;}
static uint32_t WIN32_API surface_set_color(dd_surface_t*s UNUSED,uint32_t flags UNUSED,const void*key UNUSED){return S_OK;}
static uint32_t WIN32_API surface_set_position(dd_surface_t*s UNUSED,int32_t x UNUSED,int32_t y UNUSED){return S_OK;}
static uint32_t WIN32_API surface_set_palette(dd_surface_t*s,dd_palette_t*p){if(!surface_valid(s)||!palette_valid(p))return DDERR_INVALIDPARAMS;s->palette=p;return S_OK;}
static uint32_t WIN32_API surface_unlock(dd_surface_t*s,void*pointer UNUSED){if(!surface_valid(s))return DDERR_INVALIDPARAMS;return(s->caps&DDSCAPS_PRIMARYSURFACE)?surface_present(s):S_OK;}
static uint32_t WIN32_API surface_update5(dd_surface_t*s UNUSED,const rect_t*a UNUSED,dd_surface_t*b UNUSED,const rect_t*c UNUSED,uint32_t flags UNUSED,void*fx UNUSED){return DDERR_UNSUPPORTED;}
static uint32_t WIN32_API surface_update2(dd_surface_t*s UNUSED,uint32_t flags UNUSED){return S_OK;}
static uint32_t WIN32_API surface_update3(dd_surface_t*s UNUSED,uint32_t flags UNUSED,dd_surface_t*reference UNUSED){return S_OK;}
static void *surface_vtable[]={com_query,surface_addref,surface_release,surface_add_attached,surface_overlay_rect,surface_blt,surface_blt_batch,surface_blt_fast,surface_delete_attached,surface_enum_attached,surface_enum_overlay,surface_flip,surface_get_attached,surface_status,surface_get_caps,surface_get_ptr,surface_get_color,surface_get_dc,surface_status,surface_get_position,surface_get_palette,surface_get_format,surface_get_desc,surface_initialize,surface_status,surface_lock,surface_release_dc,surface_restore,surface_set_ptr,surface_set_color,surface_set_position,surface_set_palette,surface_unlock,surface_update5,surface_update2,surface_update3};
static uint32_t WIN32_API palette_get_caps(dd_palette_t*p,uint32_t*caps){if(!palette_valid(p)||!caps)return DDERR_INVALIDPARAMS;*caps=0;return S_OK;}
static uint32_t WIN32_API palette_get_entries(dd_palette_t*p,uint32_t flags UNUSED,uint32_t start,uint32_t count,palette_entry_t*out){if(!palette_valid(p)||!out||start+count>256U)return DDERR_INVALIDPARAMS;kmemcpy(out,p->entries+start,count*sizeof(*out));return S_OK;}
static uint32_t WIN32_API palette_initialize(dd_palette_t*p UNUSED,dd_object_t*d UNUSED,uint32_t flags UNUSED,palette_entry_t*e UNUSED){return S_OK;}
static uint32_t WIN32_API palette_set_entries(dd_palette_t*p,uint32_t flags UNUSED,uint32_t start,uint32_t count,const palette_entry_t*in){if(!palette_valid(p)||!in||start+count>256U)return DDERR_INVALIDPARAMS;kmemcpy(p->entries+start,in,count*sizeof(*in));return S_OK;}
static void *palette_vtable[]={com_query,palette_addref,palette_release,palette_get_caps,palette_get_entries,palette_initialize,palette_set_entries};

static uint32_t WIN32_API dx_DirectDrawCreate(const void*guid UNUSED,void**object,void*outer UNUSED){dd_object_t*o;gui_desktop_t*d;if(!object)return E_POINTER;o=(dd_object_t*)kmalloc(sizeof(*o));if(!o)return DDERR_OUTOFMEMORY;d=gui_get_desktop();o->vtable=dd_vtable;o->magic=DD_MAGIC;o->refs=1;o->owner=task_current_process_id();o->window=NULL;o->width=d?(uint32_t)d->surface.width:640U;o->height=d?(uint32_t)d->surface.height:480U;o->bpp=32U;if(!track_object(o)){kfree(o);return DDERR_OUTOFMEMORY;}*object=o;return S_OK;}
static uint32_t WIN32_API dx_DirectDrawCreateEx(const void*guid,void**object,const void*iid UNUSED,void*outer){uint32_t result=dx_DirectDrawCreate(guid,object,outer);if(result==S_OK&&object&&*object)((dd_object_t*)*object)->vtable=dd_vtable7;return result;}
typedef int (WIN32_API *enum_a_t)(const void*,const char*,const char*,void*);
static uint32_t WIN32_API dx_DirectDrawEnumerateA(enum_a_t callback,void*context){if(!callback)return E_POINTER;callback(NULL,"BlesKernOS display","display",context);return S_OK;}
static uint32_t WIN32_API dx_DirectDrawEnumerateExA(enum_a_t callback,void*context,uint32_t flags UNUSED){return dx_DirectDrawEnumerateA(callback,context);}

static void *buffer_vtable[];
static uint32_t WIN32_API ds_create_buffer(ds_object_t*o,const ds_buffer_desc_t*d,ds_buffer_t**out,void*outer UNUSED){ds_buffer_t*b;if(!ds_valid(o)||!d||!out)return DSERR_INVALIDPARAM;b=(ds_buffer_t*)kmalloc(sizeof(*b));if(!b)return DSERR_OUTOFMEMORY;kmemset(b,0,sizeof(*b));b->vtable=buffer_vtable;b->magic=DSB_MAGIC;b->refs=1;b->owner=o->owner;b->parent=o;b->volume=0;b->frequency=11025U;if(d->format)b->format=*d->format;else{b->format.tag=1;b->format.channels=1;b->format.rate=11025;b->format.bits=8;b->format.align=1;}b->frequency=b->format.rate;b->size=(d->flags&DSBCAPS_PRIMARYBUFFER)&&!d->bytes?65536U:d->bytes;if(!b->size)b->size=4096U;b->data=(uint8_t*)kmalloc(b->size);if(!b->data||!track_object(b)){if(b->data)kfree(b->data);kfree(b);return DSERR_OUTOFMEMORY;}kmemset(b->data,b->format.bits==8?128:0,b->size);*out=b;return S_OK;}
static uint32_t WIN32_API ds_get_caps(ds_object_t*o UNUSED,void*caps){if(!caps)return DSERR_INVALIDPARAM;kmemset(caps,0,*(uint32_t*)caps);return S_OK;}
static uint32_t WIN32_API ds_duplicate(ds_object_t*o,ds_buffer_t*source,ds_buffer_t**out){ds_buffer_desc_t d;if(!buffer_valid(source))return DSERR_INVALIDPARAM;d.size=sizeof(d);d.flags=0;d.bytes=source->size;d.format=&source->format;d.reserved=d.algorithm=0;if(ds_create_buffer(o,&d,out,NULL)!=S_OK)return DSERR_OUTOFMEMORY;kmemcpy((*out)->data,source->data,source->size);return S_OK;}
static uint32_t WIN32_API ds_set_cooperative(ds_object_t*o,void*window,uint32_t level UNUSED){if(!ds_valid(o))return DSERR_INVALIDPARAM;o->window=window;return S_OK;}
static uint32_t WIN32_API ds_get_speaker(ds_object_t*o UNUSED,uint32_t*config){if(config)*config=4U;return config?S_OK:DSERR_INVALIDPARAM;}
static uint32_t WIN32_API ds_set_speaker(ds_object_t*o UNUSED,uint32_t config UNUSED){return S_OK;}
static uint32_t WIN32_API ds_initialize(ds_object_t*o UNUSED,const void*guid UNUSED){return S_OK;}
static uint32_t WIN32_API ds_verify(ds_object_t*o UNUSED,uint32_t*certified){if(certified)*certified=1U;return certified?S_OK:DSERR_INVALIDPARAM;}
static void *ds_vtable[]={com_query,ds_addref,ds_release,ds_create_buffer,ds_get_caps,ds_duplicate,ds_set_cooperative,dd_compact,ds_get_speaker,ds_set_speaker,ds_initialize,ds_verify};
static uint32_t WIN32_API buffer_get_caps(ds_buffer_t*b,void*caps){if(!buffer_valid(b)||!caps)return DSERR_INVALIDPARAM;kmemset(caps,0,*(uint32_t*)caps);*((uint32_t*)caps+2)=b->size;return S_OK;}
static uint32_t WIN32_API buffer_get_position(ds_buffer_t*b,uint32_t*play,uint32_t*write){if(!buffer_valid(b))return DSERR_INVALIDPARAM;if(play)*play=b->position;if(write)*write=b->position;return S_OK;}
static uint32_t WIN32_API buffer_get_format(ds_buffer_t*b,wave_format_t*f,uint32_t size,uint32_t*written){if(!buffer_valid(b))return DSERR_INVALIDPARAM;if(written)*written=sizeof(b->format);if(f&&size>=sizeof(b->format))kmemcpy(f,&b->format,sizeof(b->format));return S_OK;}
static uint32_t WIN32_API buffer_get_volume(ds_buffer_t*b,int32_t*out){if(!buffer_valid(b)||!out)return DSERR_INVALIDPARAM;*out=b->volume;return S_OK;}
static uint32_t WIN32_API buffer_get_pan(ds_buffer_t*b,int32_t*out){if(!buffer_valid(b)||!out)return DSERR_INVALIDPARAM;*out=b->pan;return S_OK;}
static uint32_t WIN32_API buffer_get_frequency(ds_buffer_t*b,uint32_t*out){if(!buffer_valid(b)||!out)return DSERR_INVALIDPARAM;*out=b->frequency;return S_OK;}
static uint32_t WIN32_API buffer_get_status(ds_buffer_t*b,uint32_t*out){if(!buffer_valid(b)||!out)return DSERR_INVALIDPARAM;b->playing=sound_pcm_is_busy();*out=b->playing?1U:0U;return S_OK;}
static uint32_t WIN32_API buffer_initialize(ds_buffer_t*b UNUSED,ds_object_t*d UNUSED,const ds_buffer_desc_t*desc UNUSED){return DSERR_UNSUPPORTED;}
static uint32_t WIN32_API buffer_lock(ds_buffer_t*b,uint32_t offset,uint32_t bytes,void**p1,uint32_t*n1,void**p2,uint32_t*n2,uint32_t flags UNUSED){if(!buffer_valid(b)||!p1||!n1||offset>=b->size)return DSERR_INVALIDPARAM;if(!bytes||bytes>b->size)bytes=b->size;if(offset+bytes<=b->size){*p1=b->data+offset;*n1=bytes;if(p2)*p2=NULL;if(n2)*n2=0;}else{*p1=b->data+offset;*n1=b->size-offset;if(p2)*p2=b->data;if(n2)*n2=bytes-*n1;}return S_OK;}
static uint32_t buffer_convert(ds_buffer_t*b){uint32_t frames,channels=b->format.channels?b->format.channels:1U,bits=b->format.bits?b->format.bits:8U,frame_bytes=channels*(bits/8U),i;if(b->format.tag!=1U||(bits!=8U&&bits!=16U)||!frame_bytes)return 0;frames=b->size/frame_bytes;if(b->converted){if(b->playing)sound_stop();kfree(b->converted);}b->converted=(uint8_t*)kmalloc(frames);if(!b->converted)return 0;for(i=0;i<frames;i++){int sample=0;for(uint32_t c=0;c<channels;c++){if(bits==8U)sample+=b->data[i*frame_bytes+c];else sample+=((int16_t*)(b->data+i*frame_bytes))[c]/256+128;}sample/=(int)channels;if(sample<0)sample=0;if(sample>255)sample=255;b->converted[i]=(uint8_t)sample;}return frames;}
static uint32_t WIN32_API buffer_play(ds_buffer_t*b,uint32_t reserved1 UNUSED,uint32_t priority UNUSED,uint32_t flags UNUSED){uint32_t frames;uint8_t volume;if(!buffer_valid(b))return DSERR_INVALIDPARAM;frames=buffer_convert(b);if(!frames)return DSERR_UNSUPPORTED;volume=b->volume<=-10000?0U:(uint8_t)((b->volume+10000)*255/10000);if(!sound_play_pcm_u8(b->converted,frames,(uint16_t)(b->frequency?b->frequency:b->format.rate),volume))return DSERR_UNSUPPORTED;b->playing=true;return S_OK;}
static uint32_t WIN32_API buffer_set_position(ds_buffer_t*b,uint32_t position){if(!buffer_valid(b)||position>=b->size)return DSERR_INVALIDPARAM;b->position=position;return S_OK;}
static uint32_t WIN32_API buffer_set_format(ds_buffer_t*b,const wave_format_t*f){if(!buffer_valid(b)||!f||f->tag!=1U)return DSERR_INVALIDPARAM;b->format=*f;b->frequency=f->rate;return S_OK;}
static uint32_t WIN32_API buffer_set_volume(ds_buffer_t*b,int32_t v){if(!buffer_valid(b))return DSERR_INVALIDPARAM;b->volume=v<-10000?-10000:(v>0?0:v);return S_OK;}
static uint32_t WIN32_API buffer_set_pan(ds_buffer_t*b,int32_t p){if(!buffer_valid(b))return DSERR_INVALIDPARAM;b->pan=p;return S_OK;}
static uint32_t WIN32_API buffer_set_frequency(ds_buffer_t*b,uint32_t f){if(!buffer_valid(b)||f<4000U||f>44100U)return DSERR_INVALIDPARAM;b->frequency=f;return S_OK;}
static uint32_t WIN32_API buffer_stop(ds_buffer_t*b){if(!buffer_valid(b))return DSERR_INVALIDPARAM;sound_stop();b->playing=false;return S_OK;}
static uint32_t WIN32_API buffer_unlock(ds_buffer_t*b,void*p1 UNUSED,uint32_t n1 UNUSED,void*p2 UNUSED,uint32_t n2 UNUSED){return buffer_valid(b)?S_OK:DSERR_INVALIDPARAM;}
static uint32_t WIN32_API buffer_restore(ds_buffer_t*b UNUSED){return S_OK;}
static void *buffer_vtable[]={com_query,buffer_addref,buffer_release,buffer_get_caps,buffer_get_position,buffer_get_format,buffer_get_volume,buffer_get_pan,buffer_get_frequency,buffer_get_status,buffer_initialize,buffer_lock,buffer_play,buffer_set_position,buffer_set_format,buffer_set_volume,buffer_set_pan,buffer_set_frequency,buffer_stop,buffer_unlock,buffer_restore};
static uint32_t WIN32_API dx_DirectSoundCreate(const void*guid UNUSED,void**object,void*outer UNUSED){ds_object_t*o;if(!object)return E_POINTER;o=(ds_object_t*)kmalloc(sizeof(*o));if(!o)return DSERR_OUTOFMEMORY;o->vtable=ds_vtable;o->magic=DS_MAGIC;o->refs=1;o->owner=task_current_process_id();o->window=NULL;if(!track_object(o)){kfree(o);return DSERR_OUTOFMEMORY;}*object=o;return S_OK;}
static uint32_t WIN32_API dx_DirectSoundCreate8(const void*guid,void**object,void*outer){return dx_DirectSoundCreate(guid,object,outer);}
static uint32_t WIN32_API dx_DirectSoundEnumerateA(enum_a_t callback,void*context){if(!callback)return E_POINTER;callback(NULL,sound_pcm_name(),"Primary Sound Driver",context);return S_OK;}
static uint32_t WIN32_API dx_DirectInputCreateA(void*instance UNUSED,uint32_t version UNUSED,void**object,void*outer UNUSED){if(object)*object=NULL;return E_NOTIMPL;}
static uint32_t WIN32_API dx_DirectInputCreateEx(void*instance UNUSED,uint32_t version UNUSED,const void*iid UNUSED,void**object,void*outer UNUSED){if(object)*object=NULL;return E_NOTIMPL;}

void win32_dx_cleanup_process(uint32_t owner){for(uint32_t i=0;i<64U;i++){void*object=dx_objects[i];uint32_t magic;if(!object||*(uint32_t*)((uint8_t*)object+12U)!=owner)continue;magic=*(uint32_t*)((uint8_t*)object+4U);dx_objects[i]=NULL;if(magic==DDS_MAGIC){dd_surface_t*s=object;if(s->pixels)kfree(s->pixels);if(s->present_pixels)kfree(s->present_pixels);s->magic=0;kfree(s);}else if(magic==DSB_MAGIC){ds_buffer_t*b=object;if(b->playing)sound_stop();if(b->data)kfree(b->data);if(b->converted)kfree(b->converted);b->magic=0;kfree(b);}else{*(uint32_t*)((uint8_t*)object+4U)=0;kfree(object);}}}
uint32_t win32_ddraw_resolve(const char*name){
#define D(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&dx_##api
 D(DirectDrawCreate);D(DirectDrawCreateEx);D(DirectDrawEnumerateA);D(DirectDrawEnumerateExA);
#undef D
 return 0;
}
uint32_t win32_dsound_resolve(const char*name){
#define D(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&dx_##api
 D(DirectSoundCreate);D(DirectSoundCreate8);D(DirectSoundEnumerateA);
#undef D
 return 0;
}
uint32_t win32_dinput_resolve(const char*name){
#define D(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&dx_##api
 D(DirectInputCreateA);D(DirectInputCreateEx);
#undef D
 return 0;
}
