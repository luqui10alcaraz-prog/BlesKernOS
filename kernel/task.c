#include "include/task.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/gdt.h"
#include "include/syscall.h"
#include "../gui/gui.h"

/*
 * Scheduler tuning:
 *
 * TASK_QUANTUM_TICKS controls how many real PIT ticks a normal task may keep
 * the CPU before round-robin switches to another READY task. A value of 2 is a
 * good middle point for a small GUI OS: less context-switch overhead than
 * switching every tick, but still responsive.
 *
 * TASK_YIELD_BUDGET_PER_TICK controls how many voluntary yields may return
 * inside the same PIT tick before the task is throttled with HLT. This keeps
 * animation/app loops from going completely uncapped, but avoids the old
 * behavior where every single yield waited a whole hardware tick.
 *
 * You can override both from the Makefile with:
 *   -DTASK_QUANTUM_TICKS=1
 *   -DTASK_YIELD_BUDGET_PER_TICK=1
 */
#ifndef TASK_QUANTUM_TICKS
#define TASK_QUANTUM_TICKS 3U
#endif

#ifndef TASK_YIELD_BUDGET_PER_TICK
#define TASK_YIELD_BUDGET_PER_TICK 8U
#endif

static task_t tasks[TASK_MAX];
static int current_index;
static uint32_t next_pid;
static uint32_t preempt_depth;
static uint32_t schedule_ticks;
static uint32_t busy_ticks;
static uint32_t sample_ticks;
static uint32_t sample_busy;
static uint8_t cpu_usage;
static uint32_t current_quantum;
static uint32_t yield_tick[TASK_MAX];
static uint8_t yield_budget[TASK_MAX];

static void task_user_upcall_return(void) __attribute__((naked));
static void task_user_upcall_return(void) {
    __asm__ volatile (
        "movl %0, %%eax\n"
        "int $0x80\n"
        "ud2\n" : : "i"(SYS_UPCALL_RETURN) : "eax", "memory");
}

static void task_reap_slot(task_t *task) {
    uint32_t process_id;
    bool process_alive = false;

    if (!task) return;
    process_id = task->process_id;
    for (int i = 0; i < TASK_MAX; i++) {
        if (&tasks[i] == task || tasks[i].process_id != process_id) continue;
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING ||
            tasks[i].state == TASK_SLEEPING) {
            process_alive = true;
            break;
        }
    }
    if (!process_alive && process_id) syscall_process_cleanup(process_id);
    if (task->stack) kfree(task->stack);
    if (task->user_stack) kfree(task->user_stack);
    kmemset(task, 0, sizeof(*task));
}

static bool task_running_in_user_cpl(void) {
    uint16_t cs;

    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return (cs & 0x3U) == 0x3U;
}

static void task_copy_launch_arg(task_t *task, const char *launch_arg) {
    if (!task) return;
    if (!launch_arg) {
        task->launch_arg[0] = '\0';
        return;
    }
    kstrncpy(task->launch_arg, launch_arg, TASK_LAUNCH_ARG_LEN - 1);
    task->launch_arg[TASK_LAUNCH_ARG_LEN - 1] = '\0';
}

static int task_pick_next(void) {
    int next = -1;

    /*
     * Un callback de ventana ya encolado representa trabajo de UI que debe
     * completarse antes de que el compositor pueda presentar el contenido.
     * Se prioriza una sola tarea pendiente; despues vuelve a entrar al mismo
     * round-robin que las demas, por lo que no crea un bucle exclusivo.
     */
    for (int checked = 0; checked < TASK_MAX; checked++) {
        int candidate = (current_index + checked + 1) % TASK_MAX;
        if (tasks[candidate].state == TASK_READY &&
            !tasks[candidate].idle && !tasks[candidate].upcall_active &&
            tasks[candidate].upcall_head != tasks[candidate].upcall_tail)
            return candidate;
    }

    /*
     * Prefer real work over idle. Round-robin starts after current_index, so a
     * task that just ran goes to the back of the line once its quantum expires.
     */
    for (int checked = 0; checked < TASK_MAX; checked++) {
        int candidate = (current_index + checked + 1) % TASK_MAX;
        if (tasks[candidate].state == TASK_READY && !tasks[candidate].idle) {
            next = candidate;
            break;
        }
    }

    if (next >= 0) return next;

    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_READY) return i;
    }

    return current_index;
}

static void idle_main(void *argument UNUSED) {
    for (;;) __asm__ volatile ("hlt");
}

static void task_bootstrap(void) {
    task_t *task = &tasks[current_index];
    sti();
    task->entry(task->argument);
    task_exit();
}

static void task_user_bootstrap(void) {
    task_t *task = &tasks[current_index];

    if (task->entry) task->entry(task->argument);
    (void)syscall1(SYS_EXIT, 0);
    for (;;) (void)syscall0(SYS_YIELD);
}

void task_init(void) {
    kmemset(tasks, 0, sizeof(tasks));
    current_index = 0;
    next_pid = 1;
    current_quantum = 0;
    kmemset(yield_tick, 0, sizeof(yield_tick));
    kmemset(yield_budget, 0, sizeof(yield_budget));
    tasks[0].pid = next_pid++;
    tasks[0].process_id = tasks[0].pid;
    kstrcpy(tasks[0].name, "kernel-gui");
    tasks[0].state = TASK_RUNNING;
    tasks[0].system = true;
    tasks[0].memory_bytes = TASK_STACK_SIZE;
    task_create("idle", idle_main, NULL);
    tasks[1].idle = true;
    tasks[1].system = true;
}

static int task_create_internal(const char *name, task_entry_t entry,
                                void *argument, bool user,
                                const char *launch_arg,
                                uint32_t process_id) {
    int index = -1;
    uint32_t *kernel_stack = NULL;
    uint32_t *user_stack = NULL;
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
    if (!kernel_stack) {
        task_preempt_enable();
        return -1;
    }
    if (user) {
        user_stack = (uint32_t *)kzalloc(TASK_STACK_SIZE);
        if (!user_stack) {
            kfree(kernel_stack);
            task_preempt_enable();
            return -1;
        }
    }

    task = &tasks[index];
    if (task->state == TASK_ZOMBIE) task_reap_slot(task);
    kmemset(task, 0, sizeof(*task));
    task->stack = kernel_stack;
    task->user_stack = user_stack;
    task->pid = next_pid++;
    task->process_id = process_id ? process_id : task->pid;
    task->parent_pid = tasks[current_index].process_id
                     ? tasks[current_index].process_id
                     : tasks[current_index].pid;
    task->console_route = tasks[current_index].console_route;
    kstrncpy(task->name, name ? name : (user ? "user" : "task"),
             TASK_NAME_LEN - 1);
    task->context = (registers_t *)((uint8_t *)kernel_stack + TASK_STACK_SIZE -
                                    sizeof(registers_t));
    kmemset(task->context, 0, sizeof(registers_t));
    task->entry = entry;
    task->argument = argument;
    task->context->int_no = 32;

    if (user) {
        task->context->ds = GDT_USER_DATA;
        task->context->es = GDT_USER_DATA;
        task->context->fs = GDT_USER_DATA;
        task->context->gs = GDT_USER_DATA;
        task->context->eip = (uint32_t)(uintptr_t)task_user_bootstrap;
        task->context->cs = GDT_USER_CODE;
        task->context->eflags = 0x3202;
        task->context->useresp = (uint32_t)(uintptr_t)
            ((uint8_t *)user_stack + TASK_STACK_SIZE);
        task->context->ss = GDT_USER_DATA;
        task->user = true;
        task->memory_bytes = TASK_STACK_SIZE * 2U;
    } else {
        task->context->ds = GDT_KERNEL_DATA;
        task->context->es = GDT_KERNEL_DATA;
        task->context->fs = GDT_KERNEL_DATA;
        task->context->gs = GDT_KERNEL_DATA;
        task->context->eip = (uint32_t)(uintptr_t)task_bootstrap;
        task->context->cs = GDT_KERNEL_CODE;
        task->context->eflags = 0x202;
        task->context->useresp = (uint32_t)(uintptr_t)task_exit;
        task->memory_bytes = TASK_STACK_SIZE;
    }

    task_copy_launch_arg(task, launch_arg);
    task->state = TASK_READY;
    yield_tick[index] = 0;
    yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
    task_preempt_enable();
    return (int)task->pid;
}

int task_create(const char *name, task_entry_t entry, void *argument) {
    return task_create_internal(name, entry, argument, tasks[current_index].user,
                                NULL, 0U);
#if 0
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

    /*
     * Los stubs de interrupción restauran segmentos desde registers_t:
     *
     *   pop gs
     *   pop fs
     *   pop es
     *   pop ds
     *
     * Antes sólo inicializábamos DS. Entonces las tareas kernel nuevas
     * arrancaban con ES/FS/GS en 0. En i386, el compilador usa REP MOVSL
     * para copiar structs, y REP MOVSL escribe usando ES:EDI.
     *
     * Resultado en hardware real: GENERAL PROTECTION FAULT (#13) al abrir
     * apps como Calendar, File Browser, Process Manager, etc.
     */
    task->context->ds = GDT_KERNEL_DATA;
    task->context->es = GDT_KERNEL_DATA;
    task->context->fs = GDT_KERNEL_DATA;
    task->context->gs = GDT_KERNEL_DATA;

    task->entry = entry;
    task->argument = argument;
    task->context->int_no = 32;
    task->context->eip = (uint32_t)task_bootstrap;
    task->context->cs = GDT_KERNEL_CODE;
    task->context->eflags = 0x202;
    task->context->useresp = (uint32_t)task_exit;
    task->state = TASK_READY;
    task->memory_bytes = TASK_STACK_SIZE;
    yield_tick[index] = 0;
    yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
    task_preempt_enable();
    return (int)task->pid;
#endif
}

int task_create_user(const char *name, void (*entry)(void)) {
    return task_create_internal(name, (task_entry_t)(uintptr_t)entry, NULL,
                                true, NULL, 0U);
#if 0
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
    yield_tick[index] = 0;
    yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
    task_preempt_enable();
    return (int)task->pid;
#endif
}

int task_create_user_program(const char *name, task_entry_t entry,
                             void *argument, const char *launch_arg) {
    return task_create_internal(name, entry, argument, true, launch_arg, 0U);
}

int task_create_user_thread(const char *name, task_entry_t entry,
                            void *argument, uint32_t process_id) {
    if (!process_id) process_id = tasks[current_index].process_id;
    return task_create_internal(name, entry, argument, true, NULL, process_id);
}

registers_t *task_schedule(registers_t *frame) {
    schedule_ticks++;

    bool ran_quantum = tasks[current_index].state == TASK_RUNNING;
    if (ran_quantum && !tasks[current_index].idle) {
        busy_ticks++;
        sample_busy++;
    }
    tasks[current_index].cpu_ticks++;

    if (++sample_ticks >= 100) {
        cpu_usage = (uint8_t)(sample_busy > 100U ? 100U : sample_busy);
        sample_ticks = 0;
        sample_busy = 0;
    }

    uint32_t now = pit_get_ticks();
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_SLEEPING &&
            (int32_t)(now - tasks[i].wake_tick) >= 0) {
            tasks[i].state = TASK_READY;
        }
    }

    if (preempt_depth || !frame) return frame;

    task_t *current = &tasks[current_index];
    current->context = frame;

    /*
     * Do not round-robin on every timer interrupt. Keeping the same task for a
     * tiny quantum reduces scheduler overhead and makes GUI drawing feel less
     * choppy, while the PIT still enforces a hard limit.
     */
    if (current->state == TASK_RUNNING && !current->idle) {
        if (++current_quantum < TASK_QUANTUM_TICKS) {
            return current->context;
        }
    }

    if (current->state == TASK_RUNNING) {
        current->state = TASK_READY;
    }

    int next = task_pick_next();
    current_index = next;
    current_quantum = 0;

    tasks[current_index].state = TASK_RUNNING;
    if (tasks[current_index].stack) {
        tss_set_kernel_stack((uint32_t)(uintptr_t)
            ((uint8_t *)tasks[current_index].stack + TASK_STACK_SIZE));
    }
    if (tasks[current_index].user && tasks[current_index].user_fs_base) {
        gdt_set_user_fs_base(tasks[current_index].user_fs_base);
        tasks[current_index].context->fs = GDT_USER_FS;
    }
    /*
     * Los callbacks GUI no deben depender de que la aplicacion vuelva a hacer
     * sleep/yield por casualidad. Si hay uno pendiente, se entrega en el mismo
     * instante en que Ring 3 vuelve a obtener CPU. Esto hace que timers,
     * terminales y monitores se refresquen aun sin eventos de teclado/mouse.
     */
    if (tasks[current_index].user)
        (void)task_prepare_user_upcall(tasks[current_index].context);
    return tasks[current_index].context;
}

void task_yield(void) {
    if (task_running_in_user_cpl()) {
        (void)syscall0(SYS_YIELD);
        return;
    }

    /*
     * Voluntary yield must NOT invoke vector 0x20.
     *
     * Vector 0x20 is IRQ0/PIT. Calling "int $0x20" from software can make the
     * kernel treat a yield as a timer tick, so kernel_ticks advances faster than
     * real time. That makes sleeps, GUI timing and games run at superspeed.
     *
     * The old safe version used "sti; hlt" on every yield. That prevented
     * superspeed, but it also forced every animation/app loop to wait a full
     * PIT tick per yield. Instead we allow a small per-task yield budget inside
     * each real PIT tick, then throttle with HLT once the budget is exhausted.
     */
    int index = current_index;

    if (tasks[index].idle || tasks[index].state != TASK_RUNNING) {
        __asm__ volatile ("sti; hlt");
        return;
    }

    uint32_t now = pit_get_ticks();
    if (yield_tick[index] != now) {
        yield_tick[index] = now;
        yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
    }

    if (yield_budget[index] > 0) {
        yield_budget[index]--;
        __asm__ volatile ("pause");
        return;
    }

    __asm__ volatile ("sti; hlt");
}

void task_sleep(uint32_t ticks) {
    if (task_running_in_user_cpl()) {
        (void)syscall1(SYS_SLEEP, ticks ? ticks : 1U);
        return;
    }

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
    if (task_running_in_user_cpl()) {
        (void)syscall1(SYS_EXIT, 0);
        for (;;) (void)syscall0(SYS_YIELD);
    }

    cli();
    tasks[current_index].exit_code = 0;
    tasks[current_index].state = TASK_ZOMBIE;
    sti();
    for (;;) task_yield();
}

void task_exit_from_interrupt(int32_t status) {
    tasks[current_index].exit_code = status;
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
    tasks[current_index].memory_bytes =
        (tasks[current_index].user ? TASK_STACK_SIZE * 2U : TASK_STACK_SIZE) +
        bytes;
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
uint32_t task_current_process_id(void) {
    return tasks[current_index].process_id ? tasks[current_index].process_id
                                           : tasks[current_index].pid;
}
uint32_t task_current_parent_pid(void) {
    return tasks[current_index].parent_pid;
}
uint32_t task_current_console_route(void) {
    return tasks[current_index].console_route;
}
void task_set_current_console_route(uint32_t route) {
    tasks[current_index].console_route = route;
}
bool task_current_is_user(void) { return tasks[current_index].user; }

bool task_queue_user_upcall(uint32_t pid, uint32_t entry,
                            const uint32_t *arguments, uint8_t argument_count,
                            const void *payload, uint8_t payload_size,
                            int8_t payload_argument) {
    task_t *task = NULL;
    uint8_t next;

    if (!pid || !entry || argument_count > TASK_UPCALL_ARGS ||
        payload_size > TASK_UPCALL_PAYLOAD ||
        (payload_size && (!payload || payload_argument < 0 ||
                          payload_argument >= argument_count))) return false;
    task_preempt_disable();
    for (int i = 1; i < TASK_MAX; i++) {
        if ((tasks[i].pid == pid || tasks[i].process_id == pid) &&
            tasks[i].user && tasks[i].state != TASK_UNUSED &&
            tasks[i].state != TASK_ZOMBIE) {
            task = &tasks[i];
            break;
        }
    }
    if (!task) {
        task_preempt_enable();
        return false;
    }
    next = (uint8_t)((task->upcall_head + 1U) % TASK_UPCALL_QUEUE);
    if (next == task->upcall_tail) {
        task_preempt_enable();
        return false;
    }
    task_upcall_t *upcall = &task->upcalls[task->upcall_head];
    kmemset(upcall, 0, sizeof(*upcall));
    upcall->entry = entry;
    upcall->argument_count = argument_count;
    upcall->payload_argument = payload_argument;
    upcall->payload_size = payload_size;
    for (uint8_t i = 0; i < argument_count; i++)
        upcall->arguments[i] = arguments ? arguments[i] : 0U;
    if (payload_size) kmemcpy(upcall->payload, payload, payload_size);
    task->upcall_head = next;
    if (task->state == TASK_SLEEPING) task->state = TASK_READY;
    task_preempt_enable();
    return true;
}

bool task_prepare_user_upcall(registers_t *regs) {
    task_t *task = &tasks[current_index];
    uint32_t stack_limit;
    uint32_t needed;
    uint32_t stack;

    /*
     * Una tarea de usuario tambien puede ser interrumpida mientras ejecuta
     * una API proxy en Ring 0. Ese frame no contiene useresp/ss validos. No
     * retire el callback de la cola hasta volver a un frame Ring 3 real.
     */
    if (!regs || (regs->cs & 3U) != 3U || !task->user || task->upcall_active ||
        task->upcall_head == task->upcall_tail || !task->user_stack)
        return false;
    needed = ((uint32_t)task->upcalls[task->upcall_tail].argument_count + 1U)
           * 4U;
    stack_limit = (uint32_t)(uintptr_t)task->user_stack;
    if (regs->useresp < stack_limit + needed) return false;

    task->active_upcall = task->upcalls[task->upcall_tail];
    task->upcall_tail = (uint8_t)((task->upcall_tail + 1U) % TASK_UPCALL_QUEUE);
    task->upcall_saved_context = *regs;
    task->upcall_active = true;
    if (task->active_upcall.payload_size &&
        task->active_upcall.payload_argument >= 0)
        task->active_upcall.arguments[
            (uint8_t)task->active_upcall.payload_argument] =
            (uint32_t)(uintptr_t)task->active_upcall.payload;
    stack = regs->useresp;
    for (int i = (int)task->active_upcall.argument_count - 1; i >= 0; i--) {
        stack -= 4U;
        *(uint32_t *)(uintptr_t)stack = task->active_upcall.arguments[i];
    }
    stack -= 4U;
    *(uint32_t *)(uintptr_t)stack =
        (uint32_t)(uintptr_t)task_user_upcall_return;
    regs->eip = task->active_upcall.entry;
    regs->useresp = stack;
    regs->eax = 0;
    return true;
}

bool task_finish_user_upcall(registers_t *regs) {
    task_t *task = &tasks[current_index];
    if (!regs || !task->upcall_active) return false;
    if (task->active_upcall.payload_argument == -2 &&
        task->active_upcall.argument_count > 0) {
        gui_window_t *window = (gui_window_t *)(uintptr_t)
            task->active_upcall.arguments[0];
        if (window) {
            gui_surface_t *surface = task->active_upcall.argument_count > 1
                ? (gui_surface_t *)(uintptr_t)
                    task->active_upcall.arguments[1]
                : NULL;
            bool captured;
            window->content_pending = false;
            captured = gui_window_capture_content(window, surface);
            gui_window_end_content_paint(window);
            if (!captured || window->content_repaint) window->dirty = true;
            else gui_desktop_invalidate_rect(gui_get_desktop(),
                                             window->bounds);
            gui_request_paint();
        }
    } else if (task->active_upcall.payload_argument == -3 &&
               task->active_upcall.argument_count > 1) {
        gui_program_t *program = (gui_program_t *)(uintptr_t)
            task->active_upcall.arguments[0];
        gui_desktop_t *desktop = (gui_desktop_t *)(uintptr_t)
            task->active_upcall.arguments[1];
        if (program) gui_program_finish_paint(program);
        if (desktop) gui_desktop_invalidate_all(desktop);
        gui_request_paint();
    } else if (task->active_upcall.payload_argument == -4 &&
               task->active_upcall.argument_count > 0) {
        gui_program_t *program = (gui_program_t *)(uintptr_t)
            task->active_upcall.arguments[0];
        gui_program_release_paint(program);
        kfree(program);
    }
    *regs = task->upcall_saved_context;
    task->upcall_active = false;
    return true;
}

int32_t task_waitpid(uint32_t pid, int32_t *status) {
    uint32_t parent = task_current_process_id();
    bool child_exists = false;

    task_preempt_disable();
    for (int i = 1; i < TASK_MAX; i++) {
        task_t *child = &tasks[i];
        if (child->state == TASK_UNUSED || child->parent_pid != parent)
            continue;
        if (pid != 0U && child->pid != pid && child->process_id != pid)
            continue;
        child_exists = true;
        if (child->state != TASK_ZOMBIE) continue;
        if (status) *status = child->exit_code;
        int32_t result = (int32_t)child->pid;
        task_reap_slot(child);
        task_preempt_enable();
        return result;
    }
    task_preempt_enable();
    return child_exists ? 0 : -1;
}
uint8_t task_cpu_usage(void) { return cpu_usage; }

const char *task_state_name(task_state_t state) {
    if (state == TASK_READY) return "Listo";
    if (state == TASK_RUNNING) return "Ejecutando";
    if (state == TASK_SLEEPING) return "Durmiendo";
    if (state == TASK_ZOMBIE) return "Terminado";
    return "Libre";
}

const char *task_launch_arg(void) {
    return tasks[current_index].launch_arg;
}


bool task_set_user_fs_base(uint32_t pid, uint32_t base) {
    bool changed = false;

    task_preempt_disable();
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE || !tasks[i].user) continue;
        tasks[i].user_fs_base = base;
        if (tasks[i].context)
            tasks[i].context->fs = base ? GDT_USER_FS : GDT_USER_DATA;
        changed = true;
        break;
    }
    task_preempt_enable();
    return changed;
}

bool task_get_user_stack_bounds(uint32_t pid, uint32_t *limit_out,
                                uint32_t *base_out) {
    bool found = false;

    if (!limit_out || !base_out) return false;
    task_preempt_disable();
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE || !tasks[i].user ||
            !tasks[i].user_stack) continue;
        *limit_out = (uint32_t)(uintptr_t)tasks[i].user_stack;
        *base_out = (uint32_t)(uintptr_t)
            ((uint8_t *)tasks[i].user_stack + TASK_STACK_SIZE);
        found = true;
        break;
    }
    task_preempt_enable();
    return found;
}
