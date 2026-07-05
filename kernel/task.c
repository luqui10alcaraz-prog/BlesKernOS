#include "include/task.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/gdt.h"

static task_t tasks[TASK_MAX];
static int current_index;
static uint32_t next_pid;
static uint32_t preempt_depth;
static uint32_t schedule_ticks;
static uint32_t busy_ticks;
static uint32_t sample_ticks;
static uint32_t sample_busy;
static uint8_t cpu_usage;

static void idle_main(void *argument UNUSED) {
    for (;;) __asm__ volatile ("hlt");
}

static void task_bootstrap(void) {
    task_t *task = &tasks[current_index];
    sti();
    task->entry(task->argument);
    task_exit();
}

void task_init(void) {
    kmemset(tasks, 0, sizeof(tasks));
    current_index = 0;
    next_pid = 1;
    tasks[0].pid = next_pid++;
    kstrcpy(tasks[0].name, "kernel-gui");
    tasks[0].state = TASK_RUNNING;
    tasks[0].system = true;
    tasks[0].memory_bytes = TASK_STACK_SIZE;
    task_create("idle", idle_main, NULL);
    tasks[1].idle = true;
    tasks[1].system = true;
}

int task_create(const char *name, task_entry_t entry, void *argument) {
    if (!entry) return -1;
    task_preempt_disable();
    int index = -1;
    for (int i = 1; i < TASK_MAX; i++)
        if (tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE) {
            index = i;
            break;
        }
    if (index < 0) {
        task_preempt_enable();
        return -1;
    }

    uint32_t *stack = (uint32_t *)kzalloc(TASK_STACK_SIZE);
    if (!stack) {
        task_preempt_enable();
        return -1;
    }
    task_t *task = &tasks[index];
    kmemset(task, 0, sizeof(*task));
    task->stack = stack;
    task->pid = next_pid++;
    kstrncpy(task->name, name ? name : "task", TASK_NAME_LEN - 1);
    task->context = (registers_t *)((uint8_t *)stack + TASK_STACK_SIZE -
                                    sizeof(registers_t));
    kmemset(task->context, 0, sizeof(registers_t));
    task->context->ds = 0x10;
    task->entry = entry;
    task->argument = argument;
    task->context->int_no = 32;
    task->context->eip = (uint32_t)task_bootstrap;
    task->context->cs = 0x08;
    task->context->eflags = 0x202;
    task->context->useresp = (uint32_t)task_exit;
    task->state = TASK_READY;
    task->memory_bytes = TASK_STACK_SIZE;
    task_preempt_enable();
    return (int)task->pid;
}

int task_create_user(const char *name, void (*entry)(void)) {
    int index = -1;
    uint32_t *kernel_stack;
    uint32_t *user_stack;
    task_t *task;

    if (!entry) return -1;
    task_preempt_disable();
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        task_preempt_enable();
        return -1;
    }

    kernel_stack = (uint32_t *)kzalloc(TASK_STACK_SIZE);
    user_stack = (uint32_t *)kzalloc(TASK_STACK_SIZE);
    if (!kernel_stack || !user_stack) {
        if (kernel_stack) kfree(kernel_stack);
        if (user_stack) kfree(user_stack);
        task_preempt_enable();
        return -1;
    }

    task = &tasks[index];
    kmemset(task, 0, sizeof(*task));
    task->stack = kernel_stack;
    task->user_stack = user_stack;
    task->pid = next_pid++;
    kstrncpy(task->name, name ? name : "user", TASK_NAME_LEN - 1);
    task->context = (registers_t *)((uint8_t *)kernel_stack + TASK_STACK_SIZE -
                                    sizeof(registers_t));
    kmemset(task->context, 0, sizeof(registers_t));
    task->context->ds = GDT_USER_DATA;
    task->context->es = GDT_USER_DATA;
    task->context->fs = GDT_USER_DATA;
    task->context->gs = GDT_USER_DATA;
    task->context->eip = (uint32_t)(uintptr_t)entry;
    task->context->cs = GDT_USER_CODE;
    task->context->eflags = 0x202;
    task->context->useresp = (uint32_t)(uintptr_t)
        ((uint8_t *)user_stack + TASK_STACK_SIZE);
    task->context->ss = GDT_USER_DATA;
    task->state = TASK_READY;
    task->user = true;
    task->memory_bytes = TASK_STACK_SIZE * 2U;
    task_preempt_enable();
    return (int)task->pid;
}

registers_t *task_schedule(registers_t *frame) {
    schedule_ticks++;
    bool ran_quantum = tasks[current_index].state == TASK_RUNNING;
    if (ran_quantum && !tasks[current_index].idle) busy_ticks++;
    tasks[current_index].cpu_ticks++;
    if (++sample_ticks >= 100) {
        cpu_usage = (uint8_t)sample_busy;
        sample_ticks = 0;
        sample_busy = 0;
    } else if (ran_quantum && !tasks[current_index].idle) {
        sample_busy++;
    }

    uint32_t now = pit_get_ticks();
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].state == TASK_SLEEPING &&
            (int32_t)(now - tasks[i].wake_tick) >= 0)
            tasks[i].state = TASK_READY;

    if (preempt_depth || !frame) return frame;
    tasks[current_index].context = frame;
    if (tasks[current_index].state == TASK_RUNNING)
        tasks[current_index].state = TASK_READY;

    int next = -1;
    for (int checked = 0; checked < TASK_MAX; checked++) {
        int candidate = (current_index + checked + 1) % TASK_MAX;
        if (tasks[candidate].state == TASK_READY && !tasks[candidate].idle) {
            next = candidate;
            break;
        }
    }
    if (next < 0) {
        for (int i = 0; i < TASK_MAX; i++) {
            if (tasks[i].state == TASK_READY) {
                next = i;
                break;
            }
        }
    }
    if (next < 0) next = current_index;
    current_index = next;
    tasks[current_index].state = TASK_RUNNING;
    if (tasks[current_index].stack) {
        tss_set_kernel_stack((uint32_t)(uintptr_t)
            ((uint8_t *)tasks[current_index].stack + TASK_STACK_SIZE));
    }
    return tasks[current_index].context;
}

void task_yield(void) {
    /*
     * Voluntary yield must NOT invoke vector 0x20.
     *
     * Vector 0x20 is IRQ0/PIT. Calling "int $0x20" from software can make the
     * kernel treat a yield as a timer tick, so kernel_ticks advances faster than
     * real time. That makes sleeps, GUI timing and games run at superspeed.
     *
     * Instead, halt until the next real hardware interrupt. The real PIT IRQ
     * will enter the normal scheduler path without faking time.
     */
    __asm__ volatile ("sti; hlt");
}

void task_sleep(uint32_t ticks) {
    if (ticks == 0) ticks = 1;

    cli();
    tasks[current_index].wake_tick = pit_get_ticks() + ticks;
    tasks[current_index].state = TASK_SLEEPING;
    sti();
    task_yield();
}

void task_sleep_from_interrupt(uint32_t ticks) {
    tasks[current_index].wake_tick = pit_get_ticks() + (ticks ? ticks : 1);
    tasks[current_index].state = TASK_SLEEPING;
}

void task_exit(void) {
    cli();
    tasks[current_index].state = TASK_ZOMBIE;
    sti();
    for (;;) task_yield();
}

void task_exit_from_interrupt(void) {
    tasks[current_index].state = TASK_ZOMBIE;
}

void task_preempt_disable(void) {
    preempt_depth++;
}

void task_preempt_enable(void) {
    if (preempt_depth) preempt_depth--;
}

bool task_request_exit(uint32_t pid) {
    bool result = false;
    task_preempt_disable();
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED) continue;
        if (tasks[i].system || tasks[i].idle) break;
        tasks[i].exit_requested = true;
        if (tasks[i].state == TASK_SLEEPING) tasks[i].state = TASK_READY;
        result = true;
        break;
    }
    task_preempt_enable();
    return result;
}

bool task_exit_requested(void) {
    return tasks[current_index].exit_requested;
}

void task_set_memory_hint(uint32_t bytes) {
    tasks[current_index].memory_bytes = TASK_STACK_SIZE + bytes;
}

void task_bind_window(struct gui_window *window) {
    tasks[current_index].window = window;
}

uint32_t task_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].state != TASK_UNUSED &&
            tasks[i].state != TASK_ZOMBIE) count++;
    return count;
}

const task_t *task_get(uint32_t index) {
    uint32_t found = 0;
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].state != TASK_UNUSED &&
            tasks[i].state != TASK_ZOMBIE) {
            if (found++ == index) return &tasks[i];
        }
    return NULL;
}

uint32_t task_current_pid(void) { return tasks[current_index].pid; }
uint8_t task_cpu_usage(void) { return cpu_usage; }

const char *task_state_name(task_state_t state) {
    if (state == TASK_READY) return "Listo";
    if (state == TASK_RUNNING) return "Ejecutando";
    if (state == TASK_SLEEPING) return "Durmiendo";
    if (state == TASK_ZOMBIE) return "Terminado";
    return "Libre";
}
