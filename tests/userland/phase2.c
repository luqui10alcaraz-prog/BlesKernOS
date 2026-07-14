#include "bleskernos.h"

void bleskernos_program_main(void *unused) {
    char cwd[BK_PATH_MAX];
    void *memory;
    (void)unused;

    if (bk_abi_version() != (bk_i32)BK_SYSCALL_ABI_VERSION)
        bk_exit(1);
    bk_puts("BlesKernOS Phase II ABI: OK");
    if (bk_getcwd(cwd, sizeof(cwd)) >= 0) {
        bk_write(1, "cwd: ", 5);
        bk_puts(cwd);
    }
    memory = bk_malloc(4096);
    if (!memory || (bk_i32)memory < 0) bk_exit(2);
    if (bk_free(memory) < 0) bk_exit(3);
    bk_exit(0);
}
