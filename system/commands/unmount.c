#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("unmount","el VFS actual aun mantiene un unico volumen activo"); }

BK_COMMAND_MAIN(run)
