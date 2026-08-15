#include "../include/bleskernos.h"

#define TEST_JOBS 32U

typedef struct {
    bk_u32 input;
    bk_u32 output;
} test_job_t;

static void square_job(void *argument) {
    test_job_t *job = (test_job_t *)argument;
    job->output = job->input * job->input;
}

/* Call from a native application's entrypoint. Returns zero on success. */
bk_i32 bleskernos_thread_pool_test(void) {
    bk_job_pool_t pool;
    test_job_t jobs[TEST_JOBS];

    if (bk_abi_version() < (bk_i32)BK_SYSCALL_ABI_VERSION) return -1;
    if (bk_job_pool_init(&pool, BK_JOB_AUTO_THREADS) < 0) return -2;
    for (bk_u32 i = 0U; i < TEST_JOBS; i++) {
        jobs[i].input = i;
        jobs[i].output = 0U;
        if (bk_job_submit(&pool, square_job, &jobs[i]) < 0) {
            bk_job_pool_destroy(&pool);
            return -3;
        }
    }
    bk_job_wait_all(&pool);
    for (bk_u32 i = 0U; i < TEST_JOBS; i++) {
        if (jobs[i].output != i * i) {
            bk_job_pool_destroy(&pool);
            return -4;
        }
    }
    bk_job_pool_destroy(&pool);
    return 0;
}
