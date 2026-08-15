#include "win32.h"
#include "../include/vfs.h"
#include "../include/memory.h"
#include "../string.h"

#define HKEY_CLASSES_ROOT 0x80000000U
#define ASSOCSTR_COMMAND 1U
#define ASSOCSTR_EXECUTABLE 2U
#define ASSOCSTR_FRIENDLYDOCNAME 3U
#define ASSOCSTR_FRIENDLYAPPNAME 4U
#define ASSOCSTR_CONTENTTYPE 14U
#define ASSOCSTR_DEFAULTICON 15U
#define ASSOCSTR_PROGID 20U
#define SH_S_OK 0x00000000U
#define SH_E_POINTER 0x80004003U
#define SH_E_FAIL 0x80004005U
#define SH_E_INSUFFICIENT_BUFFER 0x8007007AU
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static uint8_t upper(uint8_t c){return c>='a'&&c<='z'?(uint8_t)(c-32):c;}
static int WIN32_API sh_StrCmpIA(const char*a,const char*b){if(!a)return b?-1:0;if(!b)return 1;while(*a&&*b){int d=(int)upper((uint8_t)*a++)-(int)upper((uint8_t)*b++);if(d)return d;}return(int)(uint8_t)*a-(int)(uint8_t)*b;}
static char*WIN32_API sh_StrStrIA(const char*text,const char*search){if(!text||!search)return NULL;if(!*search)return(char*)text;for(;*text;text++){const char*a=text,*b=search;while(*a&&*b&&upper((uint8_t)*a)==upper((uint8_t)*b)){a++;b++;}if(!*b)return(char*)text;}return NULL;}
static char*WIN32_API sh_PathFindFileNameA(const char*path){const char*last=path;if(!path)return NULL;for(;*path;path++)if(*path=='/'||*path=='\\')last=path+1;return(char*)last;}
static char*WIN32_API sh_PathFindExtensionA(const char*path){const char*file=sh_PathFindFileNameA(path),*dot=NULL;if(!file)return NULL;for(const char*p=file;*p;p++)if(*p=='.')dot=p;return(char*)(dot?dot:file+kstrlen(file));}
static int WIN32_API sh_PathRemoveFileSpecA(char*path){char*file;if(!path||!*path)return 0;file=sh_PathFindFileNameA(path);if(file==path)return 0;while(file>path&&(file[-1]=='/'||file[-1]=='\\'))file--;*file=0;return 1;}
static char*WIN32_API sh_PathCombineA(char*out,const char*directory,const char*file){uint32_t used;if(!out||!file)return NULL;out[0]=0;if(directory&&*directory){kstrncpy(out,directory,VFS_MAX_PATH-1U);out[VFS_MAX_PATH-1U]=0;used=(uint32_t)kstrlen(out);if(used&&out[used-1]!='/'&&out[used-1]!='\\'&&used+1U<VFS_MAX_PATH){out[used++]='\\';out[used]=0;}}if(kstrlen(out)+kstrlen(file)>=VFS_MAX_PATH)return NULL;kstrcat(out,file);return out;}
static int WIN32_API sh_PathIsRelativeA(const char*path){return !path||!(path[0]=='/'||path[0]=='\\'||(path[0]&&path[1]==':'));}
static int WIN32_API sh_PathFileExistsA(const char*path){char native[VFS_MAX_PATH];void*data=NULL;uint32_t size=0,i=0,j=0;if(!path)return 0;if(path[0]&&path[1]==':')i=2;while(path[i]&&j+1U<sizeof(native)){native[j++]=path[i]=='\\'?'/':path[i];i++;}native[j]=0;if(vfs_read_all(native,&data,&size)){kfree(data);return 1;}vfs_dir_entry_t e;uint32_t count=0;return vfs_listdir(native,&e,1,&count)?1:0;}
static int WIN32_API sh_PathMatchSpecA(const char*file,const char*spec){const char*ext;if(!file||!spec)return 0;if(spec[0]=='*'&&spec[1]=='.'){ext=sh_PathFindExtensionA(file);return sh_StrCmpIA(ext,spec+1)==0;}return sh_StrCmpIA(file,spec)==0;}

static bool sh_copy_result_a(const char *value,char *out,uint32_t *count){
    uint32_t need;
    if(!count)return false;
    need=(uint32_t)kstrlen(value)+1U;
    if(!out||*count<need){*count=need;return false;}
    kstrcpy(out,value);*count=need;return true;
}
static uint32_t sh_wide_to_ansi(const uint16_t *wide,char *out,uint32_t cap){uint32_t i=0;if(!wide||!out||!cap)return 0;while(wide[i]&&i+1U<cap){out[i]=(wide[i]<=0xFFU)?(char)wide[i]:'?';i++;}out[i]=0;return i;}
static uint32_t sh_ansi_to_wide(const char *ansi,uint16_t *out,uint32_t cap){uint32_t i=0;if(!ansi||!out||!cap)return 0;while(ansi[i]&&i+1U<cap){out[i]=(uint8_t)ansi[i];i++;}out[i]=0;return i;}
static bool sh_assoc_class(const char *assoc,char *class_name,uint32_t capacity){
    if(!assoc||!*assoc||!class_name||capacity<2U)return false;
    if(assoc[0]=='.'){
        if(!win32_registry_query_string(HKEY_CLASSES_ROOT,assoc,NULL,class_name,capacity))return false;
    }else{kstrncpy(class_name,assoc,capacity-1U);class_name[capacity-1U]=0;}
    return class_name[0]!=0;
}
static void sh_command_executable(const char *command,char *out,uint32_t capacity){
    uint32_t i=0,j=0;char quote=0;
    if (!out || !capacity) return;
    out[0] = 0;
    if (!command) return;
    while(command[i]==' '||command[i]=='\t')i++;
    if(command[i]=='"'||command[i]=='\'')quote=command[i++];
    while(command[i]&&j+1U<capacity){if(quote){if(command[i]==quote)break;}else if(command[i]==' '||command[i]=='\t')break;out[j++]=command[i++];}
    out[j]=0;
}
static uint32_t WIN32_API sh_AssocQueryStringA(uint32_t flags UNUSED,uint32_t query,const char *assoc,const char *extra,char *out,uint32_t *count){
    char cls[128],key[260],value[512],verb[64],ext[32];
    if (!count) return SH_E_POINTER;
    if (out && *count) out[0] = 0;
    if(!assoc||!*assoc)return SH_E_FAIL;
    ext[0]=0;if(assoc[0]=='.'){kstrncpy(ext,assoc,sizeof(ext)-1U);ext[sizeof(ext)-1U]=0;}
    if(!sh_assoc_class(assoc,cls,sizeof(cls)))return SH_E_FAIL;
    value[0]=0;
    switch(query){
        case ASSOCSTR_PROGID:kstrncpy(value,cls,sizeof(value)-1U);value[sizeof(value)-1U]=0;break;
        case ASSOCSTR_FRIENDLYDOCNAME:
            if(!win32_registry_query_string(HKEY_CLASSES_ROOT,cls,NULL,value,sizeof(value)))kstrncpy(value,cls,sizeof(value)-1U);
            break;
        case ASSOCSTR_CONTENTTYPE:
            if(!ext[0]||!win32_registry_query_string(HKEY_CLASSES_ROOT,ext,"Content Type",value,sizeof(value)))return SH_E_FAIL;
            break;
        case ASSOCSTR_DEFAULTICON:
            kstrncpy(key,cls,sizeof(key)-1U);key[sizeof(key)-1U]=0;if(kstrlen(key)+12U>=sizeof(key))return SH_E_FAIL;kstrcat(key,"\\DefaultIcon");
            if(!win32_registry_query_string(HKEY_CLASSES_ROOT,key,NULL,value,sizeof(value)))return SH_E_FAIL;
            break;
        case ASSOCSTR_COMMAND:
        case ASSOCSTR_EXECUTABLE:
        case ASSOCSTR_FRIENDLYAPPNAME:
            kstrncpy(verb,(extra&&*extra)?extra:"open",sizeof(verb)-1U);verb[sizeof(verb)-1U]=0;
            kstrncpy(key,cls,sizeof(key)-1U);key[sizeof(key)-1U]=0;
            if(kstrlen(key)+kstrlen(verb)+17U>=sizeof(key))return SH_E_FAIL;
            kstrcat(key,"\\shell\\");kstrcat(key,verb);kstrcat(key,"\\command");
            if(!win32_registry_query_string(HKEY_CLASSES_ROOT,key,NULL,value,sizeof(value)))return SH_E_FAIL;
            if(query!=ASSOCSTR_COMMAND){char exe[260];sh_command_executable(value,exe,sizeof(exe));kstrncpy(value,query==ASSOCSTR_FRIENDLYAPPNAME?sh_PathFindFileNameA(exe):exe,sizeof(value)-1U);value[sizeof(value)-1U]=0;}
            break;
        default:return SH_E_FAIL;
    }
    return sh_copy_result_a(value,out,count)?SH_S_OK:SH_E_INSUFFICIENT_BUFFER;
}
static uint32_t WIN32_API sh_AssocQueryStringW(uint32_t flags,uint32_t query,const uint16_t *assoc,const uint16_t *extra,uint16_t *out,uint32_t *count){
    char a[128],e[64],value[512];uint32_t ansi_count=sizeof(value),need;
    if (!count) return SH_E_POINTER;
    sh_wide_to_ansi(assoc,a,sizeof(a));
    sh_wide_to_ansi(extra,e,sizeof(e));
    uint32_t result=sh_AssocQueryStringA(flags,query,a,extra?e:NULL,value,&ansi_count);
    if (result != SH_S_OK) return result;
    need=(uint32_t)kstrlen(value)+1U;
    if(!out||*count<need){*count=need;return SH_E_INSUFFICIENT_BUFFER;}
    sh_ansi_to_wide(value,out,*count);*count=need;return SH_S_OK;
}

uint32_t win32_shlwapi_resolve(const char*name){
#define H(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&sh_##api
 H(StrCmpIA);H(StrStrIA);H(PathFindFileNameA);H(PathFindExtensionA);H(PathRemoveFileSpecA);H(PathCombineA);H(PathIsRelativeA);H(PathFileExistsA);H(PathMatchSpecA);H(AssocQueryStringA);H(AssocQueryStringW);
#undef H
 return 0;
}
