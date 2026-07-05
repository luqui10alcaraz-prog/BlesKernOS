#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"

struct gui_desktop;

bool elf_execute_program(const char *path, struct gui_desktop *desktop);
const char *elf_last_error(void);

#endif
