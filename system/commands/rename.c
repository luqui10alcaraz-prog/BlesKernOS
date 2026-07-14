#include "common.h"

static int run(int argc,char **argv){ return argc==3&&bk_file_rename(argv[1],argv[2])?0:command_error("rename","uso: rename origen destino"); }

BK_COMMAND_MAIN(run)
