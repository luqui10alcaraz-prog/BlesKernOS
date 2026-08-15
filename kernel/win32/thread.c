#include "thread.h"
#include "process.h"
#include "sync.h"
#include "../include/pe_loader.h"
#include "../include/task.h"
#include "../include/pit.h"
#include "../include/memory.h"
#include "../stdio.h"

#define WIN32_THREAD_HANDLE_BASE 0x72000000U
#define WIN32_CREATE_SUSPENDED 0x00000004U

typedef struct {
    bool used;
    bool finished;
    bool handle_open;
    uint32_t tid;
    uint32_t process_id;
    uint32_t exit_code;
    win32_thread_start_t start;
    void *parameter;
} win32_thread_record_t;

static win32_thread_record_t thread_records[TASK_MAX];

static uint32_t handle_value(uint32_t slot) {
    return WIN32_THREAD_HANDLE_BASE + slot;
}

static win32_thread_record_t *record_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    uint32_t slot;
    if (value < WIN32_THREAD_HANDLE_BASE ||
        value >= WIN32_THREAD_HANDLE_BASE + TASK_MAX)
        return NULL;
    slot = value - WIN32_THREAD_HANDLE_BASE;
    return thread_records[slot].used && thread_records[slot].handle_open
        ? &thread_records[slot] : NULL;
}

static win32_thread_record_t *record_from_tid(uint32_t tid) {
    for (uint32_t i = 0; i < TASK_MAX; i++)
        if (thread_records[i].used && thread_records[i].tid == tid)
            return &thread_records[i];
    return NULL;
}

static uint32_t milliseconds_now(void) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t scaled;
    if (!hz) return 0U;
    scaled = (uint64_t)pit_get_ticks() * 1000U;
    return (uint32_t)(scaled / hz);
}

static void finish_current_thread(win32_thread_record_t *record,
                                  uint32_t exit_code) NORETURN;

static void finish_current_thread(win32_thread_record_t *record,
                                  uint32_t exit_code) {
    uint32_t tid = task_current_pid();

    pe_win32_thread_detach();
    win32_sync_thread_exit(tid);
    win32_process_thread_destroy(tid);
    kprintf("[WIN32] thread TID=%u exit=%u\n", tid, exit_code);

    task_preempt_disable();
    if (record && record->used) {
        record->exit_code = exit_code;
        record->finished = true;
        record->start = NULL;
        record->parameter = NULL;
        if (!record->handle_open) kmemset(record, 0, sizeof(*record));
    }
    task_preempt_enable();
    task_exit();
}

static void win32_thread_bootstrap(void *argument) {
    win32_thread_record_t *record = (win32_thread_record_t *)argument;
    uint32_t result = 0U;
    uint32_t teb = 0U;
    uint32_t peb = 0U;

    if (!record || !record->used || !record->start)
        finish_current_thread(record, 0U);

    __asm__ volatile ("movl %%fs:0x18, %0" : "=r"(teb));
    __asm__ volatile ("movl %%fs:0x30, %0" : "=r"(peb));
    kprintf("[WIN32] thread PID=%u TID=%u TEB=%x PEB=%x\n",
            record->process_id, record->tid, teb, peb);
    pe_win32_thread_attach();
    result = record->start(record->parameter);
    finish_current_thread(record, result);
}

void *win32_thread_create(uint32_t stack_size UNUSED,
                          win32_thread_start_t start,
                          void *parameter,
                          uint32_t creation_flags,
                          uint32_t *thread_id) {
    win32_thread_record_t *record = NULL;
    uint32_t slot = 0U;
    uint32_t process_id;
    int tid;

    if (thread_id) *thread_id = 0U;
    if (!start || (creation_flags & WIN32_CREATE_SUSPENDED)) return NULL;

    task_preempt_disable();
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (thread_records[i].used) continue;
        record = &thread_records[i];
        slot = i;
        kmemset(record, 0, sizeof(*record));
        record->used = true;
        record->handle_open = true;
        record->start = start;
        record->parameter = parameter;
        break;
    }
    if (!record) {
        task_preempt_enable();
        return NULL;
    }

    process_id = task_current_process_id();
    record->process_id = process_id;
    tid = task_create_user_thread("win32-thread", win32_thread_bootstrap,
                                  record, process_id);
    if (tid < 0) {
        kmemset(record, 0, sizeof(*record));
        task_preempt_enable();
        return NULL;
    }
    record->tid = (uint32_t)tid;
    if (!win32_process_thread_create(process_id, (uint32_t)tid)) {
        task_request_exit((uint32_t)tid);
        kmemset(record, 0, sizeof(*record));
        task_preempt_enable();
        return NULL;
    }
    task_preempt_enable();

    if (thread_id) *thread_id = (uint32_t)tid;
    return (void *)(uintptr_t)handle_value(slot);
}

void win32_thread_exit(uint32_t exit_code) {
    finish_current_thread(record_from_tid(task_current_pid()), exit_code);
}

uint32_t win32_thread_try_wait(void *handle) {
    win32_thread_record_t *record;
    uint32_t result;
    task_preempt_disable();
    record = record_from_handle(handle);
    result = !record ? WIN32_WAIT_FAILED :
        (record->finished ? WIN32_WAIT_OBJECT_0 : WIN32_WAIT_TIMEOUT);
    task_preempt_enable();
    return result;
}

uint32_t win32_thread_wait(void *handle, uint32_t milliseconds) {
    uint32_t start = milliseconds_now();

    for (;;) {
        uint32_t result = win32_thread_try_wait(handle);
        if (result != WIN32_WAIT_TIMEOUT) return result;

        if (milliseconds == 0U) return WIN32_WAIT_TIMEOUT;
        if (milliseconds != WIN32_INFINITE &&
            (uint32_t)(milliseconds_now() - start) >= milliseconds)
            return WIN32_WAIT_TIMEOUT;
        task_sleep(1U);
    }
}

bool win32_thread_get_exit_code(void *handle, uint32_t *exit_code) {
    win32_thread_record_t *record;
    if (!exit_code) return false;
    task_preempt_disable();
    record = record_from_handle(handle);
    if (!record) {
        task_preempt_enable();
        return false;
    }
    *exit_code = record->finished ? record->exit_code : WIN32_STILL_ACTIVE;
    task_preempt_enable();
    return true;
}

bool win32_thread_close_handle(void *handle) {
    win32_thread_record_t *record;
    task_preempt_disable();
    record = record_from_handle(handle);
    if (!record) {
        task_preempt_enable();
        return false;
    }
    record->handle_open = false;
    if (record->finished) kmemset(record, 0, sizeof(*record));
    task_preempt_enable();
    return true;
}

uint32_t win32_thread_get_id(void *handle) {
    win32_thread_record_t *record;
    uint32_t tid = 0U;
    task_preempt_disable();
    record = record_from_handle(handle);
    if (record) tid = record->tid;
    task_preempt_enable();
    return tid;
}

bool win32_thread_is_handle(void *handle) {
    bool valid;
    task_preempt_disable();
    valid = record_from_handle(handle) != NULL;
    task_preempt_enable();
    return valid;
}
