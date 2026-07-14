#include "win32.h"
#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/pe_loader.h"

#define PROFILE_MAX_FILE 8192U
#define PROFILE_DEFAULT_PATH "/WIN.INI"
#define PROFILE_CACHE_SLOTS 4U

typedef struct {
    bool used;
    bool dirty;
    char path[VFS_MAX_PATH];
    char *text;
    uint32_t size;
} profile_cache_t;

static profile_cache_t g_profile_cache[PROFILE_CACHE_SLOTS];

static bool equal_ci(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b){char ca=*a++,cb=*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return false;}return *a==*b;}
static bool wide_to_ansi(const uint16_t*w,char*out,uint32_t size){uint32_t i=0;if(!w||!out||!size)return false;while(w[i]){if(i+1U>=size)return false;out[i]=w[i]<=0xFFU?(char)w[i]:'?';i++;}out[i]='\0';return true;}
static uint32_t ansi_to_wide(const char*s,uint16_t*out,uint32_t size){uint32_t n=s?(uint32_t)kstrlen(s):0U;if(!out||!size)return n;uint32_t c=n<size-1U?n:size-1U;for(uint32_t i=0;i<c;i++)out[i]=(uint8_t)s[i];out[c]=0;return c;}
static void trim(char*s){uint32_t n=0,start=0;if(!s)return;while(s[start]==' '||s[start]=='\t')start++;if(start)while(s[start+n]){s[n]=s[start+n];n++;}else n=(uint32_t)kstrlen(s);s[n]='\0';while(n&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n'))s[--n]='\0';}
static bool path_from_name(const char*name,char*out,uint32_t size){if(!out||size<2U)return false;if(!name||!*name){kstrncpy(out,PROFILE_DEFAULT_PATH,size-1U);return true;}uint32_t p=0;if(name[0]!='/'&&!(name[1]==':'&&name[2]))out[p++]='/';if(name[1]==':')name+=2;for(;*name&&p+1U<size;name++)out[p++]=*name=='\\'?'/':*name;out[p]='\0';return *name=='\0';}
static profile_cache_t *profile_cache_get(const char *path, bool create) {
    profile_cache_t *free_slot = NULL;
    void *data = NULL;
    uint32_t size = 0;

    if (!path) return NULL;
    for (uint32_t i = 0; i < PROFILE_CACHE_SLOTS; i++) {
        if (g_profile_cache[i].used) {
            if (equal_ci(g_profile_cache[i].path, path))
                return &g_profile_cache[i];
        } else if (!free_slot) {
            free_slot = &g_profile_cache[i];
        }
    }
    if (!create || !free_slot) return NULL;

    free_slot->text = (char *)kzalloc(PROFILE_MAX_FILE);
    if (!free_slot->text) return NULL;
    free_slot->used = true;
    free_slot->dirty = false;
    kstrncpy(free_slot->path, path, sizeof(free_slot->path) - 1U);
    free_slot->path[sizeof(free_slot->path) - 1U] = '\0';

    if (vfs_read_all(path, &data, &size)) {
        if (size >= PROFILE_MAX_FILE) size = PROFILE_MAX_FILE - 1U;
        if (size) kmemcpy(free_slot->text, data, size);
        free_slot->text[size] = '\0';
        free_slot->size = size;
        kfree(data);
    }
    return free_slot;
}

static char *read_profile(const char *path, uint32_t *size) {
    profile_cache_t *slot = profile_cache_get(path, true);
    char *copy;

    if (!slot) return NULL;
    copy = (char *)kmalloc(slot->size + 1U);
    if (!copy) return NULL;
    if (slot->size) kmemcpy(copy, slot->text, slot->size);
    copy[slot->size] = '\0';
    if (size) *size = slot->size;
    return copy;
}

static bool profile_cache_store(const char *path, const char *text,
                                uint32_t size) {
    profile_cache_t *slot = profile_cache_get(path, true);
    if (!slot || !slot->text || size >= PROFILE_MAX_FILE) return false;
    if (size) kmemcpy(slot->text, text, size);
    slot->text[size] = '\0';
    slot->size = size;
    slot->dirty = true;
    return true;
}
static uint32_t copy_result(const char*src,const char*def,char*out,uint32_t size){const char*s=src?src:(def?def:"");uint32_t n=(uint32_t)kstrlen(s);if(!out||!size)return 0;uint32_t c=n<size-1U?n:size-1U;kmemcpy(out,s,c);out[c]='\0';return c;}
static uint32_t profile_get_a(const char*section,const char*key,const char*def,char*out,uint32_t size,const char*filename){char path[VFS_MAX_PATH];uint32_t fs=0;if(!path_from_name(filename,path,sizeof(path)))return copy_result(NULL,def,out,size);char*text=read_profile(path,&fs);if(!text)return copy_result(NULL,def,out,size);bool in_section=false;char*line=text;char found[1024];found[0]='\0';for(uint32_t i=0;i<=fs;i++){if(text[i]!='\n'&&text[i]!='\0')continue;text[i]='\0';char*cur=line;line=&text[i+1U];trim(cur);if(!*cur||*cur==';'||*cur=='#')continue;if(*cur=='['){char*end=cur+1;while(*end&&*end!=']')end++;if(*end)*end='\0';trim(cur+1);in_section=section&&equal_ci(cur+1,section);continue;}if(!in_section||!key)continue;char*eq=cur;while(*eq&&*eq!='=')eq++;if(!*eq)continue;*eq='\0';trim(cur);trim(eq+1);if(equal_ci(cur,key)){kstrncpy(found,eq+1,sizeof(found)-1U);break;}}kfree(text);return copy_result(found[0]?found:NULL,def,out,size);}
static bool append_bytes(char*out,uint32_t*used,uint32_t cap,const char*s){uint32_t n=(uint32_t)kstrlen(s);if(*used+n>=cap)return false;kmemcpy(out+*used,s,n);*used+=n;out[*used]='\0';return true;}
static int profile_write_a(const char*section,const char*key,const char*value,const char*filename){char path[VFS_MAX_PATH];uint32_t old_size=0;if(!section||!key||!path_from_name(filename,path,sizeof(path)))return 0;char*old=read_profile(path,&old_size);if(!old)return 0;char*out=(char*)kzalloc(PROFILE_MAX_FILE);if(!out){kfree(old);return 0;}uint32_t used=0;bool in_section=false,section_seen=false,written=false;char*line=old;for(uint32_t i=0;i<=old_size;i++){if(old[i]!='\n'&&old[i]!='\0')continue;old[i]='\0';char original[1024];kstrncpy(original,line,sizeof(original)-1U);char parsed[1024];kstrncpy(parsed,line,sizeof(parsed)-1U);line=&old[i+1U];trim(parsed);if(parsed[0]=='['){if(in_section&&!written&&value){append_bytes(out,&used,PROFILE_MAX_FILE,key);append_bytes(out,&used,PROFILE_MAX_FILE,"=");append_bytes(out,&used,PROFILE_MAX_FILE,value);append_bytes(out,&used,PROFILE_MAX_FILE,"\n");written=true;}char*end=parsed+1;while(*end&&*end!=']')end++;if(*end)*end='\0';trim(parsed+1);in_section=equal_ci(parsed+1,section);if(in_section)section_seen=true;}if(in_section&&parsed[0]&&parsed[0]!=';'&&parsed[0]!='#'&&parsed[0]!='['){char*eq=parsed;while(*eq&&*eq!='=')eq++;if(*eq){*eq='\0';trim(parsed);if(equal_ci(parsed,key)){if(value){append_bytes(out,&used,PROFILE_MAX_FILE,key);append_bytes(out,&used,PROFILE_MAX_FILE,"=");append_bytes(out,&used,PROFILE_MAX_FILE,value);append_bytes(out,&used,PROFILE_MAX_FILE,"\n");}written=true;continue;}}}append_bytes(out,&used,PROFILE_MAX_FILE,original);append_bytes(out,&used,PROFILE_MAX_FILE,"\n");}
if(!section_seen){append_bytes(out,&used,PROFILE_MAX_FILE,"[");append_bytes(out,&used,PROFILE_MAX_FILE,section);append_bytes(out,&used,PROFILE_MAX_FILE,"]\n");}
if(!written&&value){append_bytes(out,&used,PROFILE_MAX_FILE,key);append_bytes(out,&used,PROFILE_MAX_FILE,"=");append_bytes(out,&used,PROFILE_MAX_FILE,value);append_bytes(out,&used,PROFILE_MAX_FILE,"\n");}
bool cached = profile_cache_store(path, out, used);
bool persisted = vfs_write_all(path, out, used);
profile_cache_t *slot = profile_cache_get(path, false);
if (persisted && slot) slot->dirty = false;
kfree(out);
kfree(old);
/*
 * Los perfiles siguen siendo utilizables cuando el volumen activo es de
 * solo lectura (por ejemplo fd0). En ese caso se conserva un overlay en RAM
 * durante toda la sesion Win32.
 */
return (cached || persisted) ? 1 : 0;}
static uint32_t WIN32_API p_GetPrivateProfileStringA(const char*s,const char*k,const char*d,char*out,uint32_t n,const char*f){return profile_get_a(s,k,d,out,n,f);}
static uint32_t WIN32_API p_GetProfileStringA(const char*s,const char*k,const char*d,char*out,uint32_t n){return profile_get_a(s,k,d,out,n,NULL);}
static int WIN32_API p_WritePrivateProfileStringA(const char*s,const char*k,const char*v,const char*f){return profile_write_a(s,k,v,f);}
static int WIN32_API p_WriteProfileStringA(const char*s,const char*k,const char*v){return profile_write_a(s,k,v,NULL);}
static uint32_t parse_uint(const char*s,uint32_t def){uint32_t v=0;bool any=false;if(!s)return def;while(*s==' '||*s=='\t')s++;while(*s>='0'&&*s<='9'){any=true;v=v*10U+(uint32_t)(*s-'0');s++;}return any?v:def;}
static uint32_t WIN32_API p_GetPrivateProfileIntA(const char*s,const char*k,int d,const char*f){char b[32];profile_get_a(s,k,"",b,sizeof(b),f);return parse_uint(b,(uint32_t)d);}
static uint32_t WIN32_API p_GetProfileIntA(const char*s,const char*k,int d){return p_GetPrivateProfileIntA(s,k,d,NULL);}
static uint32_t WIN32_API p_GetPrivateProfileStringW(const uint16_t*s,const uint16_t*k,const uint16_t*d,uint16_t*out,uint32_t n,const uint16_t*f){char sa[128],ka[128],da[512],fa[VFS_MAX_PATH],tmp[1024];const char*sp=NULL,*kp=NULL,*dp=NULL,*fp=NULL;if(s){if(!wide_to_ansi(s,sa,sizeof(sa)))return 0;sp=sa;}if(k){if(!wide_to_ansi(k,ka,sizeof(ka)))return 0;kp=ka;}if(d){if(!wide_to_ansi(d,da,sizeof(da)))return 0;dp=da;}if(f){if(!wide_to_ansi(f,fa,sizeof(fa)))return 0;fp=fa;}uint32_t got=profile_get_a(sp,kp,dp,tmp,sizeof(tmp),fp);(void)got;return ansi_to_wide(tmp,out,n);}
static uint32_t WIN32_API p_GetProfileStringW(const uint16_t*s,const uint16_t*k,const uint16_t*d,uint16_t*out,uint32_t n){return p_GetPrivateProfileStringW(s,k,d,out,n,NULL);}
static int WIN32_API p_WritePrivateProfileStringW(const uint16_t*s,const uint16_t*k,const uint16_t*v,const uint16_t*f){char sa[128],ka[128],va[1024],fa[VFS_MAX_PATH];if(!wide_to_ansi(s,sa,sizeof(sa))||!wide_to_ansi(k,ka,sizeof(ka)))return 0;const char*vp=NULL,*fp=NULL;if(v){if(!wide_to_ansi(v,va,sizeof(va)))return 0;vp=va;}if(f){if(!wide_to_ansi(f,fa,sizeof(fa)))return 0;fp=fa;}return profile_write_a(sa,ka,vp,fp);}
static int WIN32_API p_WriteProfileStringW(const uint16_t*s,const uint16_t*k,const uint16_t*v){return p_WritePrivateProfileStringW(s,k,v,NULL);}
static uint32_t WIN32_API p_GetPrivateProfileIntW(const uint16_t*s,const uint16_t*k,int d,const uint16_t*f){uint16_t b[32];p_GetPrivateProfileStringW(s,k,NULL,b,32,f);char a[32];wide_to_ansi(b,a,sizeof(a));return parse_uint(a,(uint32_t)d);}
static uint32_t WIN32_API p_GetProfileIntW(const uint16_t*s,const uint16_t*k,int d){return p_GetPrivateProfileIntW(s,k,d,NULL);}
uint32_t win32_profile_resolve(const char*name){
#define P(x) if(equal_ci(name,#x))return(uint32_t)(uintptr_t)&p_##x
P(GetPrivateProfileStringA);P(GetPrivateProfileStringW);P(GetProfileStringA);P(GetProfileStringW);P(WritePrivateProfileStringA);P(WritePrivateProfileStringW);P(WriteProfileStringA);P(WriteProfileStringW);P(GetPrivateProfileIntA);P(GetPrivateProfileIntW);P(GetProfileIntA);P(GetProfileIntW);
#undef P
return 0;}
