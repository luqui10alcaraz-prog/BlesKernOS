#include "include/scheduler_smp.h"
#include "include/klock.h"
#include "include/memory.h"
#include "include/smp.h"
#include "include/vga.h"

#define SCHED_QUEUE_NONE    (-1)
#define SCHED_QUEUE_CLAIMED (-2)

typedef struct {
    kspinlock_t lock;
    int16_t slots[TASK_MAX];
    volatile uint32_t count;
    volatile uint32_t steals;
    volatile uint32_t migrations;
    volatile uint32_t resched_ipis;
} scheduler_runqueue_t;

static scheduler_runqueue_t *g_runqueues;
static task_t *g_task_table;
static uint32_t g_task_count;
static uint32_t g_online_cpus = 1U;
static volatile uint32_t g_ready;

static bool atomic_cas_i32(volatile int32_t *address,
                           int32_t expected, int32_t desired) {
    int32_t observed;
    __asm__ volatile ("lock cmpxchgl %3, %1"
                      : "=a"(observed), "+m"(*address)
                      : "0"(expected), "r"(desired)
                      : "memory", "cc");
    return observed == expected;
}

static void atomic_store_i32(volatile int32_t *address, int32_t value) {
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(value), "+m"(*address)
                      : : "memory", "cc");
}

static uint32_t clamp_cpu(uint32_t cpu) {
    if (g_online_cpus == 0U) return 0U;
    return cpu < g_online_cpus ? cpu : 0U;
}

uint32_t scheduler_smp_all_mask(void) {
    uint32_t count = g_online_cpus;
    if (count == 0U) count = 1U;
    if (count >= 32U) return 0xFFFFFFFFU;
    return (1U << count) - 1U;
}

bool scheduler_smp_cpu_allowed(const task_t *task, uint32_t cpu) {
    uint32_t mask;
    if (!task || cpu >= g_online_cpus || cpu >= 32U) return false;
    if (task->affinity_cpu >= 0)
        return cpu == (uint32_t)(uint8_t)task->affinity_cpu;
    mask = task->affinity_mask ? task->affinity_mask
                               : scheduler_smp_all_mask();
    return (mask & (1U << cpu)) != 0U;
}

bool scheduler_smp_init(task_t *table, uint32_t task_count) {
    if (!table || !task_count || task_count > TASK_MAX) return false;
    g_runqueues = (scheduler_runqueue_t *)
        kzalloc(sizeof(scheduler_runqueue_t) * SMP_MAX_CPUS);
    if (!g_runqueues) return false;
    g_task_table = table;
    g_task_count = task_count;
    for (uint32_t cpu = 0U; cpu < SMP_MAX_CPUS; cpu++)
        kspin_init(&g_runqueues[cpu].lock);
    g_online_cpus = 1U;
    g_ready = 1U;
    return true;
}

void scheduler_smp_reset(uint32_t online_cpus) {
    if (!g_runqueues) return;
    if (!online_cpus) online_cpus = 1U;
    if (online_cpus > SMP_MAX_CPUS) online_cpus = SMP_MAX_CPUS;
    g_online_cpus = online_cpus;
    for (uint32_t cpu = 0U; cpu < SMP_MAX_CPUS; cpu++) {
        uint32_t flags = kspin_lock_irqsave(&g_runqueues[cpu].lock);
        g_runqueues[cpu].count = 0U;
        g_runqueues[cpu].steals = 0U;
        g_runqueues[cpu].migrations = 0U;
        g_runqueues[cpu].resched_ipis = 0U;
        for (uint32_t i = 0U; i < TASK_MAX; i++)
            g_runqueues[cpu].slots[i] = -1;
        kspin_unlock_irqrestore(&g_runqueues[cpu].lock, flags);
    }
    for (uint32_t i = 0U; i < g_task_count; i++)
        g_task_table[i].queued_cpu = SCHED_QUEUE_NONE;
    __asm__ volatile ("" : : : "memory");
    g_ready = 1U;
}

bool scheduler_smp_ready(void) {
    return g_ready != 0U && g_runqueues != NULL;
}

uint32_t scheduler_smp_depth(uint32_t cpu) {
    if (!g_runqueues || cpu >= g_online_cpus) return 0U;
    return g_runqueues[cpu].count;
}

static uint32_t scheduler_smp_load(uint32_t cpu) {
    uint32_t load;
    if (!g_runqueues || cpu >= g_online_cpus) return 0U;
    load = g_runqueues[cpu].count;

    /* La cola no contiene la tarea que ya esta ejecutandose. Antes, un CPU
     * corriendo 3D Plus aparecia con carga cero y el siguiente programa se
     * colocaba encima del mismo nucleo. La lectura es deliberadamente
     * aproximada: estos campos cambian atomicamente y el balanceador solo
     * necesita distinguir ocupado de idle. */
    for (uint32_t i = 0U; i < g_task_count; i++) {
        const task_t *running = &g_task_table[i];
        if (running->state == TASK_RUNNING &&
            running->running_cpu == (int32_t)cpu && !running->idle) {
            load++;
            break;
        }
    }
    return load;
}

uint32_t scheduler_smp_choose_cpu(const task_t *task, uint32_t hint_cpu) {
    uint32_t best = 0U;
    uint32_t best_load = 0xFFFFFFFFU;
    bool found = false;

    if (!task || g_online_cpus <= 1U) return 0U;
    if (task->affinity_cpu >= 0)
        return clamp_cpu((uint32_t)(uint8_t)task->affinity_cpu);

    if (hint_cpu < g_online_cpus && scheduler_smp_cpu_allowed(task, hint_cpu)) {
        best = hint_cpu;
        best_load = scheduler_smp_load(hint_cpu);
        /* Preserve cache locality unless another CPU is clearly less loaded. */
        if (task->last_cpu == hint_cpu && best_load == 0U) return hint_cpu;
        found = true;
    }

    for (uint32_t cpu = 0U; cpu < g_online_cpus; cpu++) {
        uint32_t load;
        if (!scheduler_smp_cpu_allowed(task, cpu)) continue;
        load = scheduler_smp_load(cpu);
        /* CPU0 owns PIC IRQs, the GUI and most legacy drivers. Native user
         * work is biased toward APs when their queues are equally loaded. */
        if (cpu == 0U && task->user && task->affinity_cpu < 0) load += 2U;
        if (!found || load < best_load ||
            (load == best_load && cpu == task->last_cpu)) {
            best = cpu;
            best_load = load;
            found = true;
        }
    }
    return found ? best : 0U;
}

static bool rq_append_locked(scheduler_runqueue_t *rq, int slot,
                             uint32_t cpu) {
    task_t *task;
    if (!rq || slot < 0 || (uint32_t)slot >= g_task_count ||
        rq->count >= g_task_count) return false;
    task = &g_task_table[slot];
    rq->slots[rq->count++] = (int16_t)slot;
    task->queued_cpu = (int32_t)cpu;
    task->preferred_cpu = (uint8_t)cpu;
    return true;
}

bool scheduler_smp_enqueue(int slot, int32_t requested_cpu, bool notify) {
    task_t *task;
    scheduler_runqueue_t *rq;
    uint32_t cpu;
    uint32_t flags;
    bool was_empty;
    bool ok;

    if (!scheduler_smp_ready() || slot < 0 ||
        (uint32_t)slot >= g_task_count) return false;
    task = &g_task_table[slot];
    /* Only -1 means the task is completely detached from a CPU stack.
     * TASK_CPU_HANDOFF is negative too, but the previous CPU may still be
     * returning through that task's interrupt frame. Accepting any negative
     * value here allowed the same task to run on two CPUs at once. */
    if (task->state != TASK_READY || task->running_cpu != -1) return false;
    if (!atomic_cas_i32(&task->queued_cpu, SCHED_QUEUE_NONE,
                        SCHED_QUEUE_CLAIMED))
        return task->queued_cpu >= 0;

    cpu = requested_cpu >= 0
        ? clamp_cpu((uint32_t)requested_cpu)
        : scheduler_smp_choose_cpu(task, task->preferred_cpu);
    if (!scheduler_smp_cpu_allowed(task, cpu))
        cpu = scheduler_smp_choose_cpu(task, smp_cpu_index());
    rq = &g_runqueues[cpu];
    flags = kspin_lock_irqsave(&rq->lock);
    was_empty = rq->count == 0U;
    ok = rq_append_locked(rq, slot, cpu);
    kspin_unlock_irqrestore(&rq->lock, flags);
    if (!ok) {
        atomic_store_i32(&task->queued_cpu, SCHED_QUEUE_NONE);
        return false;
    }

    if (notify && smp_scheduler_started() && smp_cpu_online(cpu) &&
        cpu != smp_cpu_index() &&
        was_empty) {
        rq->resched_ipis++;
        smp_reschedule_cpu(cpu);
    }
    return true;
}

bool scheduler_smp_requeue_running(int slot, uint32_t cpu) {
    scheduler_runqueue_t *rq;
    task_t *task;
    uint32_t flags;
    bool ok;
    if (!scheduler_smp_ready() || slot < 0 ||
        (uint32_t)slot >= g_task_count || cpu >= g_online_cpus) return false;
    task = &g_task_table[slot];
    rq = &g_runqueues[cpu];
    flags = kspin_lock_irqsave(&rq->lock);
    task->running_cpu = -1;
    task->state = TASK_READY;
    task->queued_cpu = SCHED_QUEUE_CLAIMED;
    ok = rq_append_locked(rq, slot, cpu);
    if (!ok) {
        task->queued_cpu = SCHED_QUEUE_NONE;
        task->running_cpu = (int32_t)cpu;
        task->state = TASK_RUNNING;
    }
    kspin_unlock_irqrestore(&rq->lock, flags);
    return ok;
}

static int rq_take_index_locked(scheduler_runqueue_t *rq, uint32_t index,
                                uint32_t cpu, bool stolen) {
    int slot;
    task_t *task;
    if (!rq || index >= rq->count) return -1;
    slot = rq->slots[index];
    for (uint32_t i = index + 1U; i < rq->count; i++)
        rq->slots[i - 1U] = rq->slots[i];
    rq->slots[--rq->count] = -1;
    if (slot < 0 || (uint32_t)slot >= g_task_count) return -1;
    task = &g_task_table[slot];
    task->queued_cpu = SCHED_QUEUE_NONE;
    if (task->state != TASK_READY || task->running_cpu != -1 ||
        !scheduler_smp_cpu_allowed(task, cpu)) return -1;
    task->state = TASK_RUNNING;
    task->running_cpu = (int32_t)cpu;
    if (task->last_cpu != cpu) {
        if (task->last_cpu < SMP_MAX_CPUS) g_runqueues[cpu].migrations++;
        task->last_cpu = (uint8_t)cpu;
    }
    if (stolen) g_runqueues[cpu].steals++;
    return slot;
}

int scheduler_smp_dequeue(uint32_t cpu) {
    scheduler_runqueue_t *rq;
    uint32_t flags;
    int result = -1;
    if (!scheduler_smp_ready() || cpu >= g_online_cpus) return -1;
    rq = &g_runqueues[cpu];
    flags = kspin_lock_irqsave(&rq->lock);
    /* CPU0's GUI task is latency-sensitive and remains pinned. */
    if (cpu == 0U) {
        for (uint32_t i = 0U; i < rq->count; i++) {
            if (rq->slots[i] == 0) {
                result = rq_take_index_locked(rq, i, cpu, false);
                break;
            }
        }
    }
    while (result < 0 && rq->count) {
        result = rq_take_index_locked(rq, 0U, cpu, false);
    }
    kspin_unlock_irqrestore(&rq->lock, flags);
    return result;
}

int scheduler_smp_steal(uint32_t cpu) {
    uint32_t best_victim = SMP_MAX_CPUS;
    uint32_t best_count = 0U;
    if (!scheduler_smp_ready() || cpu >= g_online_cpus) return -1;

    for (uint32_t victim = 0U; victim < g_online_cpus; victim++) {
        uint32_t count;
        if (victim == cpu) continue;
        count = scheduler_smp_depth(victim);
        if (count > best_count) {
            best_count = count;
            best_victim = victim;
        }
    }
    if (best_victim >= g_online_cpus || best_count == 0U) return -1;

    scheduler_runqueue_t *rq = &g_runqueues[best_victim];
    uint32_t flags;
    if (!kspin_try_lock_irqsave(&rq->lock, &flags)) return -1;
    for (uint32_t offset = 0U; offset < rq->count; offset++) {
        uint32_t index = rq->count - 1U - offset;
        int slot = rq->slots[index];
        task_t *task;
        if (slot < 0 || (uint32_t)slot >= g_task_count) continue;
        task = &g_task_table[slot];
        if (slot == 0 || !scheduler_smp_cpu_allowed(task, cpu) ||
            task->affinity_cpu >= 0) continue;
        slot = rq_take_index_locked(rq, index, cpu, true);
        kspin_unlock_irqrestore(&rq->lock, flags);
        return slot;
    }
    kspin_unlock_irqrestore(&rq->lock, flags);
    return -1;
}

void scheduler_smp_remove(int slot) {
    if (!scheduler_smp_ready() || slot < 0 ||
        (uint32_t)slot >= g_task_count) return;
    for (uint32_t cpu = 0U; cpu < g_online_cpus; cpu++) {
        scheduler_runqueue_t *rq = &g_runqueues[cpu];
        uint32_t flags = kspin_lock_irqsave(&rq->lock);
        for (uint32_t i = 0U; i < rq->count; i++) {
            if (rq->slots[i] != slot) continue;
            for (uint32_t j = i + 1U; j < rq->count; j++)
                rq->slots[j - 1U] = rq->slots[j];
            rq->slots[--rq->count] = -1;
            g_task_table[slot].queued_cpu = SCHED_QUEUE_NONE;
            break;
        }
        kspin_unlock_irqrestore(&rq->lock, flags);
    }
}

uint32_t scheduler_smp_steals(uint32_t cpu) {
    return g_runqueues && cpu < g_online_cpus ? g_runqueues[cpu].steals : 0U;
}
uint32_t scheduler_smp_migrations(uint32_t cpu) {
    return g_runqueues && cpu < g_online_cpus ? g_runqueues[cpu].migrations : 0U;
}
uint32_t scheduler_smp_ipis(uint32_t cpu) {
    return g_runqueues && cpu < g_online_cpus ? g_runqueues[cpu].resched_ipis : 0U;
}
