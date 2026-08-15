#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_mkdir(argv[1])?0:command_error("mkdir","@H4EDD3991"); }

BK_COMMAND_MAIN(run)
