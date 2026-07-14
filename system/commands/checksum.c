#include "common.h"

static int run(int argc,char **argv){ return argc==2?command_checksum(argv[1]):command_error("checksum","falta el archivo"); }

BK_COMMAND_MAIN(run)
