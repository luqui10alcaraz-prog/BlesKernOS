#ifndef BLESKERNOS_WIN32_THREAD_H
#define BLESKERNOS_WIN32_THREAD_H

#include "sync.h"

#define WIN32_STILL_ACTIVE  259U

typedef uint32_t (WIN32_API *win32_thread_start_t)(void *parameter);

void *win32_thread_create(uint32_t stack_size,
                          win32_thread_start_t start,
                          void *parameter,
                          uint32_t creation_flags,
                          uint32_t *thread_id);
void win32_thread_exit(uint32_t exit_code) NORETURN;
uint32_t win32_thread_wait(void *handle, uint32_t milliseconds);
uint32_t win32_thread_try_wait(void *handle);
bool win32_thread_get_exit_code(void *handle, uint32_t *exit_code);
bool win32_thread_close_handle(void *handle);
uint32_t win32_thread_get_id(void *handle);
bool win32_thread_is_handle(void *handle);

#endif
