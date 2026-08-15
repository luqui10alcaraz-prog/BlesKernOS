#include "win32.h"
#include "../include/memory.h"
#include "../include/pit.h"
#include "../include/task.h"
typedef struct PACKED{uint32_t d1;uint16_t d2,d3;uint8_t d4[8];}uuid_t;
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint8_t hex(uint8_t v){return v<10?(uint8_t)('0'+v):(uint8_t)('a'+v-10);}
static uint32_t WIN32_API rpc_UuidCreate(uuid_t*u){static uint32_t seq;uint32_t seed;if(!u)return 87U;seed=pit_get_ticks()^(task_current_pid()<<12)^++seq;u->d1=0xB1E50000U^seed;u->d2=(uint16_t)seed;u->d3=(uint16_t)(0x4000U|(seed&0xFFFU));for(uint32_t i=0;i<8;i++)u->d4[i]=(uint8_t)(seed>>((i&3U)*8U));u->d4[0]=(uint8_t)((u->d4[0]&0x3FU)|0x80U);return 0;}
static uint32_t WIN32_API rpc_UuidCreateSequential(uuid_t*u){return rpc_UuidCreate(u);}
static uint32_t WIN32_API rpc_UuidCompare(const uuid_t*a,const uuid_t*b,uint32_t*status){int d;if(status)*status=0;if(!a||!b){if(status)*status=87;return 0;}d=kmemcmp(a,b,sizeof(*a));return d<0?(uint32_t)-1:(d>0?1U:0U);}
static int WIN32_API rpc_UuidEqual(const uuid_t*a,const uuid_t*b,uint32_t*status){return rpc_UuidCompare(a,b,status)==0;}
static int WIN32_API rpc_UuidIsNil(const uuid_t*u,uint32_t*status){uuid_t nil;kmemset(&nil,0,sizeof(nil));return rpc_UuidEqual(u,&nil,status);}
static uint32_t WIN32_API rpc_UuidToStringA(const uuid_t*u,char**output){uint8_t bytes[16];char*s;uint8_t groups[5]={4,2,2,2,6};uint32_t p=0,n=0;if(!u||!output)return 87U;s=(char*)kmalloc(37);if(!s)return 14U;bytes[0]=u->d1>>24;bytes[1]=u->d1>>16;bytes[2]=u->d1>>8;bytes[3]=u->d1;bytes[4]=u->d2>>8;bytes[5]=u->d2;bytes[6]=u->d3>>8;bytes[7]=u->d3;kmemcpy(bytes+8,u->d4,8);for(uint32_t g=0;g<5;g++){if(g)s[p++]='-';for(uint32_t i=0;i<groups[g];i++){uint8_t v=bytes[n++];s[p++]=hex(v>>4);s[p++]=hex(v&15U);}}s[p]=0;*output=s;return 0;}
static uint32_t WIN32_API rpc_RpcStringFreeA(char**string){if(string&&*string){kfree(*string);*string=NULL;}return 0;}
static void*WIN32_API rpc_NdrOleAllocate(uint32_t size){return kmalloc(size?size:1U);}
static void WIN32_API rpc_NdrOleFree(void*memory){if(memory)kfree(memory);}
uint32_t win32_rpcrt4_resolve(const char*name){
#define R(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&rpc_##api
 R(UuidCreate);R(UuidCreateSequential);R(UuidCompare);R(UuidEqual);R(UuidIsNil);R(UuidToStringA);R(RpcStringFreeA);R(NdrOleAllocate);R(NdrOleFree);
#undef R
 return 0;
}
