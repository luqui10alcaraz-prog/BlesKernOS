#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_remove(argv[1])?0:command_error("rmdir","uso: rmdir directorio-vacio"); }

BK_COMMAND_MAIN(run)
