#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { bk_console_write("Reiniciando...\n"); bk_sys_reboot(); return 0; }

BK_COMMAND_MAIN(run)
