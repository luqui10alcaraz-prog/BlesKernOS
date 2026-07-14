#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_mkdir(argv[1])?0:command_error("mkdir","uso: mkdir directorio"); }

BK_COMMAND_MAIN(run)
