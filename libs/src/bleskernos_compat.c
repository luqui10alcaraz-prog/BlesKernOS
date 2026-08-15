/* TinyGL -> BlesKernOS freestanding compatibility glue. */

#include <stdint.h>

static const char *tinygl_last_fatal_error;

/* Do not depend on the private kprintf symbol from an external library.
 * TinyGL callers can inspect the last fatal message while the renderer
 * remains loadable in normal native applications. */
void gl_fatal_error(const char *text, ...)
{
    tinygl_last_fatal_error = text;
}

const char *bk_tinygl_last_fatal_error(void)
{
    return tinygl_last_fatal_error;
}

void bk_tinygl_clear_fatal_error(void)
{
    tinygl_last_fatal_error = 0;
}

int isfinite(double x)
{
    union {
        double d;
        uint64_t u;
    } value;

    value.d = x;
    return (int)(((value.u >> 52) & 0x7ffULL) != 0x7ffULL);
}
