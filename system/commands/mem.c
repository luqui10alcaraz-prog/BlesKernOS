#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ system_memory_info_t info;if(!bk_sys_memory_info(&info))return 1;kprintf("Total=%u KB usado=%u KB libre=%u KB\n",(uint32_t)(info.total_bytes/1024U),(uint32_t)(info.used_bytes/1024U),(uint32_t)(info.free_bytes/1024U));return 0; }

BK_COMMAND_MAIN(run)
