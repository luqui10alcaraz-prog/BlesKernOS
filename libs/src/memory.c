/*
 * Memory allocator for TinyGL - BlesKernOS port
 *
 * TinyGL calls gl_malloc/gl_zalloc/gl_free when TGL_FEATURE_CUSTOM_MALLOC=1.
 * Keep this allocator small and independent from host libc.
 */

#include "../../kernel/include/memory.h"
#include "zgl.h"

void gl_free(void *p)
{
    if (p) kfree(p);
}

void *gl_malloc(GLint size)
{
    if (size <= 0) return 0;
    return kzalloc((uint32_t)size);
}

void *gl_zalloc(GLint size)
{
    if (size <= 0) return 0;
    return kzalloc((uint32_t)size);
}
