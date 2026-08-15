#ifndef KERNEL_STDIO_H
#define KERNEL_STDIO_H

#include "include/types.h"
#include "stdarg.h"

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);

int printf(const char *fmt, ...);
void kprintf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list args);
int snprintf(char *buffer, size_t size, const char *fmt, ...);
int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args);
int sscanf(const char *buffer, const char *fmt, ...);
int putchar(int c);
int puts(const char *s);

void libc_set_exit_handler(void (*handler)(int status));

#endif
