#include "common.h"

static void put32(uint8_t *p, uint32_t value) {
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}

static int run(int argc, char **argv) {
    void *raw = NULL;
    uint8_t *packed;
    uint32_t size = 0, out = 8;
    if (argc != 3) return command_error("compress", "uso: compress origen destino.bkz");
    if (!bk_file_read_all(argv[1], &raw, &size))
        return command_error("compress", "no se pudo leer el origen");
    packed = (uint8_t *)bk_sys_alloc(size * 2U + 8U);
    if (!packed) { bk_sys_free(raw); return command_error("compress", "sin memoria"); }
    packed[0]='B'; packed[1]='K'; packed[2]='Z'; packed[3]='1'; put32(packed+4,size);
    for (uint32_t i=0; i<size;) {
        uint8_t value=((uint8_t *)raw)[i], count=1;
        while (i+count<size && count<255U && ((uint8_t *)raw)[i+count]==value) count++;
        packed[out++]=count; packed[out++]=value; i+=count;
    }
    if (!bk_file_write_all(argv[2], packed, out)) {
        bk_sys_free(packed); bk_sys_free(raw);
        return command_error("compress", "no se pudo escribir el destino");
    }
    kprintf("%u -> %u bytes (%s)\n",size,out,argv[2]);
    bk_sys_free(packed); bk_sys_free(raw); return 0;
}

BK_COMMAND_MAIN(run)
