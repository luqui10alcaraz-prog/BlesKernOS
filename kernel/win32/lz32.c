#include "win32.h"
#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/task.h"
#include "../include/pe_loader.h"
#include "../string.h"
#define LZ_BASE 0x7C000000U
#define LZ_SLOTS 8U
typedef struct{bool used;uint32_t owner,position,size;uint8_t*data;}lz_file_t;
static lz_file_t files[LZ_SLOTS];
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool native_path(const char*source,char*out){uint32_t i=0,j=0;if(!source||!out)return false;if(source[0]&&source[1]==':')i=2;while(source[i]&&j+1U<VFS_MAX_PATH){out[j++]=source[i]=='\\'?'/':source[i];i++;}out[j]=0;return source[i]==0;}
static lz_file_t*from_handle(int handle){uint32_t value=(uint32_t)handle;if(value<LZ_BASE||value>=LZ_BASE+LZ_SLOTS)return NULL;value-=LZ_BASE;return files[value].used&&files[value].owner==task_current_process_id()?&files[value]:NULL;}
static int WIN32_API lz_LZOpenFileA(const char*name,void*reopen UNUSED,uint16_t style UNUSED){char path[VFS_MAX_PATH];void*data=NULL;uint32_t size=0;if(!native_path(name,path)||!vfs_read_all(path,&data,&size))return-1;for(uint32_t i=0;i<LZ_SLOTS;i++)if(!files[i].used){files[i].used=true;files[i].owner=task_current_process_id();files[i].data=(uint8_t*)data;files[i].size=size;files[i].position=0;return(int)(LZ_BASE+i);}kfree(data);return-1;}
static int WIN32_API lz_LZRead(int handle,char*buffer,int count){lz_file_t*f=from_handle(handle);uint32_t take;if(!f||!buffer||count<0)return-1;take=f->size-f->position;if(take>(uint32_t)count)take=(uint32_t)count;if(take)kmemcpy(buffer,f->data+f->position,take);f->position+=take;return(int)take;}
static int32_t WIN32_API lz_LZSeek(int handle,int32_t offset,int origin){lz_file_t*f=from_handle(handle);int64_t pos;if(!f)return-1;pos=origin==0?offset:(origin==1?(int64_t)f->position+offset:(int64_t)f->size+offset);if(pos<0||pos>(int64_t)f->size)return-1;f->position=(uint32_t)pos;return(int32_t)f->position;}
static void WIN32_API lz_LZClose(int handle){lz_file_t*f=from_handle(handle);if(f){kfree(f->data);kmemset(f,0,sizeof(*f));}}
static int32_t WIN32_API lz_LZCopy(int source,int destination){typedef int(WIN32_API*write_t)(void*,const void*,uint32_t,uint32_t*,void*);write_t write=(write_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","WriteFile");lz_file_t*f=from_handle(source);uint32_t written=0;if(!f||!write)return-1;if(!write((void*)(uintptr_t)destination,f->data+f->position,f->size-f->position,&written,NULL))return-1;f->position+=written;return(int32_t)written;}
static int WIN32_API lz_GetExpandedNameA(const char*source,char*out){if(!source||!out)return-1;kstrcpy(out,source);return 1;}
void win32_lz32_cleanup_process(uint32_t owner){for(uint32_t i=0;i<LZ_SLOTS;i++)if(files[i].used&&files[i].owner==owner){kfree(files[i].data);kmemset(&files[i],0,sizeof(files[i]));}}
uint32_t win32_lz32_resolve(const char*name){
#define L(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&lz_##api
 L(LZOpenFileA);L(LZRead);L(LZSeek);L(LZClose);L(LZCopy);L(GetExpandedNameA);
#undef L
 return 0;
}
