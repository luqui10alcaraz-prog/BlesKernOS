#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { bk_console_write("Apagando...\n"); bk_sys_shutdown(); return 0; }

BK_COMMAND_MAIN(run)
