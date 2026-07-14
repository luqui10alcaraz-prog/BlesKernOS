#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_hexdump(argv[1]):command_error("objdump","falta el archivo"); }

BK_COMMAND_MAIN(run)
