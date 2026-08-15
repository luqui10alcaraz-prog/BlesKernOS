/* Mesa 3.5 TNL -> BlesKernOS GFX3D bridge.
 *
 * Mesa keeps the OpenGL state machine, transforms, lighting, clipping and
 * primitive assembly.  This module replaces only the final rasterization
 * callbacks when the current state can be represented by GFX3D.  Unsupported
 * state falls back to the original swrast path.
 */

#include "glheader.h"
#include "context.h"
#include "colormac.h"
#include "macros.h"
#include "mem.h"
#include "mtypes.h"
#include "GL/osmesa.h"
#include "swrast/swrast.h"
#include "swrast_setup/swrast_setup.h"
#include "swrast_setup/ss_context.h"
#include "tnl/tnl.h"
#include "tnl/t_context.h"
#include "bkmesa_gpu.h"
#include <math.h>

#if defined(MESA_BLESKERNOS)

typedef unsigned int bk_u32;
typedef signed int bk_s32;
typedef unsigned short bk_u16;
typedef unsigned char bk_bool;
typedef bk_u32 bk_surface;

typedef struct {
   bk_s32 x, y, w, h;
} bk_rect;

typedef struct {
   bk_u16 width;
   bk_u16 height;
   bk_u32 format;
   bk_u32 flags;
} bk_surface_desc;

typedef struct __attribute__((packed)) {
   float x, y, z, rhw;
   bk_u32 color;
   float u, v;
} bk_vertex;

#define BK_SURFACE_INVALID 0U
#define BK_FORMAT_ARGB8888 2U
#define BK_SURFACE_RENDER_TARGET (1U << 0)
#define BK_SURFACE_TEXTURE       (1U << 1)
#define BK_SURFACE_DYNAMIC       (1U << 2)

#define BK_CAP_FIXED_FUNCTION  (1U << 0)
#define BK_CAP_RENDER_TARGETS  (1U << 1)
#define BK_CAP_VERTEX_BUFFERS  (1U << 2)
#define BK_CAP_ALPHA_BLEND     (1U << 5)
#define BK_CAP_TEXTURES        (1U << 6)
#define BK_CAP_DEPTH_BUFFER          (1U << 12)
#define BK_CAP_DEPTH_SURFACE_IO      (1U << 13)
#define BK_CAP_DEPTH_FUNCS           (1U << 14)
#define BK_CAP_BLEND_ADDITIVE        (1U << 15)
#define BK_CAP_TEXTURE_REGION_UPLOAD (1U << 16)

#define BK_DRAW_DEPTH_TEST     (1U << 0)
#define BK_DRAW_DEPTH_WRITE    (1U << 1)
#define BK_DRAW_BLEND          (1U << 2)
#define BK_DRAW_TEXTURED       (1U << 3)
#define BK_DRAW_LINEAR         (1U << 4)
#define BK_DRAW_CLEAR_COLOR    (1U << 5)
#define BK_DRAW_CLEAR_DEPTH    (1U << 6)
#define BK_DRAW_REPEAT_U       (1U << 9)
#define BK_DRAW_REPEAT_V       (1U << 10)
#define BK_DRAW_DEPTH_FUNC_VALID (1U << 11)
#define BK_DRAW_DEPTH_FUNC_SHIFT 12U
#define BK_DRAW_BLEND_ADDITIVE   (1U << 15)
#define BK_DRAW_DEPTH_FUNC(f) (BK_DRAW_DEPTH_FUNC_VALID | \
   (((bk_u32)(f) & 7U) << BK_DRAW_DEPTH_FUNC_SHIFT))

extern bk_bool gfx3d_available(void);
extern const char *gfx3d_driver_name(void);
extern bk_u32 gfx3d_capabilities(void);
extern bk_bool gfx3d_surface_create(const bk_surface_desc *desc,
                                    bk_surface *handle_out);
extern bk_bool gfx3d_surface_destroy(bk_surface handle);
extern bk_bool gfx3d_surface_upload(bk_surface handle,
                                    const bk_u32 *pixels,
                                    bk_u32 source_pitch,
                                    const bk_rect *rect);
extern bk_bool gfx3d_surface_upload_region(bk_surface handle,
                                             const bk_u32 *pixels,
                                             bk_u32 source_pitch,
                                             bk_u32 destination_x,
                                             bk_u32 destination_y,
                                             bk_u32 width, bk_u32 height);
extern bk_bool gfx3d_surface_download(bk_surface handle,
                                      bk_u32 *pixels,
                                      bk_u32 destination_pitch,
                                      const bk_rect *rect);
extern bk_bool gfx3d_depth_upload(bk_surface target, const bk_u16 *depth,
                                  bk_u32 source_pitch, const bk_rect *rect);
extern bk_bool gfx3d_depth_download(bk_surface target, bk_u16 *depth,
                                    bk_u32 destination_pitch,
                                    const bk_rect *rect);
extern void bk_sys_log(const char *message);
extern bk_bool gfx3d_begin(bk_surface target, bk_u32 clear_color,
                           float clear_depth, bk_u32 flags);
extern bk_bool gfx3d_draw_triangles(bk_surface target, bk_surface texture,
                                    const bk_vertex *vertices,
                                    bk_u32 vertex_count, bk_u32 flags,
                                    bk_u32 *fence_out);
extern bk_bool gfx3d_end(bk_surface target, bk_u32 *fence_out);
extern bk_bool gfx3d_wait_fence(bk_u32 fence);

#define BKMESA_BATCH_VERTICES 3072U
#define BKMESA_TEXTURE_CACHE 64U

typedef struct {
   struct gl_texture_object *owner;
   struct gl_texture_image *image;
   const void *data;
   bk_surface surface;
   GLuint width, height;
   GLuint last_use;
} bkmesa_texture_slot;

typedef struct bkmesa_gpu_context {
   GLcontext *ctx;
   struct bkmesa_gpu_context *next;
   void *buffer;
   GLint width, height, rowlength;
   GLenum format;
   GLboolean yup;
   GLint requested_renderer;
   GLboolean supported;
   GLboolean render_gpu;
   GLboolean draw_open;
   GLboolean gpu_dirty;
   GLboolean cpu_dirty;
   GLboolean gpu_depth_dirty;
   GLboolean cpu_depth_dirty;
   GLboolean software_locked;
   GLboolean strict_failed;
   GLint last_fallback;
   bk_u16 *depth_staging;
   GLuint depth_staging_count;
   bk_surface target;
   bk_vertex batch[BKMESA_BATCH_VERTICES];
   GLuint batch_count;
   bk_u32 batch_flags;
   bk_surface batch_texture;
   bkmesa_texture_slot textures[BKMESA_TEXTURE_CACHE];
   GLuint texture_clock;
   bkmesa_gpu_stats stats;
} bkmesa_gpu_context;

static bkmesa_gpu_context *contexts;

static bkmesa_gpu_context *lookup(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu;
   for (gpu = contexts; gpu; gpu = gpu->next)
      if (gpu->ctx == ctx) return gpu;
   return NULL;
}

static GLboolean policy_allows_gpu(const bkmesa_gpu_context *gpu)
{
   return gpu && gpu->requested_renderer != BKMESA_RENDERER_SOFTWARE;
}

static GLboolean strict_mode(const bkmesa_gpu_context *gpu)
{
   return gpu && gpu->requested_renderer == BKMESA_RENDERER_GPU_STRICT;
}

static const char *fallback_string(GLint reason)
{
   switch (reason) {
   case BKMESA_FALLBACK_DRIVER_UNAVAILABLE: return "driver GFX3D no disponible";
   case BKMESA_FALLBACK_UNSUPPORTED_STATE: return "estado OpenGL no acelerado";
   case BKMESA_FALLBACK_SOFTWARE_OPERATION: return "operacion de pixels/SWRAST";
   case BKMESA_FALLBACK_COLOR_SYNC: return "fallo sincronizando color";
   case BKMESA_FALLBACK_DEPTH_SYNC: return "fallo sincronizando Z16";
   case BKMESA_FALLBACK_BEGIN_FAILED: return "gfx3d_begin fallo";
   case BKMESA_FALLBACK_DRAW_FAILED: return "draw/fence GFX3D fallo";
   case BKMESA_FALLBACK_TEXTURE_UPLOAD: return "subida de textura fallo";
   case BKMESA_FALLBACK_PROJECTIVE_TEXTURE: return "textura proyectiva q != 1";
   case BKMESA_FALLBACK_INTERNAL: return "fallo interno del backend";
   default: return "sin fallback";
   }
}

static void mark_fallback(bkmesa_gpu_context *gpu, GLint reason)
{
   if (!gpu) return;
   gpu->stats.fallbacks++;
   gpu->last_fallback = reason;
   if (strict_mode(gpu)) {
      if (!gpu->strict_failed) {
         bk_sys_log("[MESA35/GPU_STRICT] fallback detectado");
         bk_sys_log(fallback_string(reason));
      }
      gpu->strict_failed = GL_TRUE;
      gpu->stats.strict_failures++;
   }
}

static void destroy_texture_slot(bkmesa_texture_slot *slot)
{
   if (!slot) return;
   if (slot->surface) (void)gfx3d_surface_destroy(slot->surface);
   MEMSET(slot, 0, sizeof(*slot));
}

static void destroy_textures(bkmesa_gpu_context *gpu)
{
   GLuint i;
   for (i = 0; i < BKMESA_TEXTURE_CACHE; ++i)
      destroy_texture_slot(&gpu->textures[i]);
}

static void destroy_target(bkmesa_gpu_context *gpu)
{
   if (!gpu) return;
   if (gpu->draw_open) {
      bk_u32 fence = 0;
      (void)gfx3d_end(gpu->target, &fence);
      if (fence) (void)gfx3d_wait_fence(fence);
      gpu->draw_open = GL_FALSE;
   }
   if (gpu->target) (void)gfx3d_surface_destroy(gpu->target);
   gpu->target = BK_SURFACE_INVALID;
   if (gpu->depth_staging) FREE(gpu->depth_staging);
   gpu->depth_staging = NULL;
   gpu->depth_staging_count = 0;
   gpu->supported = GL_FALSE;
   gpu->gpu_dirty = GL_FALSE;
   gpu->cpu_dirty = GL_TRUE;
   gpu->gpu_depth_dirty = GL_FALSE;
   gpu->cpu_depth_dirty = GL_TRUE;
   gpu->software_locked = GL_FALSE;
   gpu->batch_count = 0;
}

static GLboolean create_target(bkmesa_gpu_context *gpu)
{
   bk_surface_desc desc;
   bk_u32 caps;
   if (!gpu || !gpu->buffer || gpu->width < 1 || gpu->height < 1 ||
       gpu->width > 65535 || gpu->height > 65535 ||
       gpu->format != OSMESA_BGRA || gpu->yup ||
       gpu->rowlength < gpu->width || !gfx3d_available())
      return GL_FALSE;
   caps = gfx3d_capabilities();
   if ((caps & (BK_CAP_FIXED_FUNCTION | BK_CAP_RENDER_TARGETS |
                BK_CAP_VERTEX_BUFFERS)) !=
       (BK_CAP_FIXED_FUNCTION | BK_CAP_RENDER_TARGETS |
        BK_CAP_VERTEX_BUFFERS))
      return GL_FALSE;
   MEMSET(&desc, 0, sizeof(desc));
   desc.width = (bk_u16)gpu->width;
   desc.height = (bk_u16)gpu->height;
   desc.format = BK_FORMAT_ARGB8888;
   desc.flags = BK_SURFACE_RENDER_TARGET | BK_SURFACE_TEXTURE |
                BK_SURFACE_DYNAMIC;
   if (!gfx3d_surface_create(&desc, &gpu->target)) return GL_FALSE;
   gpu->supported = GL_TRUE;
   gpu->cpu_dirty = GL_TRUE;
   gpu->cpu_depth_dirty = GL_TRUE;
   return GL_TRUE;
}

static GLboolean end_draw(bkmesa_gpu_context *gpu)
{
   bk_u32 fence = 0;
   GLboolean ok = GL_TRUE;
   if (!gpu || !gpu->draw_open) return GL_TRUE;
   if (gpu->batch_count) {
      if (!gfx3d_draw_triangles(gpu->target, gpu->batch_texture,
                                gpu->batch, gpu->batch_count,
                                gpu->batch_flags, &fence))
         ok = GL_FALSE;
      else {
         gpu->stats.draw_calls++;
         if (gpu->batch_flags & BK_DRAW_DEPTH_WRITE) {
            gpu->gpu_depth_dirty = GL_TRUE;
            gpu->cpu_depth_dirty = GL_FALSE;
         }
         if (fence && !gfx3d_wait_fence(fence)) ok = GL_FALSE;
      }
      gpu->batch_count = 0;
   }
   fence = 0;
   if (!gfx3d_end(gpu->target, &fence)) ok = GL_FALSE;
   if (fence && !gfx3d_wait_fence(fence)) ok = GL_FALSE;
   gpu->draw_open = GL_FALSE;
   if (ok) {
      gpu->gpu_dirty = GL_TRUE;
      gpu->cpu_dirty = GL_FALSE;
      gpu->stats.frames_finished++;
   }
   else {
      mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
      gpu->render_gpu = GL_FALSE;
   }
   return ok;
}

static GLboolean resolve_color(bkmesa_gpu_context *gpu)
{
   bk_rect rect;
   if (!gpu || !gpu->supported || !gpu->target) return GL_TRUE;
   if (!end_draw(gpu)) return GL_FALSE;
   if (!gpu->gpu_dirty) return GL_TRUE;
   rect.x = 0; rect.y = 0; rect.w = gpu->width; rect.h = gpu->height;
   if (!gfx3d_surface_download(gpu->target, (bk_u32 *)gpu->buffer,
                               (bk_u32)gpu->rowlength, &rect)) {
      mark_fallback(gpu, BKMESA_FALLBACK_COLOR_SYNC);
      return GL_FALSE;
   }
   gpu->stats.downloads++;
   gpu->gpu_dirty = GL_FALSE;
   gpu->cpu_dirty = GL_FALSE;
   return GL_TRUE;
}

static GLboolean upload_color(bkmesa_gpu_context *gpu)
{
   bk_rect rect;
   if (!gpu || !gpu->supported || !gpu->target) return GL_FALSE;
   if (!gpu->cpu_dirty) return GL_TRUE;
   rect.x = 0; rect.y = 0; rect.w = gpu->width; rect.h = gpu->height;
   if (!gfx3d_surface_upload(gpu->target, (const bk_u32 *)gpu->buffer,
                             (bk_u32)gpu->rowlength, &rect)) {
      mark_fallback(gpu, BKMESA_FALLBACK_COLOR_SYNC);
      return GL_FALSE;
   }
   gpu->stats.uploads++;
   gpu->cpu_dirty = GL_FALSE;
   gpu->gpu_dirty = GL_FALSE;
   return GL_TRUE;
}

static GLboolean ensure_depth_staging(bkmesa_gpu_context *gpu)
{
   GLuint count;
   if (!gpu || !gpu->ctx || !gpu->ctx->DrawBuffer ||
       !gpu->ctx->DrawBuffer->DepthBuffer) return GL_TRUE;
   count = (GLuint)gpu->width * (GLuint)gpu->height;
   if (gpu->depth_staging && gpu->depth_staging_count == count) return GL_TRUE;
   if (gpu->depth_staging) FREE(gpu->depth_staging);
   gpu->depth_staging = (bk_u16 *)MALLOC(count * sizeof(bk_u16));
   gpu->depth_staging_count = gpu->depth_staging ? count : 0;
   return gpu->depth_staging != NULL;
}

static void mesa_depth_to_z16(bkmesa_gpu_context *gpu)
{
   GLuint x, y;
   GLframebuffer *fb = gpu->ctx->DrawBuffer;
   if (gpu->ctx->Visual.depthBits <= 16) {
      const GLushort *src = (const GLushort *)fb->DepthBuffer;
      for (y = 0; y < (GLuint)gpu->height; y++) {
         const GLushort *row = src +
            ((GLuint)gpu->height - 1U - y) * (GLuint)fb->Width;
         bk_u16 *dst = gpu->depth_staging + y * (GLuint)gpu->width;
         for (x = 0; x < (GLuint)gpu->width; x++) dst[x] = row[x];
      }
   }
   else {
      const GLuint *src = (const GLuint *)fb->DepthBuffer;
      for (y = 0; y < (GLuint)gpu->height; y++) {
         const GLuint *row = src +
            ((GLuint)gpu->height - 1U - y) * (GLuint)fb->Width;
         bk_u16 *dst = gpu->depth_staging + y * (GLuint)gpu->width;
         for (x = 0; x < (GLuint)gpu->width; x++) dst[x] = (bk_u16)(row[x] >> 16);
      }
   }
}

static void z16_to_mesa_depth(bkmesa_gpu_context *gpu)
{
   GLuint x, y;
   GLframebuffer *fb = gpu->ctx->DrawBuffer;
   if (gpu->ctx->Visual.depthBits <= 16) {
      GLushort *dst = (GLushort *)fb->DepthBuffer;
      for (y = 0; y < (GLuint)gpu->height; y++) {
         GLushort *row = dst +
            ((GLuint)gpu->height - 1U - y) * (GLuint)fb->Width;
         const bk_u16 *src = gpu->depth_staging + y * (GLuint)gpu->width;
         for (x = 0; x < (GLuint)gpu->width; x++) row[x] = src[x];
      }
   }
   else {
      GLuint *dst = (GLuint *)fb->DepthBuffer;
      for (y = 0; y < (GLuint)gpu->height; y++) {
         GLuint *row = dst +
            ((GLuint)gpu->height - 1U - y) * (GLuint)fb->Width;
         const bk_u16 *src = gpu->depth_staging + y * (GLuint)gpu->width;
         for (x = 0; x < (GLuint)gpu->width; x++)
            row[x] = ((GLuint)src[x] << 16) | src[x];
      }
   }
}

static GLboolean resolve_depth(bkmesa_gpu_context *gpu)
{
   bk_rect rect;
   if (!gpu || !gpu->gpu_depth_dirty || !gpu->ctx->DrawBuffer ||
       !gpu->ctx->DrawBuffer->DepthBuffer) return GL_TRUE;
   if (!(gfx3d_capabilities() & BK_CAP_DEPTH_SURFACE_IO) ||
       !ensure_depth_staging(gpu)) {
      mark_fallback(gpu, BKMESA_FALLBACK_DEPTH_SYNC);
      return GL_FALSE;
   }
   rect.x = 0; rect.y = 0; rect.w = gpu->width; rect.h = gpu->height;
   if (!gfx3d_depth_download(gpu->target, gpu->depth_staging,
                             (bk_u32)gpu->width, &rect)) {
      mark_fallback(gpu, BKMESA_FALLBACK_DEPTH_SYNC);
      return GL_FALSE;
   }
   z16_to_mesa_depth(gpu);
   gpu->stats.depth_downloads++;
   gpu->gpu_depth_dirty = GL_FALSE;
   gpu->cpu_depth_dirty = GL_FALSE;
   return GL_TRUE;
}

static GLboolean upload_depth(bkmesa_gpu_context *gpu)
{
   bk_rect rect;
   if (!gpu || !gpu->cpu_depth_dirty || !gpu->ctx->DrawBuffer ||
       !gpu->ctx->DrawBuffer->DepthBuffer) return GL_TRUE;
   if (!(gfx3d_capabilities() & BK_CAP_DEPTH_SURFACE_IO) ||
       !ensure_depth_staging(gpu)) {
      mark_fallback(gpu, BKMESA_FALLBACK_DEPTH_SYNC);
      return GL_FALSE;
   }
   mesa_depth_to_z16(gpu);
   rect.x = 0; rect.y = 0; rect.w = gpu->width; rect.h = gpu->height;
   if (!gfx3d_depth_upload(gpu->target, gpu->depth_staging,
                           (bk_u32)gpu->width, &rect)) {
      mark_fallback(gpu, BKMESA_FALLBACK_DEPTH_SYNC);
      return GL_FALSE;
   }
   gpu->stats.depth_uploads++;
   gpu->cpu_depth_dirty = GL_FALSE;
   gpu->gpu_depth_dirty = GL_FALSE;
   return GL_TRUE;
}

static void before_cpu_reason(bkmesa_gpu_context *gpu, GLint reason)
{
   if (!gpu) return;
   if (!resolve_color(gpu) || !resolve_depth(gpu))
      mark_fallback(gpu, BKMESA_FALLBACK_DEPTH_SYNC);
   if (reason != BKMESA_FALLBACK_NONE) mark_fallback(gpu, reason);
   gpu->render_gpu = GL_FALSE;
   gpu->cpu_dirty = GL_TRUE;
   gpu->cpu_depth_dirty = GL_TRUE;
   gpu->software_locked =
      (gfx3d_capabilities() & BK_CAP_DEPTH_SURFACE_IO) ? GL_FALSE : GL_TRUE;
}

static void before_cpu(bkmesa_gpu_context *gpu)
{
   before_cpu_reason(gpu, BKMESA_FALLBACK_SOFTWARE_OPERATION);
}

static GLboolean blend_supported(const GLcontext *ctx)
{
   GLboolean normal, additive;
   if (!ctx->Color.BlendEnabled) return GL_TRUE;
   if (ctx->Color.BlendEquation != GL_FUNC_ADD) return GL_FALSE;
   normal = ctx->Color.BlendSrcRGB == GL_SRC_ALPHA &&
            ctx->Color.BlendDstRGB == GL_ONE_MINUS_SRC_ALPHA &&
            ctx->Color.BlendSrcA == GL_SRC_ALPHA &&
            ctx->Color.BlendDstA == GL_ONE_MINUS_SRC_ALPHA;
   additive = ctx->Color.BlendSrcRGB == GL_SRC_ALPHA &&
              ctx->Color.BlendDstRGB == GL_ONE &&
              ctx->Color.BlendSrcA == GL_SRC_ALPHA &&
              ctx->Color.BlendDstA == GL_ONE;
   return normal || additive;
}

static GLboolean texture_supported(const GLcontext *ctx)
{
   const struct gl_texture_unit *unit;
   const struct gl_texture_object *obj;
   const struct gl_texture_image *img;
   GLuint i;
   if (!ctx->Texture._ReallyEnabled) return GL_TRUE;
   if (ctx->Texture.Unit[0]._ReallyEnabled != TEXTURE0_2D) return GL_FALSE;
   for (i = 1; i < ctx->Const.MaxTextureUnits; ++i)
      if (ctx->Texture.Unit[i]._ReallyEnabled) return GL_FALSE;
   unit = &ctx->Texture.Unit[0];
   if (unit->EnvMode != GL_MODULATE && unit->EnvMode != GL_REPLACE)
      return GL_FALSE;
   obj = unit->_Current;
   if (!obj || !obj->Complete || obj->BorderColor[0] || obj->BorderColor[1] ||
       obj->BorderColor[2] || obj->BorderColor[3]) return GL_FALSE;
   if (obj->BaseLevel < 0 || obj->BaseLevel >= MAX_TEXTURE_LEVELS)
      return GL_FALSE;
   img = obj->Image[obj->BaseLevel];
   if (!img || !img->Data || img->Border || !img->FetchTexel ||
       img->Width2 < 1 || img->Height2 < 1 || img->Depth2 != 1)
      return GL_FALSE;
   if (obj->MinFilter != GL_NEAREST && obj->MinFilter != GL_LINEAR &&
       obj->MinFilter != GL_NEAREST_MIPMAP_NEAREST &&
       obj->MinFilter != GL_LINEAR_MIPMAP_NEAREST)
      return GL_FALSE;
   if (obj->MagFilter != GL_NEAREST && obj->MagFilter != GL_LINEAR)
      return GL_FALSE;
   if ((obj->WrapS != GL_REPEAT && obj->WrapS != GL_CLAMP &&
        obj->WrapS != GL_CLAMP_TO_EDGE) ||
       (obj->WrapT != GL_REPEAT && obj->WrapT != GL_CLAMP &&
        obj->WrapT != GL_CLAMP_TO_EDGE))
      return GL_FALSE;
   return GL_TRUE;
}

static GLboolean state_supported(bkmesa_gpu_context *gpu, GLcontext *ctx)
{
   bk_u32 caps;
   if (!gpu || !ctx || !gpu->supported || !policy_allows_gpu(gpu) ||
       gpu->software_locked || gpu->strict_failed ||
       ctx->RenderMode != GL_RENDER || !ctx->Visual.rgbMode ||
       ctx->Color.AlphaEnabled || ctx->Stencil.Enabled ||
       (ctx->Fog.Enabled && ctx->Fog.Mode != GL_LINEAR) ||
       ctx->Color.ColorLogicOpEnabled || ctx->Color.IndexLogicOpEnabled ||
       ctx->Color.MultiDrawBuffer || ctx->Polygon.SmoothFlag ||
       ctx->Polygon.StippleFlag ||
       (ctx->_TriangleCaps & (DD_TRI_LIGHT_TWOSIDE |
                             DD_SEPARATE_SPECULAR | DD_TRI_SMOOTH |
                             DD_TRI_STIPPLE | DD_TRI_CULL_FRONT_BACK)) ||
       ctx->Line.SmoothFlag || ctx->Line.StippleFlag ||
       ctx->Point.SmoothFlag || ctx->Point._Attenuated ||
       ctx->Color.ColorMask[0] != 0xff ||
       ctx->Color.ColorMask[1] != 0xff ||
       ctx->Color.ColorMask[2] != 0xff ||
       ctx->Color.ColorMask[3] != 0xff || !blend_supported(ctx) ||
       !texture_supported(ctx))
      return GL_FALSE;
   caps = gfx3d_capabilities();
   if (ctx->Depth.Test && !(caps & BK_CAP_DEPTH_BUFFER)) return GL_FALSE;
   if (ctx->Depth.Test && !(caps & BK_CAP_DEPTH_FUNCS) &&
       ctx->Depth.Func != GL_LESS && ctx->Depth.Func != GL_LEQUAL)
      return GL_FALSE;
   if (ctx->Color.BlendEnabled && !(caps & BK_CAP_ALPHA_BLEND)) return GL_FALSE;
   if (ctx->Color.BlendEnabled && ctx->Color.BlendDstRGB == GL_ONE &&
       !(caps & BK_CAP_BLEND_ADDITIVE)) return GL_FALSE;
   if (ctx->Texture._ReallyEnabled && !(caps & BK_CAP_TEXTURES)) return GL_FALSE;
   return GL_TRUE;
}

static bk_u32 color_to_argb(const GLchan color[4])
{
   return ((bk_u32)color[3] << 24) | ((bk_u32)color[0] << 16) |
          ((bk_u32)color[1] << 8) | (bk_u32)color[2];
}

static void convert_vertex(const bkmesa_gpu_context *gpu,
                           const GLcontext *ctx, bk_vertex *out,
                           const SWvertex *in, bk_u32 forced_color,
                           GLboolean force_color)
{
   GLfloat z = ctx->DepthMaxF ? in->win[2] / ctx->DepthMaxF : 0.0F;
   out->x = in->win[0];
   out->y = (GLfloat)(gpu->height - 1) - in->win[1];
   out->z = CLAMP(z, 0.0F, 1.0F);
   out->rhw = in->win[3];
   out->color = force_color ? forced_color : color_to_argb(in->color);
   if (ctx->Fog.Enabled) {
      bk_u32 c = out->color;
      GLfloat f = CLAMP(in->fog, 0.0F, 1.0F);
      GLfloat g = 1.0F - f;
      GLuint r = (GLuint)(((c >> 16) & 255U) * f +
                 CLAMP(ctx->Fog.Color[0], 0.0F, 1.0F) * 255.0F * g);
      GLuint gr = (GLuint)(((c >> 8) & 255U) * f +
                  CLAMP(ctx->Fog.Color[1], 0.0F, 1.0F) * 255.0F * g);
      GLuint b = (GLuint)((c & 255U) * f +
                 CLAMP(ctx->Fog.Color[2], 0.0F, 1.0F) * 255.0F * g);
      out->color = (c & 0xff000000U) | (r << 16) | (gr << 8) | b;
   }
   out->u = in->texcoord[0][0];
   out->v = in->texcoord[0][1];
}

static bk_u32 depth_func_flag(GLenum func)
{
   switch (func) {
   case GL_NEVER: return BK_DRAW_DEPTH_FUNC(0U);
   case GL_LESS: return BK_DRAW_DEPTH_FUNC(1U);
   case GL_EQUAL: return BK_DRAW_DEPTH_FUNC(2U);
   case GL_LEQUAL: return BK_DRAW_DEPTH_FUNC(3U);
   case GL_GREATER: return BK_DRAW_DEPTH_FUNC(4U);
   case GL_NOTEQUAL: return BK_DRAW_DEPTH_FUNC(5U);
   case GL_GEQUAL: return BK_DRAW_DEPTH_FUNC(6U);
   default: return BK_DRAW_DEPTH_FUNC(7U);
   }
}

static bk_u32 draw_flags(const GLcontext *ctx)
{
   bk_u32 flags = 0;
   const struct gl_texture_object *obj = ctx->Texture.Unit[0]._Current;
   if (ctx->Depth.Test) flags |= BK_DRAW_DEPTH_TEST | depth_func_flag(ctx->Depth.Func);
   if (ctx->Depth.Test && ctx->Depth.Mask) flags |= BK_DRAW_DEPTH_WRITE;
   if (ctx->Color.BlendEnabled) {
      flags |= BK_DRAW_BLEND;
      if (ctx->Color.BlendDstRGB == GL_ONE) flags |= BK_DRAW_BLEND_ADDITIVE;
   }
   if (ctx->Texture._ReallyEnabled) {
      flags |= BK_DRAW_TEXTURED;
      if (!obj || obj->MinFilter == GL_LINEAR || obj->MagFilter == GL_LINEAR)
         flags |= BK_DRAW_LINEAR;
      if (!obj || obj->WrapS == GL_REPEAT) flags |= BK_DRAW_REPEAT_U;
      if (!obj || obj->WrapT == GL_REPEAT) flags |= BK_DRAW_REPEAT_V;
   }
   return flags;
}

static bkmesa_texture_slot *find_texture(bkmesa_gpu_context *gpu,
                                         struct gl_texture_object *owner,
                                         struct gl_texture_image *image)
{
   GLuint i;
   for (i = 0; i < BKMESA_TEXTURE_CACHE; ++i) {
      bkmesa_texture_slot *slot = &gpu->textures[i];
      if (slot->owner == owner && slot->image == image &&
          slot->data == image->Data && slot->surface)
         return slot;
   }
   return NULL;
}

static bkmesa_texture_slot *choose_texture_slot(bkmesa_gpu_context *gpu)
{
   bkmesa_texture_slot *oldest = &gpu->textures[0];
   GLuint i;
   for (i = 0; i < BKMESA_TEXTURE_CACHE; ++i) {
      bkmesa_texture_slot *slot = &gpu->textures[i];
      if (!slot->surface || !slot->owner) return slot;
      if (slot->last_use < oldest->last_use) oldest = slot;
   }
   return oldest;
}

static GLboolean flush_batch(bkmesa_gpu_context *gpu)
{
   bk_u32 fence = 0;
   if (!gpu || !gpu->batch_count) return GL_TRUE;
   if (!gfx3d_draw_triangles(gpu->target, gpu->batch_texture,
                             gpu->batch, gpu->batch_count,
                             gpu->batch_flags, &fence)) {
      gpu->batch_count = 0;
      mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
      return GL_FALSE;
   }
   if (fence && !gfx3d_wait_fence(fence)) {
      gpu->batch_count = 0;
      mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
      return GL_FALSE;
   }
   gpu->stats.draw_calls++;
   if (gpu->batch_flags & BK_DRAW_DEPTH_WRITE) {
      gpu->gpu_depth_dirty = GL_TRUE;
      gpu->cpu_depth_dirty = GL_FALSE;
   }
   gpu->batch_count = 0;
   return GL_TRUE;
}

static bk_surface sync_texture(bkmesa_gpu_context *gpu, GLcontext *ctx,
                               struct gl_texture_image *selected)
{
   struct gl_texture_object *obj;
   struct gl_texture_image *img;
   bkmesa_texture_slot *slot;
   bk_surface_desc desc;
   bk_rect rect;
   bk_u32 *pixels;
   GLuint x, y;
   if (!ctx->Texture._ReallyEnabled) return BK_SURFACE_INVALID;
   obj = ctx->Texture.Unit[0]._Current;
   if (!obj) return BK_SURFACE_INVALID;
   img = selected ? selected : obj->Image[obj->BaseLevel];
   if (!img) return BK_SURFACE_INVALID;
   slot = find_texture(gpu, obj, img);
   if (slot) {
      slot->last_use = ++gpu->texture_clock;
      return slot->surface;
   }
   if (!flush_batch(gpu)) return BK_SURFACE_INVALID;
   slot = choose_texture_slot(gpu);
   destroy_texture_slot(slot);
   pixels = (bk_u32 *)MALLOC(img->Width2 * img->Height2 * sizeof(bk_u32));
   if (!pixels) return BK_SURFACE_INVALID;
   for (y = 0; y < img->Height2; ++y) {
      for (x = 0; x < img->Width2; ++x) {
         GLchan rgba[4] = {0, 0, 0, 255};
         img->FetchTexel(img, (GLint)x, (GLint)y, 0, rgba);
         pixels[y * img->Width2 + x] = color_to_argb(rgba);
      }
   }
   MEMSET(&desc, 0, sizeof(desc));
   desc.width = (bk_u16)img->Width2;
   desc.height = (bk_u16)img->Height2;
   desc.format = BK_FORMAT_ARGB8888;
   desc.flags = BK_SURFACE_TEXTURE | BK_SURFACE_DYNAMIC;
   if (!gfx3d_surface_create(&desc, &slot->surface)) {
      FREE(pixels);
      return BK_SURFACE_INVALID;
   }
   rect.x = 0; rect.y = 0; rect.w = img->Width2; rect.h = img->Height2;
   if (!gfx3d_surface_upload(slot->surface, pixels, img->Width2, &rect)) {
      FREE(pixels);
      destroy_texture_slot(slot);
      return BK_SURFACE_INVALID;
   }
   FREE(pixels);
   slot->owner = obj;
   slot->image = img;
   slot->data = img->Data;
   slot->width = img->Width2;
   slot->height = img->Height2;
   slot->last_use = ++gpu->texture_clock;
   gpu->stats.texture_uploads++;
   return slot->surface;
}

static struct gl_texture_image *select_texture_image(
   GLcontext *ctx, const SWvertex *a, const SWvertex *b, const SWvertex *c)
{
   struct gl_texture_object *obj = ctx->Texture.Unit[0]._Current;
   struct gl_texture_image *base;
   GLuint level, max_level;
   GLfloat rho = 1.0F;
   const SWvertex *v[3] = {a, b, c};
   if (!obj) return NULL;
   level = (GLuint)obj->BaseLevel;
   base = obj->Image[level];
   if (!base) return NULL;
   if (obj->MinFilter != GL_NEAREST_MIPMAP_NEAREST &&
       obj->MinFilter != GL_LINEAR_MIPMAP_NEAREST) return base;
   for (GLuint i = 0; i < 3U; i++) {
      const SWvertex *p = v[i], *q = v[(i + 1U) % 3U];
      GLfloat dx = q->win[0] - p->win[0];
      GLfloat dy = q->win[1] - p->win[1];
      GLfloat pixels = (GLfloat)sqrt((double)(dx * dx + dy * dy));
      GLfloat du = (q->texcoord[0][0] - p->texcoord[0][0]) * base->Width2;
      GLfloat dv = (q->texcoord[0][1] - p->texcoord[0][1]) * base->Height2;
      GLfloat texels = (GLfloat)sqrt((double)(du * du + dv * dv));
      if (pixels > 0.001F && texels / pixels > rho) rho = texels / pixels;
   }
   max_level = obj->_MaxLevel >= obj->BaseLevel ?
      (GLuint)obj->_MaxLevel : (GLuint)obj->BaseLevel;
   while (rho > 1.5F && level < max_level && level + 1U < MAX_TEXTURE_LEVELS) {
      rho *= 0.5F;
      level++;
   }
   return obj->Image[level] ? obj->Image[level] : base;
}

static bk_u32 lerp_color(bk_u32 a, bk_u32 b, GLfloat t)
{
   bk_u32 result = 0U;
   for (GLuint shift = 0U; shift <= 24U; shift += 8U) {
      GLfloat av = (GLfloat)((a >> shift) & 255U);
      GLfloat bv = (GLfloat)((b >> shift) & 255U);
      bk_u32 value = (bk_u32)CLAMP(av + (bv - av) * t, 0.0F, 255.0F);
      result |= value << shift;
   }
   return result;
}

static bk_vertex lerp_vertex(const bk_vertex *a, const bk_vertex *b, GLfloat t)
{
   bk_vertex out;
   out.x = a->x + (b->x - a->x) * t;
   out.y = a->y + (b->y - a->y) * t;
   out.z = a->z + (b->z - a->z) * t;
   out.rhw = a->rhw + (b->rhw - a->rhw) * t;
   out.color = lerp_color(a->color, b->color, t);
   out.u = a->u + (b->u - a->u) * t;
   out.v = a->v + (b->v - a->v) * t;
   return out;
}

static void scissor_bounds(const bkmesa_gpu_context *gpu,
                           const GLcontext *ctx,
                           GLfloat *xmin, GLfloat *ymin,
                           GLfloat *xmax, GLfloat *ymax)
{
   if (!ctx->Scissor.Enabled) {
      *xmin = 0.0F; *ymin = 0.0F;
      *xmax = (GLfloat)gpu->width; *ymax = (GLfloat)gpu->height;
   }
   else {
      *xmin = (GLfloat)ctx->Scissor.X;
      *xmax = (GLfloat)(ctx->Scissor.X + ctx->Scissor.Width);
      *ymin = (GLfloat)(gpu->height -
              (ctx->Scissor.Y + ctx->Scissor.Height));
      *ymax = (GLfloat)(gpu->height - ctx->Scissor.Y);
   }
}

static GLuint clip_polygon_edge(const bk_vertex *input, GLuint count,
                                bk_vertex *output, GLuint edge, GLfloat bound)
{
   GLuint out_count = 0U;
   if (!count) return 0U;
   for (GLuint i = 0U; i < count; i++) {
      const bk_vertex *a = &input[i];
      const bk_vertex *b = &input[(i + 1U) % count];
      GLfloat av = edge < 2U ? a->x : a->y;
      GLfloat bv = edge < 2U ? b->x : b->y;
      GLboolean ain = (edge == 0U || edge == 2U) ? av >= bound : av <= bound;
      GLboolean bin = (edge == 0U || edge == 2U) ? bv >= bound : bv <= bound;
      if (ain) output[out_count++] = *a;
      if (ain != bin && out_count < 12U) {
         GLfloat denom = bv - av;
         GLfloat t = denom != 0.0F ? (bound - av) / denom : 0.0F;
         output[out_count++] = lerp_vertex(a, b, CLAMP(t, 0.0F, 1.0F));
      }
   }
   return out_count;
}

static GLuint clip_triangle_scissor(const bkmesa_gpu_context *gpu,
                                    const GLcontext *ctx,
                                    const bk_vertex input[3],
                                    bk_vertex output[12])
{
   bk_vertex a[12], b[12];
   GLfloat xmin, ymin, xmax, ymax;
   GLuint n = 3U;
   a[0] = input[0]; a[1] = input[1]; a[2] = input[2];
   scissor_bounds(gpu, ctx, &xmin, &ymin, &xmax, &ymax);
   n = clip_polygon_edge(a, n, b, 0U, xmin);
   n = clip_polygon_edge(b, n, a, 1U, xmax);
   n = clip_polygon_edge(a, n, b, 2U, ymin);
   n = clip_polygon_edge(b, n, output, 3U, ymax);
   return n;
}

static void apply_polygon_offset(const GLcontext *ctx, bk_vertex v[3])
{
   GLfloat dx1, dy1, dz1, dx2, dy2, dz2, det, dzdx, dzdy, offset;
   if (!ctx->Polygon._OffsetAny) return;
   dx1 = v[1].x - v[0].x; dy1 = v[1].y - v[0].y;
   dz1 = v[1].z - v[0].z;
   dx2 = v[2].x - v[0].x; dy2 = v[2].y - v[0].y;
   dz2 = v[2].z - v[0].z;
   det = dx1 * dy2 - dx2 * dy1;
   dzdx = dzdy = 0.0F;
   if (det > 0.000001F || det < -0.000001F) {
      dzdx = (dz1 * dy2 - dz2 * dy1) / det;
      dzdy = (dx1 * dz2 - dx2 * dz1) / det;
   }
   if (dzdx < 0.0F) dzdx = -dzdx;
   if (dzdy < 0.0F) dzdy = -dzdy;
   offset = ctx->Polygon.OffsetFactor * (dzdx > dzdy ? dzdx : dzdy) +
            ctx->Polygon.OffsetMRD;
   for (GLuint i = 0U; i < 3U; i++)
      v[i].z = CLAMP(v[i].z + offset, 0.0F, 1.0F);
}

static GLboolean line_clip_test(GLfloat p, GLfloat q,
                                GLfloat *u0, GLfloat *u1)
{
   GLfloat r;
   if (p == 0.0F) return q >= 0.0F;
   r = q / p;
   if (p < 0.0F) { if (r > *u1) return GL_FALSE; if (r > *u0) *u0 = r; }
   else { if (r < *u0) return GL_FALSE; if (r < *u1) *u1 = r; }
   return GL_TRUE;
}

static GLboolean clip_line_scissor(const bkmesa_gpu_context *gpu,
                                   const GLcontext *ctx,
                                   bk_vertex *a, bk_vertex *b)
{
   GLfloat xmin, ymin, xmax, ymax, u0 = 0.0F, u1 = 1.0F;
   bk_vertex original_a = *a, original_b = *b;
   GLfloat dx = b->x - a->x, dy = b->y - a->y;
   scissor_bounds(gpu, ctx, &xmin, &ymin, &xmax, &ymax);
   if (!line_clip_test(-dx, a->x - xmin, &u0, &u1) ||
       !line_clip_test(dx, xmax - a->x, &u0, &u1) ||
       !line_clip_test(-dy, a->y - ymin, &u0, &u1) ||
       !line_clip_test(dy, ymax - a->y, &u0, &u1)) return GL_FALSE;
   if (u1 < 1.0F) *b = lerp_vertex(&original_a, &original_b, u1);
   if (u0 > 0.0F) *a = lerp_vertex(&original_a, &original_b, u0);
   return GL_TRUE;
}

static GLboolean begin_draw(bkmesa_gpu_context *gpu, GLcontext *ctx)
{
   if (!gpu || gpu->draw_open) return gpu && gpu->draw_open;
   if (!upload_color(gpu) || !upload_depth(gpu)) return GL_FALSE;
   if (!gfx3d_begin(gpu->target, 0, ctx->Depth.Clear, 0)) {
      mark_fallback(gpu, BKMESA_FALLBACK_BEGIN_FAILED);
      return GL_FALSE;
   }
   gpu->draw_open = GL_TRUE;
   gpu->batch_count = 0;
   gpu->batch_flags = 0;
   gpu->batch_texture = BK_SURFACE_INVALID;
   gpu->stats.frames_started++;
   return GL_TRUE;
}

static GLboolean append_vertices(bkmesa_gpu_context *gpu,
                                 bk_surface texture, bk_u32 flags,
                                 const bk_vertex *verts, GLuint count)
{
   GLuint i;
   if (!gpu || !gpu->draw_open || count > BKMESA_BATCH_VERTICES)
      return GL_FALSE;
   if (gpu->batch_count &&
       (gpu->batch_flags != flags || gpu->batch_texture != texture) &&
       !flush_batch(gpu)) return GL_FALSE;
   if (gpu->batch_count + count > BKMESA_BATCH_VERTICES &&
       !flush_batch(gpu)) return GL_FALSE;
   gpu->batch_flags = flags;
   gpu->batch_texture = texture;
   for (i = 0; i < count; ++i)
      gpu->batch[gpu->batch_count++] = verts[i];
   return GL_TRUE;
}

static GLboolean append_screen_line(bkmesa_gpu_context *gpu,
                                    GLcontext *ctx,
                                    bk_vertex a, bk_vertex b,
                                    GLfloat width)
{
   bk_vertex v[6];
   GLfloat dx, dy, len, half, px, py;
   bk_u32 flags;
   if (!clip_line_scissor(gpu, ctx, &a, &b)) return GL_TRUE;
   dx = b.x - a.x; dy = b.y - a.y;
   len = (GLfloat)sqrt((double)(dx * dx + dy * dy));
   if (len < 0.0001F) return GL_TRUE;
   half = width * 0.5F; if (half < 0.5F) half = 0.5F;
   px = -dy / len * half; py = dx / len * half;
   v[0] = a; v[0].x += px; v[0].y += py;
   v[1] = b; v[1].x += px; v[1].y += py;
   v[2] = b; v[2].x -= px; v[2].y -= py;
   v[3] = a; v[3].x += px; v[3].y += py;
   v[4] = b; v[4].x -= px; v[4].y -= py;
   v[5] = a; v[5].x -= px; v[5].y -= py;
   flags = draw_flags(ctx) & ~BK_DRAW_TEXTURED;
   return append_vertices(gpu, BK_SURFACE_INVALID, flags, v, 6U);
}

static GLboolean append_screen_point(bkmesa_gpu_context *gpu,
                                     GLcontext *ctx,
                                     bk_vertex center, GLfloat size)
{
   bk_vertex v[6];
   GLfloat xmin, ymin, xmax, ymax, half;
   bk_u32 flags;
   scissor_bounds(gpu, ctx, &xmin, &ymin, &xmax, &ymax);
   if (center.x < xmin || center.x >= xmax || center.y < ymin || center.y >= ymax)
      return GL_TRUE;
   half = size * 0.5F; if (half < 0.5F) half = 0.5F;
#define SPV(n, ox, oy) do { v[n] = center; v[n].x += (ox); v[n].y += (oy); } while (0)
   SPV(0,-half,-half); SPV(1,half,-half); SPV(2,half,half);
   SPV(3,-half,-half); SPV(4,half,half); SPV(5,-half,half);
#undef SPV
   flags = draw_flags(ctx) & ~BK_DRAW_TEXTURED;
   return append_vertices(gpu, BK_SURFACE_INVALID, flags, v, 6U);
}

static GLboolean submit_triangle(GLcontext *ctx, const SWvertex *a,
                                 const SWvertex *b, const SWvertex *c)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   bk_vertex v[3], clipped[12], fan[30];
   bk_surface texture = BK_SURFACE_INVALID;
   bk_u32 flags, flat = 0;
   GLboolean force = GL_FALSE;
   GLuint n, out = 0U;
   GLenum mode = GL_FILL;
   struct gl_texture_image *image = NULL;
   if (!gpu || !gpu->render_gpu || !gpu->draw_open) return GL_FALSE;
   flags = draw_flags(ctx);
   if (ctx->Texture._ReallyEnabled) {
      if (a->texcoord[0][3] < 0.99999F || a->texcoord[0][3] > 1.00001F ||
          b->texcoord[0][3] < 0.99999F || b->texcoord[0][3] > 1.00001F ||
          c->texcoord[0][3] < 0.99999F || c->texcoord[0][3] > 1.00001F) {
         mark_fallback(gpu, BKMESA_FALLBACK_PROJECTIVE_TEXTURE);
         return GL_FALSE;
      }
      image = select_texture_image(ctx, a, b, c);
      texture = sync_texture(gpu, ctx, image);
      if (!texture) { mark_fallback(gpu, BKMESA_FALLBACK_TEXTURE_UPLOAD); return GL_FALSE; }
   }
   if (ctx->Light.ShadeModel == GL_FLAT) { flat = color_to_argb(c->color); force = GL_TRUE; }
   if (ctx->Texture._ReallyEnabled && ctx->Texture.Unit[0].EnvMode == GL_REPLACE) {
      flat = 0xffffffffU; force = GL_TRUE;
   }
   convert_vertex(gpu, ctx, &v[0], a, flat, force);
   convert_vertex(gpu, ctx, &v[1], b, flat, force);
   convert_vertex(gpu, ctx, &v[2], c, flat, force);
   apply_polygon_offset(ctx, v);
   if (ctx->_TriangleCaps & DD_TRI_UNFILLED) {
      if (ctx->Polygon.FrontMode == GL_POINT || ctx->Polygon.BackMode == GL_POINT)
         mode = GL_POINT;
      else mode = GL_LINE;
   }
   if (mode == GL_LINE) {
      if (!append_screen_line(gpu, ctx, v[0], v[1], ctx->Line._Width) ||
          !append_screen_line(gpu, ctx, v[1], v[2], ctx->Line._Width) ||
          !append_screen_line(gpu, ctx, v[2], v[0], ctx->Line._Width)) return GL_FALSE;
      gpu->stats.lines += 3U;
      return GL_TRUE;
   }
   if (mode == GL_POINT) {
      if (!append_screen_point(gpu, ctx, v[0], ctx->Point._Size) ||
          !append_screen_point(gpu, ctx, v[1], ctx->Point._Size) ||
          !append_screen_point(gpu, ctx, v[2], ctx->Point._Size)) return GL_FALSE;
      gpu->stats.points += 3U;
      return GL_TRUE;
   }
   n = clip_triangle_scissor(gpu, ctx, v, clipped);
   if (n < 3U) return GL_TRUE;
   for (GLuint i = 1U; i + 1U < n; i++) {
      fan[out++] = clipped[0]; fan[out++] = clipped[i]; fan[out++] = clipped[i + 1U];
   }
   if (!append_vertices(gpu, texture, flags, fan, out)) return GL_FALSE;
   gpu->stats.triangles += out / 3U;
   return GL_TRUE;
}

static void gpu_triangle(GLcontext *ctx, GLuint i0, GLuint i1, GLuint i2)
{
   SWvertex *v = SWSETUP_CONTEXT(ctx)->verts;
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu || !gpu->render_gpu ||
       !submit_triangle(ctx, &v[i0], &v[i1], &v[i2])) {
      if (gpu && gpu->last_fallback == BKMESA_FALLBACK_NONE)
         mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
      if (strict_mode(gpu)) return;
      before_cpu_reason(gpu, BKMESA_FALLBACK_NONE);
      _swsetup_Triangle(ctx, i0, i1, i2);
   }
}

static void gpu_quad(GLcontext *ctx, GLuint i0, GLuint i1,
                     GLuint i2, GLuint i3)
{
   gpu_triangle(ctx, i0, i1, i3);
   gpu_triangle(ctx, i1, i2, i3);
}

static GLboolean submit_line(GLcontext *ctx, const SWvertex *a,
                             const SWvertex *b)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   bk_vertex av, bv;
   if (!gpu || !gpu->render_gpu || !gpu->draw_open ||
       ctx->Texture._ReallyEnabled) return GL_FALSE;
   convert_vertex(gpu, ctx, &av, a, 0, GL_FALSE);
   convert_vertex(gpu, ctx, &bv, b, 0, GL_FALSE);
   if (!append_screen_line(gpu, ctx, av, bv, ctx->Line._Width)) return GL_FALSE;
   gpu->stats.lines++;
   return GL_TRUE;
}

static void gpu_line(GLcontext *ctx, GLuint i0, GLuint i1)
{
   SWvertex *v = SWSETUP_CONTEXT(ctx)->verts;
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu || !submit_line(ctx, &v[i0], &v[i1])) {
      if (gpu && gpu->last_fallback == BKMESA_FALLBACK_NONE)
         mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
      if (strict_mode(gpu)) return;
      before_cpu_reason(gpu, BKMESA_FALLBACK_NONE);
      _swsetup_Line(ctx, i0, i1);
   }
}

static GLboolean submit_point(GLcontext *ctx, const SWvertex *p)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   bk_vertex center;
   GLfloat size;
   if (!gpu || !gpu->render_gpu || !gpu->draw_open ||
       ctx->Texture._ReallyEnabled) return GL_FALSE;
   convert_vertex(gpu, ctx, &center, p, 0, GL_FALSE);
   size = p->pointSize > 0.0F ? p->pointSize : ctx->Point._Size;
   if (!append_screen_point(gpu, ctx, center, size)) return GL_FALSE;
   gpu->stats.points++;
   return GL_TRUE;
}

static void gpu_points(GLcontext *ctx, GLuint first, GLuint last)
{
   struct vertex_buffer *VB = &TNL_CONTEXT(ctx)->vb;
   SWvertex *v = SWSETUP_CONTEXT(ctx)->verts;
   bkmesa_gpu_context *gpu = lookup(ctx);
   GLuint i, index;
   if (!gpu || !gpu->render_gpu) {
      if (gpu && strict_mode(gpu)) { mark_fallback(gpu, BKMESA_FALLBACK_UNSUPPORTED_STATE); return; }
      before_cpu_reason(gpu, BKMESA_FALLBACK_UNSUPPORTED_STATE);
      _swsetup_Points(ctx, first, last);
      return;
   }
   for (i = first; i < last; ++i) {
      index = VB->Elts ? VB->Elts[i] : i;
      if (VB->ClipMask[index] == 0 && !submit_point(ctx, &v[index])) {
         if (gpu->last_fallback == BKMESA_FALLBACK_NONE)
            mark_fallback(gpu, BKMESA_FALLBACK_DRAW_FAILED);
         if (strict_mode(gpu)) return;
         before_cpu_reason(gpu, BKMESA_FALLBACK_NONE);
         _swsetup_Points(ctx, i, last);
         return;
      }
   }
}

static void gpu_render_start(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   GLboolean candidate;
   _swsetup_RenderStart(ctx);
   if (!gpu) return;
   gpu->last_fallback = BKMESA_FALLBACK_NONE;
   candidate = gpu->supported && policy_allows_gpu(gpu) && !gpu->software_locked;
   gpu->render_gpu = state_supported(gpu, ctx);
   if (gpu->render_gpu && !begin_draw(gpu, ctx)) gpu->render_gpu = GL_FALSE;
   if (!gpu->render_gpu) {
      GLint reason = candidate ? BKMESA_FALLBACK_UNSUPPORTED_STATE :
                                 BKMESA_FALLBACK_DRIVER_UNAVAILABLE;
      mark_fallback(gpu, reason);
      if (!strict_mode(gpu)) before_cpu_reason(gpu, BKMESA_FALLBACK_NONE);
   }
}

static void gpu_render_finish(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu && gpu->render_gpu) (void)end_draw(gpu);
   _swsetup_RenderFinish(ctx);
}

static void span_start(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (strict_mode(gpu)) { mark_fallback(gpu, BKMESA_FALLBACK_SOFTWARE_OPERATION); return; }
   before_cpu(gpu);
}

GLboolean bkmesa_gpu_attach(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu;
   if (!ctx || lookup(ctx)) return GL_FALSE;
   gpu = (bkmesa_gpu_context *)CALLOC(sizeof(*gpu));
   if (!gpu) return GL_FALSE;
   gpu->ctx = ctx;
   gpu->requested_renderer = BKMESA_RENDERER_AUTO;
   gpu->last_fallback = BKMESA_FALLBACK_NONE;
   gpu->cpu_dirty = GL_TRUE;
   gpu->cpu_depth_dirty = GL_TRUE;
   gpu->next = contexts;
   contexts = gpu;
   return GL_TRUE;
}

void bkmesa_gpu_detach(GLcontext *ctx)
{
   bkmesa_gpu_context **link = &contexts;
   while (*link) {
      bkmesa_gpu_context *gpu = *link;
      if (gpu->ctx == ctx) {
         (void)resolve_color(gpu);
         destroy_textures(gpu);
         destroy_target(gpu);
         *link = gpu->next;
         FREE(gpu);
         return;
      }
      link = &(*link)->next;
   }
}

void bkmesa_gpu_bind(GLcontext *ctx, void *buffer, GLint width, GLint height,
                     GLint rowlength, GLenum format, GLboolean yup)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu) return;
   (void)resolve_color(gpu);
   destroy_textures(gpu);
   destroy_target(gpu);
   gpu->buffer = buffer;
   gpu->width = width;
   gpu->height = height;
   gpu->rowlength = rowlength;
   gpu->format = format;
   gpu->yup = yup;
   (void)create_target(gpu);
}

void bkmesa_gpu_update_state(GLcontext *ctx, GLuint new_state)
{
   struct swrast_device_driver *swdd;
   TNLcontext *tnl;
   (void)new_state;
   if (!lookup(ctx)) return;
   swdd = _swrast_GetDeviceDriverReference(ctx);
   tnl = TNL_CONTEXT(ctx);
   swdd->SpanRenderStart = span_start;
   swdd->SpanRenderFinish = NULL;
   tnl->Driver.RenderStart = gpu_render_start;
   tnl->Driver.RenderFinish = gpu_render_finish;
   tnl->Driver.PointsFunc = gpu_points;
   tnl->Driver.LineFunc = gpu_line;
   tnl->Driver.TriangleFunc = gpu_triangle;
   tnl->Driver.QuadFunc = gpu_quad;
}

void bkmesa_gpu_before_cpu(GLcontext *ctx)
{
   before_cpu(lookup(ctx));
}

GLboolean bkmesa_gpu_try_clear(GLcontext *ctx, GLbitfield mask,
                               GLboolean all, GLint x, GLint y,
                               GLint width, GLint height)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   bk_u32 flags = 0, fence = 0, color;
   GLboolean ok;
   (void)x; (void)y;
   if (!gpu || !gpu->supported || !policy_allows_gpu(gpu) || !all ||
       width != gpu->width || height != gpu->height || ctx->Scissor.Enabled ||
       (mask & ~(DD_FRONT_LEFT_BIT | DD_DEPTH_BIT)) ||
       ctx->Color.ColorMask[0] != 0xff || ctx->Color.ColorMask[1] != 0xff ||
       ctx->Color.ColorMask[2] != 0xff || ctx->Color.ColorMask[3] != 0xff)
      return GL_FALSE;
   if ((mask & DD_DEPTH_BIT) && !ctx->Depth.Mask) mask &= ~DD_DEPTH_BIT;
   if (!mask) return GL_TRUE;
   if (!upload_color(gpu)) return GL_FALSE;
   if (mask & DD_FRONT_LEFT_BIT) flags |= BK_DRAW_CLEAR_COLOR;
   if (mask & DD_DEPTH_BIT) flags |= BK_DRAW_CLEAR_DEPTH;
   color = color_to_argb(ctx->Color.ClearColor);
   ok = gfx3d_begin(gpu->target, color, ctx->Depth.Clear, flags) &&
        gfx3d_end(gpu->target, &fence);
   if (ok && fence) ok = gfx3d_wait_fence(fence);
   if (!ok) {
      mark_fallback(gpu, BKMESA_FALLBACK_BEGIN_FAILED);
      return GL_FALSE;
   }
   gpu->gpu_dirty = (mask & DD_FRONT_LEFT_BIT) ? GL_TRUE : gpu->gpu_dirty;
   gpu->cpu_dirty = GL_FALSE;
   if (mask & DD_DEPTH_BIT) {
      gpu->gpu_depth_dirty = GL_TRUE;
      gpu->cpu_depth_dirty = GL_FALSE;
   }
   if ((mask & DD_FRONT_LEFT_BIT) &&
       (!ctx->Visual.depthBits || (mask & DD_DEPTH_BIT)))
      gpu->software_locked = GL_FALSE;
   gpu->stats.frames_started++;
   gpu->stats.frames_finished++;
   return GL_TRUE;
}

GLboolean bkmesa_gpu_resolve(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu || gpu->strict_failed) return GL_FALSE;
   return resolve_color(gpu) && resolve_depth(gpu);
}

void bkmesa_gpu_flush(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu && gpu->draw_open) (void)flush_batch(gpu);
}

void bkmesa_gpu_finish(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu && gpu->draw_open) (void)end_draw(gpu);
}

void bkmesa_gpu_texture_changed(GLcontext *ctx,
                                struct gl_texture_object *texObj)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   GLuint i;
   if (!gpu || !texObj) return;
   for (i = 0; i < BKMESA_TEXTURE_CACHE; ++i)
      if (gpu->textures[i].owner == texObj)
         destroy_texture_slot(&gpu->textures[i]);
}

void bkmesa_gpu_texture_region_changed(GLcontext *ctx,
                                       struct gl_texture_object *texObj,
                                       struct gl_texture_image *image,
                                       GLint x, GLint y,
                                       GLsizei width, GLsizei height)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   bkmesa_texture_slot *slot;
   bk_u32 *pixels;
   GLuint px, py;
   if (!gpu || !texObj || !image || x < 0 || y < 0 || width <= 0 || height <= 0 ||
       x + width > (GLint)image->Width2 || y + height > (GLint)image->Height2)
      return;
   slot = find_texture(gpu, texObj, image);
   if (!slot) return;
   if (!(gfx3d_capabilities() & BK_CAP_TEXTURE_REGION_UPLOAD) ||
       !flush_batch(gpu)) {
      destroy_texture_slot(slot);
      return;
   }
   pixels = (bk_u32 *)MALLOC((GLuint)width * (GLuint)height * sizeof(bk_u32));
   if (!pixels) { destroy_texture_slot(slot); return; }
   for (py = 0U; py < (GLuint)height; py++)
      for (px = 0U; px < (GLuint)width; px++) {
         GLchan rgba[4] = {0, 0, 0, 255};
         image->FetchTexel(image, x + (GLint)px, y + (GLint)py, 0, rgba);
         pixels[py * (GLuint)width + px] = color_to_argb(rgba);
      }
   if (!gfx3d_surface_upload_region(slot->surface, pixels, (bk_u32)width,
                                    (bk_u32)x, (bk_u32)y,
                                    (bk_u32)width, (bk_u32)height)) {
      FREE(pixels);
      destroy_texture_slot(slot);
      mark_fallback(gpu, BKMESA_FALLBACK_TEXTURE_UPLOAD);
      return;
   }
   FREE(pixels);
   slot->data = image->Data;
   slot->last_use = ++gpu->texture_clock;
   gpu->stats.texture_region_uploads++;
}

void bkmesa_gpu_texture_deleted(GLcontext *ctx,
                                struct gl_texture_object *texObj)
{
   bkmesa_gpu_texture_changed(ctx, texObj);
}

GLboolean bkmesa_gpu_set_renderer(GLcontext *ctx, GLint renderer)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu || renderer < BKMESA_RENDERER_AUTO ||
       renderer > BKMESA_RENDERER_GPU_STRICT) return GL_FALSE;
   if (renderer == BKMESA_RENDERER_SOFTWARE) {
      (void)resolve_color(gpu);
      gpu->software_locked = GL_TRUE;
   }
   gpu->requested_renderer = renderer;
   if (renderer != BKMESA_RENDERER_GPU_STRICT) gpu->strict_failed = GL_FALSE;
   gpu->last_fallback = BKMESA_FALLBACK_NONE;
   return GL_TRUE;
}

GLint bkmesa_gpu_requested_renderer(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   return gpu ? gpu->requested_renderer : BKMESA_RENDERER_SOFTWARE;
}

GLint bkmesa_gpu_active_renderer(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu && gpu->supported && policy_allows_gpu(gpu))
      return BKMESA_RENDERER_GPU;
   return BKMESA_RENDERER_SOFTWARE;
}

GLboolean bkmesa_gpu_available(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   return gpu && gpu->supported;
}

GLuint bkmesa_gpu_surface(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu || !gpu->supported || !policy_allows_gpu(gpu) ||
       gpu->strict_failed) return 0;
   if (gpu->draw_open) (void)end_draw(gpu);
   return gpu->target;
}

const char *bkmesa_gpu_renderer_name(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu && gpu->supported && policy_allows_gpu(gpu)) {
      const char *name = gfx3d_driver_name();
      return name ? name : "BlesKernOS GFX3D";
   }
   return "Mesa 3.5 software rasterizer";
}

void bkmesa_gpu_get_stats(GLcontext *ctx, bkmesa_gpu_stats *stats)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!stats) return;
   if (gpu) *stats = gpu->stats;
   else MEMSET(stats, 0, sizeof(*stats));
}

void bkmesa_gpu_reset_stats(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (gpu) MEMSET(&gpu->stats, 0, sizeof(gpu->stats));
}

GLint bkmesa_gpu_last_fallback(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   return gpu ? gpu->last_fallback : BKMESA_FALLBACK_DRIVER_UNAVAILABLE;
}

const char *bkmesa_gpu_last_fallback_string(GLcontext *ctx)
{
   return fallback_string(bkmesa_gpu_last_fallback(ctx));
}

GLboolean bkmesa_gpu_strict_failed(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   return gpu ? gpu->strict_failed : GL_TRUE;
}

void bkmesa_gpu_clear_error(GLcontext *ctx)
{
   bkmesa_gpu_context *gpu = lookup(ctx);
   if (!gpu) return;
   gpu->strict_failed = GL_FALSE;
   gpu->last_fallback = BKMESA_FALLBACK_NONE;
   gpu->software_locked = GL_FALSE;
}

#else

GLboolean bkmesa_gpu_attach(GLcontext *ctx) { (void)ctx; return GL_FALSE; }
void bkmesa_gpu_detach(GLcontext *ctx) { (void)ctx; }
void bkmesa_gpu_bind(GLcontext *ctx, void *b, GLint w, GLint h, GLint r,
                     GLenum f, GLboolean y)
{ (void)ctx; (void)b; (void)w; (void)h; (void)r; (void)f; (void)y; }
void bkmesa_gpu_update_state(GLcontext *ctx, GLuint s) { (void)ctx; (void)s; }
void bkmesa_gpu_before_cpu(GLcontext *ctx) { (void)ctx; }
GLboolean bkmesa_gpu_try_clear(GLcontext *ctx, GLbitfield m, GLboolean a,
                               GLint x, GLint y, GLint w, GLint h)
{ (void)ctx; (void)m; (void)a; (void)x; (void)y; (void)w; (void)h; return GL_FALSE; }
GLboolean bkmesa_gpu_resolve(GLcontext *ctx) { (void)ctx; return GL_TRUE; }
void bkmesa_gpu_flush(GLcontext *ctx) { (void)ctx; }
void bkmesa_gpu_finish(GLcontext *ctx) { (void)ctx; }
void bkmesa_gpu_texture_changed(GLcontext *ctx, struct gl_texture_object *o)
{ (void)ctx; (void)o; }
void bkmesa_gpu_texture_region_changed(GLcontext *ctx,
                                       struct gl_texture_object *o,
                                       struct gl_texture_image *i,
                                       GLint x, GLint y,
                                       GLsizei w, GLsizei h)
{ (void)ctx; (void)o; (void)i; (void)x; (void)y; (void)w; (void)h; }
void bkmesa_gpu_texture_deleted(GLcontext *ctx, struct gl_texture_object *o)
{ (void)ctx; (void)o; }
GLboolean bkmesa_gpu_set_renderer(GLcontext *ctx, GLint r)
{ (void)ctx; (void)r; return GL_FALSE; }
GLint bkmesa_gpu_requested_renderer(GLcontext *ctx)
{ (void)ctx; return BKMESA_RENDERER_SOFTWARE; }
GLint bkmesa_gpu_active_renderer(GLcontext *ctx)
{ (void)ctx; return BKMESA_RENDERER_SOFTWARE; }
GLboolean bkmesa_gpu_available(GLcontext *ctx) { (void)ctx; return GL_FALSE; }
GLuint bkmesa_gpu_surface(GLcontext *ctx) { (void)ctx; return 0; }
const char *bkmesa_gpu_renderer_name(GLcontext *ctx)
{ (void)ctx; return "Mesa 3.5 software rasterizer"; }
void bkmesa_gpu_get_stats(GLcontext *ctx, bkmesa_gpu_stats *s)
{ (void)ctx; if (s) MEMSET(s, 0, sizeof(*s)); }
void bkmesa_gpu_reset_stats(GLcontext *ctx) { (void)ctx; }
GLint bkmesa_gpu_last_fallback(GLcontext *ctx)
{ (void)ctx; return BKMESA_FALLBACK_DRIVER_UNAVAILABLE; }
const char *bkmesa_gpu_last_fallback_string(GLcontext *ctx)
{ (void)ctx; return "driver GFX3D no disponible"; }
GLboolean bkmesa_gpu_strict_failed(GLcontext *ctx) { (void)ctx; return GL_TRUE; }
void bkmesa_gpu_clear_error(GLcontext *ctx) { (void)ctx; }

#endif
