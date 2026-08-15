#include "include/task.h"
#include "include/memory.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/gdt.h"
#include "include/syscall.h"
#include "include/vga.h"
#include "include/elf_loader.h"
#include "include/pe_loader.h"
#include "include/ne_loader.h"
#include "include/perfmon.h"
#include "include/compat_mode.h"
#include "include/smp.h"
#include "include/klock.h"
#include "include/kernel_domains.h"
#include "include/paging.h"
#include "include/smp_watchdog.h"
#include "include/scheduler_smp.h"
#include "include/usercopy.h"
#include "../gui/gui.h"

/* El detalle de cada DllMain es útil al depurar Wine, pero escribir tres
 * líneas por módulo en COM1 penaliza perceptiblemente la apertura normal. */
#define TASK_TRACE_DLLMAIN 0

/*
 * Scheduler tuning:
 *
 * TASK_QUANTUM_MILLISECONDS controls the real duration of a normal quantum.
 * Keeping it in milliseconds allows the Pentium II/III profile to lower PIT
 * and LAPIC timers to 100 Hz without turning a two-tick quantum into 20 ms.
 *
 * TASK_YIELD_BUDGET_PER_TICK controls how many voluntary yields may return
 * inside the same PIT tick before the task is throttled with HLT. This keeps
 * animation/app loops from going completely uncapped, but avoids the old
 * behavior where every single yield waited a whole hardware tick.
 *
 * You can override both from the Makefile with:
 *   -DTASK_QUANTUM_MILLISECONDS=10
 *   -DTASK_YIELD_BUDGET_PER_TICK=1
 */
#ifndef TASK_QUANTUM_MILLISECONDS
#define TASK_QUANTUM_MILLISECONDS 10U
#endif

#ifndef TASK_YIELD_BUDGET_PER_TICK
#define TASK_YIELD_BUDGET_PER_TICK 8U
#endif

static task_t tasks[TASK_MAX];
typedef struct {
    /* -1 means the private idle context for this CPU. Idle contexts are not
     * process-table entries and therefore do not consume TASK_MAX slots. */
    int current_slot;
    uint32_t preempt_nesting;
    uint32_t quantum_ticks;
    int rr_cursor; /* used only by the pre-SMP compatibility scheduler */
    int deferred_ready_slot; /* published after ESP has left the old stack */
    uint32_t watchdog_ticks;
} task_cpu_state_t;

static task_cpu_state_t cpu_states[SMP_MAX_CPUS];
static task_t *g_idle_tasks;
static uint32_t next_pid;
static uint32_t sample_ticks;
static uint32_t sample_busy;
static uint8_t cpu_usage;
static uint32_t cpu_sample_ticks[SMP_MAX_CPUS];
static uint32_t cpu_sample_busy[SMP_MAX_CPUS];
static uint8_t cpu_usage_per_cpu[SMP_MAX_CPUS];
static uint32_t g_quantum_limit_ticks;
static uint32_t yield_tick[TASK_MAX];
static uint8_t yield_budget[TASK_MAX];
static uint8_t g_initial_fpu_state[TASK_FPU_STATE_SIZE] __attribute__((aligned(16)));
static kspinlock_t g_task_table_lock = KSPINLOCK_INITIALIZER;
/* A slot is reserved while its stacks are allocated outside the scheduler
 * spinlock. This keeps the hot scheduling lock short even for 128 KiB stacks. */
static uint8_t g_task_slot_reserved[TASK_MAX];
static uint8_t g_task_reap_claim[TASK_MAX];
/* task_get() historically returns a pointer. Return a per-CPU immutable copy
 * instead of exposing a live task-table slot to Process Manager readers. */
static task_t g_task_query_snapshot[SMP_MAX_CPUS];

/* Completed joinable threads are copied here before their task slot is
 * recycled. This prevents the asynchronous reaper from racing a later
 * bk_thread_join() while keeping zombie stacks out of TASK_MAX. */
typedef struct {
    uint32_t tid;
    uint32_t process_id;
    int32_t result;
    uint32_t sequence;
    bool valid;
} task_thread_completion_t;

static task_thread_completion_t g_thread_completions[TASK_MAX];
static uint32_t g_thread_completion_sequence;

#define TASK_STACK_GUARD_BYTE 0xA5U
/* Leave enough room for the timer/exception frame and the containment path.
 * If a user API drives ESP below this reserve, discard only that process
 * before the stack reaches heap metadata or another allocation. */
#define TASK_KERNEL_STACK_EMERGENCY_RESERVE 8192U
#define TASK_STACK_GUARD_PROBE_BYTES 32U

/* Small CPL3-only gates. They live in the shared user heap because Phase 1
 * correctly marks kernel .text and .bss supervisor-only. */
#define TASK_USER_GATE_SIZE 32U
static uint8_t *g_user_process_exit_gate;
static uint8_t *g_user_upcall_return_gate;

/* A large part of the historical public ABI returns const char pointers.
 * Those pointers used to refer directly to kernel .rodata/.bss, which is no
 * longer user-accessible after Phase 1. Cache mirrors by source identity so
 * stable strings remain stable without allocating on every frame. */
#define TASK_USER_STRING_EXPORT_LIMIT 64U
typedef struct task_user_string_export {
    const char *source;
    char *copy;
    uint32_t capacity;
    struct task_user_string_export *next;
} task_user_string_export_t;

static task_cpu_state_t *task_cpu_local(void) {
    uint32_t cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    return &cpu_states[cpu];
}

#define current_index        (task_cpu_local()->current_slot)
#define preempt_depth        (task_cpu_local()->preempt_nesting)
#define current_quantum      (task_cpu_local()->quantum_ticks)
#define round_robin_cursor   (task_cpu_local()->rr_cursor)

static task_t *task_idle_for_cpu(uint32_t cpu) {
    if (!g_idle_tasks || cpu >= SMP_MAX_CPUS) return NULL;
    return &g_idle_tasks[cpu];
}

static task_t *task_from_slot_cpu(int slot, uint32_t cpu) {
    if (slot >= 0 && slot < TASK_MAX) return &tasks[slot];
    return task_idle_for_cpu(cpu);
}

static task_t *task_current_entry(void) {
    uint32_t cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    return task_from_slot_cpu(cpu_states[cpu].current_slot, cpu);
}

#define X86_EFLAGS_IF 0x200U

/*
 * Context-switch handoff rule
 * ---------------------------
 *
 * task_schedule() is called only from an interrupt/trap frame.  Once it has
 * changed cpu_states[cpu].current_slot, the CPU must not accept another
 * interrupt until the assembly stub has installed the returned stack pointer.
 *
 * This matters especially for int 0x80: it is a trap gate, so IF is normally
 * still set.  Restoring the scheduler spinlock's saved IF before the caller
 * executes `mov esp, eax` creates a short but fatal split-brain window:
 * current_slot already names task B while ESP still points into task A's
 * kernel stack.  A LAPIC tick in that window stores B's context on A's stack,
 * and the later syscall IRET tries to load a random CS (observed as #GP with
 * selectors such as 0x009C and 0x3004).
 *
 * Release the table lock while keeping local IRQs masked.  IRET restores IF
 * from the selected task's saved EFLAGS after ESP, CS and SS are coherent.
 */
static void task_scheduler_unlock_for_iret(uint32_t scheduler_flags) {
    kspin_unlock_irqrestore(&g_task_table_lock,
                            scheduler_flags & ~X86_EFLAGS_IF);
}


/* Native Ring-3 programs use the flat data selector as part of their ABI.
 * Normalize frames before they are persisted: otherwise a frame captured
 * during an API/upcall transition can preserve null selectors and replay
 * them on every later context switch. */
static void task_normalize_user_segments(task_t *task, registers_t *regs) {
    if (!task || !regs || !task->user || task->win16 ||
        (regs->cs & 3U) != 3U)
        return;
    regs->ds = GDT_USER_DATA;
    regs->es = GDT_USER_DATA;
    regs->gs = GDT_USER_DATA;
    regs->fs = task->user_fs_base ? GDT_USER_FS : GDT_USER_DATA;
}

/* BLES_TASK_X87_CONTEXT_20260731
 * Native and SDK code is compiled with -mfpmath=387.  FSAVE/FRSTOR works on
 * the i386-class CPUs supported by BlesKernOS and avoids requiring SSE/FXSR.
 * The scheduler runs with interrupts masked by the ISR gate, so the save and
 * restore form one atomic part of a context switch. */
static void task_fpu_prepare_cpu(void) {
    uint32_t cr0;
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((1U << 2) | (1U << 3)); /* clear EM and TS */
    cr0 |= (1U << 1) | (1U << 5);    /* MP + NE: errores x87 por excepcion */
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("fninit" : : : "memory");
}

static void task_fpu_build_initial_state(void) {
    __asm__ volatile ("fninit; fnsave %0; fwait; frstor %0"
                      : "=m"(g_initial_fpu_state) : : "memory");
}

static void task_fpu_assign_initial(task_t *task) {
    if (!task) return;
    kmemcpy(task->fpu_state, g_initial_fpu_state, TASK_FPU_STATE_SIZE);
    task->fpu_state_valid = true;
}

static void task_fpu_save(task_t *task) {
    if (!task) return;
    __asm__ volatile ("fnsave %0; fwait"
                      : "=m"(task->fpu_state) : : "memory");
    task->fpu_state_valid = true;
}

static void task_fpu_restore(task_t *task) {
    if (!task) return;
    if (task->fpu_state_valid) {
        __asm__ volatile ("frstor %0" : : "m"(task->fpu_state) : "memory");
    } else {
        task_fpu_assign_initial(task);
        __asm__ volatile ("frstor %0" : : "m"(task->fpu_state) : "memory");
    }
}

/* Ring-3 GUI callbacks are asynchronous, not ordinary C calls. They can
 * arrive while TinyGL or the modeller has live temporaries in ST(0..7).
 * Starting a callback on that x87 stack corrupts it and can turn otherwise
 * finite transforms into NaNs. Give the callback a clean x87 state and then
 * restore the interrupted instruction stream byte-for-byte. */
static void task_fpu_begin_upcall(task_t *task) {
    if (!task || task->upcall_fpu_saved) return;
    __asm__ volatile ("fnsave %0; fwait"
                      : "=m"(task->upcall_fpu_state) : : "memory");
    task->upcall_fpu_saved = true;
    __asm__ volatile ("frstor %0"
                      : : "m"(g_initial_fpu_state) : "memory");
}

static void task_fpu_end_upcall(task_t *task) {
    if (!task || !task->upcall_fpu_saved) return;
    __asm__ volatile ("fninit; frstor %0"
                      : : "m"(task->upcall_fpu_state) : "memory");
    task->upcall_fpu_saved = false;
}


static uint32_t *task_stack_allocate(uint32_t requested_size, bool user_stack,
                                     void **allocation_out,
                                     uint32_t *actual_size_out) {
    return (uint32_t *)mm_alloc_guarded_stack(requested_size, user_stack,
                                               allocation_out,
                                               actual_size_out);
}

static bool task_stack_guard_quick_ok(const task_t *task) {
    uint32_t lower;
    uint32_t upper;
    if (!task || !task->stack || !task->stack_size) return true;
    lower = (uint32_t)(uintptr_t)task->stack - PAGING_PAGE_SIZE;
    upper = (uint32_t)(uintptr_t)task->stack + task->stack_size;
    return !paging_page_present(lower) && !paging_page_present(upper);
}

static uint32_t task_frame_stack_remaining(const task_t *task,
                                           const registers_t *frame) {
    uintptr_t low, high, position;
    if (!task || !task->stack || !task->stack_size || !frame)
        return 0xFFFFFFFFU;
    low = (uintptr_t)task->stack;
    high = low + task->stack_size;
    position = (uintptr_t)frame;
    if (position < low || position > high) return 0U;
    return (uint32_t)(position - low);
}

static void task_stack_release(task_t *task, void *allocation,
                               uint32_t *stack, uint32_t stack_size,
                               const char *kind) {
    uint32_t lower;
    uint32_t upper;
    if (!stack) return;
    lower = (uint32_t)(uintptr_t)stack - PAGING_PAGE_SIZE;
    upper = (uint32_t)(uintptr_t)stack + stack_size;
    if (paging_page_present(lower) || paging_page_present(upper))
        kprintf("[TASK] guard de pagina %s alterado pid=%u proceso=%u nombre=%s\n",
                kind, task ? task->pid : 0U,
                task ? task->process_id : 0U,
                task ? task->name : "?");
    mm_free_guarded_stack(allocation, stack, stack_size);
}

static bool task_process_can_release(uint32_t process_id) {
    bool can_release = true;
    uint32_t flags;
    if (!process_id) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].process_id != process_id) continue;
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING ||
            tasks[i].state == TASK_SLEEPING ||
            (tasks[i].state == TASK_ZOMBIE && !tasks[i].resources_released)) {
            can_release = false;
            break;
        }
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return can_release;
}

static void task_thread_record_completion(const task_t *task) {
    uint32_t flags;
    uint32_t chosen = TASK_MAX;
    uint32_t oldest = 0xFFFFFFFFU;
    if (!task || !task->thread || !task->thread_joinable) return;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (uint32_t i = 0U; i < TASK_MAX; i++) {
        if (g_thread_completions[i].valid &&
            g_thread_completions[i].tid == task->pid) {
            chosen = i;
            break;
        }
        if (!g_thread_completions[i].valid) {
            chosen = i;
            break;
        }
        if (g_thread_completions[i].sequence < oldest) {
            oldest = g_thread_completions[i].sequence;
            chosen = i;
        }
    }
    if (chosen < TASK_MAX) {
        task_thread_completion_t *completion = &g_thread_completions[chosen];
        completion->tid = task->pid;
        completion->process_id = task->process_id;
        completion->result = task->exit_code;
        completion->sequence = ++g_thread_completion_sequence;
        completion->valid = true;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
}

static void task_release_upcall_buffers(task_t *task) {
    uint8_t index;
    if (!task) return;
    if (task->upcall_active && task->active_upcall.release_after_upcall) {
        kfree(task->active_upcall.release_after_upcall);
        task->active_upcall.release_after_upcall = NULL;
    }
    index = task->upcall_tail;
    while (index != task->upcall_head) {
        task_upcall_t *upcall = &task->upcalls[index];
        if (upcall->release_after_upcall) {
            kfree(upcall->release_after_upcall);
            upcall->release_after_upcall = NULL;
        }
        index = (uint8_t)((index + 1U) % TASK_UPCALL_QUEUE);
    }
}

static void task_release_resources(task_t *task) {
    uint32_t process_id;
    size_t released;

    if (!task || task->resources_released) return;
    kernel_domains_abandon_owner(task->pid);
    task_release_upcall_buffers(task);
    process_id = task->process_id;
    /* A TID and its process ID are equal for the main thread. Historically
     * that made the reaper destroy process-owned windows as soon as the main
     * entry returned, even when a worker thread was still alive and about to
     * display the application. Tear down only callbacks owned by this TID;
     * process-owned windows are released once the final sibling exits. */
    gui_desktop_cleanup_thread(gui_get_desktop(), task->pid);
    task_stack_release(task, task->stack_allocation, task->stack,
                       task->stack_size, "kernel");
    task_stack_release(task, task->user_stack_allocation, task->user_stack,
                       task->user_stack_size, "usuario");
    {
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        task->stack = NULL;
        task->user_stack = NULL;
        task->stack_allocation = NULL;
        task->user_stack_allocation = NULL;
        task->context = NULL;
        task->resources_released = true;
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
    }
    if (!task_process_can_release(process_id)) return;
    gui_desktop_cleanup_process(gui_get_desktop(), process_id);
    /* BLES_WINE_REAPER_CLEANUP_CALL_20260723
     * El entrypoint puede retornar o el task puede morir sin llamar
     * ExitProcess. Libere primero la imagen PE y su vista fija; además esto
     * consume el handoff pendiente antes de que mm_release_process() borre
     * las allocations restantes del proceso. */
    pe_win32_cleanup_process(process_id);
    ne_win16_cleanup_process(process_id);
    syscall_process_cleanup(process_id);
    elf_process_cleanup(process_id);
    released = mm_release_process(process_id);
    if (released)
        kprintf("[MM] proceso %u: %u KiB recuperados\n", process_id,
                (uint32_t)((released + 1023U) / 1024U));
}

static void task_reap_claimed_index(int index) {
    task_t *task;
    uint32_t flags;
    if (index <= 0 || index >= TASK_MAX) return;
    task = &tasks[index];
    scheduler_smp_remove(index);

    task_thread_record_completion(task);
    kernel_domains_enter(KDOMAIN_GUI | KDOMAIN_WINE | KDOMAIN_LEGACY);
    task_release_resources(task);
    kernel_domains_exit(KDOMAIN_GUI | KDOMAIN_WINE | KDOMAIN_LEGACY);

    flags = kspin_lock_irqsave(&g_task_table_lock);
    if (tasks[index].state == TASK_ZOMBIE &&
        tasks[index].resources_released) {
        kmemset(&tasks[index], 0, sizeof(tasks[index]));
        g_task_slot_reserved[index] = 0U;
    }
    g_task_reap_claim[index] = 0U;
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
}

/* Resource destruction may call GUI, Wine and heap code and therefore must
 * never run under the scheduler spinlock. Claim a zombie briefly, release the
 * table, perform the slow cleanup in task context, then recycle the slot. */
void task_reap_deferred(void) {
    /* A launch burst can leave several short-lived ELF entry tasks waiting
     * behind the GUI loop. Reaping only one per frame made the 32-slot table
     * appear exhausted even though most entries were already zombies. Keep
     * the batch deliberately small so cleanup cannot monopolize CPU0. */
    for (uint32_t pass = 0U; pass < 4U; pass++) {
        int index = -1;
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        for (int i = 1; i < TASK_MAX; i++) {
            if (tasks[i].state != TASK_ZOMBIE || tasks[i].running_cpu != -1 ||
                tasks[i].queued_cpu != -1 || tasks[i].resources_released ||
                g_task_reap_claim[i]) continue;
            g_task_reap_claim[i] = 1U;
            index = i;
            break;
        }
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
        if (index < 0) break;
        task_reap_claimed_index(index);
    }
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

static int task_pick_next_up(void) {
    int next = -1;

    /* Preserve the proven uniprocessor scheduler until SMP is explicitly
     * released. Idle is represented by slot -1, outside TASK_MAX. */
    if (current_index != 0 && tasks[0].state == TASK_READY)
        return 0;

    for (int checked = 0; checked < TASK_MAX; checked++) {
        int candidate = (round_robin_cursor + checked + 1) % TASK_MAX;
        if (tasks[candidate].state == TASK_READY) {
            next = candidate;
            break;
        }
    }

    if (next >= 0) {
        round_robin_cursor = next;
        return next;
    }
    return -1;
}

static void task_name_idle_cpu(char *name, uint32_t cpu);

static void idle_main(void *argument UNUSED) {
    for (;;) __asm__ volatile ("hlt");
}

static void task_bootstrap(void) {
    task_t *task = task_current_entry();
    sti();
    if (task && task->entry) task->entry(task->argument);
    task_exit();
}

static void task_user_bootstrap(void) {
    task_t *task = &tasks[current_index];

    if (task->entry) task->entry(task->argument);
    (void)syscall1(SYS_EXIT, 0);
    for (;;) (void)syscall0(SYS_YIELD);
}

static bool task_idle_initialize(uint32_t cpu, void *stack_allocation,
                                 uint32_t *stack, uint32_t stack_size) {
    task_t *idle;
    if (!g_idle_tasks || cpu >= SMP_MAX_CPUS || !stack ||
        stack_size < 4096U) return false;
    idle = &g_idle_tasks[cpu];
    kmemset(idle, 0, sizeof(*idle));
    idle->running_cpu = -1;
    idle->queued_cpu = -1;
    idle->affinity_cpu = (int8_t)cpu;
    idle->affinity_mask = cpu < 32U ? (1U << cpu) : 0U;
    idle->preferred_cpu = (uint8_t)cpu;
    idle->last_cpu = (uint8_t)cpu;
    idle->stack = stack;
    idle->stack_allocation = stack_allocation;
    idle->stack_size = stack_size;
    idle->memory_bytes = stack_size;
    idle->idle = true;
    idle->system = true;
    idle->state = TASK_READY;
    task_name_idle_cpu(idle->name, cpu);
    idle->entry = idle_main;
    idle->context = (registers_t *)((uint8_t *)stack + stack_size -
                                    sizeof(registers_t));
    kmemset(idle->context, 0, sizeof(registers_t));
    idle->context->ds = GDT_KERNEL_DATA;
    idle->context->es = GDT_KERNEL_DATA;
    idle->context->fs = GDT_KERNEL_DATA;
    idle->context->gs = GDT_KERNEL_DATA;
    idle->context->int_no = 32U;
    idle->context->eip = (uint32_t)(uintptr_t)task_bootstrap;
    idle->context->cs = GDT_KERNEL_CODE;
    idle->context->eflags = 0x202U;
    idle->context->useresp = (uint32_t)(uintptr_t)task_exit;
    task_fpu_assign_initial(idle);
    return true;
}

static bool task_user_gates_init(void) {
    uint8_t *exit_gate;
    uint8_t *upcall_gate;

    if (g_user_process_exit_gate && g_user_upcall_return_gate) return true;
    exit_gate = (uint8_t *)kmalloc(TASK_USER_GATE_SIZE);
    upcall_gate = (uint8_t *)kmalloc(TASK_USER_GATE_SIZE);
    if (!exit_gate || !upcall_gate) {
        if (exit_gate) kfree(exit_gate);
        if (upcall_gate) kfree(upcall_gate);
        return false;
    }
    (void)mm_set_allocation_owner(exit_gate, 0U);
    (void)mm_set_allocation_owner(upcall_gate, 0U);
    kmemset(exit_gate, 0x90, TASK_USER_GATE_SIZE);
    kmemset(upcall_gate, 0x90, TASK_USER_GATE_SIZE);

    /* A normal user entry returns here: exit(0), then yield forever if a
     * malformed syscall path unexpectedly returns. */
    exit_gate[0] = 0x31; exit_gate[1] = 0xDB;             /* xor ebx,ebx */
    exit_gate[2] = 0xB8;                                 /* mov eax,SYS_EXIT */
    *(uint32_t *)(void *)(exit_gate + 3) = SYS_EXIT;
    exit_gate[7] = 0xCD; exit_gate[8] = 0x80;             /* int 0x80 */
    exit_gate[9] = 0xB8;                                 /* mov eax,SYS_YIELD */
    *(uint32_t *)(void *)(exit_gate + 10) = SYS_YIELD;
    exit_gate[14] = 0xCD; exit_gate[15] = 0x80;
    exit_gate[16] = 0xEB; exit_gate[17] = 0xF7;           /* jmp 9 */

    /* Preserve callback EAX in EBX, exactly like the old kernel-text gate. */
    upcall_gate[0] = 0x89; upcall_gate[1] = 0xC3;         /* mov ebx,eax */
    upcall_gate[2] = 0xB8;                               /* mov eax,UPCALL */
    *(uint32_t *)(void *)(upcall_gate + 3) = SYS_UPCALL_RETURN;
    upcall_gate[7] = 0xCD; upcall_gate[8] = 0x80;
    upcall_gate[9] = 0x0F; upcall_gate[10] = 0x0B;        /* ud2 */
    upcall_gate[11] = 0xEB; upcall_gate[12] = 0xFE;

    __asm__ volatile ("" : : : "memory");
    g_user_process_exit_gate = exit_gate;
    g_user_upcall_return_gate = upcall_gate;
    return true;
}

void task_init(void) {
    uint32_t *idle_stack;
    void *idle_allocation = NULL;
    uint32_t idle_stack_size;

    kspin_init(&g_task_table_lock);
    kmemset(tasks, 0, sizeof(tasks));
    kmemset(g_task_slot_reserved, 0, sizeof(g_task_slot_reserved));
    kmemset(g_task_reap_claim, 0, sizeof(g_task_reap_claim));
    kmemset(g_task_query_snapshot, 0, sizeof(g_task_query_snapshot));
    kmemset(g_thread_completions, 0, sizeof(g_thread_completions));
    g_thread_completion_sequence = 0U;
    kmemset(cpu_states, 0, sizeof(cpu_states));
    kmemset(cpu_sample_ticks, 0, sizeof(cpu_sample_ticks));
    kmemset(cpu_sample_busy, 0, sizeof(cpu_sample_busy));
    kmemset(cpu_usage_per_cpu, 0, sizeof(cpu_usage_per_cpu));
    sample_ticks = 0U;
    sample_busy = 0U;
    cpu_usage = 0U;
    g_quantum_limit_ticks = 0U;

    if (!task_user_gates_init()) {
        kprintf("[TASK] no hay memoria para gates Ring 3 seguros\n");
        cli();
        for (;;) __asm__ volatile ("hlt");
    }

    g_idle_tasks = (task_t *)kzalloc(sizeof(task_t) * SMP_MAX_CPUS);
    if (!g_idle_tasks || !scheduler_smp_init(tasks, TASK_MAX)) {
        kprintf("[TASK] no hay memoria para estructuras SMP del scheduler\n");
        cli();
        for (;;) __asm__ volatile ("hlt");
    }

    for (uint32_t cpu = 0U; cpu < SMP_MAX_CPUS; cpu++) {
        cpu_states[cpu].current_slot = -1;
        cpu_states[cpu].rr_cursor = -1;
        cpu_states[cpu].deferred_ready_slot = -1;
    }
    for (int i = 0; i < TASK_MAX; i++) {
        tasks[i].running_cpu = -1;
        tasks[i].queued_cpu = -1;
        tasks[i].affinity_cpu = -1;
        tasks[i].preferred_cpu = 0U;
        tasks[i].last_cpu = 0xFFU;
    }

    cpu_states[0].current_slot = 0;
    next_pid = 1;
    current_quantum = 0;
    round_robin_cursor = 0;
    kmemset(yield_tick, 0, sizeof(yield_tick));
    kmemset(yield_budget, 0, sizeof(yield_budget));

    tasks[0].pid = next_pid++;
    tasks[0].process_id = tasks[0].pid;
    kstrcpy(tasks[0].name, "kernel-gui");
    tasks[0].state = TASK_RUNNING;
    tasks[0].running_cpu = 0;
    tasks[0].queued_cpu = -1;
    tasks[0].affinity_cpu = 0;
    tasks[0].affinity_mask = 1U;
    tasks[0].preferred_cpu = 0U;
    tasks[0].last_cpu = 0U;
    tasks[0].system = true;
    tasks[0].stack_size = compat_mode_stack_size("kernel-gui", false);
    tasks[0].memory_bytes = tasks[0].stack_size;

    task_fpu_prepare_cpu();
    task_fpu_build_initial_state();
    task_fpu_assign_initial(&tasks[0]);

    idle_stack_size = compat_mode_stack_size("idle", false);
    idle_stack = task_stack_allocate(idle_stack_size, false,
                                     &idle_allocation, &idle_stack_size);
    if (!idle_stack ||
        !task_idle_initialize(0U, idle_allocation, idle_stack,
                              idle_stack_size)) {
        kprintf("[TASK] no se pudo crear el contexto idle de CPU0\n");
        cli();
        for (;;) __asm__ volatile ("hlt");
    }
}

static int task_create_internal(const char *name, task_entry_t entry,
                                void *argument, bool user,
                                const char *launch_arg,
                                uint32_t process_id,
                                bool force_cpu0,
                                uint32_t requested_user_stack,
                                bool is_thread,
                                bool thread_joinable) {
    int index = -1;
    uint32_t *kernel_stack = NULL;
    uint32_t *user_stack = NULL;
    void *kernel_stack_allocation = NULL;
    void *user_stack_allocation = NULL;
    uint32_t kernel_stack_size;
    uint32_t user_stack_size;
    uint32_t pid = 0U;
    uint32_t parent_pid = 0U;
    uint32_t parent_console = 0U;
    int8_t affinity_cpu;
    task_t *task;
    uint32_t active_tasks = 0U;
    uint32_t table_flags;

    if (!entry) return -1;
    /* Flat Ring-3 tasks must start in a user-mapped image. The one exception
     * is the temporary Win16 placeholder, whose segmented frame is installed
     * before the outer preemption guard is released. Rejecting a kernel-text
     * entry turns old PE compatibility paths into a clean launch failure
     * instead of a supervisor-page #PF. */
    if (user && entry != (task_entry_t)task_user_bootstrap &&
        !user_access_ok((const void *)(uintptr_t)entry, 1U, false)) {
        kprintf("[TASK] entrada Ring 3 no accesible: %x (%s)\n",
                (uint32_t)(uintptr_t)entry, name ? name : "?");
        return -1;
    }
    if (user && !compat_mode_allow_user_programs()) {
        kprintf("[COMPAT] programa rechazado: 4 MiB reservados para escritorio\n");
        return -1;
    }

    /* Recycle completed launcher/worker slots before looking for a new one.
     * Without this, a burst could hit 32 occupied table entries even though
     * several of them were already zombies waiting for the GUI reaper. */
    task_reap_deferred();

    /* Keep the caller's per-CPU current slot stable, but do not hold the
     * cross-CPU task-table lock while allocating and clearing large stacks. */
    task_preempt_disable();
    table_flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++)
        if ((tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_ZOMBIE &&
             !tasks[i].idle) || g_task_slot_reserved[i]) active_tasks++;
    if (active_tasks >= compat_mode_task_limit()) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        task_preempt_enable();
        kprintf("[COMPAT] limite de tareas alcanzado (%u)\n", active_tasks);
        return -1;
    }
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_UNUSED && !g_task_slot_reserved[i]) {
            index = i;
            g_task_slot_reserved[i] = 1U;
            break;
        }
    }
    if (index < 0) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        task_preempt_enable();
        return -1;
    }
    pid = next_pid++;
    {
        task_t *parent = task_current_entry();
        parent_pid = parent && parent->process_id
                   ? parent->process_id : (parent ? parent->pid : 0U);
        parent_console = parent ? parent->console_route : 0U;
    }
    affinity_cpu = (user && smp_scheduler_started() && !force_cpu0) ? -1 : 0;
    kspin_unlock_irqrestore(&g_task_table_lock, table_flags);

    kernel_stack_size = compat_mode_stack_size(name, false);
    user_stack_size = user ? compat_mode_stack_size(name, true) : 0U;
    if (user && requested_user_stack) {
        if (requested_user_stack < 16384U) requested_user_stack = 16384U;
        if (requested_user_stack > TASK_STACK_MAX_SIZE)
            requested_user_stack = TASK_STACK_MAX_SIZE;
        requested_user_stack = (requested_user_stack + 4095U) & ~4095U;
        user_stack_size = requested_user_stack;
    }
    kernel_stack = task_stack_allocate(kernel_stack_size, false,
                                       &kernel_stack_allocation,
                                       &kernel_stack_size);
    if (kernel_stack && user)
        user_stack = task_stack_allocate(user_stack_size, true,
                                         &user_stack_allocation,
                                         &user_stack_size);
    if (!kernel_stack || (user && !user_stack)) {
        if (kernel_stack)
            task_stack_release(NULL, kernel_stack_allocation, kernel_stack,
                               kernel_stack_size, "kernel");
        if (user_stack)
            task_stack_release(NULL, user_stack_allocation, user_stack,
                               user_stack_size, "usuario");
        table_flags = kspin_lock_irqsave(&g_task_table_lock);
        g_task_slot_reserved[index] = 0U;
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        task_preempt_enable();
        return -1;
    }

    task = &tasks[index];
    kmemset(task, 0, sizeof(*task));
    task->running_cpu = -1;
    task->queued_cpu = -1;
    task->affinity_cpu = affinity_cpu;
    task->affinity_mask = affinity_cpu >= 0
        ? (1U << (uint32_t)(uint8_t)affinity_cpu)
        : scheduler_smp_all_mask();
    task->preferred_cpu = affinity_cpu >= 0
        ? (uint8_t)affinity_cpu : (uint8_t)smp_cpu_index();
    task->last_cpu = 0xFFU;
    task->stack = kernel_stack;
    task->user_stack = user_stack;
    task->stack_allocation = kernel_stack_allocation;
    task->user_stack_allocation = user_stack_allocation;
    task->stack_size = kernel_stack_size;
    task->user_stack_size = user_stack_size;
    task->pid = pid;
    task->process_id = process_id ? process_id : task->pid;
    task->thread = is_thread;
    task->thread_joinable = is_thread && thread_joinable;
    task->parent_pid = parent_pid;
    task->console_route = parent_console;
    kstrncpy(task->name, name ? name : (user ? "user" : "task"),
             TASK_NAME_LEN - 1);
    task->context = (registers_t *)((uint8_t *)kernel_stack + kernel_stack_size -
                                    sizeof(registers_t));
    kmemset(task->context, 0, sizeof(registers_t));
    task->entry = entry;
    task->argument = argument;
    task_fpu_assign_initial(task);
    task->context->int_no = 32;

    if (user) {
        task->context->ds = GDT_USER_DATA;
        task->context->es = GDT_USER_DATA;
        task->context->fs = GDT_USER_DATA;
        task->context->gs = GDT_USER_DATA;
        task->context->cs = GDT_USER_CODE;
        task->context->ss = GDT_USER_DATA;
        if (entry == (task_entry_t)task_user_bootstrap) {
            /* Temporary Win16 placeholder. task_create_user16_program()
             * replaces the complete segmented frame before it can run. */
            task->context->eip = (uint32_t)(uintptr_t)entry;
            task->context->eflags = 0x202U;
            task->context->useresp = (uint32_t)(uintptr_t)
                ((uint8_t *)user_stack + user_stack_size);
        } else {
            uint32_t aligned_top =
                ((uint32_t)(uintptr_t)user_stack + user_stack_size) & ~15U;
            uint32_t *initial_user_stack =
                (uint32_t *)(uintptr_t)(aligned_top - 20U);
            /* Normal cdecl entry: [return address][argument]. Native thread
             * trampolines are noreturn, while process entries may return to
             * the shared user exit gate. */
            initial_user_stack[0] = is_thread && thread_joinable
                ? 0U : (uint32_t)(uintptr_t)g_user_process_exit_gate;
            initial_user_stack[1] = (uint32_t)(uintptr_t)argument;
            initial_user_stack[2] = 0U;
            initial_user_stack[3] = 0U;
            initial_user_stack[4] = 0U;
            task->context->eip = (uint32_t)(uintptr_t)entry;
            task->context->eflags = is_thread && thread_joinable
                ? 0x202U : 0x3202U;
            task->context->useresp = (uint32_t)(uintptr_t)initial_user_stack;
        }
        task->user = true;
        task->memory_bytes = kernel_stack_size + user_stack_size;
    } else {
        task->context->ds = GDT_KERNEL_DATA;
        task->context->es = GDT_KERNEL_DATA;
        task->context->fs = GDT_KERNEL_DATA;
        task->context->gs = GDT_KERNEL_DATA;
        task->context->eip = (uint32_t)(uintptr_t)task_bootstrap;
        task->context->cs = GDT_KERNEL_CODE;
        task->context->eflags = 0x202;
        task->context->useresp = (uint32_t)(uintptr_t)task_exit;
        task->memory_bytes = kernel_stack_size;
    }

    task_copy_launch_arg(task, launch_arg);
    table_flags = kspin_lock_irqsave(&g_task_table_lock);
    yield_tick[index] = 0;
    yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
    __asm__ volatile ("" : : : "memory");
    task->state = TASK_READY;
    g_task_slot_reserved[index] = 0U;
    kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
    if (smp_scheduler_started())
        (void)scheduler_smp_enqueue(index, -1, true);
    task_preempt_enable();
    return (int)task->pid;
}

int task_create(const char *name, task_entry_t entry, void *argument) {
    task_t *current = task_current_entry();
    return task_create_internal(name, entry, argument,
                                current ? current->user : false,
                                NULL, 0U, false, 0U, false, false);
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
    /* Copying the template does not disturb the currently running task's
     * live x87 stack. */
    task_fpu_assign_initial(task);
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
                                true, NULL, 0U, false, 0U, false, false);
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
    return task_create_internal(name, entry, argument, true, launch_arg, 0U,
                                false, 0U, false, false);
}


int task_create_user16_program(const char *name, uint16_t cs, uint16_t ip,
                               uint16_t ss, uint16_t sp, uint16_t ds,
                               uint16_t es, uint16_t stack_size,
                               uint16_t heap_size, const char *launch_arg) {
    int pid;
    uint32_t table_flags;
    /* Keep the new Win16 task on CPU0 and prevent CPU0 from scheduling it
     * before its segmented CS:IP/SS:SP frame has been installed. */
    task_preempt_disable();
    pid = task_create_internal(name, (task_entry_t)task_user_bootstrap,
                               NULL, true, launch_arg, 0U, true,
                               0U, false, false);
    if (pid < 0) {
        task_preempt_enable();
        return pid;
    }
    table_flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 1; i < TASK_MAX; i++) {
        task_t *task = &tasks[i];
        if (task->pid != (uint32_t)pid || task->state == TASK_UNUSED) continue;
        task->win16 = true;
        task->affinity_cpu = 0;
        task->user_fs_base = 0U;
        /* SS:SP pertenece al bloque segmentado NE; la pila Ring 3 plana que
         * reserva el creador no se usa en Win16. */
        task_stack_release(task, task->user_stack_allocation,
                           task->user_stack, task->user_stack_size,
                           "usuario-win16-no-usada");
        task->user_stack = NULL;
        task->user_stack_allocation = NULL;
        task->user_stack_size = 0U;
        task->memory_bytes = task->stack_size;
        task->context->ds = ds;
        task->context->es = es;
        task->context->fs = ds;
        task->context->gs = ds;
        task->context->eip = ip;
        task->context->cs = cs;
        task->context->eflags = 0x202U;
        task->context->useresp = sp;
        task->context->ss = ss;
        task->context->eax = 0U;
        /* Win16 startup code passes these registers to KERNEL.InitTask. */
        task->context->ebx = stack_size;
        task->context->ecx = heap_size;
        task->context->edx = ds;
        task->context->esi = 0U;
        task->context->edi = 0U;
        task->context->ebp = 0U;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
    task_preempt_enable();
    return pid;
}


/* BLES_WINE_KERNEL_WORKER_API_20260723 */
int task_create_kernel(const char *name, task_entry_t entry,
                       void *argument) {
    return task_create_internal(name, entry, argument, false,
                                NULL, 0U, false, 0U, false, false);
}
int task_create_user_thread_ex(const char *name, task_entry_t entry,
                               void *argument, uint32_t process_id,
                               uint32_t user_stack_size, bool joinable) {
    task_t *current = task_current_entry();
    if (!process_id && current) process_id = current->process_id;
    if (!process_id) return -1;
    return task_create_internal(name, entry, argument, true, NULL, process_id,
                                false, user_stack_size, true, joinable);
}

int task_create_user_thread(const char *name, task_entry_t entry,
                            void *argument, uint32_t process_id) {
    /* Preserve the historical fire-and-forget API used by Wine. */
    return task_create_user_thread_ex(name, entry, argument, process_id,
                                      0U, false);
}

static void task_wake_sleepers_smp(uint32_t now) {
    int wake_slots[TASK_MAX];
    uint32_t wake_count = 0U;
    uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int slot = 0; slot < TASK_MAX; slot++) {
        task_t *task = &tasks[slot];
        if (task->state != TASK_SLEEPING ||
            (int32_t)(now - task->wake_tick) < 0) continue;
        /* SYS_SLEEP publishes TASK_SLEEPING before the originating CPU has
         * left the task's kernel stack.  Waking it during that short window
         * would make the same stack runnable elsewhere.  A later BSP tick
         * will observe running_cpu == -1 after the context switch completes. */
        if (task->running_cpu != -1) continue;
        task->state = TASK_READY;
        if (task->queued_cpu < 0 && wake_count < TASK_MAX)
            wake_slots[wake_count++] = slot;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    for (uint32_t i = 0U; i < wake_count; i++)
        (void)scheduler_smp_enqueue(wake_slots[i], -1, true);
}

#define TASK_CPU_HANDOFF (-2)

static void task_publish_deferred_ready(uint32_t cpu) {
    int slot;
    task_cpu_state_t *state;
    task_t *task;
    if (cpu >= SMP_MAX_CPUS) return;
    state = &cpu_states[cpu];
    slot = state->deferred_ready_slot;
    if (slot < 0 || slot >= TASK_MAX) return;
    state->deferred_ready_slot = -1;
    task = &tasks[slot];
    if (task->state != TASK_READY || task->running_cpu != TASK_CPU_HANDOFF)
        return;
    task->running_cpu = -1;
    (void)scheduler_smp_enqueue(slot, (int32_t)cpu, false);
}

static uint32_t task_quantum_limit(void) {
    if (!g_quantum_limit_ticks) {
        uint32_t hz = pit_get_frequency_hz();
        uint32_t ticks = (hz * TASK_QUANTUM_MILLISECONDS + 999U) / 1000U;
        g_quantum_limit_ticks = ticks ? ticks : 1U;
    }
    return g_quantum_limit_ticks;
}

static void task_watchdog_tick(uint32_t cpu) {
    task_cpu_state_t *state;
    uint32_t hz;
    uint32_t interval;

    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    state = &cpu_states[cpu];
    hz = pit_get_frequency_hz();
    if (!hz) hz = 100U;
    interval = hz / 10U;
    if (!interval) interval = 1U;
    if (++state->watchdog_ticks < interval) return;
    state->watchdog_ticks = 0U;
    smp_watchdog_heartbeat();
    /* Un solo CPU inspecciona la tabla. Antes cada LAPIC entraba al lock de
       polling en cada tick, aun cuando el informe sólo puede cambiar 1 vez/s. */
    if (cpu == 0U) smp_watchdog_poll();
}

static void task_account_tick(task_t *current, uint32_t cpu) {
    uint32_t online = smp_online_cpu_count();
    bool busy;
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    if (!online) online = 1U;
    busy = current && current->state == TASK_RUNNING && !current->idle;
    perfmon_scheduler_tick(busy);
    if (current) current->cpu_ticks++;
    if (busy) cpu_sample_busy[cpu]++;
    if (++cpu_sample_ticks[cpu] >=
        (pit_get_frequency_hz() ? pit_get_frequency_hz() : 100U)) {
        uint32_t total = 0U;
        uint32_t usage = (cpu_sample_busy[cpu] * 100U) /
                         cpu_sample_ticks[cpu];
        cpu_usage_per_cpu[cpu] = (uint8_t)(usage > 100U ? 100U : usage);
        cpu_sample_ticks[cpu] = 0U;
        cpu_sample_busy[cpu] = 0U;
        if (online > SMP_MAX_CPUS) online = SMP_MAX_CPUS;
        for (uint32_t i = 0U; i < online; i++) total += cpu_usage_per_cpu[i];
        cpu_usage = (uint8_t)(total / online);
    }
}

static registers_t *task_schedule_internal(registers_t *frame,
                                             bool nonblocking_ap_timer,
                                             bool force_reschedule) {
    uint32_t scheduler_flags = 0U;
    uint64_t perf_scheduler_started;
    uint32_t cpu;
    bool smp_mode;
    task_t *current;
    task_t *next_task;
    int old_slot;
    int next;

    (void)nonblocking_ap_timer;
    cpu = smp_cpu_index();
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    paging_sync_cpu();
    task_watchdog_tick(cpu);
    smp_mode = smp_scheduler_started() && scheduler_smp_ready();

    /* int 0x80 is a trap gate. Mask local IRQs before publishing any change
     * to current_slot; IRET restores the selected task's IF atomically. */
    if (smp_mode) {
        __asm__ volatile ("pushfl; popl %0; cli"
                          : "=r"(scheduler_flags) : : "memory", "cc");
        task_publish_deferred_ready(cpu);
        if (cpu == 0U) task_wake_sleepers_smp(pit_get_ticks());
    } else {
        scheduler_flags = kspin_lock_irqsave(&g_task_table_lock);
    }

    perf_scheduler_started = perfmon_scope_begin();
#define TASK_SCHEDULE_FINISH(value) do { \
        perfmon_scope_end(PERF_SCOPE_SCHEDULER, perf_scheduler_started); \
        if (!smp_mode) task_scheduler_unlock_for_iret(scheduler_flags); \
        return (value); \
    } while (0)

    current = task_current_entry();
    if (!current || !frame) {
        perfmon_scheduler_preempt_blocked();
        TASK_SCHEDULE_FINISH(frame);
    }
    task_account_tick(current, cpu);

    if (!smp_mode) {
        uint32_t now = pit_get_ticks();
        for (int i = 0; i < TASK_MAX; i++) {
            if (tasks[i].state == TASK_SLEEPING &&
                (int32_t)(now - tasks[i].wake_tick) >= 0)
                tasks[i].state = TASK_READY;
        }
    }

    if (preempt_depth) {
        perfmon_scheduler_preempt_blocked();
        TASK_SCHEDULE_FINISH(frame);
    }

    {
        bool kernel_frame = (frame->cs & 3U) == 0U;
        if (smp_mode && current->user && kernel_frame &&
            !current->kernel_switch_safe) {
            uint32_t remaining = task_frame_stack_remaining(current, frame);
            bool guard_ok = task_stack_guard_quick_ok(current);
            if (guard_ok && (remaining == 0xFFFFFFFFU ||
                             remaining >= TASK_KERNEL_STACK_EMERGENCY_RESERVE)) {
                perfmon_scheduler_preempt_blocked();
                TASK_SCHEDULE_FINISH(frame);
            }
            kprintf("[TASK] carga pesada contenida pid=%u cpu=%u nombre=%s "
                    "stack=%u guard=%s; proceso terminado\n",
                    current->pid, cpu, current->name, remaining,
                    guard_ok ? "OK" : "DANADO");
            current->exit_code = 0xA502;
            current->state = TASK_ZOMBIE;
            current->kernel_switch_safe = true;
        }
        if (kernel_frame) current->kernel_switch_safe = false;
    }

    task_normalize_user_segments(current, frame);
    current->context = frame;

    if (!force_reschedule && current->state == TASK_RUNNING &&
        !current->idle) {
        bool gui_waiting = cpu == 0U && current_index != 0 &&
                           tasks[0].state == TASK_READY;
        if (!gui_waiting && ++current_quantum < task_quantum_limit()) {
            perfmon_scheduler_fast_quantum();
            TASK_SCHEDULE_FINISH(current->context);
        }
    }

    old_slot = current_index;
    if (smp_mode) {
        /* Reserve the next task first, but do not publish the old task until
         * this CPU has actually left its kernel stack. Publishing it now
         * would let a thief restore the same frame on another CPU while the
         * original CPU is still returning through it. */
        bool must_migrate = old_slot >= 0 &&
            current->state == TASK_RUNNING && !current->idle &&
            !scheduler_smp_cpu_allowed(current, cpu);
        next = scheduler_smp_dequeue(cpu);
        if (next < 0) next = scheduler_smp_steal(cpu);

        if (current->state == TASK_RUNNING && !current->idle && next < 0 &&
            !must_migrate) {
            current_quantum = 0U;
            TASK_SCHEDULE_FINISH(current->context);
        }
        if (current->idle && next < 0) {
            current_quantum = 0U;
            TASK_SCHEDULE_FINISH(current->context);
        }

        {
            uint64_t perf_fpu_started = perfmon_scope_begin();
            task_fpu_save(current);
            perfmon_scope_end(PERF_SCOPE_FPU_SWITCH, perf_fpu_started);
        }

        if (current->state == TASK_RUNNING && !current->idle) {
            if (cpu_states[cpu].deferred_ready_slot >= 0) {
                /* A second deferred slot would lose a runnable task. Keep the
                 * current task and return the reserved task to its queue. */
                if (next >= 0) {
                    task_t *reserved = &tasks[next];
                    reserved->state = TASK_READY;
                    reserved->running_cpu = -1;
                    (void)scheduler_smp_enqueue(next, (int32_t)cpu, false);
                }
                task_fpu_restore(current);
                TASK_SCHEDULE_FINISH(current->context);
            }
            current->state = TASK_READY;
            current->running_cpu = TASK_CPU_HANDOFF;
            cpu_states[cpu].deferred_ready_slot = old_slot;
        } else {
            current->running_cpu = -1;
            if (current->idle) current->state = TASK_READY;
        }

        if (next < 0) {
            next = -1;
            next_task = task_idle_for_cpu(cpu);
            if (!next_task) {
                task_fpu_restore(current);
                TASK_SCHEDULE_FINISH(current->context);
            }
            next_task->state = TASK_RUNNING;
            next_task->running_cpu = (int32_t)cpu;
            next_task->last_cpu = (uint8_t)cpu;
        } else {
            next_task = &tasks[next];
        }
    } else {
        {
            uint64_t perf_fpu_started = perfmon_scope_begin();
            task_fpu_save(current);
            perfmon_scope_end(PERF_SCOPE_FPU_SWITCH, perf_fpu_started);
        }
        if (old_slot >= 0 && current->state == TASK_RUNNING) {
            current->state = TASK_READY;
            current->running_cpu = -1;
        } else if (current->idle) {
            current->state = TASK_READY;
            current->running_cpu = -1;
        }
        next = task_pick_next_up();
        next_task = task_from_slot_cpu(next, 0U);
        if (!next_task) {
            task_fpu_restore(current);
            TASK_SCHEDULE_FINISH(current->context);
        }
        next_task->state = TASK_RUNNING;
        next_task->running_cpu = 0;
        next_task->last_cpu = 0U;
    }

    {
        uint64_t perf_fpu_started = perfmon_scope_begin();
        if (next != old_slot) {
            current_index = next;
            perfmon_scheduler_switch();
        }
        task_fpu_restore(next_task);
        perfmon_scope_end(PERF_SCOPE_FPU_SWITCH, perf_fpu_started);
    }
    current_quantum = 0U;

    if (next_task->stack) {
        tss_set_kernel_stack((uint32_t)(uintptr_t)
            ((uint8_t *)next_task->stack + next_task->stack_size));
    }
    if (next_task->user && !next_task->win16 && next_task->user_fs_base) {
        gdt_set_user_fs_base(next_task->user_fs_base);
        next_task->context->fs = GDT_USER_FS;
    }
    if (next_task->user && !next_task->win16)
        (void)task_prepare_user_upcall(next_task->context);
    task_normalize_user_segments(next_task, next_task->context);
    TASK_SCHEDULE_FINISH(next_task->context);
#undef TASK_SCHEDULE_FINISH
}

registers_t *task_schedule(registers_t *frame) {
    return task_schedule_internal(frame, false, false);
}

registers_t *task_schedule_lapic(registers_t *frame) {
    return task_schedule_internal(frame, smp_cpu_index() != 0U, false);
}

registers_t *task_schedule_reschedule_ipi(registers_t *frame) {
    return task_schedule_internal(frame, true, true);
}

void task_yield(void) {
    uint32_t flags;
    int index;
    task_t *current;
    kernel_domain_snapshot_t domain_snapshot;

    if (task_running_in_user_cpl()) {
        (void)syscall0(SYS_YIELD);
        return;
    }

    index = current_index;
    current = task_current_entry();
    if (!current) return;

    if (!smp_scheduler_started()) {
        if (current->idle || current->state != TASK_RUNNING || index < 0) {
            __asm__ volatile ("sti; hlt");
            return;
        }
        {
            uint32_t now = pit_get_ticks();
            if (yield_tick[index] != now) {
                yield_tick[index] = now;
                yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
            }
            if (yield_budget[index] > 0U) {
                yield_budget[index]--;
                __asm__ volatile ("pause");
                return;
            }
        }
        __asm__ volatile ("sti; hlt");
        return;
    }

    __asm__ volatile ("pushfl; popl %0" : "=r"(flags) : : "memory", "cc");
    if (current->idle || current->state != TASK_RUNNING || index < 0) {
        if (current->user) current->kernel_switch_safe = true;
        kernel_domains_drop_current(&domain_snapshot);
        if (flags & X86_EFLAGS_IF)
            __asm__ volatile ("sti; hlt; cli" : : : "memory");
        else
            __asm__ volatile ("pause" : : : "memory");
        if (current->user) current->kernel_switch_safe = false;
        if (!current->idle) kernel_domains_restore(&domain_snapshot);
        if (flags & X86_EFLAGS_IF) sti(); else cli();
        return;
    }

    {
        uint32_t now = pit_get_ticks();
        if (yield_tick[index] != now) {
            yield_tick[index] = now;
            yield_budget[index] = TASK_YIELD_BUDGET_PER_TICK;
        }
    }
    if (current->user) current->kernel_switch_safe = true;
    kernel_domains_drop_current(&domain_snapshot);
    if (yield_budget[index] > 0U) {
        yield_budget[index]--;
        __asm__ volatile ("pause" : : : "memory");
    } else if (flags & X86_EFLAGS_IF) {
        __asm__ volatile ("sti; hlt; cli" : : : "memory");
    } else {
        __asm__ volatile ("pause" : : : "memory");
    }
    if (current->user) current->kernel_switch_safe = false;
    kernel_domains_restore(&domain_snapshot);
    if (flags & X86_EFLAGS_IF) sti(); else cli();
}

void task_sleep(uint32_t ticks) {
    task_t *current;
    if (task_running_in_user_cpl()) {
        (void)syscall1(SYS_SLEEP, ticks ? ticks : 1U);
        return;
    }
    current = task_current_entry();
    if (!current || current->idle) return;
    if (ticks == 0U) ticks = 1U;
    {
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        current->wake_tick = pit_get_ticks() + ticks;
        current->state = TASK_SLEEPING;
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
    }
    task_yield();
}

void task_sleep_from_interrupt(uint32_t ticks) {
    task_t *current = task_current_entry();
    uint32_t flags;
    if (!current || current->idle) return;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    current->wake_tick = pit_get_ticks() + (ticks ? ticks : 1U);
    current->state = TASK_SLEEPING;
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
}

void task_exit(void) {
    task_t *current;
    if (task_running_in_user_cpl()) {
        (void)syscall1(SYS_EXIT, 0);
        for (;;) (void)syscall0(SYS_YIELD);
    }
    current = task_current_entry();
    if (!current || current->idle) {
        for (;;) __asm__ volatile ("sti; hlt");
    }
    kernel_domains_abandon_current();
    {
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        current->exit_code = 0;
        current->state = TASK_ZOMBIE;
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
    }
    for (;;) task_yield();
}

void task_exit_from_interrupt(int32_t status) {
    task_t *current = task_current_entry();
    uint32_t flags;
    if (!current || current->idle) return;
    kernel_domains_abandon_current();
    flags = kspin_lock_irqsave(&g_task_table_lock);
    current->exit_code = status;
    current->state = TASK_ZOMBIE;
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
}

void task_allow_kernel_switch_once(void) {
    if (current_index < 0 || current_index >= TASK_MAX) return;
    if (tasks[current_index].user)
        tasks[current_index].kernel_switch_safe = true;
}

uint32_t task_kernel_stack_remaining(void) {
    uint32_t stack_pointer;
    uintptr_t low, high;
    int index = current_index;

    if (index < 0 || index >= TASK_MAX || !tasks[index].stack ||
        tasks[index].stack_size == 0U)
        return 0xFFFFFFFFU;

    __asm__ volatile ("movl %%esp, %0" : "=r"(stack_pointer));
    low = (uintptr_t)tasks[index].stack;
    high = low + tasks[index].stack_size;
    if ((uintptr_t)stack_pointer < low || (uintptr_t)stack_pointer > high)
        return 0U;
    return (uint32_t)((uintptr_t)stack_pointer - low);
}

void task_user_api_guard_enter(const char *name, uint32_t target,
                               uint32_t token) {
    task_t *task;
    if (current_index < 0 || current_index >= TASK_MAX) return;
    task = &tasks[current_index];
    task->api_guard_name = name ? name : "(API sin nombre)";
    task->api_guard_target = target;
    task->api_guard_token = token;
    task->api_guard_depth++;
}

void task_user_api_guard_leave(void) {
    task_t *task;
    if (current_index < 0 || current_index >= TASK_MAX) return;
    task = &tasks[current_index];
    if (task->api_guard_depth) task->api_guard_depth--;
}

bool task_user_api_guard_info(const char **name_out, uint32_t *target_out,
                              uint32_t *token_out) {
    task_t *task;
    if (current_index < 0 || current_index >= TASK_MAX) return false;
    task = &tasks[current_index];
    if (!task->api_guard_depth) return false;
    if (name_out) *name_out = task->api_guard_name
        ? task->api_guard_name : "(API sin nombre)";
    if (target_out) *target_out = task->api_guard_target;
    if (token_out) *token_out = task->api_guard_token;
    return true;
}

void task_user_api_guard_clear(void) {
    task_t *task;
    if (current_index < 0 || current_index >= TASK_MAX) return;
    task = &tasks[current_index];
    task->api_guard_depth = 0U;
    task->api_guard_target = 0U;
    task->api_guard_token = 0U;
}

void task_preempt_disable(void) {
    preempt_depth++;
}

void task_preempt_enable(void) {
    if (preempt_depth) preempt_depth--;
}

bool task_request_exit(uint32_t pid) {
    bool result = false;
    int wake_slot = -1;
    int32_t running_cpu = -1;
    uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED) continue;
        if (tasks[i].system) break;
        tasks[i].exit_requested = true;
        running_cpu = tasks[i].running_cpu;
        if (tasks[i].state == TASK_SLEEPING) {
            if (running_cpu == -1) {
                tasks[i].state = TASK_READY;
                wake_slot = i;
            } else {
                tasks[i].wake_tick = pit_get_ticks();
            }
        }
        result = true;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    if (wake_slot >= 0 && smp_scheduler_started())
        (void)scheduler_smp_enqueue(wake_slot, -1, true);
    else if (running_cpu >= 0 && smp_scheduler_started())
        smp_reschedule_cpu((uint32_t)running_cpu);
    return result;
}

bool task_request_exit_process(uint32_t process_id, int32_t exit_code) {
    int wake_slots[TASK_MAX];
    uint32_t wake_count = 0U;
    uint32_t running_mask = 0U;
    bool result = false;
    uint32_t flags;
    if (!process_id) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].process_id != process_id ||
            tasks[i].state == TASK_UNUSED || tasks[i].system)
            continue;
        tasks[i].exit_code = exit_code;
        tasks[i].exit_requested = true;
        if (tasks[i].running_cpu >= 0 && tasks[i].running_cpu < 32)
            running_mask |= 1U << (uint32_t)tasks[i].running_cpu;
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].running_cpu == -1) {
                tasks[i].state = TASK_READY;
                if (wake_count < TASK_MAX) wake_slots[wake_count++] = i;
            } else {
                tasks[i].wake_tick = pit_get_ticks();
            }
        }
        result = true;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    if (smp_scheduler_started()) {
        for (uint32_t i = 0U; i < wake_count; i++)
            (void)scheduler_smp_enqueue(wake_slots[i], -1, true);
        smp_reschedule_mask(running_mask);
    }
    return result;
}

bool task_query_pid(uint32_t pid, bool *active, int32_t *exit_code,
                    uint32_t *process_id) {
    bool found = false;
    uint32_t flags;
    if (active) *active = false;
    if (exit_code) *exit_code = 0;
    if (process_id) *process_id = 0U;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED) continue;
        found = true;
        if (active) *active = tasks[i].state == TASK_READY ||
            tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_SLEEPING;
        if (exit_code) *exit_code = tasks[i].exit_code;
        if (process_id) *process_id = tasks[i].process_id;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return found;
}

bool task_query_process(uint32_t process_id, bool *active,
                        int32_t *exit_code, uint32_t *main_pid) {
    bool found = false;
    bool any_active = false;
    int32_t status = 0;
    uint32_t main = 0U;
    uint32_t flags;
    if (active) *active = false;
    if (exit_code) *exit_code = 0;
    if (main_pid) *main_pid = 0U;
    if (!process_id) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].process_id != process_id || tasks[i].state == TASK_UNUSED)
            continue;
        found = true;
        if (!main || tasks[i].pid == process_id) {
            main = tasks[i].pid;
            status = tasks[i].exit_code;
        }
        if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING ||
            tasks[i].state == TASK_SLEEPING) any_active = true;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    if (active) *active = any_active;
    if (exit_code) *exit_code = status;
    if (main_pid) *main_pid = main ? main : process_id;
    return found;
}

bool task_exit_requested(void) {
    task_t *task = task_current_entry();
    return task ? task->exit_requested : false;
}

void task_set_memory_hint(uint32_t bytes) {
    task_t *task = task_current_entry();
    if (!task || task->idle) return;
    task->memory_bytes = task->stack_size + task->user_stack_size + bytes;
}

uint32_t task_process_memory_bytes(uint32_t process_id) {
    uint64_t stacks = 0U;
    uint64_t hints = 0U;
    uint64_t heap;
    uint64_t total;
    uint32_t flags;

    if (!process_id) return 0U;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        uint32_t base;
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE ||
            tasks[i].process_id != process_id) continue;
        base = tasks[i].stack_size + tasks[i].user_stack_size;
        stacks += base;
        if (tasks[i].memory_bytes > base)
            hints += tasks[i].memory_bytes - base;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    heap = mm_get_process_usage(process_id);
    total = heap > stacks + hints ? heap : stacks + hints;
    return total > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)total;
}

void task_bind_window(struct gui_window *window) {
    task_t *task = task_current_entry();
    if (task && !task->idle) task->window = window;
}

void task_fpu_prepare_secondary_cpu(void) {
    task_fpu_prepare_cpu();
    __asm__ volatile ("frstor %0" : : "m"(g_initial_fpu_state) : "memory");
}

static void task_name_idle_cpu(char *name, uint32_t cpu) {
    kstrcpy(name, "idle-cpu");
    if (cpu >= 10U) {
        name[8] = (char)('0' + ((cpu / 10U) % 10U));
        name[9] = (char)('0' + (cpu % 10U));
        name[10] = '\0';
    } else {
        name[8] = (char)('0' + cpu);
        name[9] = '\0';
    }
}

bool task_smp_prepare_cpu(uint32_t cpu_index, void *stack_allocation,
                          uint32_t *stack, uint32_t stack_size) {
    if (cpu_index == 0U || cpu_index >= SMP_MAX_CPUS) return false;
    return task_idle_initialize(cpu_index, stack_allocation, stack,
                                stack_size);
}

void task_smp_ap_online(uint32_t cpu_index) {
    task_cpu_state_t *state;
    task_t *idle;
    if (cpu_index == 0U || cpu_index >= SMP_MAX_CPUS) return;
    state = &cpu_states[cpu_index];
    idle = task_idle_for_cpu(cpu_index);
    if (!idle || !idle->stack) return;
    state->current_slot = -1;
    state->preempt_nesting = 0U;
    state->quantum_ticks = 0U;
    state->deferred_ready_slot = -1;
    idle->state = TASK_RUNNING;
    idle->running_cpu = (int32_t)cpu_index;
    idle->last_cpu = (uint8_t)cpu_index;
    tss_set_kernel_stack((uint32_t)(uintptr_t)
        ((uint8_t *)idle->stack + idle->stack_size));
}

bool task_smp_scheduler_start(void) {
    uint32_t online = smp_online_cpu_count();
    uint32_t all_mask;
    if (!scheduler_smp_ready()) return false;
    if (!online) online = 1U;
    if (online > SMP_MAX_CPUS) online = SMP_MAX_CPUS;
    scheduler_smp_reset(online);
    all_mask = scheduler_smp_all_mask();

    for (uint32_t cpu = 0U; cpu < online; cpu++) {
        task_t *idle = task_idle_for_cpu(cpu);
        if (idle) {
            idle->affinity_mask = cpu < 32U ? (1U << cpu) : 0U;
            idle->queued_cpu = -1;
            if (cpu != 0U) idle->state = TASK_READY;
        }
    }

    /* Seed only READY tasks. The GUI task is currently RUNNING on CPU0;
     * sleepers are inserted when their wake tick expires. */
    for (int slot = 0; slot < TASK_MAX; slot++) {
        task_t *task = &tasks[slot];
        task->queued_cpu = -1;
        if (task->state == TASK_UNUSED || task->state == TASK_ZOMBIE)
            continue;
        if (task->affinity_cpu >= 0)
            task->affinity_mask = 1U << (uint32_t)(uint8_t)task->affinity_cpu;
        else if (!task->affinity_mask)
            task->affinity_mask = all_mask;
        if (task->state == TASK_READY)
            (void)scheduler_smp_enqueue(slot, -1, false);
    }
    kprintf("[SCHED] runqueues por CPU activas; idle fuera de TASK_MAX (%u slots libres)\n",
            (uint32_t)TASK_MAX - task_count());
    return true;
}

bool task_current_is_idle(void) {
    task_t *task = task_current_entry();
    return task ? task->idle : true;
}

uint32_t task_current_cpu(void) { return smp_cpu_index(); }

uint32_t task_count(void) {
    uint32_t count = 0;
    uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].state != TASK_UNUSED &&
            tasks[i].state != TASK_ZOMBIE) count++;
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return count;
}

uint32_t task_snapshot_public(task_public_snapshot_t *out,
                              uint32_t capacity) {
    uint32_t count = 0U;
    uint32_t flags;
    if (!out || !capacity) return 0U;
    if (capacity > TASK_MAX) capacity = TASK_MAX;

    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX && count < capacity; i++) {
        task_public_snapshot_t *snapshot;
        uint32_t base;
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE)
            continue;
        snapshot = &out[count++];
        snapshot->pid = tasks[i].pid;
        snapshot->process_id = tasks[i].process_id;
        kstrncpy(snapshot->name, tasks[i].name, sizeof(snapshot->name));
        snapshot->name[sizeof(snapshot->name) - 1U] = '\0';
        snapshot->state = tasks[i].state;
        snapshot->cpu_ticks = tasks[i].cpu_ticks;
        base = tasks[i].stack_size + tasks[i].user_stack_size;
        snapshot->stack_bytes = base;
        snapshot->memory_hint_bytes = tasks[i].memory_bytes > base
            ? tasks[i].memory_bytes - base : 0U;
        snapshot->system = tasks[i].system;
        snapshot->user = tasks[i].user;
        snapshot->exit_requested = tasks[i].exit_requested;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return count;
}

const task_t *task_get(uint32_t index) {
    uint32_t found = 0;
    uint32_t cpu = smp_cpu_index();
    const task_t *result = NULL;
    uint32_t flags;
    if (cpu >= SMP_MAX_CPUS) cpu = 0U;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++)
        if (tasks[i].state != TASK_UNUSED &&
            tasks[i].state != TASK_ZOMBIE) {
            if (found++ == index) {
                g_task_query_snapshot[cpu] = tasks[i];
                result = &g_task_query_snapshot[cpu];
                break;
            }
        }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    /* g_task_query_snapshot is kernel BSS. Legacy Process Manager builds run
     * in Ring 3 and must receive the existing per-task user-heap mirror. */
    return task_user_export_snapshot(result);
}

uint32_t task_current_pid(void) {
    task_t *task = task_current_entry();
    return task && !task->idle ? task->pid : 0U;
}
uint32_t task_current_process_id(void) {
    task_t *task = task_current_entry();
    if (!task || task->idle) return 0U;
    return task->process_id ? task->process_id : task->pid;
}
uint32_t task_current_parent_pid(void) {
    task_t *task = task_current_entry();
    return task && !task->idle ? task->parent_pid : 0U;
}
uint32_t task_current_console_route(void) {
    task_t *task = task_current_entry();
    return task && !task->idle ? task->console_route : 0U;
}
void task_set_current_console_route(uint32_t route) {
    task_t *task = task_current_entry();
    if (task && !task->idle) task->console_route = route;
}
bool task_current_is_user(void) {
    task_t *task = task_current_entry();
    return task ? task->user : false;
}
bool task_current_is_win16(void) {
    task_t *task = task_current_entry();
    return task ? task->win16 : false;
}

static bool task_upcall_is_mouse_move(const task_upcall_t *upcall) {
    const gui_event_t *event;
    if (!upcall || (upcall->payload_argument != 1 &&
                    upcall->payload_argument != 2) ||
        upcall->payload_size != sizeof(gui_event_t)) return false;
    event = (const gui_event_t *)(const void *)upcall->payload;
    return event->type == GUI_EVENT_MOUSE_MOVE;
}

static gui_window_t *task_upcall_window(const task_upcall_t *upcall) {
    return upcall ? (gui_window_t *)upcall->window_target : NULL;
}

static uint8_t task_upcall_queue_count(const task_t *task) {
    if (!task) return 0U;
    return (uint8_t)((task->upcall_head + TASK_UPCALL_QUEUE -
                      task->upcall_tail) % TASK_UPCALL_QUEUE);
}

/* Remove one queued mouse move while preserving FIFO order for every other
 * callback.  Mouse positions are state samples, not transactions; keeping an
 * old sample is less important than admitting a click, key or paint callback. */
static bool task_upcall_remove_oldest_mouse_move(task_t *task) {
    uint8_t index;
    if (!task) return false;
    index = task->upcall_tail;
    while (index != task->upcall_head) {
        if (task_upcall_is_mouse_move(&task->upcalls[index])) {
            uint8_t current = index;
            uint8_t next = (uint8_t)((current + 1U) % TASK_UPCALL_QUEUE);
            while (next != task->upcall_head) {
                task->upcalls[current] = task->upcalls[next];
                current = next;
                next = (uint8_t)((next + 1U) % TASK_UPCALL_QUEUE);
            }
            task->upcall_head = (uint8_t)((task->upcall_head +
                TASK_UPCALL_QUEUE - 1U) % TASK_UPCALL_QUEUE);
            return true;
        }
        index = (uint8_t)((index + 1U) % TASK_UPCALL_QUEUE);
    }
    return false;
}

static bool task_queue_user_upcall_internal(
                            uint32_t pid, uint32_t entry,
                            const uint32_t *arguments, uint8_t argument_count,
                            const void *payload, uint8_t payload_size,
                            int8_t payload_argument,
                            void *release_after_upcall,
                            gui_window_t *window_target) {
    task_t *task = NULL;
    int task_slot = -1;
    bool woke = false;
    int32_t running_cpu = -1;
    uint8_t next;
    uint32_t table_flags;
    bool mouse_move = false;

    if (!pid || !entry || argument_count > TASK_UPCALL_ARGS ||
        payload_size > TASK_UPCALL_PAYLOAD ||
        (payload_size && (!payload || payload_argument < 0 ||
                          payload_argument >= argument_count))) return false;
    if ((payload_argument == 1 || payload_argument == 2) &&
        payload_size == sizeof(gui_event_t) && payload)
        mouse_move = ((const gui_event_t *)payload)->type ==
                     GUI_EVENT_MOUSE_MOVE;
    table_flags = kspin_lock_irqsave(&g_task_table_lock);
    /* A GUI callback is owned by a thread ID, not by an arbitrary sibling of
     * the same process.  Prefer the exact TID first.  The process-ID fallback
     * is retained for legacy Win32/process-wide callbacks whose original
     * owner thread has already disappeared. */
    for (int pass = 0; pass < 2 && !task; pass++) {
        for (int i = 1; i < TASK_MAX; i++) {
            bool matches = pass == 0 ? tasks[i].pid == pid
                                     : tasks[i].process_id == pid;
            if (!matches || !tasks[i].user || tasks[i].win16 ||
                tasks[i].state == TASK_UNUSED ||
                tasks[i].state == TASK_ZOMBIE) continue;
            task = &tasks[i];
            task_slot = i;
            break;
        }
    }
    if (!task) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        return false;
    }
    if (window_target && window_target->destroy_state != 0U) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        return false;
    }
    if (mouse_move) {
        uint8_t index = task->upcall_tail;
        while (index != task->upcall_head) {
            task_upcall_t *queued = &task->upcalls[index];
            if (task_upcall_is_mouse_move(queued) &&
                queued->entry == entry &&
                queued->payload_argument == payload_argument &&
                queued->argument_count == argument_count &&
                (!argument_count || !arguments ||
                 queued->arguments[0] == arguments[0])) {
                for (uint8_t i = 0; i < argument_count; i++)
                    queued->arguments[i] = arguments ? arguments[i] : 0U;
                kmemcpy(queued->payload, payload, payload_size);
                kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
                return true;
            }
            index = (uint8_t)((index + 1U) % TASK_UPCALL_QUEUE);
        }
        /* Reserve a few entries for release/click/key/paint callbacks. A
         * dropped intermediate move is harmless because the desktop already
         * stores the newest absolute cursor coordinates. */
        if (task_upcall_queue_count(task) >= TASK_UPCALL_QUEUE - 4U) {
            kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
            return true;
        }
    }

    next = (uint8_t)((task->upcall_head + 1U) % TASK_UPCALL_QUEUE);
    if (next == task->upcall_tail && !mouse_move &&
        task_upcall_remove_oldest_mouse_move(task))
        next = (uint8_t)((task->upcall_head + 1U) % TASK_UPCALL_QUEUE);
    if (next == task->upcall_tail) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        return mouse_move;
    }
    task_upcall_t *upcall = &task->upcalls[task->upcall_head];
    kmemset(upcall, 0, sizeof(*upcall));
    upcall->entry = entry;
    upcall->argument_count = argument_count;
    upcall->payload_argument = payload_argument;
    upcall->payload_size = payload_size;
    upcall->release_after_upcall = release_after_upcall;
    upcall->window_target = window_target;
    for (uint8_t i = 0; i < argument_count; i++)
        upcall->arguments[i] = arguments ? arguments[i] : 0U;
    if (payload_size) kmemcpy(upcall->payload, payload, payload_size);
    /*
     * Los callbacks GUI Ring 3 pueden cerrar su propia ventana, así que al
     * volver ya no es seguro desreferenciar arguments[0]. Guarde ahora una
     * copia de sus límites. Esto reemplaza la invalidación histórica de toda
     * la pantalla por el rectángulo afectado, sin usar un puntero posiblemente
     * destruido después del callback.
     */
    if (window_target &&
        (payload_argument == -1 || payload_argument == 1)) {
        upcall->invalidate_valid = true;
        upcall->invalidate_x = window_target->bounds.x;
        upcall->invalidate_y = window_target->bounds.y;
        upcall->invalidate_w = window_target->bounds.w;
        upcall->invalidate_h = window_target->bounds.h;
    }
    task->upcall_head = next;
    running_cpu = task->running_cpu;
    if (task->state == TASK_SLEEPING) {
        if (running_cpu == -1) {
            task->state = TASK_READY;
            woke = true;
        } else {
            /* The callback arrived while SYS_SLEEP still owns this task's
             * kernel stack. Ask that CPU to reschedule, then let the BSP wake
             * it only after running_cpu becomes -1. */
            task->wake_tick = pit_get_ticks();
        }
    }
    if (TASK_TRACE_DLLMAIN && payload_argument == -5) {
        kprintf("[UPCALL:DLLMAIN] QUEUE pid=%u entry=%x module=%x reason=%u reserved=%x head=%u tail=%u\n",
                task->pid, entry,
                argument_count > 0U ? upcall->arguments[0] : 0U,
                argument_count > 1U ? upcall->arguments[1] : 0U,
                argument_count > 2U ? upcall->arguments[2] : 0U,
                (uint32_t)task->upcall_head,
                (uint32_t)task->upcall_tail);
    }
    kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
    if (smp_scheduler_started()) {
        if (woke && task_slot >= 0)
            (void)scheduler_smp_enqueue(task_slot, -1, true);
        else if (running_cpu >= 0)
            smp_reschedule_cpu((uint32_t)running_cpu);
    }
    return true;
}

bool task_queue_user_upcall(uint32_t pid, uint32_t entry,
                            const uint32_t *arguments, uint8_t argument_count,
                            const void *payload, uint8_t payload_size,
                            int8_t payload_argument) {
    return task_queue_user_upcall_internal(pid, entry, arguments,
                                           argument_count, payload,
                                           payload_size, payload_argument,
                                           NULL, NULL);
}

bool task_queue_window_upcall(struct gui_window *raw_window,
                              uint32_t pid, uint32_t entry,
                              const uint32_t *arguments,
                              uint8_t argument_count,
                              const void *payload, uint8_t payload_size,
                              int8_t payload_argument) {
    gui_window_t *window = (gui_window_t *)raw_window;
    if (!window) return false;
    return task_queue_user_upcall_internal(pid, entry, arguments,
                                           argument_count, payload,
                                           payload_size, payload_argument,
                                           NULL, window);
}

bool task_queue_user_upcall_owned(uint32_t pid, uint32_t entry,
                                  const uint32_t *arguments,
                                  uint8_t argument_count,
                                  void *release_after_upcall) {
    if (!release_after_upcall) return false;
    return task_queue_user_upcall_internal(pid, entry, arguments,
                                           argument_count, NULL, 0U, -6,
                                           release_after_upcall, NULL);
}

bool task_cancel_window_upcalls(struct gui_window *raw_window) {
    gui_window_t *window = (gui_window_t *)raw_window;
    bool active = false;
    bool cancelled_paint = false;

    if (!window) return true;
    for (int slot = 1; slot < TASK_MAX; slot++) {
        task_upcall_t kept[TASK_UPCALL_QUEUE];
        void *release[TASK_UPCALL_QUEUE];
        uint8_t kept_count = 0U;
        uint8_t release_count = 0U;
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        task_t *task = &tasks[slot];
        uint8_t index;

        if (task->state == TASK_UNUSED) {
            kspin_unlock_irqrestore(&g_task_table_lock, flags);
            continue;
        }
        if (task->upcall_active &&
            task_upcall_window(&task->active_upcall) == window)
            active = true;

        index = task->upcall_tail;
        while (index != task->upcall_head) {
            task_upcall_t *upcall = &task->upcalls[index];
            bool matches = task_upcall_window(upcall) == window;
            if (matches) {
                if (upcall->payload_argument == -2)
                    cancelled_paint = true;
                if (upcall->release_after_upcall &&
                    release_count < TASK_UPCALL_QUEUE)
                    release[release_count++] = upcall->release_after_upcall;
            } else if (kept_count < TASK_UPCALL_QUEUE - 1U) {
                kept[kept_count++] = *upcall;
            }
            index = (uint8_t)((index + 1U) % TASK_UPCALL_QUEUE);
        }
        kmemset(task->upcalls, 0, sizeof(task->upcalls));
        for (uint8_t i = 0U; i < kept_count; i++)
            task->upcalls[i] = kept[i];
        task->upcall_tail = 0U;
        task->upcall_head = kept_count;
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
        for (uint8_t i = 0U; i < release_count; i++)
            kfree(release[i]);
    }
    if (cancelled_paint) {
        window->content_pending = false;
        window->content_repaint = false;
        gui_window_end_content_paint(window);
    }
    return !active;
}

bool task_prepare_user_upcall(registers_t *regs) {
    task_t *task = &tasks[current_index];
    uint32_t stack_limit;
    uint32_t payload_bytes;
    uint32_t needed;
    uint32_t stack;
    uint32_t user_payload = 0U;
    uint32_t table_flags;

    /*
     * Una tarea de usuario tambien puede ser interrumpida mientras ejecuta
     * una API proxy en Ring 0. Ese frame no contiene useresp/ss validos. No
     * retire el callback de la cola hasta volver a un frame Ring 3 real.
     *
     * CPU0 produce most GUI upcalls while the application can consume them on
     * another CPU.  head/tail and the selected entry therefore must be read as
     * one transaction; the old unlocked dequeue could observe a half-published
     * entry or race the producer into treating a live slot as free.
     */
    if (!regs || (regs->cs & 3U) != 3U || !task->user || !task->user_stack)
        return false;
    task_normalize_user_segments(task, regs);
    stack_limit = (uint32_t)(uintptr_t)task->user_stack;

    table_flags = kspin_lock_irqsave(&g_task_table_lock);
    if (task->upcall_active || task->upcall_head == task->upcall_tail) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        return false;
    }
    payload_bytes =
        ((uint32_t)task->upcalls[task->upcall_tail].payload_size + 3U) & ~3U;
    needed = payload_bytes +
        ((uint32_t)task->upcalls[task->upcall_tail].argument_count + 1U) * 4U;
    if (regs->useresp < stack_limit ||
        regs->useresp - stack_limit < needed) {
        kspin_unlock_irqrestore(&g_task_table_lock, table_flags);
        return false;
    }
    task->active_upcall = task->upcalls[task->upcall_tail];
    task->upcall_tail = (uint8_t)((task->upcall_tail + 1U) % TASK_UPCALL_QUEUE);
    task->upcall_saved_context = *regs;
    task->upcall_active = true;
    kspin_unlock_irqrestore(&g_task_table_lock, table_flags);

    task_fpu_begin_upcall(task);
    stack = regs->useresp;
    if (task->active_upcall.payload_size &&
        task->active_upcall.payload_argument >= 0) {
        /* active_upcall lives in the supervisor-only task table. Passing its
         * payload address to Ring 3 made every GUI event fault as soon as the
         * application read event->type. Keep the callback ABI unchanged, but
         * materialize the payload in the interrupted task's own user stack. */
        payload_bytes =
            ((uint32_t)task->active_upcall.payload_size + 3U) & ~3U;
        stack -= payload_bytes;
        user_payload = stack;
        kmemset((void *)(uintptr_t)user_payload, 0, payload_bytes);
        kmemcpy((void *)(uintptr_t)user_payload,
                task->active_upcall.payload,
                task->active_upcall.payload_size);
        task->active_upcall.arguments[
            (uint8_t)task->active_upcall.payload_argument] = user_payload;
    }
    for (int i = (int)task->active_upcall.argument_count - 1; i >= 0; i--) {
        stack -= 4U;
        *(uint32_t *)(uintptr_t)stack = task->active_upcall.arguments[i];
    }
    stack -= 4U;
    *(uint32_t *)(uintptr_t)stack =
        (uint32_t)(uintptr_t)g_user_upcall_return_gate;
    regs->eip = task->active_upcall.entry;
    regs->useresp = stack;
    regs->eax = 0;
    if (TASK_TRACE_DLLMAIN &&
        task->active_upcall.payload_argument == -5) {
        kprintf("[UPCALL:DLLMAIN] ENTER pid=%u entry=%x module=%x reason=%u useresp=%x return=%x\n",
                task->pid,
                task->active_upcall.entry,
                task->active_upcall.arguments[0],
                task->active_upcall.arguments[1],
                regs->useresp,
                (uint32_t)(uintptr_t)g_user_upcall_return_gate);
    }
    return true;
}

bool task_finish_user_upcall(registers_t *regs) {
    task_t *task = &tasks[current_index];
    gui_window_t *deferred_window = NULL;
    uint32_t callback_eax;

    if (!regs || !task->upcall_active) return false;
    {
        gui_window_t *candidate = task_upcall_window(&task->active_upcall);
        if (candidate && candidate->destroy_state == 1U)
            deferred_window = candidate;
    }
    callback_eax = regs->eax;

    /* The return trampoline preserves callback EAX in EBX. */
    if (task->active_upcall.payload_argument == -5)
        callback_eax = regs->ebx;

    /* BLES_WINE_DLL_DIAGNOSTICS_TASK_20260723 */
    if (TASK_TRACE_DLLMAIN &&
        task->active_upcall.payload_argument == -5) {
        kprintf("[UPCALL:DLLMAIN] RETURN pid=%u entry=%x module=%x reason=%u eax=%x (%s)\n",
                task->pid,
                task->active_upcall.entry,
                task->active_upcall.arguments[0],
                task->active_upcall.arguments[1],
                callback_eax,
                callback_eax ? "TRUE" : "FALSE");
    }
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
            bool direct_low_memory;
            bool first_content_frame = !window->content_ready;
            window->content_pending = false;
            direct_low_memory = window->content_staging_slot == -3;
            captured = gui_window_capture_content(window, surface);
            gui_window_end_content_paint(window);
            if (captured && direct_low_memory) {
                gui_gfx_present_rect(&gui_get_desktop()->surface,
                                     gui_window_content_rect(window));
                window->dirty = false;
            } else if (!captured || window->content_repaint) {
                window->dirty = true;
            } else if (first_content_frame) {
                /*
                 * El marco pudo haberse presentado mientras Ring 3 todavía
                 * preparaba su primer cliente. Recomponga una vez el cuadro
                 * completo al recibir ese cache: evita conservar franjas del
                 * frame intermedio (el histórico negro al abrir Display).
                 * Los cuadros siguientes mantienen dirty rectangles.
                 */
                gui_gfx_invalidate_front();
                gui_desktop_invalidate_all(gui_get_desktop());
            } else {
                gui_desktop_invalidate_rect(gui_get_desktop(),
                                            window->bounds);
            }
            /* A mode change can invalidate a direct surface while its old
               callback is still returning. Ask for the replacement frame
               even on the no-cache path; otherwise the cleared background
               could remain visible until unrelated input arrives. */
            if (!direct_low_memory || !captured) gui_request_paint();
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
    } else if (task->active_upcall.payload_argument == -1 ||
               task->active_upcall.payload_argument == 1) {
        /* Menu, widget o evento de una ventana Ring 3. El callback puede haber
           cambiado su estado mientras el compositor terminaba otro frame; una
           dirty booleana sola podia ser limpiada por esa carrera. Convierta el
           resultado en una invalidacion generacional que no puede perderse. */
        /* El callback tambien puede haber cerrado/destruido la ventana, por
           lo que no se vuelve a desreferenciar arguments[0] al retornar. */
        gui_desktop_t *desktop = gui_get_desktop();
        if (desktop && task->active_upcall.invalidate_valid) {
            gui_desktop_invalidate_rect(desktop, (gui_rect_t){
                task->active_upcall.invalidate_x,
                task->active_upcall.invalidate_y,
                task->active_upcall.invalidate_w,
                task->active_upcall.invalidate_h
            });
        } else {
            gui_desktop_invalidate_all(desktop);
        }
        gui_request_paint();
    } else if (task->active_upcall.payload_argument == 2 &&
               task->active_upcall.argument_count > 1) {
        /* Evento de un gui_program externo. */
        gui_desktop_t *desktop = (gui_desktop_t *)(uintptr_t)
            task->active_upcall.arguments[1];
        if (desktop) gui_desktop_invalidate_all(desktop);
        gui_request_paint();
    }
    task_fpu_end_upcall(task);
    *regs = task->upcall_saved_context;
    task_normalize_user_segments(task, regs);
    {
        void *release_after_upcall;
        uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
        release_after_upcall = task->active_upcall.release_after_upcall;
        task->active_upcall.release_after_upcall = NULL;
        task->upcall_active = false;
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
        if (release_after_upcall) kfree(release_after_upcall);
    }
    if (deferred_window && deferred_window->destroy_state == 1U)
        gui_window_destroy(deferred_window);
    return true;
}

int32_t task_thread_join_try(uint32_t tid, int32_t *status) {
    uint32_t process_id = task_current_process_id();
    uint32_t self = task_current_pid();
    int reap_index = -1;
    int32_t result = -BK_ECHILD;
    uint32_t flags;

    if (!tid || !process_id || tid == self) return -BK_EINVAL;
    flags = kspin_lock_irqsave(&g_task_table_lock);

    for (uint32_t i = 0U; i < TASK_MAX; i++) {
        task_thread_completion_t *completion = &g_thread_completions[i];
        if (!completion->valid || completion->tid != tid) continue;
        if (completion->process_id != process_id) {
            result = -BK_EACCES;
        } else {
            if (status) *status = completion->result;
            completion->valid = false;
            result = (int32_t)tid;
        }
        kspin_unlock_irqrestore(&g_task_table_lock, flags);
        return result;
    }

    for (int i = 1; i < TASK_MAX; i++) {
        task_t *thread = &tasks[i];
        if (thread->pid != tid || thread->state == TASK_UNUSED) continue;
        if (!thread->thread || thread->process_id != process_id) {
            result = -BK_EACCES;
            break;
        }
        if (!thread->thread_joinable) {
            result = -BK_EINVAL;
            break;
        }
        if (thread->state != TASK_ZOMBIE || thread->running_cpu != -1 ||
            thread->queued_cpu != -1 || g_task_reap_claim[i]) {
            result = 0;
            break;
        }
        if (status) *status = thread->exit_code;
        thread->thread_joinable = false;
        g_task_reap_claim[i] = 1U;
        reap_index = i;
        result = (int32_t)tid;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);

    if (reap_index >= 0) task_reap_claimed_index(reap_index);
    return result ? result : 0;
}

bool task_thread_detach(uint32_t tid) {
    uint32_t process_id = task_current_process_id();
    uint32_t flags;
    bool found = false;
    if (!tid || !process_id) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (uint32_t i = 0U; i < TASK_MAX; i++) {
        if (g_thread_completions[i].valid &&
            g_thread_completions[i].tid == tid &&
            g_thread_completions[i].process_id == process_id) {
            g_thread_completions[i].valid = false;
            found = true;
            break;
        }
    }
    if (!found) {
        for (int i = 1; i < TASK_MAX; i++) {
            if (tasks[i].pid != tid || tasks[i].state == TASK_UNUSED) continue;
            if (!tasks[i].thread || tasks[i].process_id != process_id) break;
            tasks[i].thread_joinable = false;
            found = true;
            break;
        }
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return found;
}

uint32_t task_current_tid(void) {
    return task_current_pid();
}

bool task_set_current_tls_base(uint32_t base) {
    task_t *task;
    uint32_t flags;
    /* FS is a per-CPU descriptor. Keep this task on the current CPU until
     * both its saved base and the live GDT descriptor agree. */
    task_preempt_disable();
    task = task_current_entry();
    if (!task || !task->user || task->win16 || task->idle) {
        task_preempt_enable();
        return false;
    }
    flags = kspin_lock_irqsave(&g_task_table_lock);
    task->user_fs_base = base;
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    gdt_set_user_fs_base(base);
    task_preempt_enable();
    return true;
}

uint32_t task_current_tls_base(void) {
    task_t *task = task_current_entry();
    return task && task->user && !task->win16 ? task->user_fs_base : 0U;
}

uint32_t task_thread_count(uint32_t process_id) {
    uint32_t count = 0U;
    uint32_t flags;
    if (!process_id) process_id = task_current_process_id();
    if (!process_id) return 0U;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].process_id == process_id && tasks[i].thread &&
            tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_ZOMBIE)
            count++;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return count;
}

int32_t task_waitpid(uint32_t pid, int32_t *status) {
    uint32_t parent = task_current_process_id();
    bool child_exists = false;
    int index = -1;
    int32_t result = -1;
    int32_t child_status = 0;
    uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);

    for (int i = 1; i < TASK_MAX; i++) {
        task_t *child = &tasks[i];
        if (child->state == TASK_UNUSED || child->parent_pid != parent ||
            child->thread)
            continue;
        if (pid != 0U && child->pid != pid && child->process_id != pid)
            continue;
        child_exists = true;
        if (child->state != TASK_ZOMBIE || child->running_cpu != -1 ||
            child->queued_cpu != -1 || g_task_reap_claim[i]) continue;
        g_task_reap_claim[i] = 1U;
        index = i;
        result = (int32_t)child->pid;
        child_status = child->exit_code;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);

    if (index < 0) return child_exists ? 0 : -1;
    if (status) *status = child_status;
    task_reap_claimed_index(index);
    return result;
}

uint8_t task_cpu_usage(void) { return cpu_usage; }

uint32_t task_cpu_count(void) {
    uint32_t count = smp_online_cpu_count();
    if (!count) count = 1U;
    return count > SMP_MAX_CPUS ? SMP_MAX_CPUS : count;
}

uint8_t task_cpu_usage_for(uint32_t cpu_index) {
    if (cpu_index >= task_cpu_count()) return 0U;
    return cpu_usage_per_cpu[cpu_index];
}

uint32_t task_runqueue_depth(uint32_t cpu_index) {
    return scheduler_smp_depth(cpu_index);
}

uint32_t task_scheduler_steals(uint32_t cpu_index) {
    return scheduler_smp_steals(cpu_index);
}

uint32_t task_scheduler_migrations(uint32_t cpu_index) {
    return scheduler_smp_migrations(cpu_index);
}

uint32_t task_scheduler_ipis(uint32_t cpu_index) {
    return scheduler_smp_ipis(cpu_index);
}

bool task_set_affinity_mask(uint32_t pid, uint32_t mask) {
    int slot = -1;
    int32_t running = -1;
    bool was_queued = false;
    uint32_t allowed = scheduler_smp_all_mask();
    uint32_t flags;
    if (!pid) pid = task_current_pid();
    mask &= allowed;
    if (!pid || !mask) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *task = &tasks[i];
        if (task->pid != pid || task->state == TASK_UNUSED ||
            task->state == TASK_ZOMBIE || task->system) continue;
        {
            uint32_t current_mask = task->affinity_cpu >= 0
                ? (1U << (uint32_t)(uint8_t)task->affinity_cpu)
                : (task->affinity_mask ? task->affinity_mask : allowed);
            if (current_mask == mask) {
                kspin_unlock_irqrestore(&g_task_table_lock, flags);
                return true;
            }
        }
        task->affinity_cpu = -1;
        task->affinity_mask = mask;
        task->preferred_cpu = (uint8_t)scheduler_smp_choose_cpu(
            task, task->last_cpu < SMP_MAX_CPUS ? task->last_cpu : 0U);
        running = task->running_cpu;
        was_queued = task->queued_cpu >= 0;
        slot = i;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    if (slot < 0) return false;
    if (was_queued) {
        scheduler_smp_remove(slot);
        (void)scheduler_smp_enqueue(slot, -1, true);
    } else if (running >= 0 &&
               ((mask & (1U << (uint32_t)running)) == 0U)) {
        smp_reschedule_cpu((uint32_t)running);
    } else if (smp_scheduler_started()) {
        /* Covers a dequeue/affinity race: the task may have left its queue
         * between the snapshot and scheduler_smp_remove(). */
        smp_reschedule_mask(scheduler_smp_all_mask());
    }
    return true;
}

uint32_t task_get_affinity_mask(uint32_t pid) {
    uint32_t result = 0U;
    uint32_t flags;
    if (!pid) pid = task_current_pid();
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE) continue;
        result = tasks[i].affinity_cpu >= 0
            ? (1U << (uint32_t)(uint8_t)tasks[i].affinity_cpu)
            : (tasks[i].affinity_mask ? tasks[i].affinity_mask
                                      : scheduler_smp_all_mask());
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return result;
}

const char *task_state_name(task_state_t state) {
    if (state == TASK_READY) return "Listo";
    if (state == TASK_RUNNING) return "Ejecutando";
    if (state == TASK_SLEEPING) return "Durmiendo";
    if (state == TASK_ZOMBIE) return "Terminado";
    return "Libre";
}

const char *task_launch_arg(void) {
    task_t *task = task_current_entry();
    return task && !task->idle ? task->launch_arg : "";
}


bool task_set_user_fs_base(uint32_t pid, uint32_t base) {
    bool changed = false;
    uint32_t flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE || !tasks[i].user) continue;
        tasks[i].user_fs_base = base;
        if (tasks[i].context)
            tasks[i].context->fs = base ? GDT_USER_FS : GDT_USER_DATA;
        changed = true;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return changed;
}

const char *task_user_export_string(const char *source) {
    task_t *task = task_current_entry();
    task_user_string_export_t *node;
    uint32_t length;
    uint32_t capacity;

    if (!source) source = "";
    if (!task || !task->user || task->idle) return source;

    length = (uint32_t)kstrlen(source) + 1U;
    if (!length || length > 65536U) return NULL;
    capacity = (length + 63U) & ~63U;

    node = (task_user_string_export_t *)task->user_string_exports;
    while (node) {
        if (node->source == source) {
            if (node->capacity < capacity) {
                char *replacement = (char *)krealloc(node->copy, capacity);
                if (!replacement) return NULL;
                node->copy = replacement;
                node->capacity = capacity;
            }
            kmemcpy(node->copy, source, length);
            return node->copy;
        }
        node = node->next;
    }

    if (task->user_string_export_count < TASK_USER_STRING_EXPORT_LIMIT) {
        node = (task_user_string_export_t *)kzalloc(sizeof(*node));
        if (!node) return NULL;
        node->copy = (char *)kmalloc(capacity);
        if (!node->copy) {
            kfree(node);
            return NULL;
        }
        node->source = source;
        node->capacity = capacity;
        node->next = (task_user_string_export_t *)task->user_string_exports;
        task->user_string_exports = node;
        task->user_string_export_count++;
        kmemcpy(node->copy, source, length);
        return node->copy;
    }

    /* Pathological callers that translate an unbounded number of distinct
     * pointers share one overflow slot instead of leaking until process exit. */
    node = (task_user_string_export_t *)task->user_string_overflow;
    if (!node) {
        node = (task_user_string_export_t *)kzalloc(sizeof(*node));
        if (!node) return NULL;
        task->user_string_overflow = node;
    }
    if (node->capacity < capacity) {
        char *replacement = (char *)krealloc(node->copy, capacity);
        if (!replacement) return NULL;
        node->copy = replacement;
        node->capacity = capacity;
    }
    node->source = source;
    kmemcpy(node->copy, source, length);
    return node->copy;
}

const task_t *task_user_export_snapshot(const task_t *source) {
    task_t *task = task_current_entry();
    task_t *snapshot;

    if (!source) return NULL;
    if (!task || !task->user || task->idle) return source;
    snapshot = (task_t *)task->user_task_snapshot;
    if (!snapshot) {
        snapshot = (task_t *)kmalloc(sizeof(*snapshot));
        if (!snapshot) return NULL;
        task->user_task_snapshot = snapshot;
    }
    kmemcpy(snapshot, source, sizeof(*snapshot));
    /* Never publish live kernel stack/context pointers through the legacy
     * snapshot. Old Process Manager builds only need metadata. */
    snapshot->stack = NULL;
    snapshot->user_stack = NULL;
    snapshot->stack_allocation = NULL;
    snapshot->user_stack_allocation = NULL;
    snapshot->context = NULL;
    snapshot->user_string_exports = NULL;
    snapshot->user_string_overflow = NULL;
    snapshot->user_task_snapshot = NULL;
    return snapshot;
}

bool task_get_user_stack_bounds(uint32_t pid, uint32_t *limit_out,
                                uint32_t *base_out) {
    bool found = false;
    uint32_t flags;

    if (!limit_out || !base_out) return false;
    flags = kspin_lock_irqsave(&g_task_table_lock);
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].pid != pid || tasks[i].state == TASK_UNUSED ||
            tasks[i].state == TASK_ZOMBIE || !tasks[i].user ||
            !tasks[i].user_stack) continue;
        *limit_out = (uint32_t)(uintptr_t)tasks[i].user_stack;
        *base_out = (uint32_t)(uintptr_t)
            ((uint8_t *)tasks[i].user_stack + tasks[i].user_stack_size);
        found = true;
        break;
    }
    kspin_unlock_irqrestore(&g_task_table_lock, flags);
    return found;
}
