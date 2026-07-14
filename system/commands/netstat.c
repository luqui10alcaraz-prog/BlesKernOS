#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("netstat","la pila de red aun no existe"); }

BK_COMMAND_MAIN(run)
