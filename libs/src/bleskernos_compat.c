/*
 * TinyGL -> BlesKernOS compatibility glue.
 *
 * This file fills small libc/libm symbols that TinyGL expects while the
 * BlesKernOS libc is still minimal.
 */

#include <stdarg.h>
#include <stdint.h>

#include "../../kernel/include/vga.h"

/* TinyGL calls this on unrecoverable internal errors. For now we log and return.
   Later you can replace this with panic() if you want hard failures. */
void gl_fatal_error(const char *text, ...)
{
    (void)text;
    kprintf("[TinyGL] fatal error\n");
}

/* C99 normally provides isfinite() as a macro. TinyGL currently calls it like a
   function, so provide a real freestanding implementation. */
int isfinite(double x)
{
    union {
        double d;
        uint64_t u;
    } v;

    v.d = x;

    /* IEEE-754 double: exponent bits all 1 => Inf or NaN */
    return (int)(((v.u >> 52) & 0x7ffULL) != 0x7ffULL);
}
