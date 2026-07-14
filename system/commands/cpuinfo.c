#include "common.h"

static int run(int argc UNUSED,char **argv UNUSED){ uint32_t a,b,c,d;char vendor[13];__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0));*(uint32_t*)&vendor[0]=b;*(uint32_t*)&vendor[4]=d;*(uint32_t*)&vendor[8]=c;vendor[12]=0;kprintf("CPU i386 vendor=%s max_leaf=%u\n",vendor,a);return 0; }

BK_COMMAND_MAIN(run)
