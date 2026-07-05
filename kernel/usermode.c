#include "include/syscall.h"

/*
 * Minimal Ring 3 smoke test.  Reaching SYS_EXIT proves that the user selectors,
 * TSS kernel stack and DPL=3 syscall gate all work together.
 */
void usermode_smoke_entry(void) {
    (void)syscall0(SYS_ABI_VERSION);
    (void)syscall0(SYS_GETPID);
    (void)syscall1(SYS_SLEEP, 200);
    (void)syscall1(SYS_EXIT, 0);
    for (;;) (void)syscall0(SYS_YIELD);
}
