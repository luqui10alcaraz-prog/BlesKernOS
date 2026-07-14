#include "include/syscall.h"

/*
 * Early Ring 3 smoke test. It intentionally uses no kernel API symbol: all
 * transitions happen through the public ABI before the filesystem/GUI starts.
 */
void usermode_smoke_entry(void) {
    int32_t memory;

    if (syscall0(SYS_ABI_VERSION) != SYSCALL_ABI_VERSION)
        (void)syscall1(SYS_EXIT, 100);
    if (syscall0(SYS_GETPID) <= 0 || syscall0(SYS_GETPPID) <= 0)
        (void)syscall1(SYS_EXIT, 101);
    memory = syscall1(SYS_ALLOC, 128U);
    if (memory <= 0) (void)syscall1(SYS_EXIT, 102);
    if (syscall1(SYS_FREE, (uint32_t)memory) != 0)
        (void)syscall1(SYS_EXIT, 103);
    if (syscall3(SYS_WRITE, 1U, 0x1000U, 1U) != -BK_EFAULT)
        (void)syscall1(SYS_EXIT, 104);
    (void)syscall1(SYS_EXIT, 0);
    for (;;) (void)syscall0(SYS_YIELD);
}
