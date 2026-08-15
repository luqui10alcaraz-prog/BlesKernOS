#ifndef BK_MESA_STDLIB_H
#define BK_MESA_STDLIB_H
#include <stddef.h>
void *malloc(size_t size); void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size); void free(void *pointer);
void abort(void) __attribute__((noreturn)); void exit(int status) __attribute__((noreturn));
int abs(int value); int atoi(const char *text); double atof(const char *text);
char *getenv(const char *name);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
#endif
