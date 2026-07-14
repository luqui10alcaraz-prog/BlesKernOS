#include "win32.h"
#include "../include/memory.h"
#include "../include/pe_loader.h"
#include "../include/vfs.h"
#include "../string.h"

#define ERROR_SUCCESS 0U
#define ERROR_FILE_NOT_FOUND 2U
#define ERROR_ACCESS_DENIED 5U
#define ERROR_INVALID_HANDLE 6U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define ERROR_INVALID_PARAMETER 87U
#define ERROR_MORE_DATA 234U
#define REG_NONE 0U
#define REG_SZ 1U
#define REG_BINARY 3U
#define REG_DWORD 4U
#define HKEY_CLASSES_ROOT 0x80000000U
#define HKEY_CURRENT_USER 0x80000001U
#define HKEY_LOCAL_MACHINE 0x80000002U
#define HKEY_USERS 0x80000003U
#define REG_HANDLE_BASE 0x7A100000U
#define REG_MAX_KEYS 32U
#define REG_MAX_VALUES 96U
#define REG_VALUE_DATA 256U
#define REG_DISK_VERSION 1U
#define REG_PRIMARY_PATH "/SYSTEM/USER/CONFIG/WINREG.DAT"
#define REG_FALLBACK_PATH "/WINREG.DAT"

typedef struct {
    bool used;
    uint32_t root;
    char path[128];
} reg_key_t;

typedef struct {
    bool used;
    uint32_t root;
    char path[128];
    char name[64];
    uint32_t type;
    uint32_t size;
    uint8_t data[REG_VALUE_DATA];
} reg_value_t;

typedef struct PACKED {
    uint8_t magic[4];
    uint32_t version;
    uint32_t key_count;
    uint32_t value_count;
} reg_disk_header_t;

typedef struct PACKED {
    uint32_t root;
    char path[128];
} reg_disk_key_t;

typedef struct PACKED {
    uint32_t root;
    char path[128];
    char name[64];
    uint32_t type;
    uint32_t size;
    uint8_t data[REG_VALUE_DATA];
} reg_disk_value_t;

static reg_key_t reg_keys[REG_MAX_KEYS];
static reg_value_t reg_values[REG_MAX_VALUES];
static bool registry_loaded;
static bool registry_dirty;

static bool equal(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool equal_ci(const char *a,const char *b){uint8_t ca,cb;if(!a||!b)return false;do{ca=(uint8_t)*a++;cb=(uint8_t)*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return false;}while(ca);return true;}

static bool registry_parse(const uint8_t *buffer, uint32_t size) {
    const reg_disk_header_t *header;
    uint32_t offset;
    if (!buffer || size < sizeof(reg_disk_header_t)) return false;
    header = (const reg_disk_header_t *)buffer;
    if (header->magic[0] != 'B' || header->magic[1] != 'K' ||
        header->magic[2] != 'R' || header->magic[3] != 'G' ||
        header->version != REG_DISK_VERSION ||
        header->key_count > REG_MAX_KEYS ||
        header->value_count > REG_MAX_VALUES) return false;
    offset = sizeof(*header);
    if (offset + header->key_count * sizeof(reg_disk_key_t) > size) return false;
    for (uint32_t i = 0; i < header->key_count; i++) {
        const reg_disk_key_t *src = (const reg_disk_key_t *)(buffer + offset);
        reg_keys[i].used = true;
        reg_keys[i].root = src->root;
        kstrncpy(reg_keys[i].path, src->path, sizeof(reg_keys[i].path) - 1U);
        reg_keys[i].path[sizeof(reg_keys[i].path) - 1U] = '\0';
        offset += sizeof(*src);
    }
    if (offset + header->value_count * sizeof(reg_disk_value_t) > size) return false;
    for (uint32_t i = 0; i < header->value_count; i++) {
        const reg_disk_value_t *src = (const reg_disk_value_t *)(buffer + offset);
        reg_values[i].used = true;
        reg_values[i].root = src->root;
        kstrncpy(reg_values[i].path, src->path, sizeof(reg_values[i].path) - 1U);
        reg_values[i].path[sizeof(reg_values[i].path) - 1U] = '\0';
        kstrncpy(reg_values[i].name, src->name, sizeof(reg_values[i].name) - 1U);
        reg_values[i].name[sizeof(reg_values[i].name) - 1U] = '\0';
        reg_values[i].type = src->type;
        reg_values[i].size = src->size > REG_VALUE_DATA ? REG_VALUE_DATA : src->size;
        if (reg_values[i].size) kmemcpy(reg_values[i].data, src->data, reg_values[i].size);
        offset += sizeof(*src);
    }
    return true;
}

static void registry_ensure_loaded(void) {
    void *buffer = NULL;
    uint32_t size = 0;
    if (registry_loaded) return;
    registry_loaded = true;
    kmemset(reg_keys, 0, sizeof(reg_keys));
    kmemset(reg_values, 0, sizeof(reg_values));
    if (!vfs_read_all(REG_PRIMARY_PATH, &buffer, &size) &&
        !vfs_read_all(REG_FALLBACK_PATH, &buffer, &size)) return;
    if (!registry_parse((const uint8_t *)buffer, size)) {
        kmemset(reg_keys, 0, sizeof(reg_keys));
        kmemset(reg_values, 0, sizeof(reg_values));
    }
    kfree(buffer);
}

static bool registry_save(void) {
    uint32_t key_count = 0, value_count = 0, size, offset;
    uint8_t *buffer;
    reg_disk_header_t *header;
    bool written;
    for (uint32_t i = 0; i < REG_MAX_KEYS; i++) if (reg_keys[i].used) key_count++;
    for (uint32_t i = 0; i < REG_MAX_VALUES; i++) if (reg_values[i].used) value_count++;
    size = sizeof(reg_disk_header_t) + key_count * sizeof(reg_disk_key_t) +
           value_count * sizeof(reg_disk_value_t);
    buffer = (uint8_t *)kzalloc(size);
    if (!buffer) return false;
    header = (reg_disk_header_t *)buffer;
    header->magic[0] = 'B'; header->magic[1] = 'K';
    header->magic[2] = 'R'; header->magic[3] = 'G';
    header->version = REG_DISK_VERSION;
    header->key_count = key_count;
    header->value_count = value_count;
    offset = sizeof(*header);
    for (uint32_t i = 0; i < REG_MAX_KEYS; i++) {
        reg_disk_key_t *dst;
        if (!reg_keys[i].used) continue;
        dst = (reg_disk_key_t *)(buffer + offset);
        dst->root = reg_keys[i].root;
        kstrncpy(dst->path, reg_keys[i].path, sizeof(dst->path) - 1U);
        offset += sizeof(*dst);
    }
    for (uint32_t i = 0; i < REG_MAX_VALUES; i++) {
        reg_disk_value_t *dst;
        if (!reg_values[i].used) continue;
        dst = (reg_disk_value_t *)(buffer + offset);
        dst->root = reg_values[i].root;
        kstrncpy(dst->path, reg_values[i].path, sizeof(dst->path) - 1U);
        kstrncpy(dst->name, reg_values[i].name, sizeof(dst->name) - 1U);
        dst->type = reg_values[i].type;
        dst->size = reg_values[i].size;
        if (dst->size) kmemcpy(dst->data, reg_values[i].data, dst->size);
        offset += sizeof(*dst);
    }
    written = vfs_write_all(REG_PRIMARY_PATH, buffer, size) ||
              vfs_write_all(REG_FALLBACK_PATH, buffer, size);
    if (written) registry_dirty = false;
    kfree(buffer);
    return written;
}

static reg_key_t *key_from_handle(void *handle,uint32_t *root,const char **path){
    uint32_t value=(uint32_t)(uintptr_t)handle;
    registry_ensure_loaded();
    if(value>=HKEY_CLASSES_ROOT&&value<=HKEY_USERS){if(root)*root=value;if(path)*path="";return (reg_key_t *)(uintptr_t)1U;}
    if(value<REG_HANDLE_BASE||value>=REG_HANDLE_BASE+REG_MAX_KEYS)return NULL;
    reg_key_t*k=&reg_keys[value-REG_HANDLE_BASE];if(!k->used)return NULL;
    if (root) *root = k->root;
    if (path) *path = k->path;
    return k;
}
static bool build_path(void *parent,const char*sub,uint32_t*root,char*out,uint32_t size){
    const char*base;if(!key_from_handle(parent,root,&base)||!out||!size)return false;
    out[0]='\0';if(base&&*base){kstrncpy(out,base,size-1U);out[size-1U]='\0';}
    if(sub&&*sub){if(*out&&kstrlen(out)+1U<size)kstrcat(out,"\\");if(kstrlen(out)+kstrlen(sub)>=size)return false;kstrcat(out,sub);}return true;
}
static reg_key_t *find_key(uint32_t root,const char*path){registry_ensure_loaded();for(uint32_t i=0;i<REG_MAX_KEYS;i++)if(reg_keys[i].used&&reg_keys[i].root==root&&equal_ci(reg_keys[i].path,path))return &reg_keys[i];return NULL;}
static void *key_handle(reg_key_t*k){return(void*)(uintptr_t)(REG_HANDLE_BASE+(uint32_t)(k-reg_keys));}
static uint32_t WIN32_API adv_RegOpenKeyExA(void*parent,const char*sub,uint32_t options UNUSED,uint32_t access UNUSED,void**result){uint32_t root;char path[128];reg_key_t*k;if(result)*result=NULL;if(!result||!build_path(parent,sub,&root,path,sizeof(path)))return ERROR_INVALID_PARAMETER;k=find_key(root,path);if(!k)return ERROR_FILE_NOT_FOUND;*result=key_handle(k);return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegCreateKeyExA(void*parent,const char*sub,uint32_t reserved UNUSED,char*class_name UNUSED,uint32_t options UNUSED,uint32_t access UNUSED,void*security UNUSED,void**result,uint32_t*disposition){uint32_t root;char path[128];reg_key_t*k;if(result)*result=NULL;if(!result||!build_path(parent,sub,&root,path,sizeof(path)))return ERROR_INVALID_PARAMETER;k=find_key(root,path);if(k){*result=key_handle(k);if(disposition)*disposition=2U;return ERROR_SUCCESS;}for(uint32_t i=0;i<REG_MAX_KEYS;i++)if(!reg_keys[i].used){k=&reg_keys[i];kmemset(k,0,sizeof(*k));k->used=true;k->root=root;kstrncpy(k->path,path,sizeof(k->path)-1U);*result=key_handle(k);if(disposition)*disposition=1U;registry_dirty=true;(void)registry_save();return ERROR_SUCCESS;}return ERROR_NOT_ENOUGH_MEMORY;}
static uint32_t WIN32_API adv_RegCloseKey(void*handle){uint32_t root;const char*path;return key_from_handle(handle,&root,&path)?ERROR_SUCCESS:ERROR_INVALID_HANDLE;}
static reg_value_t *find_value(uint32_t root,const char*path,const char*name){const char*n=name?name:"";registry_ensure_loaded();for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(reg_values[i].used&&reg_values[i].root==root&&equal_ci(reg_values[i].path,path)&&equal_ci(reg_values[i].name,n))return &reg_values[i];return NULL;}
static uint32_t WIN32_API adv_RegSetValueExA(void*handle,const char*name,uint32_t reserved UNUSED,uint32_t type,const uint8_t*data,uint32_t size){uint32_t root;const char*path;reg_value_t*v=NULL;if(!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;if(!data&&size)return ERROR_INVALID_PARAMETER;if(size>REG_VALUE_DATA)return ERROR_MORE_DATA;v=find_value(root,path,name);if(!v){for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(!reg_values[i].used){v=&reg_values[i];break;}}if(!v)return ERROR_NOT_ENOUGH_MEMORY;kmemset(v,0,sizeof(*v));v->used=true;v->root=root;kstrncpy(v->path,path,sizeof(v->path)-1U);kstrncpy(v->name,name?name:"",sizeof(v->name)-1U);v->type=type;v->size=size;if(size)kmemcpy(v->data,data,size);registry_dirty=true;(void)registry_save();return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegQueryValueExA(void*handle,const char*name,uint32_t*reserved UNUSED,uint32_t*type,uint8_t*data,uint32_t*size){uint32_t root;const char*path;reg_value_t*v;if(!size||!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;v=find_value(root,path,name);if(!v)return ERROR_FILE_NOT_FOUND;if(type)*type=v->type;if(!data||*size<v->size){*size=v->size;return data?ERROR_MORE_DATA:ERROR_SUCCESS;}if(v->size)kmemcpy(data,v->data,v->size);*size=v->size;return ERROR_SUCCESS;}
static int WIN32_API adv_IsTextUnicode(const void*buffer,int bytes,int*flags){const uint8_t*p=(const uint8_t*)buffer;int pairs,zero_high=0,ascii_low=0;if(!p||bytes<2)return 0;pairs=bytes/2;for(int i=0;i<pairs;i++){if(p[i*2+1]==0)zero_high++;if(p[i*2]>=0x20&&p[i*2]<0x7f)ascii_low++;}if(flags)*flags=zero_high*2>=pairs?1:0;return zero_high*2>=pairs&&ascii_low>0;}
uint32_t win32_advapi32_resolve(const char*name){
#define A(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&adv_##api
 A(RegOpenKeyExA);A(RegCreateKeyExA);A(RegCloseKey);A(RegSetValueExA);A(RegQueryValueExA);A(IsTextUnicode);
#undef A
 return 0;
}
