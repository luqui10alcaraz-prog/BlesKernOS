#include "win32.h"
#include "process.h"
#include "exception.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/pe_loader.h"
#include "../stdio.h"

#define HEAP_ZERO_MEMORY 0x00000008U

typedef struct {
    uint16_t length;
    uint16_t maximum_length;
    char *buffer;
} PACKED ansi_string32_t;

typedef struct {
    uint16_t length;
    uint16_t maximum_length;
    uint16_t *buffer;
} PACKED unicode_string32_t;

static uint8_t upper(uint8_t value) {
    return value >= 'a' && value <= 'z' ? (uint8_t)(value - 32) : value;
}

static bool equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right)
        if (upper((uint8_t)*left++) != upper((uint8_t)*right++)) return false;
    return *left == *right;
}

static void *WIN32_API ntdll_NtCurrentTeb(void) {
    return win32_process_current_teb();
}

static void *WIN32_API ntdll_RtlGetCurrentPeb(void) {
    return win32_process_current_peb();
}

static uint32_t WIN32_API ntdll_RtlGetLastWin32Error(void) {
    return win32_process_get_last_error();
}

static void WIN32_API ntdll_RtlSetLastWin32Error(uint32_t error) {
    win32_process_set_last_error(error);
}

static void WIN32_API ntdll_RtlRestoreLastWin32Error(uint32_t error) {
    win32_process_set_last_error(error);
}

static void *WIN32_API ntdll_RtlAllocateHeap(void *heap UNUSED,
                                             uint32_t flags,
                                             uint32_t size) {
    return (flags & HEAP_ZERO_MEMORY) ? kzalloc(size) : kmalloc(size);
}

static void *WIN32_API ntdll_RtlReAllocateHeap(void *heap UNUSED,
                                               uint32_t flags UNUSED,
                                               void *memory,
                                               uint32_t size) {
    return memory ? krealloc(memory, size) : kmalloc(size);
}

static int WIN32_API ntdll_RtlFreeHeap(void *heap UNUSED,
                                       uint32_t flags UNUSED,
                                       void *memory) {
    if (!memory) return 0;
    kfree(memory);
    return 1;
}

static void WIN32_API ntdll_RtlInitAnsiString(ansi_string32_t *destination,
                                              const char *source) {
    uint32_t length = source ? (uint32_t)kstrlen(source) : 0U;
    if (!destination) return;
    destination->length = (uint16_t)length;
    destination->maximum_length = source ? (uint16_t)(length + 1U) : 0U;
    destination->buffer = (char *)source;
}

static void WIN32_API ntdll_RtlInitUnicodeString(
    unicode_string32_t *destination, const uint16_t *source) {
    uint32_t length = 0U;
    if (!destination) return;
    if (source) while (source[length]) length++;
    destination->length = (uint16_t)(length * sizeof(uint16_t));
    destination->maximum_length = source ?
        (uint16_t)((length + 1U) * sizeof(uint16_t)) : 0U;
    destination->buffer = (uint16_t *)source;
}

static void WIN32_API ntdll_RtlCaptureContext(win32_context32_t *context) {
    win32_exception_capture_context(
        context, (uint32_t)(uintptr_t)__builtin_return_address(0));
}

static int WIN32_API ntdll_RtlDispatchException(
        win32_exception_record32_t *record, win32_context32_t *context) {
    return win32_exception_dispatch(record, context) ? 1 : 0;
}

static void WIN32_API ntdll_RtlRaiseException(
        win32_exception_record32_t *record) {
    win32_context32_t context;
    uint32_t return_eip = (uint32_t)(uintptr_t)__builtin_return_address(0);
    if (!record) return;
    if (!record->exception_address)
        record->exception_address = (void *)(uintptr_t)return_eip;
    win32_exception_capture_context(&context, return_eip);
    if (!win32_exception_dispatch(record, &context)) {
        kprintf("[SEH] RtlRaiseException no manejada %x\n",
                record->exception_code);
        pe_win32_terminate_current_process();
    }
}

static uint32_t WIN32_API ntdll_NtRaiseException(
        win32_exception_record32_t *record, win32_context32_t *context,
        uint32_t first_chance UNUSED) {
    if (!record || !context) return 0xC000000DU; /* STATUS_INVALID_PARAMETER */
    return win32_exception_dispatch(record, context) ?
        0U : record->exception_code;
}

/* Primera base de unwind: actualiza FS:[0] hasta el frame objetivo. Todavia no
 * restaura automaticamente EBP/ESP ni ejecuta filtros __finally. */
static void WIN32_API ntdll_RtlUnwind(void *target_frame,
                                      void *target_ip UNUSED,
                                      win32_exception_record32_t *record UNUSED,
                                      void *return_value UNUSED) {
    win32_process_set_exception_list(target_frame ?
        (uint32_t)(uintptr_t)target_frame : 0xFFFFFFFFU);
}

static void WIN32_API ntdll_RtlUnwindEx(
        void *target_frame, void *target_ip UNUSED,
        win32_exception_record32_t *record UNUSED, void *return_value UNUSED,
        win32_context32_t *context UNUSED, void *history_table UNUSED) {
    win32_process_set_exception_list(target_frame ?
        (uint32_t)(uintptr_t)target_frame : 0xFFFFFFFFU);
}

uint32_t win32_ntdll_resolve(const char *name) {
#define EXPORT(api) \
    if (equal_ci(name, #api)) return (uint32_t)(uintptr_t)&ntdll_##api
    EXPORT(NtCurrentTeb);
    EXPORT(RtlGetCurrentPeb);
    EXPORT(RtlGetLastWin32Error);
    EXPORT(RtlSetLastWin32Error);
    EXPORT(RtlRestoreLastWin32Error);
    EXPORT(RtlAllocateHeap);
    EXPORT(RtlReAllocateHeap);
    EXPORT(RtlFreeHeap);
    EXPORT(RtlInitAnsiString);
    EXPORT(RtlInitUnicodeString);
    EXPORT(RtlCaptureContext);
    EXPORT(RtlDispatchException);
    EXPORT(RtlRaiseException);
    EXPORT(NtRaiseException);
    EXPORT(RtlUnwind);
    EXPORT(RtlUnwindEx);
#undef EXPORT
    return 0U;
}
