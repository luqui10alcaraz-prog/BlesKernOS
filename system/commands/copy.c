#include "common.h"

static int run(int argc,char **argv){ return argc==3?command_copy_file(argv[1],argv[2],false):command_error("copy","@H819B0E0D"); }

BK_COMMAND_MAIN(run)
