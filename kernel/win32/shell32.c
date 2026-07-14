#include "win32.h"
#include "../include/types.h"
#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/pe_loader.h"
#include "../string.h"

#define SE_ERR_FNF 2U
#define SE_ERR_ACCESSDENIED 5U
#define SE_ERR_NOASSOC 31U
#define SHELL_SUCCESS 33U
#define SHELL_COMMAND_CHARS 520U

static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool equal_ci(const char*a,const char*b){uint8_t ca,cb;if(!a||!b)return false;do{ca=(uint8_t)*a++;cb=(uint8_t)*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return false;}while(ca);return true;}
static bool ends_ci(const char *text,const char *suffix){uint32_t a,b;if(!text||!suffix)return false;a=(uint32_t)kstrlen(text);b=(uint32_t)kstrlen(suffix);return a>=b&&equal_ci(text+a-b,suffix);}
static bool shell_native_path(const char *source,char *output,uint32_t capacity){
    uint32_t in=0,out=0;
    if(!source||!output||capacity<2U)return false;
    if(source[0]&&source[1]==':')in=2U;
    if(source[in]!='/'&&source[in]!='\\'){
        const char *cwd=vfs_getcwd();
        if(cwd&&*cwd){while(*cwd&&out+1U<capacity)output[out++]=*cwd++;if(out&&output[out-1U]!='/')output[out++]='/';}
    }
    while(source[in]&&out+1U<capacity){char c=source[in++];output[out++]=c=='\\'?'/':c;}
    output[out]='\0';
    return source[in]=='\0';
}
static bool shell_launch(const char *path,const char *arguments){
    char native[VFS_MAX_PATH], command[SHELL_COMMAND_CHARS];
    uint32_t used=0;
    if(!shell_native_path(path,native,sizeof(native)))return false;
    command[used++]='"';
    for(uint32_t i=0;path[i]&&used+2U<sizeof(command);i++)command[used++]=path[i];
    command[used++]='"';
    if(arguments&&*arguments&&used+2U<sizeof(command)){
        command[used++]=' ';
        for(uint32_t i=0;arguments[i]&&used+1U<sizeof(command);i++)command[used++]=arguments[i];
    }
    command[used]='\0';
    return pe_execute_program_command_line(native,command);
}
static uint32_t WIN32_API shell_DragQueryFileA(void *drop UNUSED,uint32_t index UNUSED,
                                               char *path,uint32_t size){if(path&&size)path[0]='\0';return 0;}
static void WIN32_API shell_DragFinish(void *drop UNUSED){}
static void *WIN32_API shell_ShellExecuteA(void *owner UNUSED,const char *verb,
                                           const char *file,const char *params,
                                           const char *directory,int show UNUSED){
    static const char *editors[]={"/CDROM/METAPAD.EXE","/SYSTEM/WIN32/METAPAD.EXE","/METAPAD.EXE"};
    void *probe=NULL;uint32_t probe_size=0;char native_dir[VFS_MAX_PATH];
    if(!file||!*file)return(void*)(uintptr_t)SE_ERR_FNF;
    if(verb&&*verb&&!equal_ci(verb,"open")&&!equal_ci(verb,"edit"))return(void*)(uintptr_t)SE_ERR_NOASSOC;
    if(directory&&*directory&&shell_native_path(directory,native_dir,sizeof(native_dir)))
        (void)vfs_chdir(native_dir);
    if(ends_ci(file,".EXE")||ends_ci(file,".COM"))
        return(void*)(uintptr_t)(shell_launch(file,params)?SHELL_SUCCESS:SE_ERR_FNF);
    if(ends_ci(file,".TXT")||ends_ci(file,".INI")||ends_ci(file,".LOG")||ends_ci(file,".C")||ends_ci(file,".H")){
        char quoted[VFS_MAX_PATH + 3U];uint32_t n=0;
        quoted[n++]='"';for(uint32_t i=0;file[i]&&n+2U<sizeof(quoted);i++)quoted[n++]=file[i];quoted[n++]='"';quoted[n]='\0';
        for(uint32_t i=0;i<sizeof(editors)/sizeof(editors[0]);i++){
            if(vfs_read_all(editors[i],&probe,&probe_size)){kfree(probe);return(void*)(uintptr_t)(shell_launch(editors[i],quoted)?SHELL_SUCCESS:SE_ERR_ACCESSDENIED);}
        }
        return(void*)(uintptr_t)SE_ERR_NOASSOC;
    }
    return(void*)(uintptr_t)SE_ERR_NOASSOC;
}
uint32_t win32_shell32_resolve(const char *name){
#define S(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&shell_##api
    S(DragQueryFileA);S(DragFinish);S(ShellExecuteA);
#undef S
    return 0;
}
