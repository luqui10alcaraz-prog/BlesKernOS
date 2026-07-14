#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_write_all(argv[1],"",0)?0:command_error("touch","uso: touch archivo"); }

BK_COMMAND_MAIN(run)
