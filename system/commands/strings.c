#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_strings(argv[1]):command_error("strings","@H5D8BCF60"); }

BK_COMMAND_MAIN(run)
