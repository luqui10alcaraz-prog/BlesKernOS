#include "common.h"

static int run(int argc,char **argv){ if(argc!=3)return command_error("move","uso: move origen destino"); if(bk_file_rename(argv[1],argv[2]))return 0; return command_copy_file(argv[1],argv[2],true); }

BK_COMMAND_MAIN(run)
