#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("link","requiere portar el toolchain al espacio de usuario"); }

BK_COMMAND_MAIN(run)
