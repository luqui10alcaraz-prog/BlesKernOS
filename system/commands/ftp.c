#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("ftp","la pila de red aun no existe"); }

BK_COMMAND_MAIN(run)
