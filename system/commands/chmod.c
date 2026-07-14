#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ return command_error("chmod","FAT no ofrece permisos POSIX"); }

BK_COMMAND_MAIN(run)
