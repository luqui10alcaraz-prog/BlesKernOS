#ifndef BLESKERNOS_WIN32_PROCESS_HANDLE_H
#define BLESKERNOS_WIN32_PROCESS_HANDLE_H

#include "../include/types.h"

void *win32_process_handle_create(uint32_t target_id, bool thread_handle);
void *win32_process_handle_open(uint32_t process_id);
bool win32_process_handle_is_handle(void *handle);
bool win32_process_handle_close(void *handle);
uint32_t win32_process_handle_wait(void *handle, uint32_t milliseconds);
uint32_t win32_process_handle_try_wait(void *handle);
bool win32_process_handle_get_exit_code(void *handle, uint32_t *exit_code);
bool win32_process_handle_terminate(void *handle, uint32_t exit_code);
uint32_t win32_process_handle_get_id(void *handle);
void win32_process_handle_cleanup_process(uint32_t owner_process_id);

#endif
