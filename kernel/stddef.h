#ifndef KERNEL_STDDEF_H
#define KERNEL_STDDEF_H

#include "include/types.h"

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
