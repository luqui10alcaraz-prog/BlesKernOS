#include "common.h"

static int run(int argc,char **argv){ if(argc!=4)return command_error("calc","uso: calc numero operador numero");bool oka,okb;uint32_t a=command_number(argv[1],&oka),b=command_number(argv[3],&okb),r=0;if(!oka||!okb)return command_error("calc","numero invalido");if(command_is(argv[2],"+"))r=a+b;else if(command_is(argv[2],"-"))r=a-b;else if(command_is(argv[2],"*"))r=a*b;else if(command_is(argv[2],"/")&&b)r=a/b;else if(command_is(argv[2],"%")&&b)r=a%b;else return command_error("calc","operador invalido o division por cero");kprintf("%u\n",r);return 0; }

BK_COMMAND_MAIN(run)
