# SMP Phase 3: native threads and job pools

Phase 3 exposes the SMP scheduler to native BlesKernOS applications. A process
may now create joinable user threads that share its loaded image, heap, files
and GUI objects while keeping private Ring-3/kernel stacks, register state,
x87 state, affinity and TLS base.

## ABI

The syscall ABI is version 5 and appends these calls after `SYS_WIN16_RELAY`:

- `SYS_THREAD_CREATE(entry, argument, stack_size, name)`
- `SYS_THREAD_JOIN(tid, result)` (non-blocking kernel primitive)
- `SYS_THREAD_DETACH(tid)`
- `SYS_THREAD_EXIT(result)`
- `SYS_THREAD_SELF`
- `SYS_THREAD_TLS_SET(base)`
- `SYS_THREAD_TLS_GET`

`libblesk.a` turns the non-blocking join primitive into the blocking
`bk_thread_join()` interface by sleeping one scheduler tick while the target is
still active. Completed joinable threads are recorded before their task slot is
recycled, so the asynchronous task reaper cannot race a later join.

`bk_getpid()` now returns the shared process ID. `bk_thread_self()` returns the
calling thread ID. Main threads have the same numeric process and thread ID, so
existing single-threaded programs keep their previous behavior.

## SDK

Include `bleskernos.h` (which includes `bleskernos_thread.h`) and link the
normal `libblesk.a`.

```c
static bk_i32 worker(void *argument) {
    int *value = argument;
    *value *= 2;
    return *value;
}

bk_thread_t thread;
int value = 21;
int result;

bk_thread_create(&thread, "worker", worker, &value, 0);
bk_thread_join(thread, &result);
```

Synchronization available in user space:

- atomics and memory barrier;
- adaptive mutex;
- condition variable;
- semaphore;
- reusable barrier;
- read/write lock;
- 32 TLS pointer slots per thread.

Contended waits yield to the kernel scheduler. They do not hold a kernel spin
lock and therefore do not serialize unrelated processes or CPUs.

## Job pool

`bk_job_pool_t` owns persistent workers and a bounded 64-entry queue.
`BK_JOB_AUTO_THREADS` creates `min(cpu_count - 1, 16)` workers, leaving one CPU
available for the desktop and IRQ-heavy CPU0.

```c
bk_job_pool_t pool;

bk_job_pool_init(&pool, BK_JOB_AUTO_THREADS);
for (unsigned tile = 0; tile < tile_count; ++tile)
    bk_job_submit(&pool, render_tile, &tiles[tile]);
bk_job_wait_all(&pool);
bk_job_pool_destroy(&pool);
```

For 3D Plus, freeze the scene into a read-only render snapshot, divide the
image into tiles and let each job write only its own pixel rectangle. Keep GUI,
OpenGL/Mesa submission and final presentation on the main thread until those
libraries and drivers have explicitly thread-safe contexts.

## Resource limits

- `TASK_MAX` remains 32 because it also dimensions compatibility tables.
- Normal systems with at least 32 MiB may use all 32 task slots; low-memory
  profiles retain their smaller limits.
- A pool has at most 16 workers. On a 16-CPU VM the automatic setting creates
  15 workers.
- User thread stacks are clamped to 16-128 KiB, retain the Phase-1 guard
  pages and start with the i386 cdecl 16-byte alignment expected by GCC.

## Current limitations

Threads share the transitional flat process address space. This is intentional
for a pthread-like model, but separate processes still do not yet have separate
page directories. A bad pointer in one thread can corrupt another thread in the
same process. Kernel/user page permissions from Phase 1 still protect static
kernel memory.

The synchronization library uses scheduler-friendly polling rather than kernel
wait queues/futexes. It is suitable for coarse rendering and physics jobs, but
a later phase should add wait queues so thousands of short waits do not cause
excessive yields.

Win16 and existing Wine thread paths keep their historical detached-thread API
and remain pinned or constrained by their compatibility rules.
