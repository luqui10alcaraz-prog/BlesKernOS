#ifndef NSBK_STDIO_H
#define NSBK_STDIO_H
#include <stddef.h>
int printf(const char *format, ...);
int snprintf(char *buffer, size_t capacity, const char *format, ...);
#endif
