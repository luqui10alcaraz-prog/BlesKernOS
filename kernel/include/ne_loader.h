#ifndef NE_LOADER_H
#define NE_LOADER_H

#include "types.h"
#include "idt.h"

bool ne_is_image(const uint8_t *file, uint32_t size);
bool ne_execute_image(const uint8_t *file, uint32_t size, const char *path);
bool ne_dump_image(const uint8_t *file, uint32_t size, const char *path);
registers_t *ne_win16_syscall(registers_t *regs);
void ne_win16_cleanup_process(uint32_t process_id);
const char *ne_last_error(void);

#endif
