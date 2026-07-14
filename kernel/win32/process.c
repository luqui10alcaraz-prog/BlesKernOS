#include "process.h"
#include "sync.h"
#include "exception.h"
#include "win32.h"
#include "../include/memory.h"
#include "../include/task.h"

#define WIN32_ENV_SLOTS TASK_MAX
#define WIN32_THREAD_ENV_SLOTS TASK_MAX
#define WIN32_PATH_CHARS 260U
#define WIN32_ENV_WCHARS 128U
#define WIN32_EXCEPTION_CHAIN_END 0xFFFFFFFFU
#define WIN32_TEB_TLS_SLOTS_OFFSET 0xE10U
#define WIN32_STATIC_TLS_MODULES 16U
#define WIN32_TLS_MAX_BLOCK (1024U * 1024U)

typedef struct {
    uint32_t flink;
    uint32_t blink;
} PACKED win32_list_entry32_t;

typedef struct {
    uint16_t length;
    uint16_t maximum_length;
    uint32_t buffer;
} PACKED win32_unicode_string32_t;

typedef struct {
    uint32_t length;
    uint8_t initialized;
    uint8_t reserved1[3];
    uint32_t ss_handle;
    win32_list_entry32_t in_load_order_module_list;
    win32_list_entry32_t in_memory_order_module_list;
    win32_list_entry32_t in_initialization_order_module_list;
} PACKED win32_peb_ldr_data32_t;

typedef struct {
    uint32_t maximum_length;
    uint32_t length;
    uint32_t flags;
    uint32_t debug_flags;
    uint32_t console_handle;
    uint32_t console_flags;
    uint32_t standard_input;
    uint32_t standard_output;
    uint32_t standard_error;
    struct {
        win32_unicode_string32_t dos_path;
        uint32_t handle;
    } current_directory;
    win32_unicode_string32_t dll_path;
    win32_unicode_string32_t image_path_name;
    win32_unicode_string32_t command_line;
    uint32_t environment;
} PACKED win32_process_parameters32_t;

typedef struct {
    uint8_t inherited_address_space;
    uint8_t read_image_file_exec_options;
    uint8_t being_debugged;
    uint8_t bit_field;
    uint32_t mutant;
    uint32_t image_base_address;
    uint32_t ldr;
    uint32_t process_parameters;
    uint32_t subsystem_data;
    uint32_t process_heap;
} PACKED win32_peb32_t;

typedef struct {
    uint32_t exception_list;             /* 0x00 */
    uint32_t stack_base;                 /* 0x04 */
    uint32_t stack_limit;                /* 0x08 */
    uint32_t subsystem_tib;              /* 0x0c */
    uint32_t fiber_data;                 /* 0x10 */
    uint32_t arbitrary_user_pointer;     /* 0x14 */
    uint32_t self;                       /* 0x18 */
    uint32_t environment_pointer;        /* 0x1c */
    uint32_t client_id_unique_process;   /* 0x20 */
    uint32_t client_id_unique_thread;    /* 0x24 */
    uint32_t active_rpc_handle;          /* 0x28 */
    uint32_t thread_local_storage;       /* 0x2c */
    uint32_t process_environment_block;  /* 0x30 */
    uint32_t last_error_value;           /* 0x34 */
} PACKED win32_teb32_t;

typedef char win32_teb_self_offset_check[
    __builtin_offsetof(win32_teb32_t, self) == 0x18U ? 1 : -1];
typedef char win32_teb_peb_offset_check[
    __builtin_offsetof(win32_teb32_t, process_environment_block) == 0x30U ? 1 : -1];
typedef char win32_teb_error_offset_check[
    __builtin_offsetof(win32_teb32_t, last_error_value) == 0x34U ? 1 : -1];
typedef char win32_peb_image_offset_check[
    __builtin_offsetof(win32_peb32_t, image_base_address) == 0x08U ? 1 : -1];
typedef char win32_peb_heap_offset_check[
    __builtin_offsetof(win32_peb32_t, process_heap) == 0x18U ? 1 : -1];

typedef struct {
    void *module_base;
    uint32_t index;
    const void *template_data;
    uint32_t template_size;
    uint32_t zero_fill;
    void *main_block;
} win32_static_tls_t;

typedef struct {
    uint32_t tid;
    uint32_t process_id;
    win32_teb32_t *teb;
    void *static_blocks[WIN32_STATIC_TLS_MODULES];
} win32_thread_environment_t;

typedef struct {
    uint32_t pid;
    win32_teb32_t *teb;
    win32_peb32_t *peb;
    win32_peb_ldr_data32_t *ldr;
    win32_process_parameters32_t *parameters;
    uint32_t tls_bitmap[2];
    win32_static_tls_t static_tls[WIN32_STATIC_TLS_MODULES];
    char image_path_a[WIN32_PATH_CHARS];
    char command_line_a[WIN32_PATH_CHARS];
    uint16_t image_path_w[WIN32_PATH_CHARS];
    uint16_t command_line_w[WIN32_PATH_CHARS];
    uint16_t current_directory_w[16];
    uint16_t dll_path_w[64];
    uint16_t environment_w[WIN32_ENV_WCHARS];
} win32_process_environment_t;

static win32_process_environment_t environments[WIN32_ENV_SLOTS];
static win32_thread_environment_t thread_environments[WIN32_THREAD_ENV_SLOTS];

static uint32_t string_to_windows_path(const char *src, char *dst,
                                       uint32_t capacity) {
    uint32_t source = 0;
    uint32_t output = 0;

    if (!dst || capacity == 0U) return 0U;
    dst[0] = '\0';
    if (!src) src = "";

    if (src[0] == '/') {
        if (capacity < 4U) return 0U;
        dst[output++] = 'C';
        dst[output++] = ':';
    }
    while (src[source] && output + 1U < capacity) {
        dst[output++] = src[source] == '/' ? '\\' : src[source];
        source++;
    }
    dst[output] = '\0';
    return output;
}

static uint32_t ascii_to_utf16(const char *src, uint16_t *dst,
                               uint32_t capacity) {
    uint32_t length = 0;
    if (!dst || capacity == 0U) return 0U;
    if (!src) src = "";
    while (src[length] && length + 1U < capacity) {
        dst[length] = (uint8_t)src[length];
        length++;
    }
    dst[length] = 0U;
    return length;
}

static void unicode_init(win32_unicode_string32_t *string,
                         uint16_t *buffer, uint32_t characters,
                         uint32_t capacity) {
    if (!string) return;
    string->length = (uint16_t)(characters * sizeof(uint16_t));
    string->maximum_length = (uint16_t)(capacity * sizeof(uint16_t));
    string->buffer = (uint32_t)(uintptr_t)buffer;
}

static void list_init(win32_list_entry32_t *list) {
    uint32_t address = (uint32_t)(uintptr_t)list;
    list->flink = address;
    list->blink = address;
}

static win32_process_environment_t *find_environment(uint32_t pid) {
    for (uint32_t i = 0; i < WIN32_ENV_SLOTS; i++)
        if (environments[i].pid == pid) return &environments[i];
    return NULL;
}

static win32_thread_environment_t *find_thread_environment(uint32_t tid) {
    for (uint32_t i = 0; i < WIN32_THREAD_ENV_SLOTS; i++)
        if (thread_environments[i].tid == tid) return &thread_environments[i];
    return NULL;
}

static win32_process_environment_t *current_environment(void) {
    return find_environment(task_current_process_id());
}

static win32_teb32_t *current_teb(win32_process_environment_t *environment) {
    win32_thread_environment_t *thread;
    uint32_t tid;
    if (!environment) return NULL;
    tid = task_current_pid();
    if (tid == environment->pid) return environment->teb;
    thread = find_thread_environment(tid);
    return thread && thread->process_id == environment->pid ? thread->teb : NULL;
}

static void **tls_slots_from_teb(win32_teb32_t *teb) {
    if (!teb) return NULL;
    return (void **)((uint8_t *)teb + WIN32_TEB_TLS_SLOTS_OFFSET);
}

static void **current_tls_slots(win32_process_environment_t *environment) {
    return tls_slots_from_teb(current_teb(environment));
}

static bool tls_index_used(const win32_process_environment_t *environment,
                           uint32_t index) {
    if (!environment || index >= WIN32_TLS_SLOT_COUNT) return false;
    return (environment->tls_bitmap[index / 32U] &
            (1U << (index % 32U))) != 0U;
}

static void tls_mark_index(win32_process_environment_t *environment,
                           uint32_t index, bool used) {
    uint32_t mask;
    if (!environment || index >= WIN32_TLS_SLOT_COUNT) return;
    mask = 1U << (index % 32U);
    if (used) environment->tls_bitmap[index / 32U] |= mask;
    else environment->tls_bitmap[index / 32U] &= ~mask;
}

static uint32_t tls_allocate_index(win32_process_environment_t *environment) {
    for (uint32_t index = 0; index < WIN32_TLS_SLOT_COUNT; index++) {
        if (tls_index_used(environment, index)) continue;
        tls_mark_index(environment, index, true);
        return index;
    }
    return WIN32_TLS_OUT_OF_INDEXES;
}

static bool tls_index_is_static(const win32_process_environment_t *environment,
                                uint32_t index) {
    if (!environment) return false;
    for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++) {
        if (environment->static_tls[i].module_base &&
            environment->static_tls[i].index == index) return true;
    }
    return false;
}

static void *tls_clone_block(const win32_static_tls_t *record) {
    uint32_t total_size;
    void *block;
    if (!record || !record->module_base) return NULL;
    total_size = record->template_size + record->zero_fill;
    if (total_size == 0U) total_size = 1U;
    block = kzalloc(total_size);
    if (!block) return NULL;
    if (record->template_size && record->template_data)
        kmemcpy(block, record->template_data, record->template_size);
    return block;
}

bool win32_process_create(uint32_t pid, uint32_t image_base,
                          const char *image_path,
                          const char *command_line) {
    win32_process_environment_t *environment = NULL;
    uint32_t stack_limit = 0;
    uint32_t stack_base = 0;
    uint32_t image_characters;
    uint32_t command_characters;
    static const char current_directory[] = "C:\\";
    static const char dll_path[] = "C:\\SYSTEM\\LIBS\\WINE";
    static const char environment_block[] =
        "SystemRoot=C:\\WINDOWS\0"
        "TEMP=C:\\TEMP\0"
        "TMP=C:\\TEMP\0"
        "OS=BlesKernOS\0\0";

    if (!pid || !task_get_user_stack_bounds(pid, &stack_limit, &stack_base))
        return false;

    task_preempt_disable();
    for (uint32_t i = 0; i < WIN32_ENV_SLOTS; i++) {
        if (environments[i].pid != 0U) continue;
        environment = &environments[i];
        kmemset(environment, 0, sizeof(*environment));
        environment->pid = pid;
        break;
    }
    task_preempt_enable();
    if (!environment) return false;

    environment->teb = (win32_teb32_t *)kzalloc(4096U);
    environment->peb = (win32_peb32_t *)kzalloc(4096U);
    environment->ldr = (win32_peb_ldr_data32_t *)kzalloc(sizeof(*environment->ldr));
    environment->parameters = (win32_process_parameters32_t *)kzalloc(4096U);
    if (!environment->teb || !environment->peb || !environment->ldr ||
        !environment->parameters) {
        win32_process_destroy(pid);
        return false;
    }

    string_to_windows_path(image_path, environment->image_path_a,
                           sizeof(environment->image_path_a));
    if (command_line && *command_line)
        kstrncpy(environment->command_line_a, command_line,
                 sizeof(environment->command_line_a) - 1U);
    else
        kstrncpy(environment->command_line_a, environment->image_path_a,
                 sizeof(environment->command_line_a) - 1U);
    environment->command_line_a[sizeof(environment->command_line_a) - 1U] = '\0';

    image_characters = ascii_to_utf16(environment->image_path_a,
                                      environment->image_path_w,
                                      WIN32_PATH_CHARS);
    command_characters = ascii_to_utf16(environment->command_line_a,
                                        environment->command_line_w,
                                        WIN32_PATH_CHARS);
    ascii_to_utf16(current_directory, environment->current_directory_w,
                   sizeof(environment->current_directory_w) / sizeof(uint16_t));
    ascii_to_utf16(dll_path, environment->dll_path_w,
                   sizeof(environment->dll_path_w) / sizeof(uint16_t));
    for (uint32_t i = 0; i < sizeof(environment_block) &&
                         i < WIN32_ENV_WCHARS; i++)
        environment->environment_w[i] = (uint8_t)environment_block[i];

    environment->ldr->length = sizeof(*environment->ldr);
    environment->ldr->initialized = 1U;
    list_init(&environment->ldr->in_load_order_module_list);
    list_init(&environment->ldr->in_memory_order_module_list);
    list_init(&environment->ldr->in_initialization_order_module_list);

    environment->parameters->maximum_length = sizeof(*environment->parameters);
    environment->parameters->length = sizeof(*environment->parameters);
    environment->parameters->flags = 1U;
    environment->parameters->standard_input = 0U;
    environment->parameters->standard_output = 1U;
    environment->parameters->standard_error = 2U;
    unicode_init(&environment->parameters->current_directory.dos_path,
                 environment->current_directory_w, 3U,
                 sizeof(environment->current_directory_w) / sizeof(uint16_t));
    unicode_init(&environment->parameters->dll_path,
                 environment->dll_path_w, (uint32_t)kstrlen(dll_path),
                 sizeof(environment->dll_path_w) / sizeof(uint16_t));
    unicode_init(&environment->parameters->image_path_name,
                 environment->image_path_w, image_characters, WIN32_PATH_CHARS);
    unicode_init(&environment->parameters->command_line,
                 environment->command_line_w, command_characters,
                 WIN32_PATH_CHARS);
    environment->parameters->environment =
        (uint32_t)(uintptr_t)environment->environment_w;

    environment->peb->image_base_address = image_base;
    environment->peb->ldr = (uint32_t)(uintptr_t)environment->ldr;
    environment->peb->process_parameters =
        (uint32_t)(uintptr_t)environment->parameters;
    environment->peb->process_heap = WIN32_PROCESS_HEAP_HANDLE;

    environment->teb->exception_list = WIN32_EXCEPTION_CHAIN_END;
    environment->teb->stack_base = stack_base;
    environment->teb->stack_limit = stack_limit;
    environment->teb->self = (uint32_t)(uintptr_t)environment->teb;
    environment->teb->client_id_unique_process = pid;
    environment->teb->client_id_unique_thread = pid;
    environment->teb->thread_local_storage =
        (uint32_t)(uintptr_t)tls_slots_from_teb(environment->teb);
    environment->teb->process_environment_block =
        (uint32_t)(uintptr_t)environment->peb;
    environment->teb->last_error_value = 0U;

    if (!task_set_user_fs_base(pid,
            (uint32_t)(uintptr_t)environment->teb)) {
        win32_process_destroy(pid);
        return false;
    }
    return true;
}

bool win32_process_thread_create(uint32_t process_id, uint32_t tid) {
    win32_process_environment_t *environment;
    win32_thread_environment_t *thread = NULL;
    uint32_t stack_limit = 0U;
    uint32_t stack_base = 0U;

    if (!process_id || !tid ||
        !task_get_user_stack_bounds(tid, &stack_limit, &stack_base))
        return false;

    task_preempt_disable();
    environment = find_environment(process_id);
    for (uint32_t i = 0; environment && i < WIN32_THREAD_ENV_SLOTS; i++) {
        if (thread_environments[i].tid != 0U) continue;
        thread = &thread_environments[i];
        kmemset(thread, 0, sizeof(*thread));
        thread->tid = tid;
        thread->process_id = process_id;
        break;
    }
    task_preempt_enable();
    if (!environment || !thread) return false;

    thread->teb = (win32_teb32_t *)kzalloc(4096U);
    if (!thread->teb) {
        kmemset(thread, 0, sizeof(*thread));
        return false;
    }
    thread->teb->exception_list = WIN32_EXCEPTION_CHAIN_END;
    thread->teb->stack_base = stack_base;
    thread->teb->stack_limit = stack_limit;
    thread->teb->self = (uint32_t)(uintptr_t)thread->teb;
    thread->teb->client_id_unique_process = process_id;
    thread->teb->client_id_unique_thread = tid;
    thread->teb->thread_local_storage =
        (uint32_t)(uintptr_t)tls_slots_from_teb(thread->teb);
    thread->teb->process_environment_block =
        (uint32_t)(uintptr_t)environment->peb;

    for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++) {
        win32_static_tls_t *record = &environment->static_tls[i];
        void **slots;
        if (!record->module_base) continue;
        thread->static_blocks[i] = tls_clone_block(record);
        if (!thread->static_blocks[i]) {
            win32_process_thread_destroy(tid);
            return false;
        }
        slots = tls_slots_from_teb(thread->teb);
        slots[record->index] = thread->static_blocks[i];
    }

    if (!task_set_user_fs_base(tid, (uint32_t)(uintptr_t)thread->teb)) {
        win32_process_thread_destroy(tid);
        return false;
    }
    return true;
}

void win32_process_thread_destroy(uint32_t tid) {
    win32_exception_cleanup_thread(tid);
    win32_msvcrt_cleanup_thread(tid);
    win32_thread_environment_t *thread;
    task_preempt_disable();
    thread = find_thread_environment(tid);
    if (!thread) {
        task_preempt_enable();
        return;
    }
    task_set_user_fs_base(tid, 0U);
    for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++)
        if (thread->static_blocks[i]) kfree(thread->static_blocks[i]);
    if (thread->teb) kfree(thread->teb);
    kmemset(thread, 0, sizeof(*thread));
    task_preempt_enable();
}

void win32_process_destroy(uint32_t pid) {
    win32_exception_cleanup_process(pid);
    win32_msvcrt_cleanup_process(pid);
    win32_process_environment_t *environment;

    win32_sync_process_destroy(pid);

    for (uint32_t i = 0; i < WIN32_THREAD_ENV_SLOTS; i++)
        if (thread_environments[i].process_id == pid)
            win32_process_thread_destroy(thread_environments[i].tid);

    task_preempt_disable();
    environment = find_environment(pid);
    if (!environment) {
        task_preempt_enable();
        return;
    }
    task_set_user_fs_base(pid, 0U);
    for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++)
        if (environment->static_tls[i].main_block)
            kfree(environment->static_tls[i].main_block);
    if (environment->teb) kfree(environment->teb);
    if (environment->peb) kfree(environment->peb);
    if (environment->ldr) kfree(environment->ldr);
    if (environment->parameters) kfree(environment->parameters);
    kmemset(environment, 0, sizeof(*environment));
    task_preempt_enable();
}

void *win32_process_current_teb(void) {
    win32_process_environment_t *environment = current_environment();
    return current_teb(environment);
}

void *win32_process_current_peb(void) {
    win32_process_environment_t *environment = current_environment();
    return environment ? environment->peb : NULL;
}

const char *win32_process_current_command_line(void) {
    win32_process_environment_t *environment = current_environment();
    return environment ? environment->command_line_a : NULL;
}

const char *win32_process_current_image_path(void) {
    win32_process_environment_t *environment = current_environment();
    return environment ? environment->image_path_a : NULL;
}

uint32_t win32_process_get_last_error(void) {
    win32_teb32_t *teb = current_teb(current_environment());
    return teb ? teb->last_error_value : 0U;
}

void win32_process_set_last_error(uint32_t error) {
    win32_teb32_t *teb = current_teb(current_environment());
    if (teb) teb->last_error_value = error;
}

uint32_t win32_process_get_exception_list(void) {
    win32_teb32_t *teb = current_teb(current_environment());
    return teb ? teb->exception_list : WIN32_EXCEPTION_CHAIN_END;
}

bool win32_process_set_exception_list(uint32_t head) {
    win32_teb32_t *teb = current_teb(current_environment());
    if (!teb) return false;
    teb->exception_list = head;
    return true;
}

uint32_t win32_process_tls_alloc(void) {
    win32_process_environment_t *environment;
    uint32_t index;

    task_preempt_disable();
    environment = current_environment();
    index = tls_allocate_index(environment);
    if (environment && index != WIN32_TLS_OUT_OF_INDEXES) {
        void **slots = tls_slots_from_teb(environment->teb);
        if (slots) slots[index] = NULL;
        for (uint32_t i = 0; i < WIN32_THREAD_ENV_SLOTS; i++) {
            if (thread_environments[i].process_id != environment->pid ||
                !thread_environments[i].teb) continue;
            slots = tls_slots_from_teb(thread_environments[i].teb);
            slots[index] = NULL;
        }
    }
    task_preempt_enable();
    return index;
}

bool win32_process_tls_free(uint32_t index) {
    win32_process_environment_t *environment;
    bool result = false;

    task_preempt_disable();
    environment = current_environment();
    if (environment && index < WIN32_TLS_SLOT_COUNT &&
        tls_index_used(environment, index) &&
        !tls_index_is_static(environment, index)) {
        void **slots = tls_slots_from_teb(environment->teb);
        if (slots) slots[index] = NULL;
        for (uint32_t i = 0; i < WIN32_THREAD_ENV_SLOTS; i++) {
            if (thread_environments[i].process_id != environment->pid ||
                !thread_environments[i].teb) continue;
            slots = tls_slots_from_teb(thread_environments[i].teb);
            slots[index] = NULL;
        }
        tls_mark_index(environment, index, false);
        result = true;
    }
    task_preempt_enable();
    return result;
}

bool win32_process_tls_set(uint32_t index, void *value) {
    win32_process_environment_t *environment;
    void **slots;
    bool result = false;

    task_preempt_disable();
    environment = current_environment();
    slots = current_tls_slots(environment);
    if (environment && slots && index < WIN32_TLS_SLOT_COUNT &&
        tls_index_used(environment, index)) {
        slots[index] = value;
        result = true;
    }
    task_preempt_enable();
    return result;
}

void *win32_process_tls_get(uint32_t index, bool *valid) {
    win32_process_environment_t *environment;
    void **slots;
    void *value = NULL;
    bool found = false;

    task_preempt_disable();
    environment = current_environment();
    slots = current_tls_slots(environment);
    if (environment && slots && index < WIN32_TLS_SLOT_COUNT &&
        tls_index_used(environment, index)) {
        value = slots[index];
        found = true;
    }
    task_preempt_enable();
    if (valid) *valid = found;
    return value;
}

bool win32_process_tls_install(uint32_t pid, void *module_base,
                               const void *template_data,
                               uint32_t template_size,
                               uint32_t zero_fill,
                               uint32_t *address_of_index) {
    win32_process_environment_t *environment;
    win32_static_tls_t *record = NULL;
    uint32_t record_index = 0U;
    uint32_t index;

    if (!pid || !module_base || !address_of_index ||
        template_size > WIN32_TLS_MAX_BLOCK ||
        zero_fill > WIN32_TLS_MAX_BLOCK - template_size)
        return false;

    task_preempt_disable();
    environment = find_environment(pid);
    if (!environment) {
        task_preempt_enable();
        return false;
    }
    for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++) {
        if (environment->static_tls[i].module_base == module_base) {
            *address_of_index = environment->static_tls[i].index;
            task_preempt_enable();
            return true;
        }
        if (!record && !environment->static_tls[i].module_base) {
            record = &environment->static_tls[i];
            record_index = i;
        }
    }
    if (!record) {
        task_preempt_enable();
        return false;
    }

    index = tls_allocate_index(environment);
    if (index == WIN32_TLS_OUT_OF_INDEXES) {
        task_preempt_enable();
        return false;
    }
    record->module_base = module_base;
    record->index = index;
    record->template_data = template_data;
    record->template_size = template_size;
    record->zero_fill = zero_fill;
    record->main_block = tls_clone_block(record);
    if (!record->main_block) {
        tls_mark_index(environment, index, false);
        kmemset(record, 0, sizeof(*record));
        task_preempt_enable();
        return false;
    }
    tls_slots_from_teb(environment->teb)[index] = record->main_block;

    for (uint32_t i = 0; i < WIN32_THREAD_ENV_SLOTS; i++) {
        win32_thread_environment_t *thread = &thread_environments[i];
        if (thread->process_id != pid || !thread->teb) continue;
        thread->static_blocks[record_index] = tls_clone_block(record);
        if (!thread->static_blocks[record_index]) {
            task_preempt_enable();
            win32_process_tls_uninstall(pid, module_base);
            return false;
        }
        tls_slots_from_teb(thread->teb)[index] =
            thread->static_blocks[record_index];
    }
    *address_of_index = index;
    task_preempt_enable();
    return true;
}

bool win32_process_tls_uninstall(uint32_t pid, void *module_base) {
    win32_process_environment_t *environment;
    bool result = false;

    task_preempt_disable();
    environment = find_environment(pid);
    if (environment) {
        for (uint32_t i = 0; i < WIN32_STATIC_TLS_MODULES; i++) {
            win32_static_tls_t *record = &environment->static_tls[i];
            if (record->module_base != module_base) continue;
            if (record->index < WIN32_TLS_SLOT_COUNT) {
                void **slots = tls_slots_from_teb(environment->teb);
                if (slots) slots[record->index] = NULL;
                for (uint32_t t = 0; t < WIN32_THREAD_ENV_SLOTS; t++) {
                    win32_thread_environment_t *thread = &thread_environments[t];
                    if (thread->process_id != pid || !thread->teb) continue;
                    slots = tls_slots_from_teb(thread->teb);
                    slots[record->index] = NULL;
                    if (thread->static_blocks[i]) {
                        kfree(thread->static_blocks[i]);
                        thread->static_blocks[i] = NULL;
                    }
                }
                tls_mark_index(environment, record->index, false);
            }
            if (record->main_block) kfree(record->main_block);
            kmemset(record, 0, sizeof(*record));
            result = true;
            break;
        }
    }
    task_preempt_enable();
    return result;
}
