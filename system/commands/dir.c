#include "common.h"

static int run(int argc,char **argv){ return command_list_directory(argc>1?argv[1]:"."); }

BK_COMMAND_MAIN(run)
