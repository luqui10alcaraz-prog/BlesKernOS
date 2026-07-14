#include "common.h"

static int run(int argc UNUSED, char **argv UNUSED) { uint32_t ms=bk_sys_uptime_ms(); kprintf("%u dias, %u:%u:%u\n",ms/86400000U,(ms/3600000U)%24U,(ms/60000U)%60U,(ms/1000U)%60U); return 0; }

BK_COMMAND_MAIN(run)
