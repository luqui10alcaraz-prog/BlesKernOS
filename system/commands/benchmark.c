#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ uint32_t start=bk_sys_ticks(),x=1;for(uint32_t i=0;i<1000000U;i++)x=x*1664525U+1013904223U;kprintf("1M iteraciones: %u ticks (resultado %x)\n",bk_sys_ticks()-start,x);return 0; }

BK_COMMAND_MAIN(run)
