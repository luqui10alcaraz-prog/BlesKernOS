#ifndef SCHEDULER_SMP_H
#define SCHEDULER_SMP_H

#include "types.h"
#include "task.h"

/* Internal scalable scheduler backend. The public task API remains in task.h. */

bool scheduler_smp_init(task_t *table, uint32_t task_count);
void scheduler_smp_reset(uint32_t online_cpus);
bool scheduler_smp_ready(void);

uint32_t scheduler_smp_all_mask(void);
bool scheduler_smp_cpu_allowed(const task_t *task, uint32_t cpu);
uint32_t scheduler_smp_choose_cpu(const task_t *task, uint32_t hint_cpu);

bool scheduler_smp_enqueue(int slot, int32_t requested_cpu, bool notify);
bool scheduler_smp_requeue_running(int slot, uint32_t cpu);
int scheduler_smp_dequeue(uint32_t cpu);
int scheduler_smp_steal(uint32_t cpu);
void scheduler_smp_remove(int slot);

uint32_t scheduler_smp_depth(uint32_t cpu);
uint32_t scheduler_smp_steals(uint32_t cpu);
uint32_t scheduler_smp_migrations(uint32_t cpu);
uint32_t scheduler_smp_ipis(uint32_t cpu);

#endif
