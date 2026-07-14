#include "common.h"

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static int run(int argc, char **argv) {
    void *raw=NULL;
    uint8_t *output;
    uint32_t size=0, expected, in=8, out=0;
    if(argc!=3)return command_error("extract","uso: extract archivo.bkz destino");
    if(!bk_file_read_all(argv[1],&raw,&size)||size<8U)return command_error("extract","archivo invalido");
    if(((uint8_t*)raw)[0]!='B'||((uint8_t*)raw)[1]!='K'||((uint8_t*)raw)[2]!='Z'||((uint8_t*)raw)[3]!='1'){
        bk_sys_free(raw);return command_error("extract","formato BKZ1 invalido");
    }
    expected=get32((uint8_t*)raw+4);
    output=(uint8_t*)bk_sys_alloc(expected?expected:1U);
    if(!output){bk_sys_free(raw);return command_error("extract","sin memoria");}
    while(in+1U<size&&out<expected){
        uint8_t count=((uint8_t*)raw)[in++],value=((uint8_t*)raw)[in++];
        if(!count||out+count>expected){bk_sys_free(output);bk_sys_free(raw);return command_error("extract","datos BKZ1 corruptos");}
        for(uint32_t n=0;n<count;n++)output[out++]=value;
    }
    if(out!=expected||!bk_file_write_all(argv[2],output,expected)){
        bk_sys_free(output);bk_sys_free(raw);return command_error("extract","archivo truncado o destino no escribible");
    }
    kprintf("%u bytes extraidos en %s\n",expected,argv[2]);
    bk_sys_free(output);bk_sys_free(raw);return 0;
}

BK_COMMAND_MAIN(run)
