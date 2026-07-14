#ifndef BLESKERNOS_WIN32_PROCESS_H
#define BLESKERNOS_WIN32_PROCESS_H

#include "../include/types.h"

#define WIN32_PROCESS_HEAP_HANDLE 0x70001000U
#define WIN32_TLS_SLOT_COUNT 64U
#define WIN32_TLS_OUT_OF_INDEXES 0xFFFFFFFFU

bool win32_process_create(uint32_t pid, uint32_t image_base,
                          const char *image_path,
                          const char *command_line);
void win32_process_destroy(uint32_t pid);

/* TEB y TLS privados para hilos secundarios del mismo proceso Win32. */
bool win32_process_thread_create(uint32_t process_id, uint32_t tid);
void win32_process_thread_destroy(uint32_t tid);

void *win32_process_current_teb(void);
void *win32_process_current_peb(void);
const char *win32_process_current_command_line(void);
const char *win32_process_current_image_path(void);

uint32_t win32_process_get_last_error(void);
void win32_process_set_last_error(uint32_t error);

/* Cadena SEH clasica almacenada en TEB+0x00 (FS:[0] en x86). */
uint32_t win32_process_get_exception_list(void);
bool win32_process_set_exception_list(uint32_t head);

/* TLS Win32. Los indices son globales al proceso y los valores viven en la
 * tabla apuntada por TEB+0x2c (FS:[0x2c] en x86). */
uint32_t win32_process_tls_alloc(void);
bool win32_process_tls_free(uint32_t index);
bool win32_process_tls_set(uint32_t index, void *value);
void *win32_process_tls_get(uint32_t index, bool *valid);

/* TLS estatico descrito por IMAGE_TLS_DIRECTORY32. */
bool win32_process_tls_install(uint32_t pid, void *module_base,
                               const void *template_data,
                               uint32_t template_size,
                               uint32_t zero_fill,
                               uint32_t *address_of_index);
bool win32_process_tls_uninstall(uint32_t pid, void *module_base);

#endif
