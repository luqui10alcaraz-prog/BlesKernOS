#ifndef KLOCK_H
#define KLOCK_H

#include "types.h"
#include "smp.h"

/*
 * Synchronization primitives for the SMP kernel.
 *
 * kspinlock_t: very short IRQ-safe critical sections. Ownership is per CPU;
 *              the lock disables local interrupts while held.
 * kmutex_t:    long process-context sections. Ownership is per task and a
 *              waiter yields instead of burning a complete CPU.
 * krwlock_t:   read-mostly process-context state with writer preference.
 *
 * The sleeping locks must never be acquired from a hardware IRQ handler.
 */

typedef struct {
    volatile uint32_t state;
    volatile uint32_t owner_cpu;
    uint16_t depth;
    uint16_t reserved;
    volatile uint32_t contentions;
} kspinlock_t;

typedef struct {
    volatile uint32_t owner;
    uint16_t depth;
    uint16_t reserved;
    volatile uint32_t waiters;
    volatile uint32_t contentions;
} kmutex_t;

typedef struct {
    volatile int32_t state;          /* -1 writer, >=0 reader count */
    volatile uint32_t writer;
    uint16_t writer_depth;
    uint16_t reserved;
    volatile uint32_t waiting_writers;
    volatile uint32_t contentions;
} krwlock_t;

typedef struct {
    uint32_t spin_acquires;
    uint32_t spin_contentions;
    uint32_t mutex_acquires;
    uint32_t mutex_contentions;
    uint32_t rw_read_acquires;
    uint32_t rw_write_acquires;
    uint32_t rw_contentions;
} klock_cpu_stats_t;

#define KSPINLOCK_INITIALIZER {0U, 0U, 0U, 0U, 0U}
#define KMUTEX_INITIALIZER    {0U, 0U, 0U, 0U, 0U}
#define KRWLOCK_INITIALIZER   {0, 0U, 0U, 0U, 0U, 0U}

void kspin_init(kspinlock_t *lock);
uint32_t kspin_lock_irqsave(kspinlock_t *lock);
bool kspin_try_lock_irqsave(kspinlock_t *lock, uint32_t *flags_out);
void kspin_unlock_irqrestore(kspinlock_t *lock, uint32_t flags);
bool kspin_is_held(const kspinlock_t *lock);

void kmutex_init(kmutex_t *lock);
void kmutex_lock(kmutex_t *lock);
bool kmutex_try_lock(kmutex_t *lock);
void kmutex_unlock(kmutex_t *lock);
uint16_t kmutex_depth_owned(const kmutex_t *lock, uint32_t owner);
uint16_t kmutex_drop_owner(kmutex_t *lock, uint32_t owner);
void kmutex_restore_depth(kmutex_t *lock, uint16_t depth);
void kmutex_abandon_owner(kmutex_t *lock, uint32_t owner);

void krwlock_init(krwlock_t *lock);
void krwlock_read_lock(krwlock_t *lock);
void krwlock_read_unlock(krwlock_t *lock);
void krwlock_write_lock(krwlock_t *lock);
void krwlock_write_unlock(krwlock_t *lock);
void krwlock_abandon_writer(krwlock_t *lock, uint32_t owner);

uint32_t klock_current_owner_token(void);
void klock_get_cpu_stats(uint32_t cpu, klock_cpu_stats_t *out);

#endif
