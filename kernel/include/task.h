#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "idt.h"

struct gui_window;

/* TASK_MAX también dimensiona tablas PE/ELF/Win32. Los contextos idle son
 * ahora privados por CPU y se asignan fuera de esta tabla, así que los 32
 * slots quedan disponibles para procesos e hilos reales. */
#define TASK_MAX       32
/* Las pilas se dimensionan por perfil y tipo de tarea. Quake 1.09 necesita
 * casi 256 KiB durante SCR_UpdateScreen; el hilo y el adaptador agregan sus
 * propios marcos, así que el caso aislado dispone de 512 KiB. Las tareas
 * comunes y los perfiles de memoria baja no se agrandan. */
#define TASK_STACK_MAX_SIZE   524288U
#define TASK_STACK_SIZE       TASK_STACK_MAX_SIZE
#define TASK_STACK_GUARD_SIZE 1024U
#define TASK_NAME_LEN      24
#define TASK_LAUNCH_ARG_LEN 256
#define TASK_UPCALL_QUEUE 16
#define TASK_UPCALL_ARGS 4
#define TASK_UPCALL_PAYLOAD 64
#define TASK_FPU_STATE_SIZE 108U

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;

typedef void (*task_entry_t)(void *argument);

typedef struct {
    uint32_t entry;
    uint32_t arguments[TASK_UPCALL_ARGS];
    uint8_t argument_count;
    int8_t payload_argument;
    uint8_t payload_size;
    uint8_t payload[TASK_UPCALL_PAYLOAD];
    /* Optional user-visible heap block owned by the queued callback.  The
     * kernel releases it only after the Ring-3 callback returns (or when the
     * target task dies before delivery). */
    void *release_after_upcall;
    /* Set only by task_queue_window_upcall(). Generic PE/Win32 callbacks may
     * use the same payload_argument values, but their arguments[0] is not a
     * native gui_window pointer. */
    struct gui_window *window_target;
    bool invalidate_valid;
    int32_t invalidate_x;
    int32_t invalidate_y;
    int32_t invalidate_w;
    int32_t invalidate_h;
} task_upcall_t;

typedef struct {
    uint32_t pid;
    uint32_t process_id;
    uint32_t parent_pid;
    char name[TASK_NAME_LEN];
    task_state_t state;
    /* -1 means not executing / no fixed affinity. Kernel and Win16 tasks are
     * pinned to CPU0 until those legacy subsystems become re-entrant. */
    /* Scheduler placement. running_cpu and queued_cpu are 32-bit so the
     * scalable scheduler can update them atomically without byte races. */
    volatile int32_t running_cpu;
    int8_t affinity_cpu;          /* legacy single-CPU pin, -1 = mask */
    uint8_t preferred_cpu;
    uint8_t last_cpu;
    uint8_t scheduler_reserved;
    volatile int32_t queued_cpu;  /* -1 none, -2 transient claim, >=0 runqueue */
    uint32_t affinity_mask;       /* bit per allowed CPU; zero = all online */
    uint32_t *stack;
    uint32_t *user_stack;
    /* Raw heap blocks backing the page-aligned guarded stacks. The exposed
     * stack pointers intentionally skip a non-present page at each edge. */
    void *stack_allocation;
    void *user_stack_allocation;
    uint32_t stack_size;
    uint32_t user_stack_size;
    registers_t *context;
    task_entry_t entry;
    void *argument;
    uint32_t cpu_ticks;
    uint32_t wake_tick;
    bool idle;
    bool system;
    bool user;
    bool win16;
    /* Native user threads share process_id/resources but keep independent
     * stacks, register/FPU state, affinity and TLS. pid acts as the TID. */
    bool thread;
    bool thread_joinable;
    bool exit_requested;
    bool resources_released;
    int32_t exit_code;
    uint32_t memory_bytes;
    /* Canal de consola heredado por procesos hijos. Cero significa que la
       tarea escribe solamente en la consola/COM1 del sistema. */
    uint32_t console_route;
    uint32_t user_fs_base;
    /* Phase 1 keeps kernel BSS supervisor-only. Public APIs that historically
     * returned const char pointers therefore need per-task user-heap mirrors
     * rather than exposing kernel strings directly. */
    void *user_string_exports;
    void *user_string_overflow;
    uint32_t user_string_export_count;
    void *user_task_snapshot;
    struct gui_window *window;
    char launch_arg[TASK_LAUNCH_ARG_LEN];
    task_upcall_t upcalls[TASK_UPCALL_QUEUE];
    uint8_t upcall_head;
    uint8_t upcall_tail;
    bool upcall_active;
    task_upcall_t active_upcall;
    registers_t upcall_saved_context;
    /* An asynchronous Ring-3 upcall may interrupt the application in the
       middle of an x87 expression. Keep that interrupted x87 image separate
       from the callback's normal per-task FPU image. */
    uint8_t upcall_fpu_state[TASK_FPU_STATE_SIZE] __attribute__((aligned(16)));
    bool upcall_fpu_saved;
    /* Guard for public API proxy execution. It belongs to the task rather
     * than the CPU because an explicit task_yield() may migrate a suspended
     * kernel frame to another processor. */
    uint32_t api_guard_depth;
    const char *api_guard_name;
    uint32_t api_guard_target;
    uint32_t api_guard_token;
    /* A user task may only be switched away from a CPL0 frame after it has
     * explicitly dropped its subsystem-domain locks in task_yield()/task_sleep().
     * This bit belongs to the task because the suspended kernel frame may
     * later resume on another CPU. */
    bool kernel_switch_safe;
    /* GCC i386 uses the x87 stack for float/double.  This state must be
     * private to each task; otherwise a timer switch can corrupt TinyGL and
     * any application doing floating-point work. */
    uint8_t fpu_state[TASK_FPU_STATE_SIZE] __attribute__((aligned(16)));
    bool fpu_state_valid;
} task_t;

typedef struct {
    uint32_t pid;
    uint32_t process_id;
    char name[TASK_NAME_LEN];
    task_state_t state;
    uint32_t cpu_ticks;
    uint32_t stack_bytes;
    uint32_t memory_hint_bytes;
    bool system;
    bool user;
    bool exit_requested;
} task_public_snapshot_t;

void task_init(void);
int task_create(const char *name, task_entry_t entry, void *argument);
int task_create_user(const char *name, void (*entry)(void));
int task_create_user_program(const char *name, task_entry_t entry,
                             void *argument, const char *launch_arg);
int task_create_user16_program(const char *name, uint16_t cs, uint16_t ip,
                               uint16_t ss, uint16_t sp, uint16_t ds,
                               uint16_t es, uint16_t stack_size,
                               uint16_t heap_size, const char *launch_arg);

/* BLES_WINE_KERNEL_WORKER_API_20260723 */
int task_create_kernel(const char *name, task_entry_t entry,
                       void *argument);
int task_create_user_thread(const char *name, task_entry_t entry,
                            void *argument, uint32_t process_id);
int task_create_user_thread_ex(const char *name, task_entry_t entry,
                               void *argument, uint32_t process_id,
                               uint32_t user_stack_size, bool joinable);
registers_t *task_schedule(registers_t *current_frame);
/* Local-APIC timers on APs use a non-blocking scheduler admission path.
 * A missed quantum is harmless; spinning 15 CPUs inside an interrupt is not. */
registers_t *task_schedule_lapic(registers_t *current_frame);
registers_t *task_schedule_reschedule_ipi(registers_t *current_frame);
void task_yield(void);
void task_sleep(uint32_t ticks);
void task_sleep_from_interrupt(uint32_t ticks);
void task_exit(void) NORETURN;
void task_exit_from_interrupt(int32_t status);
/* Non-blocking join primitive used by the user ABI: >0 completed TID,
 * 0 still running, negative on invalid ownership/state. */
int32_t task_thread_join_try(uint32_t tid, int32_t *status);
bool task_thread_detach(uint32_t tid);
uint32_t task_current_tid(void);
bool task_set_current_tls_base(uint32_t base);
uint32_t task_current_tls_base(void);
uint32_t task_thread_count(uint32_t process_id);
/* Slow zombie cleanup runs in process context, never in the timer spinlock. */
void task_reap_deferred(void);
void task_preempt_disable(void);
void task_preempt_enable(void);
/* Internal stability hooks. A user task running in CPL0 is non-preemptible
 * unless it reaches an explicit safe point after dropping domain locks. API thunks
 * can also reject calls before exhausting the task's kernel stack. */
void task_allow_kernel_switch_once(void);
uint32_t task_kernel_stack_remaining(void);
void task_user_api_guard_enter(const char *name, uint32_t target,
                               uint32_t token);
void task_user_api_guard_leave(void);
bool task_user_api_guard_info(const char **name_out, uint32_t *target_out,
                              uint32_t *token_out);
void task_user_api_guard_clear(void);
bool task_request_exit(uint32_t pid);
bool task_request_exit_process(uint32_t process_id, int32_t exit_code);
bool task_query_pid(uint32_t pid, bool *active, int32_t *exit_code,
                    uint32_t *process_id);
bool task_query_process(uint32_t process_id, bool *active,
                        int32_t *exit_code, uint32_t *main_pid);
bool task_exit_requested(void);
void task_set_memory_hint(uint32_t bytes);
uint32_t task_process_memory_bytes(uint32_t process_id);
void task_bind_window(struct gui_window *window);
uint32_t task_count(void);
uint32_t task_snapshot_public(task_public_snapshot_t *out, uint32_t capacity);
const task_t *task_get(uint32_t index);
uint32_t task_current_pid(void);
uint32_t task_current_process_id(void);
uint32_t task_current_parent_pid(void);
uint32_t task_current_console_route(void);
void task_set_current_console_route(uint32_t route);
bool task_current_is_user(void);
bool task_current_is_win16(void);
bool task_current_is_idle(void);
uint32_t task_current_cpu(void);

/* Internal SMP bootstrap hooks used by smp.c. */
bool task_smp_prepare_cpu(uint32_t cpu_index, void *stack_allocation,
                          uint32_t *stack, uint32_t stack_size);
void task_smp_ap_online(uint32_t cpu_index);
/* Called by smp_start_scheduler() while interrupts are masked. It seeds the
 * per-CPU runqueues and removes idle contexts from the normal process table. */
bool task_smp_scheduler_start(void);
void task_fpu_prepare_secondary_cpu(void);
int32_t task_waitpid(uint32_t pid, int32_t *status);
bool task_queue_user_upcall(uint32_t pid, uint32_t entry,
                            const uint32_t *arguments, uint8_t argument_count,
                            const void *payload, uint8_t payload_size,
                            int8_t payload_argument);
bool task_queue_window_upcall(struct gui_window *window,
                              uint32_t pid, uint32_t entry,
                              const uint32_t *arguments,
                              uint8_t argument_count,
                              const void *payload, uint8_t payload_size,
                              int8_t payload_argument);
/* Queue a callback whose arguments contain one temporary user-visible heap
 * block. Ownership transfers only on success; the block is freed after the
 * callback returns or if the target task exits before consuming it. */
bool task_queue_user_upcall_owned(uint32_t pid, uint32_t entry,
                                  const uint32_t *arguments,
                                  uint8_t argument_count,
                                  void *release_after_upcall);
/* Cancel queued GUI callbacks that still reference a window before it is
 * freed. Returns false while a callback for that window is already active. */
bool task_cancel_window_upcalls(struct gui_window *window);
bool task_prepare_user_upcall(registers_t *regs);
bool task_finish_user_upcall(registers_t *regs);
uint8_t task_cpu_usage(void);
uint32_t task_cpu_count(void);
uint8_t task_cpu_usage_for(uint32_t cpu_index);
uint32_t task_runqueue_depth(uint32_t cpu_index);
uint32_t task_scheduler_steals(uint32_t cpu_index);
uint32_t task_scheduler_migrations(uint32_t cpu_index);
uint32_t task_scheduler_ipis(uint32_t cpu_index);
bool task_set_affinity_mask(uint32_t pid, uint32_t mask);
uint32_t task_get_affinity_mask(uint32_t pid);
const char *task_state_name(task_state_t state);
const char *task_launch_arg(void);
bool task_set_user_fs_base(uint32_t pid, uint32_t base);
bool task_get_user_stack_bounds(uint32_t pid, uint32_t *limit_out,
                                uint32_t *base_out);
/* Export a kernel-owned string through a stable user-heap mirror for the
 * current Ring-3 task. Kernel callers receive the original pointer. */
const char *task_user_export_string(const char *source);
/* Legacy bk_proc_get() compatibility without exposing the supervisor-only
 * scheduler table. The returned copy lives in the current task's user heap. */
const task_t *task_user_export_snapshot(const task_t *source);

#endif
