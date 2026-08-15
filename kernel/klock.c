#include "include/klock.h"
#include "include/task.h"
#include "include/memory.h"

#define X86_EFLAGS_IF (1U << 9)

static klock_cpu_stats_t g_lock_stats[SMP_MAX_CPUS];

static uint32_t irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli"
                      : "=r"(flags) : : "memory", "cc");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & X86_EFLAGS_IF) __asm__ volatile ("sti" : : : "memory");
    else __asm__ volatile ("cli" : : : "memory");
}

static uint32_t atomic_xchg32(volatile uint32_t *address, uint32_t value) {
    __asm__ volatile ("xchgl %0, %1"
                      : "+r"(value), "+m"(*address)
                      : : "memory", "cc");
    return value;
}

static bool atomic_cas32(volatile uint32_t *address,
                         uint32_t expected, uint32_t desired) {
    uint32_t observed;
    __asm__ volatile ("lock cmpxchgl %3, %1"
                      : "=a"(observed), "+m"(*address)
                      : "0"(expected), "r"(desired)
                      : "memory", "cc");
    return observed == expected;
}

static bool atomic_cas_i32(volatile int32_t *address,
                           int32_t expected, int32_t desired) {
    int32_t observed;
    __asm__ volatile ("lock cmpxchgl %3, %1"
                      : "=a"(observed), "+m"(*address)
                      : "0"(expected), "r"(desired)
                      : "memory", "cc");
    return observed == expected;
}

static void atomic_inc32(volatile uint32_t *value) {
    __asm__ volatile ("lock incl %0" : "+m"(*value) : : "memory", "cc");
}

static void atomic_dec32(volatile uint32_t *value) {
    __asm__ volatile ("lock decl %0" : "+m"(*value) : : "memory", "cc");
}

static uint32_t lock_cpu(void) {
    uint32_t cpu = smp_cpu_index();
    return cpu < SMP_MAX_CPUS ? cpu : 0U;
}

static klock_cpu_stats_t *lock_stats(void) {
    return &g_lock_stats[lock_cpu()];
}

uint32_t klock_current_owner_token(void) {
    uint32_t pid = task_current_pid();
    if (pid) return pid;
    /* During the very early boot no task PID exists yet. Keep CPU tokens in a
     * disjoint range so they cannot alias a later process. */
    return 0x80000000U | (lock_cpu() + 1U);
}

void kspin_init(kspinlock_t *lock) {
    if (lock) kmemset(lock, 0, sizeof(*lock));
}

uint32_t kspin_lock_irqsave(kspinlock_t *lock) {
    uint32_t flags;
    uint32_t owner;
    bool contended = false;

    if (!lock) return irq_save_disable();
    flags = irq_save_disable();
    owner = lock_cpu() + 1U;
    if (lock->owner_cpu == owner && lock->depth) {
        if (lock->depth != 0xFFFFU) lock->depth++;
        lock_stats()->spin_acquires++;
        return flags;
    }
    while (atomic_xchg32(&lock->state, 1U) != 0U) {
        contended = true;
        __asm__ volatile ("pause");
    }
    lock->owner_cpu = owner;
    lock->depth = 1U;
    __asm__ volatile ("" : : : "memory");
    lock_stats()->spin_acquires++;
    if (contended) {
        atomic_inc32(&lock->contentions);
        lock_stats()->spin_contentions++;
    }
    return flags;
}

bool kspin_try_lock_irqsave(kspinlock_t *lock, uint32_t *flags_out) {
    uint32_t flags = irq_save_disable();
    uint32_t owner = lock_cpu() + 1U;
    bool ok = false;

    if (!lock) ok = true;
    else if (lock->owner_cpu == owner && lock->depth) {
        if (lock->depth != 0xFFFFU) lock->depth++;
        ok = true;
    } else if (atomic_cas32(&lock->state, 0U, 1U)) {
        lock->owner_cpu = owner;
        lock->depth = 1U;
        __asm__ volatile ("" : : : "memory");
        ok = true;
    }
    if (ok) lock_stats()->spin_acquires++;
    else irq_restore(flags);
    if (flags_out) *flags_out = flags;
    return ok;
}

void kspin_unlock_irqrestore(kspinlock_t *lock, uint32_t flags) {
    uint32_t owner = lock_cpu() + 1U;
    if (!lock || lock->owner_cpu != owner || !lock->depth) {
        irq_restore(flags);
        return;
    }
    if (--lock->depth == 0U) {
        __asm__ volatile ("" : : : "memory");
        lock->owner_cpu = 0U;
        lock->state = 0U;
    }
    irq_restore(flags);
}

bool kspin_is_held(const kspinlock_t *lock) {
    return lock && lock->owner_cpu == lock_cpu() + 1U && lock->depth != 0U;
}

void kmutex_init(kmutex_t *lock) {
    if (lock) kmemset(lock, 0, sizeof(*lock));
}

void kmutex_lock(kmutex_t *lock) {
    uint32_t owner;
    uint32_t flags;
    bool can_yield;
    bool contended = false;

    if (!lock) return;
    owner = klock_current_owner_token();
    if (lock->owner == owner && lock->depth) {
        if (lock->depth != 0xFFFFU) lock->depth++;
        lock_stats()->mutex_acquires++;
        return;
    }
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    can_yield = (flags & X86_EFLAGS_IF) != 0U;
    atomic_inc32(&lock->waiters);
    while (!atomic_cas32(&lock->owner, 0U, owner)) {
        contended = true;
        if (can_yield && smp_scheduler_started()) task_yield();
        else __asm__ volatile ("pause");
    }
    atomic_dec32(&lock->waiters);
    lock->depth = 1U;
    __asm__ volatile ("" : : : "memory");
    lock_stats()->mutex_acquires++;
    if (contended) {
        atomic_inc32(&lock->contentions);
        lock_stats()->mutex_contentions++;
    }
}

bool kmutex_try_lock(kmutex_t *lock) {
    uint32_t owner;
    if (!lock) return true;
    owner = klock_current_owner_token();
    if (lock->owner == owner && lock->depth) {
        if (lock->depth != 0xFFFFU) lock->depth++;
        lock_stats()->mutex_acquires++;
        return true;
    }
    if (!atomic_cas32(&lock->owner, 0U, owner)) return false;
    lock->depth = 1U;
    lock_stats()->mutex_acquires++;
    return true;
}

void kmutex_unlock(kmutex_t *lock) {
    uint32_t owner = klock_current_owner_token();
    if (!lock || lock->owner != owner || !lock->depth) return;
    if (--lock->depth != 0U) return;
    __asm__ volatile ("" : : : "memory");
    lock->owner = 0U;
}

uint16_t kmutex_depth_owned(const kmutex_t *lock, uint32_t owner) {
    return lock && lock->owner == owner ? lock->depth : 0U;
}

uint16_t kmutex_drop_owner(kmutex_t *lock, uint32_t owner) {
    uint16_t depth;
    if (!lock || !owner || lock->owner != owner || !lock->depth) return 0U;
    depth = lock->depth;
    lock->depth = 0U;
    __asm__ volatile ("" : : : "memory");
    lock->owner = 0U;
    return depth;
}

void kmutex_restore_depth(kmutex_t *lock, uint16_t depth) {
    if (!lock || !depth) return;
    kmutex_lock(lock);
    lock->depth = depth;
}

void kmutex_abandon_owner(kmutex_t *lock, uint32_t owner) {
    (void)kmutex_drop_owner(lock, owner);
}

void krwlock_init(krwlock_t *lock) {
    if (lock) kmemset(lock, 0, sizeof(*lock));
}

void krwlock_read_lock(krwlock_t *lock) {
    uint32_t flags;
    bool can_yield;
    bool contended = false;
    if (!lock) return;
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    can_yield = (flags & X86_EFLAGS_IF) != 0U;
    for (;;) {
        int32_t state = lock->state;
        if (state >= 0 && lock->waiting_writers == 0U &&
            atomic_cas_i32(&lock->state, state, state + 1)) break;
        contended = true;
        if (can_yield && smp_scheduler_started()) task_yield();
        else __asm__ volatile ("pause");
    }
    lock_stats()->rw_read_acquires++;
    if (contended) {
        atomic_inc32(&lock->contentions);
        lock_stats()->rw_contentions++;
    }
}

void krwlock_read_unlock(krwlock_t *lock) {
    if (!lock || lock->state <= 0) return;
    __asm__ volatile ("lock decl %0" : "+m"(lock->state) : : "memory", "cc");
}

void krwlock_write_lock(krwlock_t *lock) {
    uint32_t owner;
    uint32_t flags;
    bool can_yield;
    bool contended = false;
    if (!lock) return;
    owner = klock_current_owner_token();
    if (lock->writer == owner && lock->writer_depth) {
        if (lock->writer_depth != 0xFFFFU) lock->writer_depth++;
        lock_stats()->rw_write_acquires++;
        return;
    }
    __asm__ volatile ("pushfl; popl %0" : "=r"(flags));
    can_yield = (flags & X86_EFLAGS_IF) != 0U;
    atomic_inc32(&lock->waiting_writers);
    while (!atomic_cas_i32(&lock->state, 0, -1)) {
        contended = true;
        if (can_yield && smp_scheduler_started()) task_yield();
        else __asm__ volatile ("pause");
    }
    atomic_dec32(&lock->waiting_writers);
    lock->writer = owner;
    lock->writer_depth = 1U;
    __asm__ volatile ("" : : : "memory");
    lock_stats()->rw_write_acquires++;
    if (contended) {
        atomic_inc32(&lock->contentions);
        lock_stats()->rw_contentions++;
    }
}

void krwlock_write_unlock(krwlock_t *lock) {
    uint32_t owner = klock_current_owner_token();
    if (!lock || lock->writer != owner || !lock->writer_depth) return;
    if (--lock->writer_depth != 0U) return;
    __asm__ volatile ("" : : : "memory");
    lock->writer = 0U;
    lock->state = 0;
}

void krwlock_abandon_writer(krwlock_t *lock, uint32_t owner) {
    if (!lock || !owner || lock->writer != owner) return;
    lock->writer_depth = 0U;
    lock->writer = 0U;
    lock->state = 0;
}

void klock_get_cpu_stats(uint32_t cpu, klock_cpu_stats_t *out) {
    if (!out) return;
    if (cpu >= SMP_MAX_CPUS) {
        kmemset(out, 0, sizeof(*out));
        return;
    }
    *out = g_lock_stats[cpu];
}
