#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_show_processes(false); }

BK_COMMAND_MAIN(run)
