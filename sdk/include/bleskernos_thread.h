#ifndef BLESKERNOS_THREAD_API_H
#define BLESKERNOS_THREAD_API_H

#ifndef BLESKERNOS_USER_API_H
#include "bleskernos.h"
#endif

#define BK_THREAD_DEFAULT_STACK (64U * 1024U)
#define BK_THREAD_MIN_STACK     (16U * 1024U)
#define BK_THREAD_MAX_STACK     (128U * 1024U)
#define BK_TLS_SLOTS            32U

/* Negative return values used by the thread/synchronization API. */
#define BK_THREAD_ECHILD  (-10)
#define BK_THREAD_ENOMEM  (-12)
#define BK_THREAD_EACCES  (-13)
#define BK_THREAD_EBUSY   (-16)
#define BK_THREAD_EINVAL  (-22)
#define BK_THREAD_EDEADLK (-35)

#define BK_JOB_AUTO_THREADS     0U
#define BK_JOB_MAX_WORKERS      16U
#define BK_JOB_QUEUE_CAPACITY   64U

typedef bk_u32 bk_thread_t;
typedef bk_i32 (*bk_thread_fn_t)(void *argument);
typedef void (*bk_job_fn_t)(void *argument);

typedef struct {
    volatile bk_u32 value;
} bk_atomic32_t;

typedef struct {
    bk_atomic32_t state;
    volatile bk_u32 owner;
} bk_mutex_t;

typedef struct {
    bk_atomic32_t sequence;
} bk_cond_t;

typedef struct {
    bk_atomic32_t value;
} bk_semaphore_t;

typedef struct {
    bk_atomic32_t arrived;
    bk_atomic32_t generation;
    bk_u32 participants;
} bk_barrier_t;

typedef struct {
    bk_atomic32_t readers;
    bk_atomic32_t writer;
} bk_rwlock_t;

typedef struct {
    bk_job_fn_t function;
    void *argument;
} bk_job_t;

typedef struct {
    bk_mutex_t lock;
    bk_cond_t work_available;
    bk_cond_t became_idle;
    bk_job_t queue[BK_JOB_QUEUE_CAPACITY];
    bk_thread_t workers[BK_JOB_MAX_WORKERS];
    bk_u32 head;
    bk_u32 tail;
    bk_u32 queued;
    bk_u32 active;
    bk_u32 worker_count;
    volatile bk_u32 stopping;
    volatile bk_u32 initialized;
} bk_job_pool_t;

/* Native threads. TIDs are task IDs; all threads created here share the
 * caller's process resources and address space but own independent stacks,
 * x87 state, affinity and TLS base. */
bk_i32 bk_thread_create(bk_thread_t *thread, const char *name,
                        bk_thread_fn_t function, void *argument,
                        bk_u32 stack_size);
bk_i32 bk_thread_join(bk_thread_t thread, bk_i32 *result);
bk_i32 bk_thread_detach(bk_thread_t thread);
void bk_thread_exit(bk_i32 result) __attribute__((noreturn));
bk_thread_t bk_thread_self(void);

/* Basic per-thread storage. Each native thread gets 32 pointer-sized slots.
 * The block is backed by the task's per-CPU FS descriptor but these helpers
 * avoid requiring compiler-specific TLS syntax in applications. */
bk_i32 bk_tls_set(bk_u32 slot, void *value);
void *bk_tls_get(bk_u32 slot);
void *bk_tls_base(void);

/* x86 atomic primitives with compiler and CPU ordering. */
void bk_atomic_init(bk_atomic32_t *atomic, bk_u32 value);
bk_u32 bk_atomic_load(const bk_atomic32_t *atomic);
void bk_atomic_store(bk_atomic32_t *atomic, bk_u32 value);
bk_u32 bk_atomic_fetch_add(bk_atomic32_t *atomic, bk_i32 value);
bk_i32 bk_atomic_compare_exchange(bk_atomic32_t *atomic,
                                  bk_u32 *expected, bk_u32 desired);
void bk_memory_barrier(void);

/* Adaptive user-space synchronization. Contended waits yield to the SMP
 * scheduler instead of spinning indefinitely. */
void bk_mutex_init(bk_mutex_t *mutex);
bk_i32 bk_mutex_try_lock(bk_mutex_t *mutex);
bk_i32 bk_mutex_lock(bk_mutex_t *mutex);
bk_i32 bk_mutex_unlock(bk_mutex_t *mutex);
void bk_cond_init(bk_cond_t *condition);
void bk_cond_wait(bk_cond_t *condition, bk_mutex_t *mutex);
void bk_cond_signal(bk_cond_t *condition);
void bk_cond_broadcast(bk_cond_t *condition);
void bk_semaphore_init(bk_semaphore_t *semaphore, bk_u32 value);
void bk_semaphore_wait(bk_semaphore_t *semaphore);
bk_i32 bk_semaphore_try_wait(bk_semaphore_t *semaphore);
void bk_semaphore_post(bk_semaphore_t *semaphore);
void bk_barrier_init(bk_barrier_t *barrier, bk_u32 participants);
void bk_barrier_wait(bk_barrier_t *barrier);
void bk_rwlock_init(bk_rwlock_t *lock);
void bk_rwlock_read_lock(bk_rwlock_t *lock);
void bk_rwlock_read_unlock(bk_rwlock_t *lock);
void bk_rwlock_write_lock(bk_rwlock_t *lock);
void bk_rwlock_write_unlock(bk_rwlock_t *lock);

/* Reusable work queue. BK_JOB_AUTO_THREADS creates min(cpu_count - 1, 16)
 * workers, preserving one CPU for the desktop and IRQ-heavy CPU0. */
bk_i32 bk_job_pool_init(bk_job_pool_t *pool, bk_u32 worker_count);
bk_i32 bk_job_submit(bk_job_pool_t *pool, bk_job_fn_t function,
                     void *argument);
void bk_job_wait_all(bk_job_pool_t *pool);
void bk_job_pool_destroy(bk_job_pool_t *pool);

#endif
