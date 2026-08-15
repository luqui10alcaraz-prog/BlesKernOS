/* $Id: osmesa.h,v 1.6 2000/09/08 16:41:38 brianp Exp $ */

/*
 * Mesa 3-D graphics library
 * Version:  3.5
 *
 * Copyright (C) 1999-2000  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


/*
 * Mesa Off-Screen rendering interface.
 *
 * This is an operating system and window system independent interface to
 * Mesa which allows one to render images into a client-supplied buffer in
 * main memory.  Such images may manipulated or saved in whatever way the
 * client wants.
 *
 * These are the API functions:
 *   OSMesaCreateContext - create a new Off-Screen Mesa rendering context
 *   OSMesaMakeCurrent - bind an OSMesaContext to a client's image buffer
 *                       and make the specified context the current one.
 *   OSMesaDestroyContext - destroy an OSMesaContext
 *   OSMesaGetCurrentContext - return thread's current context ID
 *   OSMesaPixelStore - controls how pixels are stored in image buffer
 *   OSMesaGetIntegerv - return OSMesa state parameters
 *
 *
 * The limits on the width and height of an image buffer are MAX_WIDTH and
 * MAX_HEIGHT as defined in Mesa/src/config.h.  Defaults are 1280 and 1024.
 * You can increase them as needed but beware that many temporary arrays in
 * Mesa are dimensioned by MAX_WIDTH or MAX_HEIGHT.
 */


#ifndef OSMESA_H
#define OSMESA_H


#ifdef __cplusplus
extern "C" {
#endif


#include <GL/gl.h>


#define OSMESA_MAJOR_VERSION 3
#define OSMESA_MINOR_VERSION 5



/*
 * Values for the format parameter of OSMesaCreateContext()
 * New in version 2.0.
 */
#define OSMESA_COLOR_INDEX	GL_COLOR_INDEX
#define OSMESA_RGBA		GL_RGBA
#define OSMESA_BGRA		0x1
#define OSMESA_ARGB		0x2
#define OSMESA_RGB		GL_RGB
#define OSMESA_BGR		0x4


/*
 * OSMesaPixelStore() parameters:
 * New in version 2.0.
 */
#define OSMESA_ROW_LENGTH	0x10
#define OSMESA_Y_UP		0x11


/*
 * Accepted by OSMesaGetIntegerv:
 */
#define OSMESA_WIDTH		0x20
#define OSMESA_HEIGHT		0x21
#define OSMESA_FORMAT		0x22
#define OSMESA_TYPE		0x23


typedef struct osmesa_context *OSMesaContext;


/* BlesKernOS renderer selection.  These extensions are available only in the
 * BlesKernOS build of Mesa 3.5; other Mesa platforms may omit them. */
#define OSMESA_BLESK_RENDERER_AUTO      0
#define OSMESA_BLESK_RENDERER_SOFTWARE  1
#define OSMESA_BLESK_RENDERER_GPU       2
#define OSMESA_BLESK_RENDERER_GPU_STRICT 3

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
} OSMesaBleskStats;

typedef enum {
   OSMESA_BLESK_FALLBACK_NONE = 0,
   OSMESA_BLESK_FALLBACK_DRIVER_UNAVAILABLE,
   OSMESA_BLESK_FALLBACK_UNSUPPORTED_STATE,
   OSMESA_BLESK_FALLBACK_SOFTWARE_OPERATION,
   OSMESA_BLESK_FALLBACK_COLOR_SYNC,
   OSMESA_BLESK_FALLBACK_DEPTH_SYNC,
   OSMESA_BLESK_FALLBACK_BEGIN_FAILED,
   OSMESA_BLESK_FALLBACK_DRAW_FAILED,
   OSMESA_BLESK_FALLBACK_TEXTURE_UPLOAD,
   OSMESA_BLESK_FALLBACK_PROJECTIVE_TEXTURE,
   OSMESA_BLESK_FALLBACK_INTERNAL
} OSMesaBleskFallback;

GLAPI GLboolean GLAPIENTRY
OSMesaBleskSetRenderer(OSMesaContext ctx, GLint renderer);
GLAPI GLint GLAPIENTRY
OSMesaBleskGetRenderer(OSMesaContext ctx);
GLAPI GLint GLAPIENTRY
OSMesaBleskGetActiveRenderer(OSMesaContext ctx);
GLAPI GLboolean GLAPIENTRY
OSMesaBleskGPUAvailable(OSMesaContext ctx);
GLAPI GLboolean GLAPIENTRY
OSMesaBleskResolve(OSMesaContext ctx);
GLAPI GLuint GLAPIENTRY
OSMesaBleskGPUSurface(OSMesaContext ctx);
GLAPI const char * GLAPIENTRY
OSMesaBleskRendererName(OSMesaContext ctx);
GLAPI void GLAPIENTRY
OSMesaBleskGetStats(OSMesaContext ctx, OSMesaBleskStats *stats);
GLAPI void GLAPIENTRY
OSMesaBleskResetStats(OSMesaContext ctx);
GLAPI GLint GLAPIENTRY
OSMesaBleskLastFallback(OSMesaContext ctx);
GLAPI const char * GLAPIENTRY
OSMesaBleskLastFallbackString(OSMesaContext ctx);
GLAPI GLboolean GLAPIENTRY
OSMesaBleskStrictFailed(OSMesaContext ctx);
GLAPI void GLAPIENTRY
OSMesaBleskClearError(OSMesaContext ctx);


#if defined(__BEOS__) || defined(__QUICKDRAW__)
#pragma export on
#endif


/*
 * Create an Off-Screen Mesa rendering context.  The only attribute needed is
 * an RGBA vs Color-Index mode flag.
 *
 * Input:  format - one of OSMESA_COLOR_INDEX, OSMESA_RGBA, OSMESA_BGRA,
 *                  OSMESA_ARGB, OSMESA_RGB, or OSMESA_BGR.
 *         sharelist - specifies another OSMesaContext with which to share
 *                     display lists.  NULL indicates no sharing.
 * Return:  an OSMesaContext or 0 if error
 */
GLAPI OSMesaContext GLAPIENTRY
OSMesaCreateContext( GLenum format, OSMesaContext sharelist );



/*
 * Create an Off-Screen Mesa rendering context and specify desired
 * size of depth buffer, stencil buffer and accumulation buffer.
 * If you specify zero for depthBits, stencilBits, accumBits you
 * can save some memory.
 *
 * New in Mesa 3.5
 */
GLAPI OSMesaContext GLAPIENTRY
OSMesaCreateContextExt( GLenum format, GLint depthBits, GLint stencilBits,
                        GLint accumBits, OSMesaContext sharelist);


/*
 * Destroy an Off-Screen Mesa rendering context.
 *
 * Input:  ctx - the context to destroy
 */
GLAPI void GLAPIENTRY
OSMesaDestroyContext( OSMesaContext ctx );



/*
 * Bind an OSMesaContext to an image buffer.  The image buffer is just a
 * block of memory which the client provides.  Its size must be at least
 * as large as width*height*sizeof(type).  Its address should be a multiple
 * of 4 if using RGBA mode.
 *
 * Image data is stored in the order of glDrawPixels:  row-major order
 * with the lower-left image pixel stored in the first array position
 * (ie. bottom-to-top).
 *
 * Since the only type initially supported is GL_UNSIGNED_BYTE, if the
 * context is in RGBA mode, each pixel will be stored as a 4-byte RGBA
 * value.  If the context is in color indexed mode, each pixel will be
 * stored as a 1-byte value.
 *
 * If the context's viewport hasn't been initialized yet, it will now be
 * initialized to (0,0,width,height).
 *
 * Input:  ctx - the rendering context
 *         buffer - the image buffer memory
 *         type - data type for pixel components, only GL_UNSIGNED_BYTE
 *                supported now
 *         width, height - size of image buffer in pixels, at least 1
 * Return:  GL_TRUE if success, GL_FALSE if error because of invalid ctx,
 *          invalid buffer address, type!=GL_UNSIGNED_BYTE, width<1, height<1,
 *          width>internal limit or height>internal limit.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaMakeCurrent( OSMesaContext ctx, void *buffer, GLenum type,
                   GLsizei width, GLsizei height );




/*
 * Return the current Off-Screen Mesa rendering context handle.
 */
GLAPI OSMesaContext GLAPIENTRY
OSMesaGetCurrentContext( void );



/*
 * Set pixel store/packing parameters for the current context.
 * This is similar to glPixelStore.
 * Input:  pname - OSMESA_ROW_LENGTH
 *                    specify actual pixels per row in image buffer
 *                    0 = same as image width (default)
 *                 OSMESA_Y_UP
 *                    zero = Y coordinates increase downward
 *                    non-zero = Y coordinates increase upward (default)
 *         value - the value for the parameter pname
 *
 * New in version 2.0.
 */
GLAPI void GLAPIENTRY
OSMesaPixelStore( GLint pname, GLint value );



/*
 * Return an integer value like glGetIntegerv.
 * Input:  pname -
 *                 OSMESA_WIDTH  return current image width
 *                 OSMESA_HEIGHT  return current image height
 *                 OSMESA_FORMAT  return image format
 *                 OSMESA_TYPE  return color component data type
 *                 OSMESA_ROW_LENGTH return row length in pixels
 *                 OSMESA_Y_UP returns 1 or 0 to indicate Y axis direction
 *         value - pointer to integer in which to return result.
 */
GLAPI void GLAPIENTRY
OSMesaGetIntegerv( GLint pname, GLint *value );



/*
 * Return the depth buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          bytesPerValue - bytes per depth value (2 or 4)
 *          buffer - pointer to depth buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 *
 * New in Mesa 2.4.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaGetDepthBuffer( OSMesaContext c, GLint *width, GLint *height,
                      GLint *bytesPerValue, void **buffer );



/*
 * Return the color buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          format - buffer format (OSMESA_FORMAT)
 *          buffer - pointer to depth buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 *
 * New in Mesa 3.3.
 */
GLAPI GLboolean GLAPIENTRY
OSMesaGetColorBuffer( OSMesaContext c, GLint *width, GLint *height,
                      GLint *format, void **buffer );



#if defined(__BEOS__) || defined(__QUICKDRAW__)
#pragma export off
#endif


#ifdef __cplusplus
}
#endif


#endif
