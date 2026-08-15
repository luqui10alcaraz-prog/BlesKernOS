#ifndef BLESKERNOS_WIN32_SYNC_H
#define BLESKERNOS_WIN32_SYNC_H

#include "win32.h"

#define WIN32_WAIT_OBJECT_0    0x00000000U
#define WIN32_WAIT_ABANDONED_0 0x00000080U
#define WIN32_WAIT_TIMEOUT     0x00000102U
#define WIN32_WAIT_FAILED      0xFFFFFFFFU
#define WIN32_INFINITE         0xFFFFFFFFU
#define WIN32_MAXIMUM_WAIT_OBJECTS 64U

void *win32_sync_create_event(bool manual_reset, bool initial_state);
bool win32_sync_set_event(void *handle);
bool win32_sync_reset_event(void *handle);

void *win32_sync_create_mutex(bool initial_owner);
bool win32_sync_release_mutex(void *handle);

void *win32_sync_create_semaphore(int32_t initial_count,
                                  int32_t maximum_count);
bool win32_sync_release_semaphore(void *handle, int32_t release_count,
                                  int32_t *previous_count);

bool win32_sync_is_handle(void *handle);
bool win32_sync_close_handle(void *handle);

/* Consulta/adquisicion no bloqueante. Devuelve WAIT_TIMEOUT si el objeto aun
 * no esta senalado y WAIT_FAILED si el handle no pertenece a esta capa. */
uint32_t win32_sync_try_wait(void *handle, uint32_t tid, bool consume);
uint32_t win32_sync_wait(void *handle, uint32_t milliseconds);

void win32_sync_thread_exit(uint32_t tid);
void win32_sync_process_destroy(uint32_t process_id);

void win32_critical_section_initialize(void *critical_section,
                                       uint32_t spin_count);
void win32_critical_section_delete(void *critical_section);
void win32_critical_section_enter(void *critical_section);
bool win32_critical_section_try_enter(void *critical_section);
bool win32_critical_section_leave(void *critical_section);
uint32_t win32_critical_section_set_spin(void *critical_section,
                                         uint32_t spin_count);

int32_t win32_interlocked_increment(volatile int32_t *value);
int32_t win32_interlocked_decrement(volatile int32_t *value);
int32_t win32_interlocked_exchange(volatile int32_t *target, int32_t value);
int32_t win32_interlocked_exchange_add(volatile int32_t *target,
                                       int32_t value);
int32_t win32_interlocked_compare_exchange(volatile int32_t *target,
                                            int32_t exchange,
                                            int32_t compare);

#endif
