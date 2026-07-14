#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "idt.h"

struct gui_window;

#define TASK_MAX       16
/* Doom y otras aplicaciones más pesadas necesitan más stack que el valor
 * original de 16 KiB para no colapsar al arrancar. */
#define TASK_STACK_SIZE    65536
#define TASK_NAME_LEN      24
#define TASK_LAUNCH_ARG_LEN 256
#define TASK_UPCALL_QUEUE 8
#define TASK_UPCALL_ARGS 4
#define TASK_UPCALL_PAYLOAD 64

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
} task_upcall_t;

typedef struct {
    uint32_t pid;
    uint32_t process_id;
    uint32_t parent_pid;
    char name[TASK_NAME_LEN];
    task_state_t state;
    uint32_t *stack;
    uint32_t *user_stack;
    registers_t *context;
    task_entry_t entry;
    void *argument;
    uint32_t cpu_ticks;
    uint32_t wake_tick;
    bool idle;
    bool system;
    bool user;
    bool exit_requested;
    int32_t exit_code;
    uint32_t memory_bytes;
    /* Canal de consola heredado por procesos hijos. Cero significa que la
       tarea escribe solamente en la consola/COM1 del sistema. */
    uint32_t console_route;
    uint32_t user_fs_base;
    struct gui_window *window;
    char launch_arg[TASK_LAUNCH_ARG_LEN];
    task_upcall_t upcalls[TASK_UPCALL_QUEUE];
    uint8_t upcall_head;
    uint8_t upcall_tail;
    bool upcall_active;
    task_upcall_t active_upcall;
    registers_t upcall_saved_context;
} task_t;

void task_init(void);
int task_create(const char *name, task_entry_t entry, void *argument);
int task_create_user(const char *name, void (*entry)(void));
int task_create_user_program(const char *name, task_entry_t entry,
                             void *argument, const char *launch_arg);
int task_create_user_thread(const char *name, task_entry_t entry,
                            void *argument, uint32_t process_id);
registers_t *task_schedule(registers_t *current_frame);
void task_yield(void);
void task_sleep(uint32_t ticks);
void task_sleep_from_interrupt(uint32_t ticks);
void task_exit(void) NORETURN;
void task_exit_from_interrupt(int32_t status);
void task_preempt_disable(void);
void task_preempt_enable(void);
bool task_request_exit(uint32_t pid);
bool task_exit_requested(void);
void task_set_memory_hint(uint32_t bytes);
void task_bind_window(struct gui_window *window);
uint32_t task_count(void);
const task_t *task_get(uint32_t index);
uint32_t task_current_pid(void);
uint32_t task_current_process_id(void);
uint32_t task_current_parent_pid(void);
uint32_t task_current_console_route(void);
void task_set_current_console_route(uint32_t route);
bool task_current_is_user(void);
int32_t task_waitpid(uint32_t pid, int32_t *status);
bool task_queue_user_upcall(uint32_t pid, uint32_t entry,
                            const uint32_t *arguments, uint8_t argument_count,
                            const void *payload, uint8_t payload_size,
                            int8_t payload_argument);
bool task_prepare_user_upcall(registers_t *regs);
bool task_finish_user_upcall(registers_t *regs);
uint8_t task_cpu_usage(void);
const char *task_state_name(task_state_t state);
const char *task_launch_arg(void);
bool task_set_user_fs_base(uint32_t pid, uint32_t base);
bool task_get_user_stack_bounds(uint32_t pid, uint32_t *limit_out,
                                uint32_t *base_out);

#endif
