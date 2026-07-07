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

/* Minimal floor() good enough for viewport/image math. */
double floor(double x)
{
    int i = (int)x;
    if ((double)i > x) {
        i--;
    }
    return (double)i;
}

/* TinyGL mainly needs pow() for lighting/specular exponents.
   This handles integer-ish exponents well, which is enough for gears
   shininess values like 5. */
double pow(double base, double exp)
{
    int n = (int)exp;
    double result = 1.0;
    int negative = 0;

    if (exp == 0.0) {
        return 1.0;
    }

    if (base == 0.0) {
        return 0.0;
    }

    if (n < 0) {
        negative = 1;
        n = -n;
    }

    /* If exponent is not close to an integer, use a cheap fallback.
       Better approximation can come later with log/exp. */
    if ((double)n != (negative ? -exp : exp)) {
        return base;
    }

    while (n > 0) {
        if (n & 1) {
            result *= base;
        }
        base *= base;
        n >>= 1;
    }

    return negative ? (1.0 / result) : result;
}
