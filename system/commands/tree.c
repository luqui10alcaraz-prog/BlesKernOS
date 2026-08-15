#include "common.h"

static int run(int argc,char **argv){ command_tree(argc>1?argv[1]:"."); return 0; }

BK_COMMAND_MAIN(run)
