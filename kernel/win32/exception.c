#include "exception.h"
#include "win32.h"
#include "process.h"
#include "../include/task.h"
#include "../stdio.h"
#include "../include/syscall.h"
#include "../include/gdt.h"
#include "../include/pe_loader.h"
#include "../include/memory.h"

#define WIN32_EXCEPTION_STATE_SLOTS TASK_MAX
#define WIN32_VECTORED_HANDLER_SLOTS 8U
#define WIN32_EXCEPTION_CHAIN_END 0xFFFFFFFFU
#define WIN32_EXCEPTION_MAX_CHAIN 64U

/* EXCEPTION_REGISTRATION_RECORD de x86. El handler usa __cdecl. */
typedef struct win32_exception_registration32 {
    struct win32_exception_registration32 *previous;
    int32_t (*handler)(win32_exception_record32_t *record,
                       void *establisher_frame,
                       win32_context32_t *context,
                       void *dispatcher_context);
} PACKED win32_exception_registration32_t;

typedef int32_t (WIN32_API *win32_vectored_handler_t)(
    win32_exception_pointers32_t *pointers);
typedef int32_t (WIN32_API *win32_top_filter_t)(
    win32_exception_pointers32_t *pointers);

typedef struct {
    uint32_t tid;
    uint32_t process_id;
    bool pending;
    uint32_t last_fault_code;
    uint32_t last_fault_eip;
    uint32_t repeated_faults;
    win32_exception_record32_t record;
    win32_context32_t context;
} win32_exception_state_t;

typedef struct {
    uint32_t process_id;
    void *handler;
    bool used;
} win32_vectored_entry_t;

typedef struct {
    uint32_t process_id;
    void *filter;
} win32_filter_entry_t;

static win32_exception_state_t exception_states[WIN32_EXCEPTION_STATE_SLOTS];
static win32_vectored_entry_t vectored_exception[WIN32_VECTORED_HANDLER_SLOTS];
static win32_vectored_entry_t vectored_continue[WIN32_VECTORED_HANDLER_SLOTS];
static win32_filter_entry_t unhandled_filters[TASK_MAX];

static win32_exception_state_t *find_state(uint32_t tid, bool create) {
    win32_exception_state_t *free_state = NULL;
    for (uint32_t i = 0; i < WIN32_EXCEPTION_STATE_SLOTS; i++) {
        if (exception_states[i].tid == tid) return &exception_states[i];
        if (!exception_states[i].tid && !free_state) free_state = &exception_states[i];
    }
    if (!create || !free_state) return NULL;
    kmemset(free_state, 0, sizeof(*free_state));
    free_state->tid = tid;
    free_state->process_id = task_current_process_id();
    return free_state;
}

static win32_filter_entry_t *find_filter(uint32_t process_id, bool create) {
    win32_filter_entry_t *free_entry = NULL;
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (unhandled_filters[i].process_id == process_id)
            return &unhandled_filters[i];
        if (!unhandled_filters[i].process_id && !free_entry)
            free_entry = &unhandled_filters[i];
    }
    if (!create || !free_entry) return NULL;
    free_entry->process_id = process_id;
    free_entry->filter = NULL;
    return free_entry;
}

static bool frame_in_current_stack(uint32_t address, uint32_t size) {
    uint32_t limit = 0U, base = 0U;
    if (!address || address + size < address) return false;
    if (!task_get_user_stack_bounds(task_current_pid(), &limit, &base)) return false;
    return address >= limit && address + size <= base;
}

static void context_from_registers(win32_context32_t *context,
                                   const registers_t *regs) {
    if (!context || !regs) return;
    kmemset(context, 0, sizeof(*context));
    context->context_flags = WIN32_CONTEXT_FULL;
    context->seg_gs = regs->gs;
    context->seg_fs = regs->fs;
    context->seg_es = regs->es;
    context->seg_ds = regs->ds;
    context->edi = regs->edi;
    context->esi = regs->esi;
    context->ebx = regs->ebx;
    context->edx = regs->edx;
    context->ecx = regs->ecx;
    context->eax = regs->eax;
    context->ebp = regs->ebp;
    context->eip = regs->eip;
    context->seg_cs = regs->cs;
    context->eflags = regs->eflags;
    context->esp = regs->useresp;
    context->seg_ss = regs->ss;
}

static void registers_from_context(registers_t *regs,
                                   const win32_context32_t *context) {
    uint32_t safe_flags;
    if (!regs || !context) return;
    regs->edi = context->edi;
    regs->esi = context->esi;
    regs->ebx = context->ebx;
    regs->edx = context->edx;
    regs->ecx = context->ecx;
    regs->eax = context->eax;
    regs->ebp = context->ebp;
    regs->eip = context->eip;
    regs->useresp = context->esp;

    /*
     * El handler puede modificar las banderas aritmeticas y de control
     * normales, pero no debe poder cambiar IOPL, NT ni VM.
     *
     * BlesKernOS inicia actualmente sus tareas Ring 3 con IOPL=3 (0x3000):
     * las APIs Win32 built-in siguen siendo funciones del kernel ejecutadas
     * bajo el selector de usuario y algunas rutas usan CLI/STI/HLT o I/O.
     * Borrar IOPL aqui causaba un #GP inmediatamente despues de reanudar una
     * excepcion. Se conserva el IOPL del frame del syscall, no el solicitado
     * por el CONTEXT, de modo que el handler tampoco puede elevarlo.
     */
    /*
     * TF/RF no se restauran todavia: el codigo built-in Win32 comparte el
     * espacio del kernel y una traza de una sola instruccion puede escapar
     * hacia una rutina privilegiada y producir un #DB en Ring 0.
     */
    safe_flags = (context->eflags | 0x202U) &
                 ~((1U << 8) | (3U << 12) | (1U << 14) |
                   (1U << 16) | (1U << 17));
    safe_flags |= regs->eflags & (3U << 12);
    regs->eflags = safe_flags;
    if ((context->seg_cs & 3U) == 3U) regs->cs = context->seg_cs;
    if ((context->seg_ss & 3U) == 3U) regs->ss = context->seg_ss;
    if (context->seg_fs == GDT_USER_FS || context->seg_fs == GDT_USER_DATA)
        regs->fs = context->seg_fs;
}

static uint32_t exception_code_from_vector(uint32_t vector) {
    switch (vector) {
        case 0U: return WIN32_EXCEPTION_INT_DIVIDE_BY_ZERO;
        case 1U: return WIN32_EXCEPTION_SINGLE_STEP;
        case 3U: return WIN32_EXCEPTION_BREAKPOINT;
        case 4U: return WIN32_EXCEPTION_INT_OVERFLOW;
        case 5U: return WIN32_EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
        case 6U: return WIN32_EXCEPTION_ILLEGAL_INSTRUCTION;
        case 13U:
        case 14U: return WIN32_EXCEPTION_ACCESS_VIOLATION;
        default: return 0U;
    }
}

static void call_continue_handlers(uint32_t process_id,
                                   win32_exception_pointers32_t *pointers) {
    for (uint32_t i = 0; i < WIN32_VECTORED_HANDLER_SLOTS; i++) {
        win32_vectored_handler_t handler;
        if (!vectored_continue[i].used ||
            vectored_continue[i].process_id != process_id) continue;
        handler = (win32_vectored_handler_t)vectored_continue[i].handler;
        if (handler) (void)handler(pointers);
    }
}

bool win32_exception_dispatch(win32_exception_record32_t *record,
                              win32_context32_t *context) {
    win32_exception_pointers32_t pointers;
    uint32_t process_id = task_current_process_id();
    uint32_t chain;
    uint32_t visited = 0U;

    if (!record || !context || !win32_process_current_teb()) return false;
    pointers.exception_record = record;
    pointers.context_record = context;

    /* Vectored exception handlers se ejecutan antes de la cadena FS:[0]. */
    for (uint32_t i = 0; i < WIN32_VECTORED_HANDLER_SLOTS; i++) {
        win32_vectored_handler_t handler;
        int32_t result;
        if (!vectored_exception[i].used ||
            vectored_exception[i].process_id != process_id) continue;
        handler = (win32_vectored_handler_t)vectored_exception[i].handler;
        if (!handler) continue;
        result = handler(&pointers);
        if (result == WIN32_EXCEPTION_CONTINUE_EXECUTION &&
            !(record->exception_flags & WIN32_EXCEPTION_NONCONTINUABLE)) {
            call_continue_handlers(process_id, &pointers);
            return true;
        }
    }

    chain = win32_process_get_exception_list();
    while (chain != WIN32_EXCEPTION_CHAIN_END &&
           visited++ < WIN32_EXCEPTION_MAX_CHAIN) {
        win32_exception_registration32_t *frame;
        int32_t disposition;
        if (!frame_in_current_stack(chain, sizeof(*frame))) break;
        frame = (win32_exception_registration32_t *)(uintptr_t)chain;
        if (!frame->handler) break;
        disposition = frame->handler(record, frame, context, NULL);
        if (disposition == WIN32_EXCEPTION_DISPOSITION_CONTINUE_EXECUTION &&
            !(record->exception_flags & WIN32_EXCEPTION_NONCONTINUABLE)) {
            call_continue_handlers(process_id, &pointers);
            return true;
        }
        if (disposition != WIN32_EXCEPTION_DISPOSITION_CONTINUE_SEARCH) break;
        if ((uint32_t)(uintptr_t)frame->previous <= chain &&
            (uint32_t)(uintptr_t)frame->previous != WIN32_EXCEPTION_CHAIN_END)
            break;
        chain = (uint32_t)(uintptr_t)frame->previous;
    }

    return win32_exception_unhandled_filter(&pointers) ==
           WIN32_EXCEPTION_CONTINUE_EXECUTION;
}

void win32_exception_capture_context(win32_context32_t *context,
                                     uint32_t return_eip) {
    uint32_t esp, ebp, eax, ebx, ecx, edx, esi, edi, flags;
    uint16_t cs, ss, ds, es, fs, gs;
    if (!context) return;
    __asm__ volatile ("movl %%esp,%0" : "=r"(esp));
    __asm__ volatile ("movl %%ebp,%0" : "=r"(ebp));
    __asm__ volatile ("movl %%eax,%0" : "=r"(eax));
    __asm__ volatile ("movl %%ebx,%0" : "=r"(ebx));
    __asm__ volatile ("movl %%ecx,%0" : "=r"(ecx));
    __asm__ volatile ("movl %%edx,%0" : "=r"(edx));
    __asm__ volatile ("movl %%esi,%0" : "=r"(esi));
    __asm__ volatile ("movl %%edi,%0" : "=r"(edi));
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    __asm__ volatile ("movw %%cs,%0" : "=r"(cs));
    __asm__ volatile ("movw %%ss,%0" : "=r"(ss));
    __asm__ volatile ("movw %%ds,%0" : "=r"(ds));
    __asm__ volatile ("movw %%es,%0" : "=r"(es));
    __asm__ volatile ("movw %%fs,%0" : "=r"(fs));
    __asm__ volatile ("movw %%gs,%0" : "=r"(gs));
    kmemset(context, 0, sizeof(*context));
    context->context_flags = WIN32_CONTEXT_FULL;
    context->seg_gs = gs; context->seg_fs = fs;
    context->seg_es = es; context->seg_ds = ds;
    context->edi = edi; context->esi = esi; context->ebx = ebx;
    context->edx = edx; context->ecx = ecx; context->eax = eax;
    context->ebp = ebp; context->eip = return_eip;
    context->seg_cs = cs; context->eflags = flags;
    context->esp = esp; context->seg_ss = ss;
}

void win32_exception_raise(uint32_t code, uint32_t flags,
                           uint32_t count, const uint32_t *arguments,
                           uint32_t return_eip) {
    win32_exception_record32_t record;
    win32_context32_t context;
    if (count > WIN32_EXCEPTION_MAXIMUM_PARAMETERS)
        count = WIN32_EXCEPTION_MAXIMUM_PARAMETERS;
    kmemset(&record, 0, sizeof(record));
    record.exception_code = code;
    record.exception_flags = flags | WIN32_EXCEPTION_SOFTWARE_ORIGINATE;
    record.exception_address = (void *)(uintptr_t)return_eip;
    record.number_parameters = count;
    for (uint32_t i = 0; i < count; i++)
        record.exception_information[i] = arguments ? arguments[i] : 0U;
    win32_exception_capture_context(&context, return_eip);
    if (!win32_exception_dispatch(&record, &context)) {
        kprintf("[SEH] excepcion no manejada %x en %x\n",
                code, return_eip);
        pe_win32_terminate_current_process();
    }
}

/* Se ejecuta en Ring 3 tras volver del ISR. */
static void win32_exception_user_trampoline(void) {
    win32_exception_state_t *state = find_state(task_current_pid(), false);
    bool handled = state && state->pending &&
                   win32_exception_dispatch(&state->record, &state->context);
    if (handled) {
        (void)syscall0(SYS_WIN32_EXCEPTION_RETURN);
        /* El syscall vuelve directamente al CONTEXT restaurado. */
    }
    if (state) state->pending = false;
    if (state)
        kprintf("[SEH] excepcion no manejada %x en %x\n",
                state->record.exception_code,
                (uint32_t)(uintptr_t)state->record.exception_address);
    pe_win32_terminate_current_process();
}

bool win32_exception_handle_interrupt(registers_t *regs) {
    win32_exception_state_t *state;
    uint32_t code;
    uint32_t fault_address = 0U;
    if (!regs || (regs->cs & 3U) != 3U || !win32_process_current_teb())
        return false;
    code = exception_code_from_vector(regs->int_no);
    if (!code) return false;
    state = find_state(task_current_pid(), true);
    if (!state || state->pending) return false;

    /*
     * Un handler puede reintentar legalmente la misma instruccion una vez
     * despues de corregir memoria o registros. Si la misma excepcion vuelve
     * una y otra vez, cortar antes de inundar el puerto serie para siempre.
     */
    if (state->last_fault_code == code && state->last_fault_eip == regs->eip) {
        state->repeated_faults++;
        if (state->repeated_faults >= 64U) {
            kprintf("[SEH] bucle de excepciones %x en %x; terminando hilo\n",
                    code, regs->eip);
            return false;
        }
    } else {
        state->last_fault_code = code;
        state->last_fault_eip = regs->eip;
        state->repeated_faults = 1U;
    }

    kmemset(&state->record, 0, sizeof(state->record));
    context_from_registers(&state->context, regs);
    state->record.exception_code = code;
    state->record.exception_address = (void *)(uintptr_t)regs->eip;
    if (regs->int_no == 14U) {
        __asm__ volatile ("movl %%cr2,%0" : "=r"(fault_address));
        state->record.number_parameters = 2U;
        state->record.exception_information[0] =
            (regs->err_code & 0x10U) ? 8U :
            ((regs->err_code & 0x02U) ? 1U : 0U);
        state->record.exception_information[1] = fault_address;
    } else if (regs->int_no == 13U) {
        state->record.number_parameters = 2U;
        state->record.exception_information[0] = 0U;
        state->record.exception_information[1] = 0U;
    }
    state->pending = true;
    regs->eip = (uint32_t)(uintptr_t)win32_exception_user_trampoline;
    kprintf("[SEH] CPU #%u -> %x en %x\n", regs->int_no, code,
            (uint32_t)(uintptr_t)state->record.exception_address);
    return true;
}

bool win32_exception_restore_context(registers_t *regs) {
    win32_exception_state_t *state = find_state(task_current_pid(), false);
    if (!regs || !state || !state->pending) return false;
    registers_from_context(regs, &state->context);
    state->pending = false;
    return true;
}

void *win32_exception_set_unhandled_filter(void *filter) {
    win32_filter_entry_t *entry = find_filter(task_current_process_id(), true);
    void *previous;
    if (!entry) return NULL;
    previous = entry->filter;
    entry->filter = filter;
    return previous;
}

int32_t win32_exception_unhandled_filter(win32_exception_pointers32_t *pointers) {
    win32_filter_entry_t *entry = find_filter(task_current_process_id(), false);
    win32_top_filter_t filter;
    if (!entry || !entry->filter) return WIN32_EXCEPTION_EXECUTE_HANDLER;
    filter = (win32_top_filter_t)entry->filter;
    return filter(pointers);
}

void *win32_exception_add_vectored(bool first, void *handler,
                                   bool continue_handler) {
    win32_vectored_entry_t *table = continue_handler ?
        vectored_continue : vectored_exception;
    uint32_t slot = WIN32_VECTORED_HANDLER_SLOTS;
    if (!handler) return NULL;
    for (uint32_t i = 0; i < WIN32_VECTORED_HANDLER_SLOTS; i++)
        if (!table[i].used) { slot = i; break; }
    if (slot == WIN32_VECTORED_HANDLER_SLOTS) return NULL;
    if (first && slot != 0U) {
        for (uint32_t i = slot; i > 0U; i--) table[i] = table[i - 1U];
        slot = 0U;
    }
    table[slot].used = true;
    table[slot].process_id = task_current_process_id();
    table[slot].handler = handler;
    return &table[slot];
}

bool win32_exception_remove_vectored(void *cookie, bool continue_handler) {
    win32_vectored_entry_t *table = continue_handler ?
        vectored_continue : vectored_exception;
    for (uint32_t i = 0; i < WIN32_VECTORED_HANDLER_SLOTS; i++) {
        if (cookie != &table[i] || !table[i].used ||
            table[i].process_id != task_current_process_id()) continue;
        kmemset(&table[i], 0, sizeof(table[i]));
        return true;
    }
    return false;
}

void win32_exception_cleanup_thread(uint32_t tid) {
    win32_exception_state_t *state = find_state(tid, false);
    if (state) kmemset(state, 0, sizeof(*state));
}

void win32_exception_cleanup_process(uint32_t pid) {
    for (uint32_t i = 0; i < WIN32_EXCEPTION_STATE_SLOTS; i++)
        if (exception_states[i].process_id == pid)
            kmemset(&exception_states[i], 0, sizeof(exception_states[i]));
    for (uint32_t i = 0; i < WIN32_VECTORED_HANDLER_SLOTS; i++) {
        if (vectored_exception[i].process_id == pid)
            kmemset(&vectored_exception[i], 0, sizeof(vectored_exception[i]));
        if (vectored_continue[i].process_id == pid)
            kmemset(&vectored_continue[i], 0, sizeof(vectored_continue[i]));
    }
    for (uint32_t i = 0; i < TASK_MAX; i++)
        if (unhandled_filters[i].process_id == pid)
            kmemset(&unhandled_filters[i], 0, sizeof(unhandled_filters[i]));
}
