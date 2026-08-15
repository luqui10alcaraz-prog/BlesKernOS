#ifndef KERNEL_SYS_STAT_H
#define KERNEL_SYS_STAT_H

#include "types.h"

struct stat {
    uint32_t st_size;
};

int mkdir(const char *path, int mode);

#endif
