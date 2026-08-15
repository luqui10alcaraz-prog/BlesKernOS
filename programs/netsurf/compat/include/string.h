#ifndef NSBK_STRING_H
#define NSBK_STRING_H
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
void *memchr(const void *memory, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strncpy(char *destination, const char *source, size_t count);
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t count);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *text, const char *needle);
char *strdup(const char *text);
#endif
