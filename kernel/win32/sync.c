#include "sync.h"
#include "../include/task.h"
#include "../include/pit.h"
#include "../include/memory.h"

#define WIN32_SYNC_HANDLE_BASE 0x73000000U
#define WIN32_SYNC_OBJECTS 32U

typedef enum {
    WIN32_SYNC_NONE = 0,
    WIN32_SYNC_EVENT,
    WIN32_SYNC_MUTEX,
    WIN32_SYNC_SEMAPHORE
} win32_sync_type_t;

typedef struct {
    bool used;
    bool handle_open;
    uint8_t type;
    uint8_t manual_reset;
    uint8_t signaled;
    uint8_t abandoned;
    uint32_t process_id;
    uint32_t owner_tid;
    uint32_t recursion;
    int32_t count;
    int32_t maximum_count;
} win32_sync_object_t;

/* Layout Win32 x86 de RTL_CRITICAL_SECTION/CRITICAL_SECTION. */
typedef struct {
    uint32_t debug_info;
    int32_t lock_count;
    int32_t recursion_count;
    uint32_t owning_thread;
    uint32_t lock_semaphore;
    uint32_t spin_count;
} PACKED win32_critical_section32_t;

typedef char win32_critical_section_size_check[
    sizeof(win32_critical_section32_t) == 24U ? 1 : -1];

static win32_sync_object_t sync_objects[WIN32_SYNC_OBJECTS];

static uint32_t milliseconds_now(void) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t scaled;
    if (!hz) return 0U;
    scaled = (uint64_t)pit_get_ticks() * 1000U;
    return (uint32_t)(scaled / hz);
}

static uint32_t object_handle(uint32_t slot) {
    return WIN32_SYNC_HANDLE_BASE + slot;
}

static win32_sync_object_t *object_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    uint32_t slot;
    if (value < WIN32_SYNC_HANDLE_BASE ||
        value >= WIN32_SYNC_HANDLE_BASE + WIN32_SYNC_OBJECTS)
        return NULL;
    slot = value - WIN32_SYNC_HANDLE_BASE;
    if (!sync_objects[slot].used || !sync_objects[slot].handle_open)
        return NULL;
    return &sync_objects[slot];
}

static void *allocate_object(win32_sync_type_t type) {
    uint32_t process_id = task_current_process_id();
    void *handle = NULL;
    task_preempt_disable();
    for (uint32_t i = 0; i < WIN32_SYNC_OBJECTS; i++) {
        win32_sync_object_t *object = &sync_objects[i];
        if (object->used) continue;
        kmemset(object, 0, sizeof(*object));
        object->used = true;
        object->handle_open = true;
        object->type = (uint8_t)type;
        object->process_id = process_id;
        handle = (void *)(uintptr_t)object_handle(i);
        break;
    }
    task_preempt_enable();
    return handle;
}

void *win32_sync_create_event(bool manual_reset, bool initial_state) {
    void *handle = allocate_object(WIN32_SYNC_EVENT);
    win32_sync_object_t *object;
    if (!handle) return NULL;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object) {
        object->manual_reset = manual_reset ? 1U : 0U;
        object->signaled = initial_state ? 1U : 0U;
    }
    task_preempt_enable();
    return handle;
}

bool win32_sync_set_event(void *handle) {
    win32_sync_object_t *object;
    bool result = false;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object && object->type == WIN32_SYNC_EVENT) {
        object->signaled = 1U;
        result = true;
    }
    task_preempt_enable();
    return result;
}

bool win32_sync_reset_event(void *handle) {
    win32_sync_object_t *object;
    bool result = false;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object && object->type == WIN32_SYNC_EVENT) {
        object->signaled = 0U;
        result = true;
    }
    task_preempt_enable();
    return result;
}

void *win32_sync_create_mutex(bool initial_owner) {
    void *handle = allocate_object(WIN32_SYNC_MUTEX);
    win32_sync_object_t *object;
    if (!handle) return NULL;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object && initial_owner) {
        object->owner_tid = task_current_pid();
        object->recursion = 1U;
    }
    task_preempt_enable();
    return handle;
}

bool win32_sync_release_mutex(void *handle) {
    win32_sync_object_t *object;
    uint32_t tid = task_current_pid();
    bool result = false;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object && object->type == WIN32_SYNC_MUTEX &&
        object->owner_tid == tid && object->recursion != 0U) {
        object->recursion--;
        if (object->recursion == 0U) object->owner_tid = 0U;
        result = true;
    }
    task_preempt_enable();
    return result;
}

void *win32_sync_create_semaphore(int32_t initial_count,
                                  int32_t maximum_count) {
    void *handle;
    win32_sync_object_t *object;
    if (maximum_count <= 0 || initial_count < 0 ||
        initial_count > maximum_count) return NULL;
    handle = allocate_object(WIN32_SYNC_SEMAPHORE);
    if (!handle) return NULL;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object) {
        object->count = initial_count;
        object->maximum_count = maximum_count;
    }
    task_preempt_enable();
    return handle;
}

bool win32_sync_release_semaphore(void *handle, int32_t release_count,
                                  int32_t *previous_count) {
    win32_sync_object_t *object;
    bool result = false;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object && object->type == WIN32_SYNC_SEMAPHORE &&
        release_count > 0 && object->count <=
            object->maximum_count - release_count) {
        if (previous_count) *previous_count = object->count;
        object->count += release_count;
        result = true;
    }
    task_preempt_enable();
    return result;
}

bool win32_sync_is_handle(void *handle) {
    bool result;
    task_preempt_disable();
    result = object_from_handle(handle) != NULL;
    task_preempt_enable();
    return result;
}

bool win32_sync_close_handle(void *handle) {
    win32_sync_object_t *object;
    bool result = false;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (object) {
        object->handle_open = false;
        kmemset(object, 0, sizeof(*object));
        result = true;
    }
    task_preempt_enable();
    return result;
}

uint32_t win32_sync_try_wait(void *handle, uint32_t tid, bool consume) {
    win32_sync_object_t *object;
    uint32_t result = WIN32_WAIT_FAILED;
    task_preempt_disable();
    object = object_from_handle(handle);
    if (!object) {
        task_preempt_enable();
        return WIN32_WAIT_FAILED;
    }
    if (object->process_id != task_current_process_id()) {
        task_preempt_enable();
        return WIN32_WAIT_FAILED;
    }

    switch ((win32_sync_type_t)object->type) {
        case WIN32_SYNC_EVENT:
            if (!object->signaled) result = WIN32_WAIT_TIMEOUT;
            else {
                result = WIN32_WAIT_OBJECT_0;
                if (consume && !object->manual_reset) object->signaled = 0U;
            }
            break;
        case WIN32_SYNC_MUTEX:
            if (object->owner_tid != 0U && object->owner_tid != tid) {
                result = WIN32_WAIT_TIMEOUT;
            } else {
                result = object->abandoned
                    ? WIN32_WAIT_ABANDONED_0 : WIN32_WAIT_OBJECT_0;
                if (consume) {
                    if (object->owner_tid == tid) object->recursion++;
                    else {
                        object->owner_tid = tid;
                        object->recursion = 1U;
                    }
                    object->abandoned = 0U;
                }
            }
            break;
        case WIN32_SYNC_SEMAPHORE:
            if (object->count <= 0) result = WIN32_WAIT_TIMEOUT;
            else {
                result = WIN32_WAIT_OBJECT_0;
                if (consume) object->count--;
            }
            break;
        default:
            result = WIN32_WAIT_FAILED;
            break;
    }
    task_preempt_enable();
    return result;
}

uint32_t win32_sync_wait(void *handle, uint32_t milliseconds) {
    uint32_t start = milliseconds_now();
    uint32_t tid = task_current_pid();
    for (;;) {
        uint32_t result = win32_sync_try_wait(handle, tid, true);
        if (result != WIN32_WAIT_TIMEOUT) return result;
        if (milliseconds == 0U) return WIN32_WAIT_TIMEOUT;
        if (milliseconds != WIN32_INFINITE &&
            (uint32_t)(milliseconds_now() - start) >= milliseconds)
            return WIN32_WAIT_TIMEOUT;
        task_sleep(1U);
    }
}

void win32_sync_thread_exit(uint32_t tid) {
    task_preempt_disable();
    for (uint32_t i = 0; i < WIN32_SYNC_OBJECTS; i++) {
        win32_sync_object_t *object = &sync_objects[i];
        if (!object->used || object->type != WIN32_SYNC_MUTEX ||
            object->owner_tid != tid) continue;
        object->owner_tid = 0U;
        object->recursion = 0U;
        object->abandoned = 1U;
    }
    task_preempt_enable();
}

void win32_sync_process_destroy(uint32_t process_id) {
    task_preempt_disable();
    for (uint32_t i = 0; i < WIN32_SYNC_OBJECTS; i++) {
        if (sync_objects[i].used &&
            sync_objects[i].process_id == process_id)
            kmemset(&sync_objects[i], 0, sizeof(sync_objects[i]));
    }
    task_preempt_enable();
}

static win32_critical_section32_t *critical_from_pointer(void *pointer) {
    return (win32_critical_section32_t *)pointer;
}

void win32_critical_section_initialize(void *critical_section,
                                       uint32_t spin_count) {
    win32_critical_section32_t *critical =
        critical_from_pointer(critical_section);
    if (!critical) return;
    kmemset(critical, 0, sizeof(*critical));
    critical->lock_count = -1;
    critical->spin_count = spin_count;
}

void win32_critical_section_delete(void *critical_section) {
    win32_critical_section32_t *critical =
        critical_from_pointer(critical_section);
    if (!critical) return;
    kmemset(critical, 0, sizeof(*critical));
    critical->lock_count = -1;
}

bool win32_critical_section_try_enter(void *critical_section) {
    win32_critical_section32_t *critical =
        critical_from_pointer(critical_section);
    uint32_t tid = task_current_pid();
    bool result = false;
    if (!critical) return false;
    task_preempt_disable();
    if (critical->owning_thread == 0U || critical->owning_thread == tid) {
        critical->owning_thread = tid;
        critical->recursion_count++;
        critical->lock_count = critical->recursion_count - 1;
        result = true;
    }
    task_preempt_enable();
    return result;
}

void win32_critical_section_enter(void *critical_section) {
    while (!win32_critical_section_try_enter(critical_section))
        task_sleep(1U);
}

bool win32_critical_section_leave(void *critical_section) {
    win32_critical_section32_t *critical =
        critical_from_pointer(critical_section);
    uint32_t tid = task_current_pid();
    bool result = false;
    if (!critical) return false;
    task_preempt_disable();
    if (critical->owning_thread == tid && critical->recursion_count > 0) {
        critical->recursion_count--;
        if (critical->recursion_count == 0) {
            critical->owning_thread = 0U;
            critical->lock_count = -1;
        } else {
            critical->lock_count = critical->recursion_count - 1;
        }
        result = true;
    }
    task_preempt_enable();
    return result;
}

uint32_t win32_critical_section_set_spin(void *critical_section,
                                         uint32_t spin_count) {
    win32_critical_section32_t *critical =
        critical_from_pointer(critical_section);
    uint32_t previous;
    if (!critical) return 0U;
    task_preempt_disable();
    previous = critical->spin_count;
    critical->spin_count = spin_count;
    task_preempt_enable();
    return previous;
}

int32_t win32_interlocked_increment(volatile int32_t *value) {
    int32_t add = 1;
    if (!value) return 0;
    __asm__ volatile ("lock xaddl %0, %1"
                      : "+r"(add), "+m"(*value)
                      : : "memory", "cc");
    return add + 1;
}

int32_t win32_interlocked_decrement(volatile int32_t *value) {
    int32_t add = -1;
    if (!value) return 0;
    __asm__ volatile ("lock xaddl %0, %1"
                      : "+r"(add), "+m"(*value)
                      : : "memory", "cc");
    return add - 1;
}

int32_t win32_interlocked_exchange(volatile int32_t *target, int32_t value) {
    if (!target) return 0;
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(value), "+m"(*target)
                      : : "memory");
    return value;
}

int32_t win32_interlocked_exchange_add(volatile int32_t *target,
                                       int32_t value) {
    if (!target) return 0;
    __asm__ volatile ("lock xaddl %0, %1"
                      : "+r"(value), "+m"(*target)
                      : : "memory", "cc");
    return value;
}

int32_t win32_interlocked_compare_exchange(volatile int32_t *target,
                                            int32_t exchange,
                                            int32_t compare) {
    int32_t previous = compare;
    if (!target) return 0;
    __asm__ volatile ("lock cmpxchgl %2, %1"
                      : "+a"(previous), "+m"(*target)
                      : "r"(exchange)
                      : "memory", "cc");
    return previous;
}
