#include "common.h"

static int run(int argc,char **argv){
    void *raw=NULL;uint32_t size=0,offset,value;bool ok1=false,ok2=false;
    if(argc==2)return command_hexdump(argv[1]);
    if(argc!=4)return command_error("hexedit","uso: hexedit archivo [offset byte]");
    offset=command_number(argv[2],&ok1);value=command_number(argv[3],&ok2);
    if(!ok1||!ok2||value>255U)return command_error("hexedit","offset o byte invalido; admite decimal y 0xHEX");
    if(!bk_file_read_all(argv[1],&raw,&size)||offset>=size){if(raw)bk_sys_free(raw);return command_error("hexedit","archivo inexistente u offset fuera de rango");}
    ((uint8_t*)raw)[offset]=(uint8_t)value;
    if(!bk_file_write_all(argv[1],raw,size)){bk_sys_free(raw);return command_error("hexedit","no se pudo guardar");}
    bk_sys_free(raw);kprintf("%s[%u] = 0x%x\n",argv[1],offset,value);return 0;
}

BK_COMMAND_MAIN(run)
