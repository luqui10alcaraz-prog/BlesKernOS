#include "common.h"

static int run(int argc,char **argv){ bool ok=false;uint32_t pid=argc>1?command_number(argv[1],&ok):0;return ok&&bk_proc_request_exit(pid)?0:command_error("taskkill","PID invalido o protegido"); }

BK_COMMAND_MAIN(run)
