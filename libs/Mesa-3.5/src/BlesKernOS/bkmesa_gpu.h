#ifndef BKMESA_GPU_H
#define BKMESA_GPU_H

#include "glheader.h"
#include "mtypes.h"

#define BKMESA_RENDERER_AUTO 0
#define BKMESA_RENDERER_SOFTWARE 1
#define BKMESA_RENDERER_GPU 2
#define BKMESA_RENDERER_GPU_STRICT 3

typedef struct {
   GLuint frames_started;
   GLuint frames_finished;
   GLuint draw_calls;
   GLuint triangles;
   GLuint lines;
   GLuint points;
   GLuint texture_uploads;
   GLuint uploads;
   GLuint downloads;
   GLuint fallbacks;
   GLuint strict_failures;
   GLuint depth_uploads;
   GLuint depth_downloads;
   GLuint texture_region_uploads;
} bkmesa_gpu_stats;

typedef enum {
   BKMESA_FALLBACK_NONE = 0,
   BKMESA_FALLBACK_DRIVER_UNAVAILABLE,
   BKMESA_FALLBACK_UNSUPPORTED_STATE,
   BKMESA_FALLBACK_SOFTWARE_OPERATION,
   BKMESA_FALLBACK_COLOR_SYNC,
   BKMESA_FALLBACK_DEPTH_SYNC,
   BKMESA_FALLBACK_BEGIN_FAILED,
   BKMESA_FALLBACK_DRAW_FAILED,
   BKMESA_FALLBACK_TEXTURE_UPLOAD,
   BKMESA_FALLBACK_PROJECTIVE_TEXTURE,
   BKMESA_FALLBACK_INTERNAL
} bkmesa_fallback_reason;

GLboolean bkmesa_gpu_attach(GLcontext *ctx);
void bkmesa_gpu_detach(GLcontext *ctx);
void bkmesa_gpu_bind(GLcontext *ctx, void *buffer, GLint width, GLint height,
                     GLint rowlength, GLenum format, GLboolean yup);
void bkmesa_gpu_update_state(GLcontext *ctx, GLuint new_state);
void bkmesa_gpu_before_cpu(GLcontext *ctx);
GLboolean bkmesa_gpu_try_clear(GLcontext *ctx, GLbitfield mask,
                               GLboolean all, GLint x, GLint y,
                               GLint width, GLint height);
GLboolean bkmesa_gpu_resolve(GLcontext *ctx);
void bkmesa_gpu_flush(GLcontext *ctx);
void bkmesa_gpu_finish(GLcontext *ctx);
void bkmesa_gpu_texture_changed(GLcontext *ctx,
                                struct gl_texture_object *texObj);
void bkmesa_gpu_texture_region_changed(GLcontext *ctx,
                                       struct gl_texture_object *texObj,
                                       struct gl_texture_image *image,
                                       GLint x, GLint y,
                                       GLsizei width, GLsizei height);
void bkmesa_gpu_texture_deleted(GLcontext *ctx,
                                struct gl_texture_object *texObj);
GLboolean bkmesa_gpu_set_renderer(GLcontext *ctx, GLint renderer);
GLint bkmesa_gpu_requested_renderer(GLcontext *ctx);
GLint bkmesa_gpu_active_renderer(GLcontext *ctx);
GLboolean bkmesa_gpu_available(GLcontext *ctx);
GLuint bkmesa_gpu_surface(GLcontext *ctx);
const char *bkmesa_gpu_renderer_name(GLcontext *ctx);
void bkmesa_gpu_get_stats(GLcontext *ctx, bkmesa_gpu_stats *stats);
void bkmesa_gpu_reset_stats(GLcontext *ctx);
GLint bkmesa_gpu_last_fallback(GLcontext *ctx);
const char *bkmesa_gpu_last_fallback_string(GLcontext *ctx);
GLboolean bkmesa_gpu_strict_failed(GLcontext *ctx);
void bkmesa_gpu_clear_error(GLcontext *ctx);

#endif
