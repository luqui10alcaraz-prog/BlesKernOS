#include "common.h"

static int run(int argc,char **argv){ if(argc!=2) return command_error("mount","uso: mount dispositivo"); if(!bk_device_mount_volume(argv[1])) return command_error("mount","no se pudo montar"); kprintf("%s montado.\n",argv[1]); return 0; }

BK_COMMAND_MAIN(run)
