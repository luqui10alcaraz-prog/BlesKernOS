#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { kprintf("BlesKernOS 0.6 (C) Bles.INC 2026\n"); return 0; }

BK_COMMAND_MAIN(run)
