#ifndef NSBK_STDLIB_H
#define NSBK_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void abort(void) __attribute__((noreturn));
int abs(int value);
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));
#endif
