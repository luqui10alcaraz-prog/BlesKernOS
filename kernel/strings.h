#ifndef KERNEL_STRINGS_H
#define KERNEL_STRINGS_H

#include "include/types.h"

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

#endif
