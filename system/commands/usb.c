#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_show_pci(true); }

BK_COMMAND_MAIN(run)
