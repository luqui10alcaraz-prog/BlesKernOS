#ifndef BLESKERNOS_WIN32_EXCEPTION_H
#define BLESKERNOS_WIN32_EXCEPTION_H

#include "../include/types.h"
#include "../include/idt.h"

#define WIN32_EXCEPTION_MAXIMUM_PARAMETERS 15U

#define WIN32_EXCEPTION_ACCESS_VIOLATION       0xC0000005U
#define WIN32_EXCEPTION_ILLEGAL_INSTRUCTION    0xC000001DU
#define WIN32_EXCEPTION_ARRAY_BOUNDS_EXCEEDED  0xC000008CU
#define WIN32_EXCEPTION_INT_DIVIDE_BY_ZERO      0xC0000094U
#define WIN32_EXCEPTION_INT_OVERFLOW            0xC0000095U
#define WIN32_EXCEPTION_BREAKPOINT              0x80000003U
#define WIN32_EXCEPTION_SINGLE_STEP             0x80000004U
#define WIN32_EXCEPTION_NONCONTINUABLE          0x00000001U
#define WIN32_EXCEPTION_SOFTWARE_ORIGINATE      0x00000080U

#define WIN32_EXCEPTION_CONTINUE_EXECUTION ((int32_t)-1)
#define WIN32_EXCEPTION_CONTINUE_SEARCH     ((int32_t)0)
#define WIN32_EXCEPTION_EXECUTE_HANDLER     ((int32_t)1)

/* Valores que devuelve un handler SEH de bajo nivel (EXCEPTION_DISPOSITION). */
#define WIN32_EXCEPTION_DISPOSITION_CONTINUE_EXECUTION 0
#define WIN32_EXCEPTION_DISPOSITION_CONTINUE_SEARCH    1

#define WIN32_CONTEXT_i386     0x00010000U
#define WIN32_CONTEXT_CONTROL  (WIN32_CONTEXT_i386 | 0x0001U)
#define WIN32_CONTEXT_INTEGER  (WIN32_CONTEXT_i386 | 0x0002U)
#define WIN32_CONTEXT_SEGMENTS (WIN32_CONTEXT_i386 | 0x0004U)
#define WIN32_CONTEXT_FULL     (WIN32_CONTEXT_CONTROL | WIN32_CONTEXT_INTEGER | \
                                WIN32_CONTEXT_SEGMENTS)

typedef struct {
    uint32_t control_word;
    uint32_t status_word;
    uint32_t tag_word;
    uint32_t error_offset;
    uint32_t error_selector;
    uint32_t data_offset;
    uint32_t data_selector;
    uint8_t register_area[80];
    uint32_t cr0_npx_state;
} PACKED win32_floating_save_area32_t;

typedef struct {
    uint32_t context_flags;
    uint32_t dr0, dr1, dr2, dr3, dr6, dr7;
    win32_floating_save_area32_t float_save;
    uint32_t seg_gs, seg_fs, seg_es, seg_ds;
    uint32_t edi, esi, ebx, edx, ecx, eax;
    uint32_t ebp, eip, seg_cs, eflags, esp, seg_ss;
    uint8_t extended_registers[512];
} PACKED win32_context32_t;

typedef char win32_context_eip_offset_check[
    __builtin_offsetof(win32_context32_t, eip) == 0xB8U ? 1 : -1];
typedef char win32_context_esp_offset_check[
    __builtin_offsetof(win32_context32_t, esp) == 0xC4U ? 1 : -1];
typedef char win32_context_size_check[
    sizeof(win32_context32_t) == 0x2CCU ? 1 : -1];

typedef struct win32_exception_record32 {
    uint32_t exception_code;
    uint32_t exception_flags;
    struct win32_exception_record32 *exception_record;
    void *exception_address;
    uint32_t number_parameters;
    uint32_t exception_information[WIN32_EXCEPTION_MAXIMUM_PARAMETERS];
} PACKED win32_exception_record32_t;

typedef struct {
    win32_exception_record32_t *exception_record;
    win32_context32_t *context_record;
} PACKED win32_exception_pointers32_t;

/* Intenta convertir una excepción CPU de Ring 3 en una excepción Win32. */
bool win32_exception_handle_interrupt(registers_t *regs);

/* Restaura el CONTEXT pendiente desde el syscall de retorno de excepción. */
bool win32_exception_restore_context(registers_t *regs);

/* Implementaciones compartidas por KERNEL32 y NTDLL. */
bool win32_exception_dispatch(win32_exception_record32_t *record,
                              win32_context32_t *context);
void win32_exception_capture_context(win32_context32_t *context,
                                     uint32_t return_eip);
void win32_exception_raise(uint32_t code, uint32_t flags,
                           uint32_t count, const uint32_t *arguments,
                           uint32_t return_eip);

void *win32_exception_set_unhandled_filter(void *filter);
int32_t win32_exception_unhandled_filter(win32_exception_pointers32_t *pointers);
void *win32_exception_add_vectored(bool first, void *handler, bool continue_handler);
bool win32_exception_remove_vectored(void *cookie, bool continue_handler);

void win32_exception_cleanup_thread(uint32_t tid);
void win32_exception_cleanup_process(uint32_t pid);

#endif
