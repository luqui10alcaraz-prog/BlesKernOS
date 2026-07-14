#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ kprintf("CPU sistema: %u%%\n",bk_proc_cpu_usage());return command_show_processes(true); }

BK_COMMAND_MAIN(run)
