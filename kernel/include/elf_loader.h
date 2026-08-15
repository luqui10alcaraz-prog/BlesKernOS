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
void elf_process_cleanup(uint32_t process_id);
bool elf_process_address(uint32_t process_id, uint32_t address,
                         uint32_t *base_out, uint32_t *offset_out);
const char *elf_last_error(void);
uint64_t elf_user_api_dispatch(uint32_t token, const uint32_t *arguments,
                               bool *valid, uint32_t *callee_cleanup);
uint32_t elf_user_api_thunk(const char *name, uint32_t target);
const char *elf_last_user_api_name(void);
bool elf_user_api_fault_info(const char **name_out, uint32_t *target_out,
                             uint32_t *token_out);
void elf_user_api_fault_clear(void);
/* Identifica una puerta de llamada Ring 3 a partir de su direccion.  Se usa
 * exclusivamente para diagnosticos de excepciones: las puertas viven fuera
 * de la imagen ELF que las invoco, por lo que no deben atribuirse a un offset
 * del ejecutable. */
bool elf_user_api_thunk_info(uint32_t address, const char **name_out,
                             uint32_t *target_out, uint32_t *token_out);

/* Labels exported by api_call.asm for tightly-scoped #GP recovery. */
extern uint8_t elf_api_call_raw_after_target[];
extern uint8_t elf_api_call_raw_store_cleanup[];
extern uint8_t elf_api_call_raw_end[];

#endif
