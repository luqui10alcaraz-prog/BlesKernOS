#include "common.h"

static int run(int argc,char **argv){ return argc==2&&bk_file_write_all(argv[1],"",0)?0:command_error("touch","@HE2A43F8F"); }

BK_COMMAND_MAIN(run)
