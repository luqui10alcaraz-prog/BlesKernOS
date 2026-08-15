#ifndef BOOT_MODE_H
#define BOOT_MODE_H

#include "types.h"

#define BLES_BOOT_MODE_ADDR       0x000006F0U
#define BLES_BOOT_INSTALLER_MAGIC 0x54534E49U /* bytes: I N S T */

static inline bool boot_mode_is_installer(void) {
    uint32_t value;
    __asm__ volatile ("movl (%1), %0"
                      : "=r"(value)
                      : "r"(BLES_BOOT_MODE_ADDR)
                      : "memory");
    return value == BLES_BOOT_INSTALLER_MAGIC;
}

#endif
