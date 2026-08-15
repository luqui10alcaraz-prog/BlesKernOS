/* Small freestanding compatibility layer used only by Mesa 3.5. */
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
extern void exit(int status);

void abort(void)
{
    exit(-1);
    for (;;) { }
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int sprintf(char *buffer, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = vsnprintf(buffer, (size_t)-1, format, args);
    va_end(args);
    return result;
}

char *strcpy(char *destination, const char *source)
{
    char *start = destination;
    while ((*destination++ = *source++) != '\0') { }
    return start;
}

char *strcat(char *destination, const char *source)
{
    char *start = destination;
    while (*destination) destination++;
    while ((*destination++ = *source++) != '\0') { }
    return start;
}

int isalnum(int c) { return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')); }
int isalpha(int c) { return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')); }
int iscntrl(int c) { return ((unsigned)c < 32U || c == 127); }
int isdigit(int c) { return (c >= '0' && c <= '9'); }
int isgraph(int c) { return (c > 32 && c < 127); }
int islower(int c) { return (c >= 'a' && c <= 'z'); }
int isprint(int c) { return (c >= 32 && c < 127); }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }
int isupper(int c) { return (c >= 'A' && c <= 'Z'); }
int isxdigit(int c) { return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }


static void bk_swap_bytes(unsigned char *left, unsigned char *right, size_t size)
{
    while (size--) {
        unsigned char value = *left;
        *left++ = *right;
        *right++ = value;
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *))
{
    unsigned char *bytes = (unsigned char *)base;
    size_t gap;
    size_t i;

    if (!bytes || !compare || !size || count < 2U) return;
    for (gap = count / 2U; gap > 0U; gap /= 2U) {
        for (i = gap; i < count; ++i) {
            size_t j = i;
            while (j >= gap &&
                   compare(bytes + (j - gap) * size, bytes + j * size) > 0) {
                bk_swap_bytes(bytes + (j - gap) * size, bytes + j * size, size);
                j -= gap;
            }
        }
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *))
{
    size_t low = 0U;
    size_t high = count;
    const unsigned char *bytes = (const unsigned char *)base;

    if (!key || !bytes || !compare || !size) return NULL;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        const void *item = bytes + middle * size;
        int order = compare(key, item);
        if (order < 0) high = middle;
        else if (order > 0) low = middle + 1U;
        else return (void *)item;
    }
    return NULL;
}

double log(double x)
{
    const double ln2 = 0.69314718055994530942;
    double y;
    double y2;
    double term;
    double sum;
    int exponent = 0;
    int divisor;

    if (x <= 0.0) return -1.0e308;
    while (x >= 1.4142135623730951) { x *= 0.5; ++exponent; }
    while (x < 0.7071067811865476) { x *= 2.0; --exponent; }

    y = (x - 1.0) / (x + 1.0);
    y2 = y * y;
    term = y;
    sum = 0.0;
    for (divisor = 1; divisor <= 29; divisor += 2) {
        sum += term / (double)divisor;
        term *= y2;
    }
    return 2.0 * sum + (double)exponent * ln2;
}

/* Sufficient for Mesa's fog tables; avoids requiring a host libm. */
double exp(double x)
{
    const double ln2 = 0.69314718055994530942;
    double term = 1.0;
    double sum = 1.0;
    int exponent;
    int i;

    if (x < -709.0) return 0.0;
    if (x > 709.0) x = 709.0;

    exponent = (int)(x / ln2);
    x -= (double)exponent * ln2;
    for (i = 1; i <= 18; ++i) {
        term *= x / (double)i;
        sum += term;
    }
    while (exponent > 0) { sum *= 2.0; --exponent; }
    while (exponent < 0) { sum *= 0.5; ++exponent; }
    return sum;
}
