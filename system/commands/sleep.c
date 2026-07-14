#include "common.h"

static int run(int argc, char **argv) { bool ok=true; uint32_t seconds=argc>1?command_number(argv[1],&ok):1U; if(!ok) return command_error("sleep","uso: sleep segundos"); bk_sys_sleep_ms(seconds*1000U); return 0; }

BK_COMMAND_MAIN(run)
