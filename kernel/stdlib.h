#ifndef KERNEL_STDLIB_H
#define KERNEL_STDLIB_H

#include "include/types.h"

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void exit(int status) NORETURN;
int abs(int value);
int atoi(const char *s);
double atof(const char *s);
char *getenv(const char *name);
int system(const char *command);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);

#endif
