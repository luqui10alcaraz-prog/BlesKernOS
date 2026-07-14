#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { kprintf("BlesKernOS 0.6 i386 API-%u\n", bk_sys_api_version()); return 0; }

BK_COMMAND_MAIN(run)
