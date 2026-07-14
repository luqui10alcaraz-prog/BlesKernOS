#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_strings(argv[1]):command_error("nm","falta el archivo"); }

BK_COMMAND_MAIN(run)
