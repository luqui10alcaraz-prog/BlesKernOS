#include "win32.h"
#include "../include/memory.h"
#include "../include/pe_loader.h"
#include "../include/user_config.h"
#include "../include/vfs.h"
#include "../string.h"

#define ERROR_SUCCESS 0U
#define ERROR_FILE_NOT_FOUND 2U
#define ERROR_ACCESS_DENIED 5U
#define ERROR_INVALID_HANDLE 6U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define ERROR_INVALID_PARAMETER 87U
#define ERROR_MORE_DATA 234U
#define ERROR_NO_MORE_ITEMS 259U
#define REG_NONE 0U
#define REG_SZ 1U
#define REG_EXPAND_SZ 2U
#define REG_BINARY 3U
#define REG_DWORD 4U
#define REG_MULTI_SZ 7U
#define HKEY_CLASSES_ROOT 0x80000000U
#define HKEY_CURRENT_USER 0x80000001U
#define HKEY_LOCAL_MACHINE 0x80000002U
#define HKEY_USERS 0x80000003U
#define REG_HANDLE_BASE 0x7A100000U
#define REG_MAX_KEYS 64U
#define REG_MAX_VALUES 96U
#define REG_VALUE_DATA 256U
#define REG_DISK_VERSION 2U
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
typedef struct PACKED {
    uint32_t root;
    char path[128];
    char name[64];
    uint32_t type;
    uint32_t size;
    uint8_t data[256];
} reg_disk_value_v1_t;

static reg_key_t reg_keys[REG_MAX_KEYS];
static reg_value_t reg_values[REG_MAX_VALUES];
static bool registry_loaded;
static bool registry_dirty;

static bool equal(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool equal_ci(const char *a,const char *b){uint8_t ca,cb;if(!a||!b)return false;do{ca=(uint8_t)*a++;cb=(uint8_t)*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return false;}while(ca);return true;}

static bool registry_parse(const uint8_t *buffer, uint32_t size) {
    const reg_disk_header_t *header;
    uint32_t offset, value_record_size;
    if (!buffer || size < sizeof(reg_disk_header_t)) return false;
    header = (const reg_disk_header_t *)buffer;
    if (header->magic[0] != 'B' || header->magic[1] != 'K' ||
        header->magic[2] != 'R' || header->magic[3] != 'G' ||
        (header->version != 1U && header->version != REG_DISK_VERSION) ||
        header->key_count > REG_MAX_KEYS || header->value_count > REG_MAX_VALUES)
        return false;
    value_record_size = header->version == 1U ? sizeof(reg_disk_value_v1_t)
                                              : sizeof(reg_disk_value_t);
    offset = sizeof(*header);
    if (offset + header->key_count * sizeof(reg_disk_key_t) > size) return false;
    for (uint32_t i = 0; i < header->key_count; i++) {
        const reg_disk_key_t *src = (const reg_disk_key_t *)(buffer + offset);
        reg_keys[i].used = true; reg_keys[i].root = src->root;
        kstrncpy(reg_keys[i].path, src->path, sizeof(reg_keys[i].path) - 1U);
        reg_keys[i].path[sizeof(reg_keys[i].path) - 1U] = '\0';
        offset += sizeof(*src);
    }
    if (offset + header->value_count * value_record_size > size) return false;
    for (uint32_t i = 0; i < header->value_count; i++) {
        const reg_disk_value_v1_t *old = (const reg_disk_value_v1_t *)(buffer + offset);
        const reg_disk_value_t *current = (const reg_disk_value_t *)(buffer + offset);
        uint32_t data_limit = header->version == 1U ? 256U : REG_VALUE_DATA;
        reg_values[i].used = true;
        reg_values[i].root = old->root;
        kstrncpy(reg_values[i].path, old->path, sizeof(reg_values[i].path) - 1U);
        kstrncpy(reg_values[i].name, old->name, sizeof(reg_values[i].name) - 1U);
        reg_values[i].type = old->type;
        reg_values[i].size = old->size > data_limit ? data_limit : old->size;
        if (reg_values[i].size) kmemcpy(reg_values[i].data,
            header->version == 1U ? old->data : current->data, reg_values[i].size);
        offset += value_record_size;
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
    bk_user_config_ensure_dirs();
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
static uint32_t WIN32_API adv_RegCreateKeyExA(void*parent,const char*sub,uint32_t reserved UNUSED,char*class_name UNUSED,uint32_t options UNUSED,uint32_t access UNUSED,void*security UNUSED,void**result,uint32_t*disposition){uint32_t root;char path[128];reg_key_t*k;if(result)*result=NULL;if(!result||!build_path(parent,sub,&root,path,sizeof(path)))return ERROR_INVALID_PARAMETER;k=find_key(root,path);if(k){*result=key_handle(k);if(disposition)*disposition=2U;return ERROR_SUCCESS;}for(uint32_t i=0;i<REG_MAX_KEYS;i++)if(!reg_keys[i].used){k=&reg_keys[i];kmemset(k,0,sizeof(*k));k->used=true;k->root=root;kstrncpy(k->path,path,sizeof(k->path)-1U);*result=key_handle(k);if(disposition)*disposition=1U;registry_dirty=true;return ERROR_SUCCESS;}return ERROR_NOT_ENOUGH_MEMORY;}
static uint32_t WIN32_API adv_RegCloseKey(void*handle){uint32_t root;const char*path;return key_from_handle(handle,&root,&path)?ERROR_SUCCESS:ERROR_INVALID_HANDLE;}
static reg_value_t *find_value(uint32_t root,const char*path,const char*name){const char*n=name?name:"";registry_ensure_loaded();for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(reg_values[i].used&&reg_values[i].root==root&&equal_ci(reg_values[i].path,path)&&equal_ci(reg_values[i].name,n))return &reg_values[i];return NULL;}
bool win32_registry_query_string(uint32_t root, const char *path,
                                 const char *name, char *out,
                                 uint32_t capacity) {
    reg_value_t *value;
    uint32_t length;
    if (!path || !out || capacity == 0U) return false;
    value = find_value(root, path, name);
    if (!value || (value->type != REG_SZ && value->type != REG_EXPAND_SZ) ||
        value->size == 0U) return false;
    length = value->size;
    if (length >= capacity) length = capacity - 1U;
    kmemcpy(out, value->data, length);
    out[length] = '\0';
    if (length && out[length - 1U] == '\0') out[length - 1U] = '\0';
    return true;
}
static uint32_t WIN32_API adv_RegSetValueExA(void*handle,const char*name,uint32_t reserved UNUSED,uint32_t type,const uint8_t*data,uint32_t size){uint32_t root;const char*path;reg_value_t*v=NULL;if(!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;if(!data&&size)return ERROR_INVALID_PARAMETER;if(size>REG_VALUE_DATA)return ERROR_MORE_DATA;v=find_value(root,path,name);if(!v){for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(!reg_values[i].used){v=&reg_values[i];break;}}if(!v)return ERROR_NOT_ENOUGH_MEMORY;kmemset(v,0,sizeof(*v));v->used=true;v->root=root;kstrncpy(v->path,path,sizeof(v->path)-1U);kstrncpy(v->name,name?name:"",sizeof(v->name)-1U);v->type=type;v->size=size;if(size)kmemcpy(v->data,data,size);registry_dirty=true;return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegQueryValueExA(void*handle,const char*name,uint32_t*reserved UNUSED,uint32_t*type,uint8_t*data,uint32_t*size){uint32_t root;const char*path;reg_value_t*v;if(!size||!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;v=find_value(root,path,name);if(!v)return ERROR_FILE_NOT_FOUND;if(type)*type=v->type;if(!data||*size<v->size){*size=v->size;return data?ERROR_MORE_DATA:ERROR_SUCCESS;}if(v->size)kmemcpy(data,v->data,v->size);*size=v->size;return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegOpenKeyA(void*parent,const char*sub,void**result){return adv_RegOpenKeyExA(parent,sub,0,0,result);}
static uint32_t WIN32_API adv_RegCreateKeyA(void*parent,const char*sub,void**result){return adv_RegCreateKeyExA(parent,sub,0,NULL,0,0,NULL,result,NULL);}
static uint32_t WIN32_API adv_RegDeleteValueA(void*handle,const char*name){uint32_t root;const char*path;reg_value_t*v;if(!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;v=find_value(root,path,name);if(!v)return ERROR_FILE_NOT_FOUND;kmemset(v,0,sizeof(*v));registry_dirty=true;return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegDeleteKeyA(void*parent,const char*sub){uint32_t root;char path[128];reg_key_t*k;if(!build_path(parent,sub,&root,path,sizeof(path)))return ERROR_INVALID_PARAMETER;k=find_key(root,path);if(!k)return ERROR_FILE_NOT_FOUND;for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(reg_values[i].used&&reg_values[i].root==root&&equal_ci(reg_values[i].path,path))kmemset(&reg_values[i],0,sizeof(reg_values[i]));kmemset(k,0,sizeof(*k));registry_dirty=true;return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegQueryValueA(void*handle,const char*sub,char*data,int32_t*size){void*key=handle;uint32_t bytes,type=0,result;if(!size)return ERROR_INVALID_PARAMETER;if(sub&&*sub){result=adv_RegOpenKeyA(handle,sub,&key);if(result)return result;}bytes=*size>0?(uint32_t)*size:0U;result=adv_RegQueryValueExA(key,NULL,NULL,&type,(uint8_t*)data,&bytes);*size=(int32_t)bytes;return result;}
static uint32_t WIN32_API adv_RegSetValueA(void*handle,const char*sub,uint32_t type,const char*data,uint32_t size){void*key=handle;uint32_t result;if(sub&&*sub){result=adv_RegCreateKeyA(handle,sub,&key);if(result)return result;}return adv_RegSetValueExA(key,NULL,0,type,(const uint8_t*)data,size);}
static bool ansi_from_wide(const uint16_t *wide,char*out,uint32_t size){uint32_t i=0;if(!out||!size)return false;if(!wide){out[0]=0;return true;}while(wide[i]&&i+1U<size){out[i]=(char)(wide[i]&0xFFU);i++;}out[i]=0;return wide[i]==0;}
static void wide_from_ansi(const char *ansi,uint16_t*out,uint32_t chars){uint32_t i=0;if(!out||!chars)return;while(ansi&&ansi[i]&&i+1U<chars){out[i]=(uint8_t)ansi[i];i++;}out[i]=0;}
static bool direct_child_name(const char*parent,const char*candidate,const char**name,uint32_t*length){uint32_t plen=(uint32_t)kstrlen(parent),i;if(plen){for(i=0;i<plen;i++)if(!candidate[i]||((candidate[i]>='a'&&candidate[i]<='z'?candidate[i]-32:candidate[i])!=(parent[i]>='a'&&parent[i]<='z'?parent[i]-32:parent[i])))return false;if(candidate[plen]!='\\')return false;candidate+=plen+1U;}if(!*candidate)return false;for(i=0;candidate[i]&&candidate[i]!='\\';i++);if(candidate[i])return false;if(name)*name=candidate;if(length)*length=i;return true;}
static uint32_t WIN32_API adv_RegEnumKeyExA(void*handle,uint32_t index,char*name,uint32_t*name_size,uint32_t*reserved UNUSED,char*class_name UNUSED,uint32_t*class_size UNUSED,void*last_write){uint32_t root,seen=0,length;const char*path,*child;if(last_write)kmemset(last_write,0,8U);if(!name_size||!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;for(uint32_t i=0;i<REG_MAX_KEYS;i++)if(reg_keys[i].used&&reg_keys[i].root==root&&direct_child_name(path,reg_keys[i].path,&child,&length)){if(seen++!=index)continue;if(!name||*name_size<=length){*name_size=length;return ERROR_MORE_DATA;}kmemcpy(name,child,length);name[length]=0;*name_size=length;return ERROR_SUCCESS;}return ERROR_NO_MORE_ITEMS;}
static uint32_t WIN32_API adv_RegEnumKeyA(void*handle,uint32_t index,char*name,uint32_t size){uint32_t length=size;return adv_RegEnumKeyExA(handle,index,name,&length,NULL,NULL,NULL,NULL);}
static uint32_t WIN32_API adv_RegEnumValueA(void*handle,uint32_t index,char*name,uint32_t*name_size,uint32_t*reserved UNUSED,uint32_t*type,uint8_t*data,uint32_t*data_size){uint32_t root,seen=0;const char*path;if(!name_size||!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;for(uint32_t i=0;i<REG_MAX_VALUES;i++){reg_value_t*v=&reg_values[i];uint32_t length;if(!v->used||v->root!=root||!equal_ci(v->path,path))continue;if(seen++!=index)continue;length=(uint32_t)kstrlen(v->name);if(!name||*name_size<=length){*name_size=length;return ERROR_MORE_DATA;}kstrcpy(name,v->name);*name_size=length;if(type)*type=v->type;if(data_size){if(!data||*data_size<v->size){*data_size=v->size;return data?ERROR_MORE_DATA:ERROR_SUCCESS;}if(v->size)kmemcpy(data,v->data,v->size);*data_size=v->size;}return ERROR_SUCCESS;}return ERROR_NO_MORE_ITEMS;}
static uint32_t WIN32_API adv_RegQueryInfoKeyA(void*handle,char*class_name,uint32_t*class_size,uint32_t*reserved UNUSED,uint32_t*subkeys,uint32_t*max_subkey,uint32_t*max_class,uint32_t*values,uint32_t*max_value_name,uint32_t*max_value_data,uint32_t*security_descriptor,void*last_write){uint32_t root,sub_count=0,value_count=0,max_sub=0,max_name=0,max_data=0;const char*path,*child;uint32_t length;if(!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;for(uint32_t i=0;i<REG_MAX_KEYS;i++)if(reg_keys[i].used&&reg_keys[i].root==root&&direct_child_name(path,reg_keys[i].path,&child,&length)){sub_count++;if(length>max_sub)max_sub=length;}for(uint32_t i=0;i<REG_MAX_VALUES;i++)if(reg_values[i].used&&reg_values[i].root==root&&equal_ci(reg_values[i].path,path)){uint32_t n=(uint32_t)kstrlen(reg_values[i].name);value_count++;if(n>max_name)max_name=n;if(reg_values[i].size>max_data)max_data=reg_values[i].size;}if(class_name&&class_size&&*class_size){class_name[0]=0;*class_size=0;}if(subkeys)*subkeys=sub_count;if(max_subkey)*max_subkey=max_sub;if(max_class)*max_class=0;if(values)*values=value_count;if(max_value_name)*max_value_name=max_name;if(max_value_data)*max_value_data=max_data;if(security_descriptor)*security_descriptor=0;if(last_write)kmemset(last_write,0,8U);return ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegFlushKey(void*handle){uint32_t root;const char*path;if(!key_from_handle(handle,&root,&path))return ERROR_INVALID_HANDLE;return registry_dirty&&!registry_save()?ERROR_ACCESS_DENIED:ERROR_SUCCESS;}
static uint32_t WIN32_API adv_RegOpenKeyExW(void*p,const uint16_t*s,uint32_t o,uint32_t a,void**r){char n[128];if(!ansi_from_wide(s,n,sizeof(n)))return ERROR_INVALID_PARAMETER;return adv_RegOpenKeyExA(p,s?n:NULL,o,a,r);}
static uint32_t WIN32_API adv_RegOpenKeyW(void*p,const uint16_t*s,void**r){return adv_RegOpenKeyExW(p,s,0,0,r);}
static uint32_t WIN32_API adv_RegCreateKeyExW(void*p,const uint16_t*s,uint32_t rv,uint16_t*c,uint32_t o,uint32_t a,void*sec,void**r,uint32_t*d){char n[128];(void)c;if(!ansi_from_wide(s,n,sizeof(n)))return ERROR_INVALID_PARAMETER;return adv_RegCreateKeyExA(p,s?n:NULL,rv,NULL,o,a,sec,r,d);}
static uint32_t WIN32_API adv_RegCreateKeyW(void*p,const uint16_t*s,void**r){return adv_RegCreateKeyExW(p,s,0,NULL,0,0,NULL,r,NULL);}
static uint32_t WIN32_API adv_RegSetValueExW(void*h,const uint16_t*n,uint32_t r,uint32_t t,const uint8_t*d,uint32_t z){char a[64];if(!ansi_from_wide(n,a,sizeof(a)))return ERROR_INVALID_PARAMETER;return adv_RegSetValueExA(h,n?a:NULL,r,t,d,z);}
static uint32_t WIN32_API adv_RegQueryValueExW(void*h,const uint16_t*n,uint32_t*r,uint32_t*t,uint8_t*d,uint32_t*z){char a[64];if(!ansi_from_wide(n,a,sizeof(a)))return ERROR_INVALID_PARAMETER;return adv_RegQueryValueExA(h,n?a:NULL,r,t,d,z);}
static uint32_t WIN32_API adv_RegDeleteValueW(void*h,const uint16_t*n){char a[64];if(!ansi_from_wide(n,a,sizeof(a)))return ERROR_INVALID_PARAMETER;return adv_RegDeleteValueA(h,a);}
static uint32_t WIN32_API adv_RegDeleteKeyW(void*h,const uint16_t*n){char a[128];if(!ansi_from_wide(n,a,sizeof(a)))return ERROR_INVALID_PARAMETER;return adv_RegDeleteKeyA(h,a);}
static uint32_t WIN32_API adv_RegEnumKeyExW(void*h,uint32_t index,uint16_t*name,uint32_t*name_size,uint32_t*r,uint16_t*c,uint32_t*cs,void*ft){char a[128];uint32_t cap=name_size?*name_size:0,result=adv_RegEnumKeyExA(h,index,a,&cap,r,NULL,cs,ft);if(name_size)*name_size=cap;if(result==ERROR_SUCCESS&&name)wide_from_ansi(a,name,*name_size+1U);(void)c;return result;}
static uint32_t WIN32_API adv_RegEnumValueW(void*h,uint32_t index,uint16_t*name,uint32_t*name_size,uint32_t*r,uint32_t*t,uint8_t*d,uint32_t*z){char a[64];uint32_t cap=name_size?*name_size:0,result=adv_RegEnumValueA(h,index,a,&cap,r,t,d,z);if(name_size)*name_size=cap;if(result==ERROR_SUCCESS&&name)wide_from_ansi(a,name,*name_size+1U);return result;}

static int WIN32_API adv_GetUserNameA(char*out,uint32_t*size){const char*name="User";uint32_t needed=5U;if(!size)return 0;if(!out||*size<needed){*size=needed;return 0;}kstrcpy(out,name);*size=needed;return 1;}
static int WIN32_API adv_IsTextUnicode(const void*buffer,int bytes,int*flags){const uint8_t*p=(const uint8_t*)buffer;int pairs,zero_high=0,ascii_low=0;if(!p||bytes<2)return 0;pairs=bytes/2;for(int i=0;i<pairs;i++){if(p[i*2+1]==0)zero_high++;if(p[i*2]>=0x20&&p[i*2]<0x7f)ascii_low++;}if(flags)*flags=zero_high*2>=pairs?1:0;return zero_high*2>=pairs&&ascii_low>0;}
uint32_t win32_advapi32_resolve(const char*name){
#define A(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&adv_##api
 A(RegOpenKeyA);A(RegOpenKeyExA);A(RegCreateKeyA);A(RegCreateKeyExA);A(RegCloseKey);
 A(RegSetValueA);A(RegSetValueExA);A(RegQueryValueA);A(RegQueryValueExA);
 A(RegDeleteValueA);A(RegDeleteKeyA);A(RegEnumKeyA);A(RegEnumKeyExA);A(RegEnumValueA);
 A(RegQueryInfoKeyA);A(RegFlushKey);
 A(RegOpenKeyW);A(RegOpenKeyExW);A(RegCreateKeyW);A(RegCreateKeyExW);
 A(RegSetValueExW);A(RegQueryValueExW);A(RegDeleteValueW);A(RegDeleteKeyW);
 A(RegEnumKeyExW);A(RegEnumValueW);A(GetUserNameA);A(IsTextUnicode);
#undef A
 return 0;
}
