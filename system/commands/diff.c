#include "common.h"

static int run(int argc,char **argv){ void *a=NULL,*b=NULL; uint32_t as=0,bs=0; if(argc!=3||!bk_file_read_all(argv[1],&a,&as)||!bk_file_read_all(argv[2],&b,&bs)){if(a)bk_sys_free(a);return command_error("diff","uso: diff archivo1 archivo2");} bool same=as==bs&&bk_runtime_memcmp(a,b,as)==0; bk_sys_free(a);bk_sys_free(b);kprintf("%s\n",same?"Archivos identicos":"Archivos diferentes");return same?0:1; }

BK_COMMAND_MAIN(run)
