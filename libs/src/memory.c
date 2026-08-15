/*
 * Memory allocator for TinyGL - BlesKernOS user-space port.
 *
 * Keep TinyGL independent from private kernel allocation symbols. Native
 * applications resolve malloc/free through the public ET_REL runtime.
 */

#include "zgl.h"

extern void *malloc(unsigned int size);
extern void free(void *pointer);

static void tinygl_zero(void *pointer, unsigned int size)
{
    unsigned char *bytes = (unsigned char *)pointer;
    while (size--) *bytes++ = 0;
}

void gl_free(void *pointer)
{
    if (pointer) free(pointer);
}

void *gl_malloc(GLint size)
{
    if (size <= 0) return 0;
    return malloc((unsigned int)size);
}

void *gl_zalloc(GLint size)
{
    void *pointer;
    if (size <= 0) return 0;
    pointer = malloc((unsigned int)size);
    if (pointer) tinygl_zero(pointer, (unsigned int)size);
    return pointer;
}
