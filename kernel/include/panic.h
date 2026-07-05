#ifndef PANIC_H
#define PANIC_H

#include "types.h"

void panic_show(const char *message, uint32_t interrupt,
                uint32_t error_code, uint32_t address) NORETURN;

#endif
