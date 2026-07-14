#include "common.h"

static int run(int argc,char **argv){ if(argc<2)return command_error("find","uso: find patron [ruta]"); command_find(argc>2?argv[2]:".",argv[1]); return 0; }

BK_COMMAND_MAIN(run)
