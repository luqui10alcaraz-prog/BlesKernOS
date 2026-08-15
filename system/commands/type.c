#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_print_file(argv[1],false):command_error("type","@H5D8BCF60"); }

BK_COMMAND_MAIN(run)
