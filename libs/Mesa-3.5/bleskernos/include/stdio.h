#ifndef BK_MESA_STDIO_H
#define BK_MESA_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct FILE FILE;
extern FILE *stdin; extern FILE *stdout; extern FILE *stderr;
FILE *fopen(const char *path, const char *mode); int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fflush(FILE *stream); int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list args);
int printf(const char *format, ...); int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
#endif
