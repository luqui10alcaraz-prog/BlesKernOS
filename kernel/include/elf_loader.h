#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "types.h"

struct gui_desktop;

bool elf_execute_program(const char *path, struct gui_desktop *desktop);
bool elf_execute_program_ex(const char *path, struct gui_desktop *desktop,
                            const char *launch_arg);
int elf_spawn_program_ex(const char *path, struct gui_desktop *desktop,
                         const char *launch_arg);
bool elf_load_resident(const char *path, const char *entry_symbol,
                       void **image_out, void **entry_out);
void elf_release_image(void *image);
const char *elf_last_error(void);
uint64_t elf_user_api_dispatch(uint32_t token, const uint32_t *arguments,
                               bool *valid, uint32_t *callee_cleanup);
uint32_t elf_user_api_thunk(const char *name, uint32_t target);

#endif
