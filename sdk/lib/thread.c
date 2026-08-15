#include "bleskernos.h"

/* Public API symbol resolved by the ELF loader. Keeping CPU discovery outside
 * the syscall ABI lets the job library run on both old and new kernels. */
extern bk_u32 bk_proc_cpu_count(void);

#define BK_TLS_MAGIC      0x334C5442U /* "BTL3" */
#define BK_MUTEX_SPINS    64U

typedef struct {
    bk_u32 magic;
    void *slots[BK_TLS_SLOTS];
} bk_tls_block_t;

typedef struct {
    bk_thread_fn_t function;
    void *argument;
} bk_thread_start_t;

static void bytes_zero(void *pointer, bk_u32 size) {
    bk_u8 *bytes = (bk_u8 *)pointer;
    while (size--) *bytes++ = 0U;
}

static void cpu_relax(void) {
    __asm__ volatile ("pause" : : : "memory");
}

void bk_memory_barrier(void) {
    __asm__ volatile ("lock; addl $0, 0(%%esp)" : : : "memory", "cc");
}

void bk_atomic_init(bk_atomic32_t *atomic, bk_u32 value) {
    if (atomic) atomic->value = value;
}

bk_u32 bk_atomic_load(const bk_atomic32_t *atomic) {
    bk_u32 value = atomic ? atomic->value : 0U;
    __asm__ volatile ("" : "+r"(value) : : "memory");
    return value;
}

void bk_atomic_store(bk_atomic32_t *atomic, bk_u32 value) {
    if (!atomic) return;
    __asm__ volatile ("" : : : "memory");
    atomic->value = value;
    __asm__ volatile ("" : : : "memory");
}

bk_u32 bk_atomic_fetch_add(bk_atomic32_t *atomic, bk_i32 value) {
    bk_u32 old = (bk_u32)value;
    if (!atomic) return 0U;
    __asm__ volatile ("lock; xaddl %0, %1"
                      : "+r"(old), "+m"(atomic->value)
                      : : "memory", "cc");
    return old;
}

bk_i32 bk_atomic_compare_exchange(bk_atomic32_t *atomic,
                                  bk_u32 *expected, bk_u32 desired) {
    bk_u32 observed;
    bk_u32 wanted;
    if (!atomic || !expected) return 0;
    wanted = *expected;
    __asm__ volatile ("lock; cmpxchgl %2, %1"
                      : "=a"(observed), "+m"(atomic->value)
                      : "r"(desired), "0"(wanted)
                      : "memory", "cc");
    if (observed == wanted) return 1;
    *expected = observed;
    return 0;
}

static bk_tls_block_t *tls_ensure(void) {
    bk_tls_block_t *block = (bk_tls_block_t *)bk_thread_sys_tls_get();
    if (block && block->magic == BK_TLS_MAGIC) return block;
    block = (bk_tls_block_t *)bk_malloc(4096U);
    if (!block) return (bk_tls_block_t *)0;
    bytes_zero(block, 4096U);
    block->magic = BK_TLS_MAGIC;
    if (bk_thread_sys_tls_set(block) < 0) {
        (void)bk_free(block);
        return (bk_tls_block_t *)0;
    }
    return block;
}

void *bk_tls_base(void) {
    return tls_ensure();
}

bk_i32 bk_tls_set(bk_u32 slot, void *value) {
    bk_tls_block_t *block;
    if (slot >= BK_TLS_SLOTS) return BK_THREAD_EINVAL;
    block = tls_ensure();
    if (!block) return BK_THREAD_ENOMEM;
    block->slots[slot] = value;
    bk_memory_barrier();
    return 0;
}

void *bk_tls_get(bk_u32 slot) {
    bk_tls_block_t *block;
    if (slot >= BK_TLS_SLOTS) return (void *)0;
    block = tls_ensure();
    return block ? block->slots[slot] : (void *)0;
}

static void thread_tls_release(void) {
    void *base = bk_thread_sys_tls_get();
    if (!base) return;
    (void)bk_thread_sys_tls_set((void *)0);
    (void)bk_free(base);
}

static void thread_start_trampoline(void *opaque) {
    bk_thread_start_t *start = (bk_thread_start_t *)opaque;
    bk_thread_fn_t function;
    void *argument;
    bk_i32 result;

    (void)tls_ensure();
    if (!start) bk_thread_exit(BK_THREAD_EINVAL);
    function = start->function;
    argument = start->argument;
    (void)bk_free(start);
    result = function ? function(argument) : BK_THREAD_EINVAL;
    bk_thread_exit(result);
}

bk_i32 bk_thread_create(bk_thread_t *thread, const char *name,
                        bk_thread_fn_t function, void *argument,
                        bk_u32 stack_size) {
    bk_thread_start_t *start;
    bk_i32 tid;
    if (!thread || !function) return BK_THREAD_EINVAL;
    if (!stack_size) stack_size = BK_THREAD_DEFAULT_STACK;
    if (stack_size < BK_THREAD_MIN_STACK) stack_size = BK_THREAD_MIN_STACK;
    if (stack_size > BK_THREAD_MAX_STACK) stack_size = BK_THREAD_MAX_STACK;
    start = (bk_thread_start_t *)bk_malloc(sizeof(*start));
    if (!start) return BK_THREAD_ENOMEM;
    start->function = function;
    start->argument = argument;
    tid = bk_thread_sys_create(thread_start_trampoline, start, stack_size,
                               name ? name : "app-thread");
    if (tid < 0) {
        (void)bk_free(start);
        return tid;
    }
    *thread = (bk_thread_t)tid;
    return 0;
}

bk_i32 bk_thread_join(bk_thread_t thread, bk_i32 *result) {
    bk_i32 joined;
    if (!thread || thread == bk_thread_self()) return BK_THREAD_EDEADLK;
    for (;;) {
        joined = bk_thread_sys_join(thread, result);
        if (joined > 0) return 0;
        if (joined < 0) return joined;
        bk_sleep(1U);
    }
}

bk_i32 bk_thread_detach(bk_thread_t thread) {
    return thread ? bk_thread_sys_detach(thread) : BK_THREAD_EINVAL;
}

void bk_thread_exit(bk_i32 result) {
    thread_tls_release();
    bk_thread_sys_exit(result);
}

bk_thread_t bk_thread_self(void) {
    return bk_thread_sys_self();
}

void bk_mutex_init(bk_mutex_t *mutex) {
    if (!mutex) return;
    bk_atomic_init(&mutex->state, 0U);
    mutex->owner = 0U;
}

bk_i32 bk_mutex_try_lock(bk_mutex_t *mutex) {
    bk_u32 expected = 0U;
    bk_u32 self;
    if (!mutex) return BK_THREAD_EINVAL;
    self = bk_thread_self();
    if (mutex->owner == self && bk_atomic_load(&mutex->state))
        return BK_THREAD_EDEADLK;
    if (!bk_atomic_compare_exchange(&mutex->state, &expected, 1U))
        return BK_THREAD_EBUSY;
    mutex->owner = self;
    bk_memory_barrier();
    return 0;
}

bk_i32 bk_mutex_lock(bk_mutex_t *mutex) {
    bk_i32 result;
    if (!mutex) return BK_THREAD_EINVAL;
    for (;;) {
        result = bk_mutex_try_lock(mutex);
        if (result == 0 || result == BK_THREAD_EDEADLK) return result;
        for (bk_u32 spin = 0U; spin < BK_MUTEX_SPINS; spin++) {
            if (!bk_atomic_load(&mutex->state)) break;
            cpu_relax();
        }
        if (bk_atomic_load(&mutex->state)) bk_yield();
    }
}

bk_i32 bk_mutex_unlock(bk_mutex_t *mutex) {
    if (!mutex || !bk_atomic_load(&mutex->state)) return BK_THREAD_EINVAL;
    if (mutex->owner != bk_thread_self()) return BK_THREAD_EINVAL;
    mutex->owner = 0U;
    bk_memory_barrier();
    bk_atomic_store(&mutex->state, 0U);
    return 0;
}

void bk_cond_init(bk_cond_t *condition) {
    if (condition) bk_atomic_init(&condition->sequence, 0U);
}

void bk_cond_wait(bk_cond_t *condition, bk_mutex_t *mutex) {
    bk_u32 sequence;
    if (!condition || !mutex) return;
    sequence = bk_atomic_load(&condition->sequence);
    if (bk_mutex_unlock(mutex) < 0) return;
    /* A persistent job pool can keep 15 workers idle for minutes. Yielding in
     * a tight loop leaves every worker runnable and turns an otherwise idle
     * application into a scheduler benchmark. Spin very briefly for a signal
     * that is already in flight, then sleep for one millisecond. */
    while (bk_atomic_load(&condition->sequence) == sequence) {
        for (bk_u32 spin = 0U; spin < BK_MUTEX_SPINS; spin++) {
            if (bk_atomic_load(&condition->sequence) != sequence) break;
            cpu_relax();
        }
        if (bk_atomic_load(&condition->sequence) == sequence)
            bk_sleep(1U);
    }
    (void)bk_mutex_lock(mutex);
}

void bk_cond_signal(bk_cond_t *condition) {
    if (condition) (void)bk_atomic_fetch_add(&condition->sequence, 1);
}

void bk_cond_broadcast(bk_cond_t *condition) {
    bk_cond_signal(condition);
}

void bk_semaphore_init(bk_semaphore_t *semaphore, bk_u32 value) {
    if (semaphore) bk_atomic_init(&semaphore->value, value);
}

bk_i32 bk_semaphore_try_wait(bk_semaphore_t *semaphore) {
    bk_u32 current;
    if (!semaphore) return BK_THREAD_EINVAL;
    current = bk_atomic_load(&semaphore->value);
    while (current) {
        bk_u32 expected = current;
        if (bk_atomic_compare_exchange(&semaphore->value, &expected,
                                       current - 1U)) return 0;
        current = expected;
    }
    return BK_THREAD_EBUSY;
}

void bk_semaphore_wait(bk_semaphore_t *semaphore) {
    while (bk_semaphore_try_wait(semaphore) != 0) bk_yield();
}

void bk_semaphore_post(bk_semaphore_t *semaphore) {
    if (semaphore) (void)bk_atomic_fetch_add(&semaphore->value, 1);
}

void bk_barrier_init(bk_barrier_t *barrier, bk_u32 participants) {
    if (!barrier) return;
    bk_atomic_init(&barrier->arrived, 0U);
    bk_atomic_init(&barrier->generation, 0U);
    barrier->participants = participants ? participants : 1U;
}

void bk_barrier_wait(bk_barrier_t *barrier) {
    bk_u32 generation;
    bk_u32 arrived;
    if (!barrier || barrier->participants <= 1U) return;
    generation = bk_atomic_load(&barrier->generation);
    arrived = bk_atomic_fetch_add(&barrier->arrived, 1) + 1U;
    if (arrived == barrier->participants) {
        bk_atomic_store(&barrier->arrived, 0U);
        (void)bk_atomic_fetch_add(&barrier->generation, 1);
        return;
    }
    while (bk_atomic_load(&barrier->generation) == generation) bk_yield();
}

void bk_rwlock_init(bk_rwlock_t *lock) {
    if (!lock) return;
    bk_atomic_init(&lock->readers, 0U);
    bk_atomic_init(&lock->writer, 0U);
}

void bk_rwlock_read_lock(bk_rwlock_t *lock) {
    if (!lock) return;
    for (;;) {
        while (bk_atomic_load(&lock->writer)) bk_yield();
        (void)bk_atomic_fetch_add(&lock->readers, 1);
        if (!bk_atomic_load(&lock->writer)) return;
        (void)bk_atomic_fetch_add(&lock->readers, -1);
    }
}

void bk_rwlock_read_unlock(bk_rwlock_t *lock) {
    if (lock) (void)bk_atomic_fetch_add(&lock->readers, -1);
}

void bk_rwlock_write_lock(bk_rwlock_t *lock) {
    bk_u32 expected;
    if (!lock) return;
    for (;;) {
        expected = 0U;
        if (bk_atomic_compare_exchange(&lock->writer, &expected, 1U)) break;
        bk_yield();
    }
    while (bk_atomic_load(&lock->readers)) bk_yield();
}

void bk_rwlock_write_unlock(bk_rwlock_t *lock) {
    if (lock) bk_atomic_store(&lock->writer, 0U);
}

static bk_i32 job_worker(void *argument) {
    bk_job_pool_t *pool = (bk_job_pool_t *)argument;
    for (;;) {
        bk_job_t job;
        (void)bk_mutex_lock(&pool->lock);
        while (!pool->queued && !pool->stopping)
            bk_cond_wait(&pool->work_available, &pool->lock);
        if (pool->stopping && !pool->queued) {
            (void)bk_mutex_unlock(&pool->lock);
            return 0;
        }
        job = pool->queue[pool->head];
        pool->head = (pool->head + 1U) % BK_JOB_QUEUE_CAPACITY;
        pool->queued--;
        pool->active++;
        bk_cond_broadcast(&pool->became_idle);
        (void)bk_mutex_unlock(&pool->lock);

        if (job.function) job.function(job.argument);

        (void)bk_mutex_lock(&pool->lock);
        if (pool->active) pool->active--;
        if (!pool->queued && !pool->active)
            bk_cond_broadcast(&pool->became_idle);
        (void)bk_mutex_unlock(&pool->lock);
    }
}

bk_i32 bk_job_pool_init(bk_job_pool_t *pool, bk_u32 worker_count) {
    bk_u32 created = 0U;
    bk_i32 error = 0;
    if (!pool) return BK_THREAD_EINVAL;
    bytes_zero(pool, sizeof(*pool));
    bk_mutex_init(&pool->lock);
    bk_cond_init(&pool->work_available);
    bk_cond_init(&pool->became_idle);
    if (worker_count == BK_JOB_AUTO_THREADS) {
        bk_u32 cpus = bk_proc_cpu_count();
        worker_count = cpus > 1U ? cpus - 1U : 1U;
    }
    if (!worker_count) worker_count = 1U;
    if (worker_count > BK_JOB_MAX_WORKERS)
        worker_count = BK_JOB_MAX_WORKERS;
    pool->worker_count = worker_count;
    pool->initialized = 1U;

    for (created = 0U; created < worker_count; created++) {
        error = bk_thread_create(&pool->workers[created], "job-worker",
                                 job_worker, pool, BK_THREAD_DEFAULT_STACK);
        if (error < 0) break;
    }
    if (created == worker_count) return 0;

    (void)bk_mutex_lock(&pool->lock);
    pool->stopping = 1U;
    bk_cond_broadcast(&pool->work_available);
    (void)bk_mutex_unlock(&pool->lock);
    for (bk_u32 i = 0U; i < created; i++)
        (void)bk_thread_join(pool->workers[i], (bk_i32 *)0);
    pool->worker_count = 0U;
    pool->initialized = 0U;
    return error < 0 ? error : BK_THREAD_ENOMEM;
}

bk_i32 bk_job_submit(bk_job_pool_t *pool, bk_job_fn_t function,
                     void *argument) {
    if (!pool || !pool->initialized || !function) return BK_THREAD_EINVAL;
    (void)bk_mutex_lock(&pool->lock);
    while (pool->queued >= BK_JOB_QUEUE_CAPACITY && !pool->stopping)
        bk_cond_wait(&pool->became_idle, &pool->lock);
    if (pool->stopping) {
        (void)bk_mutex_unlock(&pool->lock);
        return BK_THREAD_EBUSY;
    }
    pool->queue[pool->tail].function = function;
    pool->queue[pool->tail].argument = argument;
    pool->tail = (pool->tail + 1U) % BK_JOB_QUEUE_CAPACITY;
    pool->queued++;
    bk_cond_signal(&pool->work_available);
    (void)bk_mutex_unlock(&pool->lock);
    return 0;
}

void bk_job_wait_all(bk_job_pool_t *pool) {
    if (!pool || !pool->initialized) return;
    (void)bk_mutex_lock(&pool->lock);
    while (pool->queued || pool->active)
        bk_cond_wait(&pool->became_idle, &pool->lock);
    (void)bk_mutex_unlock(&pool->lock);
}

void bk_job_pool_destroy(bk_job_pool_t *pool) {
    if (!pool || !pool->initialized) return;
    bk_job_wait_all(pool);
    (void)bk_mutex_lock(&pool->lock);
    pool->stopping = 1U;
    bk_cond_broadcast(&pool->work_available);
    (void)bk_mutex_unlock(&pool->lock);
    for (bk_u32 i = 0U; i < pool->worker_count; i++)
        (void)bk_thread_join(pool->workers[i], (bk_i32 *)0);
    pool->worker_count = 0U;
    pool->initialized = 0U;
}
