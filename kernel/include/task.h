#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "idt.h"

struct gui_window;

#define TASK_MAX       16
/* Doom y otras aplicaciones más pesadas necesitan más stack que el valor
 * original de 16 KiB para no colapsar al arrancar. */
#define TASK_STACK_SIZE 65536
#define TASK_NAME_LEN   24

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;

typedef void (*task_entry_t)(void *argument);

typedef struct {
    uint32_t pid;
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
    uint32_t memory_bytes;
    struct gui_window *window;
} task_t;

void task_init(void);
int task_create(const char *name, task_entry_t entry, void *argument);
int task_create_user(const char *name, void (*entry)(void));
registers_t *task_schedule(registers_t *current_frame);
void task_yield(void);
void task_sleep(uint32_t ticks);
void task_sleep_from_interrupt(uint32_t ticks);
void task_exit(void) NORETURN;
void task_exit_from_interrupt(void);
void task_preempt_disable(void);
void task_preempt_enable(void);
bool task_request_exit(uint32_t pid);
bool task_exit_requested(void);
void task_set_memory_hint(uint32_t bytes);
void task_bind_window(struct gui_window *window);
uint32_t task_count(void);
const task_t *task_get(uint32_t index);
uint32_t task_current_pid(void);
uint8_t task_cpu_usage(void);
const char *task_state_name(task_state_t state);

#endif
