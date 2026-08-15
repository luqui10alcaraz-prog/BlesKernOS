#include "process_handle.h"
#include "thread.h"
#include "../include/task.h"
#include "../include/pit.h"
#include "../include/memory.h"

#define PROCESS_HANDLE_BASE 0x71300000U
#define PROCESS_HANDLE_MAX 32U

typedef struct {
    bool used;
    bool thread_handle;
    uint32_t owner_process_id;
    uint32_t target_id;
    uint32_t process_id;
    uint32_t cached_exit_code;
    bool cached_signaled;
} process_handle_record_t;

static process_handle_record_t records[PROCESS_HANDLE_MAX];

static process_handle_record_t *record_from_handle_raw(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    uint32_t slot;
    if (value < PROCESS_HANDLE_BASE || value >= PROCESS_HANDLE_BASE + PROCESS_HANDLE_MAX)
        return NULL;
    slot = value - PROCESS_HANDLE_BASE;
    return records[slot].used ? &records[slot] : NULL;
}

static process_handle_record_t *record_from_handle(void *handle) {
    process_handle_record_t *record = record_from_handle_raw(handle);
    if (!record || record->owner_process_id != task_current_process_id()) return NULL;
    return record;
}

static uint32_t milliseconds_now(void) {
    uint32_t hz = pit_get_frequency_hz();
    if (!hz) return 0U;
    return (uint32_t)(((uint64_t)pit_get_ticks() * 1000U) / hz);
}

static bool record_state(process_handle_record_t *record, bool *active,
                         int32_t *exit_code) {
    bool found;
    uint32_t process_id = 0U;
    if (!record) return false;
    if (record->cached_signaled) {
        if (active) *active = false;
        if (exit_code) *exit_code = (int32_t)record->cached_exit_code;
        return true;
    }
    if (record->thread_handle) {
        found = task_query_pid(record->target_id, active, exit_code, &process_id);
    } else {
        found = task_query_process(record->process_id, active, exit_code, NULL);
    }
    if (!found) {
        record->cached_signaled = true;
        if (active) *active = false;
        if (exit_code) *exit_code = (int32_t)record->cached_exit_code;
        return true;
    }
    if (active && !*active) {
        record->cached_signaled = true;
        if (exit_code) record->cached_exit_code = (uint32_t)*exit_code;
    }
    return true;
}

void *win32_process_handle_create(uint32_t target_id, bool thread_handle) {
    uint32_t process_id = target_id;
    bool active;
    int32_t exit_code;
    if (!target_id) return NULL;
    if (thread_handle && !task_query_pid(target_id, &active, &exit_code, &process_id))
        return NULL;
    for (uint32_t i = 0; i < PROCESS_HANDLE_MAX; i++) {
        process_handle_record_t *record = &records[i];
        if (record->used) continue;
        kmemset(record, 0, sizeof(*record));
        record->used = true;
        record->thread_handle = thread_handle;
        record->owner_process_id = task_current_process_id();
        record->target_id = target_id;
        record->process_id = process_id;
        return (void *)(uintptr_t)(PROCESS_HANDLE_BASE + i);
    }
    return NULL;
}

void *win32_process_handle_open(uint32_t process_id) {
    bool active;
    int32_t exit_code;
    if (!task_query_process(process_id, &active, &exit_code, NULL)) return NULL;
    return win32_process_handle_create(process_id, false);
}

bool win32_process_handle_is_handle(void *handle) {
    return record_from_handle(handle) != NULL;
}

bool win32_process_handle_close(void *handle) {
    process_handle_record_t *record = record_from_handle(handle);
    if (!record) return false;
    kmemset(record, 0, sizeof(*record));
    return true;
}

uint32_t win32_process_handle_try_wait(void *handle) {
    process_handle_record_t *record = record_from_handle(handle);
    bool active = false;
    int32_t exit_code = 0;
    if (!record || !record_state(record, &active, &exit_code)) return WIN32_WAIT_FAILED;
    return active ? WIN32_WAIT_TIMEOUT : WIN32_WAIT_OBJECT_0;
}

uint32_t win32_process_handle_wait(void *handle, uint32_t milliseconds) {
    uint32_t start = milliseconds_now();
    for (;;) {
        uint32_t result = win32_process_handle_try_wait(handle);
        if (result != WIN32_WAIT_TIMEOUT) return result;
        if (milliseconds == 0U) return WIN32_WAIT_TIMEOUT;
        if (milliseconds != WIN32_INFINITE &&
            (uint32_t)(milliseconds_now() - start) >= milliseconds)
            return WIN32_WAIT_TIMEOUT;
        task_sleep(1U);
    }
}

bool win32_process_handle_get_exit_code(void *handle, uint32_t *exit_code) {
    process_handle_record_t *record = record_from_handle(handle);
    bool active = false;
    int32_t status = 0;
    if (!record || !exit_code || !record_state(record, &active, &status)) return false;
    *exit_code = active ? WIN32_STILL_ACTIVE : (uint32_t)status;
    return true;
}

bool win32_process_handle_terminate(void *handle, uint32_t exit_code) {
    process_handle_record_t *record = record_from_handle(handle);
    if (!record || record->thread_handle) return false;
    if (!task_request_exit_process(record->process_id, (int32_t)exit_code)) return false;
    record->cached_exit_code = exit_code;
    return true;
}

uint32_t win32_process_handle_get_id(void *handle) {
    process_handle_record_t *record = record_from_handle(handle);
    if (!record) return 0U;
    return record->thread_handle ? record->target_id : record->process_id;
}

void win32_process_handle_cleanup_process(uint32_t owner_process_id) {
    for (uint32_t i = 0; i < PROCESS_HANDLE_MAX; i++)
        if (records[i].used && records[i].owner_process_id == owner_process_id)
            kmemset(&records[i], 0, sizeof(records[i]));
}
