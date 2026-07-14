#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_remove(argv[1])?0:command_error("delete","uso: delete archivo"); }

BK_COMMAND_MAIN(run)
