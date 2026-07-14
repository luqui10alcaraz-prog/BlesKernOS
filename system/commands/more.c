#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_print_file(argv[1],true):command_error("more","falta el archivo"); }

BK_COMMAND_MAIN(run)
