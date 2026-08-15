#include "win32.h"
#include "process_handle.h"
#include "../include/types.h"
#include "../include/vfs.h"
#include "../include/memory.h"
#include "../include/pe_loader.h"
#include "../include/elf_loader.h"
#include "../string.h"
#include "../stdio.h"
#include "resources.h"

#define SE_ERR_FNF 2U
#define SE_ERR_ACCESSDENIED 5U
#define SE_ERR_NOASSOC 31U
#define SHELL_SUCCESS 33U
#define SHELL_COMMAND_CHARS 768U
#define HKEY_CLASSES_ROOT 0x80000000U
#define SEE_MASK_NOCLOSEPROCESS 0x00000040U
#define BIF_RETURNONLYFSDIRS 0x00000001U
#define BIF_NEWDIALOGSTYLE 0x00000040U

typedef struct PACKED { uint32_t offset; int32_t x,y; int non_client; int wide; } drop_files_t;
typedef struct { char path[VFS_MAX_PATH]; } shell_pidl_t;
typedef struct PACKED {
    void *owner, *root; char *display_name; const char *title;
    uint32_t flags; void *callback; int32_t param; int image;
} browse_info_a_t;
typedef struct PACKED {
    void *owner, *root; uint16_t *display_name; const uint16_t *title;
    uint32_t flags; void *callback; int32_t param; int image;
} browse_info_w_t;
typedef struct PACKED {
    uint32_t size,mask;void*hwnd;const char*verb,*file,*parameters,*directory;
    int show;void*instance;void*id_list;const char*class_name;void*class_key;
    uint32_t hot_key;void*icon;void*process;
} shell_execute_info_a_t;
typedef struct PACKED {
    uint32_t size,mask;void*hwnd;const uint16_t*verb,*file,*parameters,*directory;
    int show;void*instance;void*id_list;const uint16_t*class_name;void*class_key;
    uint32_t hot_key;void*icon;void*process;
} shell_execute_info_w_t;
typedef struct{void*icon;int icon_index;uint32_t attributes;char display_name[260];char type_name[80];}shell_file_info_a_t;
typedef struct{void*icon;int icon_index;uint32_t attributes;uint16_t display_name[260];uint16_t type_name[80];}shell_file_info_w_t;
typedef struct PACKED{void*hwnd;uint32_t function;const char*from,*to;uint16_t flags;int aborted;void*mappings;const char*title;}sh_file_op_a_t;
typedef struct PACKED{void*hwnd;uint32_t function;const uint16_t*from,*to;uint16_t flags;int aborted;void*mappings;const uint16_t*title;}sh_file_op_w_t;

static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool equal_ci(const char*a,const char*b){uint8_t ca,cb;if(!a||!b)return false;do{ca=(uint8_t)*a++;cb=(uint8_t)*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return false;}while(ca);return true;}
static bool ends_ci(const char *text,const char *suffix){uint32_t a,b;if(!text||!suffix)return false;a=(uint32_t)kstrlen(text);b=(uint32_t)kstrlen(suffix);return a>=b&&equal_ci(text+a-b,suffix);}
static bool contains(const char *text,const char *needle){uint32_t n;if(!text||!needle)return false;n=(uint32_t)kstrlen(needle);if(!n)return true;for(uint32_t i=0;text[i];i++){uint32_t j=0;while(j<n&&text[i+j]&&text[i+j]==needle[j])j++;if(j==n)return true;}return false;}
static bool wide_to_ansi(const uint16_t*wide,char*out,uint32_t capacity){uint32_t i=0;if(!wide||!out||!capacity)return false;while(wide[i]&&i+1U<capacity){out[i]=wide[i]<=0xFFU?(char)wide[i]:'?';i++;}out[i]=0;return wide[i]==0;}
static void ansi_to_wide(const char*text,uint16_t*out,uint32_t capacity){uint32_t i=0;if(!out||!capacity)return;while(text&&text[i]&&i+1U<capacity){out[i]=(uint8_t)text[i];i++;}out[i]=0;}
static bool shell_native_path(const char *source,char *output,uint32_t capacity){
    uint32_t in=0,out=0;if(!source||!output||capacity<2U)return false;if(source[0]&&source[1]==':')in=2U;
    if(source[in]!='/'&&source[in]!='\\'){const char *cwd=vfs_getcwd();if(cwd&&*cwd){while(*cwd&&out+1U<capacity)output[out++]=*cwd++;if(out&&output[out-1U]!='/')output[out++]='/';}}
    while(source[in]&&out+1U<capacity){char c=source[in++];output[out++]=c=='\\'?'/':c;}output[out]='\0';return source[in]=='\0';
}
static const char *file_extension(const char *file){const char*base=file,*dot=NULL;if(!file)return NULL;for(const char*p=file;*p;p++){if(*p=='/'||*p=='\\'){base=p+1;dot=NULL;}else if(*p=='.')dot=p;}return dot&&dot>base?dot:NULL;}
static void quote_argument(const char *text,char*out,uint32_t capacity){uint32_t n=0;if(!out||!capacity)return;if(n+1U<capacity)out[n++]='"';for(uint32_t i=0;text&&text[i]&&n+2U<capacity;i++){if(text[i]=='"'&&n+2U<capacity)out[n++]='\\';out[n++]=text[i];}if(n+1U<capacity)out[n++]='"';out[n]=0;}
static bool command_executable(const char *command,char*out,uint32_t capacity){uint32_t i=0,n=0;bool quote=false;if(!command||!out||capacity<2U)return false;while(command[i]==' '||command[i]=='\t')i++;if(command[i]=='"'){quote=true;i++;}while(command[i]&&n+1U<capacity){char c=command[i++];if(quote){if(c=='"')break;}else if(c==' '||c=='\t')break;out[n++]=c;}out[n]=0;return n!=0U;}
static bool association_command(const char *file,const char *verb,char*out,uint32_t capacity){
    const char *ext=file_extension(file);char class_name[128],key[256],default_verb[40];
    if(!ext||!out||!capacity)return false;
    if(!win32_registry_query_string(HKEY_CLASSES_ROOT,ext,NULL,class_name,sizeof(class_name)))return false;
    if(!verb||!*verb){kstrcpy(key,class_name);kstrcat(key,"\\shell");if(!win32_registry_query_string(HKEY_CLASSES_ROOT,key,NULL,default_verb,sizeof(default_verb)))kstrcpy(default_verb,"open");verb=default_verb;}
    kstrcpy(key,class_name);kstrcat(key,"\\shell\\");if(kstrlen(key)+kstrlen(verb)+9U>=sizeof(key))return false;kstrcat(key,verb);kstrcat(key,"\\command");
    return win32_registry_query_string(HKEY_CLASSES_ROOT,key,NULL,out,capacity);
}
static bool expand_command(const char *template,const char *file,const char *params,char*out,uint32_t capacity){
    char quoted[VFS_MAX_PATH+8U];uint32_t n=0;bool used_file=false;quote_argument(file,quoted,sizeof(quoted));
    for(uint32_t i=0;template&&template[i]&&n+1U<capacity;i++){
        if(template[i]=='%'&&template[i+1]){char code=template[++i];const char*insert=NULL;if(code=='1'||code=='L'||code=='l'){insert=quoted;used_file=true;}else if(code=='*')insert=params?params:"";else if(code=='%')insert="%";if(insert){for(uint32_t j=0;insert[j]&&n+1U<capacity;j++)out[n++]=insert[j];continue;}if(n+2U<capacity){out[n++]='%';out[n++]=code;}continue;}
        out[n++]=template[i];
    }
    if(!used_file&&n+2U<capacity){out[n++]=' ';for(uint32_t i=0;quoted[i]&&n+1U<capacity;i++)out[n++]=quoted[i];}
    if(params&&*params&&(!template||!contains(template,"%*"))&&n+2U<capacity){out[n++]=' ';for(uint32_t i=0;params[i]&&n+1U<capacity;i++)out[n++]=params[i];}
    out[n]=0;return template&&template[0]&&n+1U<capacity;
}
static bool shell_launch_command(const char *command,uint32_t *pid_out){char exe[VFS_MAX_PATH],native[VFS_MAX_PATH];if(pid_out)*pid_out=0;if(!command_executable(command,exe,sizeof(exe))||!shell_native_path(exe,native,sizeof(native)))return false;if(pe_execute_program_command_line_ex(native,command,pid_out))return true;if(!file_extension(exe)&&kstrlen(native)+4U<sizeof(native)){kstrcat(native,".exe");if(pe_execute_program_command_line_ex(native,command,pid_out))return true;}return false;}
static bool shell_resolve(const char *verb,const char *file,const char *params,char*command,uint32_t capacity){
    static const char *editors[]={"C:\\SYSTEM\\WIN32\\METAPAD.EXE","C:\\METAPAD.EXE"};char templ[SHELL_COMMAND_CHARS];
    if(ends_ci(file,".EXE")||ends_ci(file,".COM")||!file_extension(file)){uint32_t n=0;command[n++]='"';for(uint32_t i=0;file[i]&&n+2U<capacity;i++)command[n++]=file[i];command[n++]='"';if(params&&*params&&n+2U<capacity){command[n++]=' ';for(uint32_t i=0;params[i]&&n+1U<capacity;i++)command[n++]=params[i];}command[n]=0;return true;}
    if(association_command(file,verb&&*verb?verb:NULL,templ,sizeof(templ)))return expand_command(templ,file,params,command,capacity);
    if(ends_ci(file,".TXT")||ends_ci(file,".INI")||ends_ci(file,".LOG")||ends_ci(file,".C")||ends_ci(file,".H")){for(uint32_t i=0;i<sizeof(editors)/sizeof(editors[0]);i++){char native[VFS_MAX_PATH];void*data=NULL;uint32_t size=0;if(shell_native_path(editors[i],native,sizeof(native))&&vfs_read_all(native,&data,&size)){kfree(data);kstrcpy(templ,"\"");kstrcat(templ,editors[i]);kstrcat(templ,"\" %1");return expand_command(templ,file,params,command,capacity);}}}
    return false;
}
static uint32_t shell_execute_core(const char *verb,const char *file,const char *params,const char *directory,uint32_t *pid_out){char command[SHELL_COMMAND_CHARS],native_dir[VFS_MAX_PATH];if(pid_out)*pid_out=0;if(!file||!*file)return SE_ERR_FNF;if(directory&&*directory&&shell_native_path(directory,native_dir,sizeof(native_dir)))(void)vfs_chdir(native_dir);if(!shell_resolve(verb,file,params,command,sizeof(command)))return SE_ERR_NOASSOC;return shell_launch_command(command,pid_out)?SHELL_SUCCESS:SE_ERR_ACCESSDENIED;}

static uint32_t WIN32_API shell_DragQueryFileA(void *drop,uint32_t index,char *path,uint32_t size){drop_files_t*header=(drop_files_t*)drop;uint8_t*base=(uint8_t*)drop;uint32_t count=0;if(path&&size)path[0]=0;if(!header||header->offset<sizeof(*header))return 0;if(header->wide){uint16_t*p=(uint16_t*)(base+header->offset);while(*p){uint32_t len=0;while(p[len])len++;if(index==count&&path&&size){uint32_t n=len<size-1U?len:size-1U;for(uint32_t i=0;i<n;i++)path[i]=(char)p[i];path[n]=0;return len;}count++;p+=len+1U;}}else{char*p=(char*)(base+header->offset);while(*p){uint32_t len=(uint32_t)kstrlen(p);if(index==count&&path&&size){kstrncpy(path,p,size-1U);path[size-1U]=0;return len;}count++;p+=len+1U;}}return index==0xFFFFFFFFU?count:0U;}
static uint32_t WIN32_API shell_DragQueryFileW(void *drop,uint32_t index,uint16_t *path,uint32_t size){char ansi[VFS_MAX_PATH];uint32_t len=shell_DragQueryFileA(drop,index,ansi,sizeof(ansi));if(path&&size)ansi_to_wide(ansi,path,size);return len;}
static void WIN32_API shell_DragFinish(void *drop UNUSED){}
static void *WIN32_API shell_ShellExecuteA(void *owner UNUSED,const char *verb,const char *file,const char *params,const char *directory,int show UNUSED){return(void*)(uintptr_t)shell_execute_core(verb,file,params,directory,NULL);}
static void *WIN32_API shell_ShellExecuteW(void *owner,const uint16_t*verb,const uint16_t*file,const uint16_t*params,const uint16_t*directory,int show){char v[40],f[VFS_MAX_PATH],p[VFS_MAX_PATH],d[VFS_MAX_PATH];if(!file||!wide_to_ansi(file,f,sizeof(f)))return(void*)(uintptr_t)SE_ERR_FNF;if(verb&&!wide_to_ansi(verb,v,sizeof(v)))return(void*)(uintptr_t)SE_ERR_NOASSOC;if(params&&!wide_to_ansi(params,p,sizeof(p)))return(void*)(uintptr_t)SE_ERR_ACCESSDENIED;if(directory&&!wide_to_ansi(directory,d,sizeof(d)))return(void*)(uintptr_t)SE_ERR_ACCESSDENIED;return shell_ShellExecuteA(owner,verb?v:NULL,f,params?p:NULL,directory?d:NULL,show);}
static int WIN32_API shell_ShellExecuteExA(shell_execute_info_a_t*info){uint32_t pid=0,result;if(!info||info->size<36U)return 0;result=shell_execute_core(info->verb,info->file,info->parameters,info->directory,&pid);info->instance=(void*)(uintptr_t)result;info->process=NULL;if(result<=32U)return 0;if((info->mask&SEE_MASK_NOCLOSEPROCESS)&&pid)info->process=win32_process_handle_open(pid);return 1;}
static int WIN32_API shell_ShellExecuteExW(shell_execute_info_w_t*info){shell_execute_info_a_t a;char v[40],f[VFS_MAX_PATH],p[VFS_MAX_PATH],d[VFS_MAX_PATH],c[128];if(!info||info->size<36U||!info->file||!wide_to_ansi(info->file,f,sizeof(f)))return 0;kmemset(&a,0,sizeof(a));a.size=sizeof(a);a.mask=info->mask;a.hwnd=info->hwnd;a.show=info->show;a.verb=info->verb&&wide_to_ansi(info->verb,v,sizeof(v))?v:NULL;a.file=f;a.parameters=info->parameters&&wide_to_ansi(info->parameters,p,sizeof(p))?p:NULL;a.directory=info->directory&&wide_to_ansi(info->directory,d,sizeof(d))?d:NULL;a.class_name=info->class_name&&wide_to_ansi(info->class_name,c,sizeof(c))?c:NULL;if(!shell_ShellExecuteExA(&a)){info->instance=a.instance;return 0;}info->instance=a.instance;info->process=a.process;return 1;}

static const char*special_folder(uint32_t id){switch(id&0xFFU){case 0:case 16:return"C:\\DESKTOP";case 2:return"C:\\SYSTEM\\PROGRAMS";case 5:return"C:\\DOCUMENTS";case 6:return"C:\\FAVORITES";case 7:return"C:\\SYSTEM\\STARTUP";case 8:return"C:\\RECENT";case 9:return"C:\\SENDTO";case 11:return"C:\\STARTMENU";case 26:case 28:return"C:\\SYSTEM\\APPDATA";case 35:return"C:\\SYSTEM\\COMMON";case 36:return"C:\\SYSTEM";case 37:return"C:\\SYSTEM\\LIBS\\WINE";case 38:return"C:\\PROGRAM FILES";default:return"C:\\";}}
static int WIN32_API shell_SHGetSpecialFolderPathA(void*hwnd UNUSED,char*out,int folder,int create){const char*path=special_folder((uint32_t)folder);char native[VFS_MAX_PATH];if(!out)return 0;kstrcpy(out,path);if(create&&shell_native_path(path,native,sizeof(native)))(void)vfs_mkdir(native);return 1;}
static int32_t WIN32_API shell_SHGetFolderPathA(void*hwnd UNUSED,int folder,void*token UNUSED,uint32_t flags UNUSED,char*out){return shell_SHGetSpecialFolderPathA(NULL,out,folder,0)?0:(int32_t)0x80070003U;}
static int WIN32_API shell_SHGetSpecialFolderPathW(void*hwnd,uint16_t*out,int folder,int create){char ansi[VFS_MAX_PATH];if(!out||!shell_SHGetSpecialFolderPathA(hwnd,ansi,folder,create))return 0;ansi_to_wide(ansi,out,VFS_MAX_PATH);return 1;}
static int32_t WIN32_API shell_SHGetFolderPathW(void*hwnd,int folder,void*token,uint32_t flags,uint16_t*out){char ansi[VFS_MAX_PATH];int32_t result=shell_SHGetFolderPathA(hwnd,folder,token,flags,ansi);if(!result&&out)ansi_to_wide(ansi,out,VFS_MAX_PATH);return result;}
static int WIN32_API shell_SHCreateDirectoryExA(void*hwnd UNUSED,const char*path,void*security UNUSED){char native[VFS_MAX_PATH];return shell_native_path(path,native,sizeof(native))&&vfs_mkdir(native)?0:183;}
static int WIN32_API shell_SHCreateDirectoryExW(void*hwnd,const uint16_t*path,void*security){char ansi[VFS_MAX_PATH];return path&&wide_to_ansi(path,ansi,sizeof(ansi))?shell_SHCreateDirectoryExA(hwnd,ansi,security):87;}
static void *shell_browse(bool wide,void *raw){char selected[VFS_MAX_PATH];const char*title="Seleccionar carpeta";shell_pidl_t*pidl;if(wide){browse_info_w_t*i=(browse_info_w_t*)raw;char t[128];if(!i)return NULL;if(i->title&&wide_to_ansi(i->title,t,sizeof(t)))title=t;if(!win32_user_path_dialog(title,selected,sizeof(selected),false))return NULL;if(i->display_name)ansi_to_wide(selected,i->display_name,260U);}else{browse_info_a_t*i=(browse_info_a_t*)raw;if(!i)return NULL;if(i->title)title=i->title;if(!win32_user_path_dialog(title,selected,sizeof(selected),false))return NULL;if(i->display_name){kstrncpy(i->display_name,selected,259U);i->display_name[259]=0;}}pidl=(shell_pidl_t*)kzalloc(sizeof(*pidl));if(!pidl)return NULL;kstrncpy(pidl->path,selected,sizeof(pidl->path)-1U);return pidl;}
static void*WIN32_API shell_SHBrowseForFolderA(browse_info_a_t*info){return shell_browse(false,info);}
static void*WIN32_API shell_SHBrowseForFolderW(browse_info_w_t*info){return shell_browse(true,info);}
static int WIN32_API shell_SHGetPathFromIDListA(const shell_pidl_t*pidl,char*out){if(!pidl||!out)return 0;kstrcpy(out,pidl->path);return 1;}
static int WIN32_API shell_SHGetPathFromIDListW(const shell_pidl_t*pidl,uint16_t*out){if(!pidl||!out)return 0;ansi_to_wide(pidl->path,out,VFS_MAX_PATH);return 1;}
static void WIN32_API shell_ILFree(void*pidl){if(pidl)kfree(pidl);}

static void*WIN32_API shell_ExtractIconA(void*instance,const char*file UNUSED,uint32_t index){if(index==0xFFFFFFFFU)return(void*)(uintptr_t)1U;return win32_icon_load(instance,(const void*)(uintptr_t)(index+1U),false,32,32);}
static void*WIN32_API shell_ExtractIconW(void*instance,const uint16_t*file,uint32_t index){char ansi[VFS_MAX_PATH];if(file&&!wide_to_ansi(file,ansi,sizeof(ansi)))return NULL;return shell_ExtractIconA(instance,file?ansi:NULL,index);}
static uint32_t WIN32_API shell_ExtractIconExA(const char*file,int index,void**large,void**small,uint32_t count){void*icon;if(index==-1)return 1U;if(!count)return 0;icon=shell_ExtractIconA(NULL,file,(uint32_t)index);if(large)*large=icon;if(small)*small=icon?win32_icon_copy(icon):NULL;if(!large&&icon)win32_icon_destroy(icon);return(large&&*large)||(small&&*small)?1U:0U;}
static uint32_t WIN32_API shell_ExtractIconExW(const uint16_t*file,int index,void**large,void**small,uint32_t count){char ansi[VFS_MAX_PATH];if(file&&!wide_to_ansi(file,ansi,sizeof(ansi)))return 0;return shell_ExtractIconExA(file?ansi:NULL,index,large,small,count);}
static void*WIN32_API shell_ExtractAssociatedIconA(void*instance,char*path,uint16_t*index){return shell_ExtractIconA(instance,path,index?*index:0U);}
static void*WIN32_API shell_ExtractAssociatedIconW(void*instance,uint16_t*path,uint16_t*index){return shell_ExtractIconW(instance,path,index?*index:0U);}
static bool association_type(const char*path,char*out,uint32_t capacity){const char*ext=file_extension(path);char class_name[128];if(!ext||!win32_registry_query_string(HKEY_CLASSES_ROOT,ext,NULL,class_name,sizeof(class_name)))return false;return win32_registry_query_string(HKEY_CLASSES_ROOT,class_name,NULL,out,capacity);}
static uintptr_t WIN32_API shell_SHGetFileInfoA(
    const char *path, uint32_t attributes, shell_file_info_a_t *info,
    uint32_t size, uint32_t flags) {
    const uint32_t SHGFI_ICON              = 0x00000100U;
    const uint32_t SHGFI_DISPLAYNAME       = 0x00000200U;
    const uint32_t SHGFI_TYPENAME          = 0x00000400U;
    const uint32_t SHGFI_ATTRIBUTES        = 0x00000800U;
    const uint32_t SHGFI_EXETYPE           = 0x00002000U;
    const uint32_t SHGFI_SYSICONINDEX      = 0x00004000U;
    const uint32_t SHGFI_SMALLICON         = 0x00000001U;
    const uint32_t SHGFI_USEFILEATTRIBUTES = 0x00000010U;
    const char *name = path ? path : "";

    kprintf("[WIN32:startup] SHGetFileInfoA path=%s attrs=%x size=%u flags=%x out=%x\\n",
            path ? path : "(null)", attributes, size, flags,
            (uint32_t)(uintptr_t)info);

    /*
     * SHGFI_EXETYPE es una consulta especial: Windows permite psfi=NULL
     * y cbFileInfo=0. WinZip usa esta forma durante su inicialización.
     */
    if (flags & SHGFI_EXETYPE) {
        if (!path || !path[0]) return 0U;

        if (ends_ci(path, ".EXE")) {
            /*
             * LOWORD = firma NE/PE aproximada; HIWORD = versión de Windows.
             * 0x00004550 equivale a la firma "PE  ".
             */
            return 0x00004550U;
        }

        if (ends_ci(path, ".COM") || ends_ci(path, ".BAT") ||
            ends_ci(path, ".PIF"))
            return 0x00005A4DU;

        return 0U;
    }

    if (!info || size < sizeof(*info)) return 0U;

    kmemset(info, 0, sizeof(*info));

    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;

    if (flags & SHGFI_ICON) {
        int icon_size = (flags & SHGFI_SMALLICON) ? 16 : 32;
        info->icon = win32_icon_load(
            NULL, (const void *)(uintptr_t)32512U, false,
            icon_size, icon_size);
    }

    if (flags & SHGFI_DISPLAYNAME)
        kstrncpy(info->display_name, name,
                 sizeof(info->display_name) - 1U);

    if (flags & SHGFI_TYPENAME) {
        if (!association_type(path, info->type_name,
                              sizeof(info->type_name))) {
            const char *type =
                ends_ci(name, ".EXE") ? "Aplicacion" :
                ends_ci(name, ".DLL") ? "Biblioteca" :
                ends_ci(name, ".ZIP") ? "Archivo ZIP" :
                                        "Archivo";

            kstrncpy(info->type_name, type,
                     sizeof(info->type_name) - 1U);
        }
    }

    if (flags & SHGFI_ATTRIBUTES) {
        /*
         * Con USEFILEATTRIBUTES, attributes viene directamente del caller.
         * Sin esa bandera, por ahora devolvemos igualmente el valor recibido
         * hasta que Shell32 consulte atributos reales mediante VFS.
         */
        info->attributes = attributes;
    } else if (flags & SHGFI_USEFILEATTRIBUTES) {
        info->attributes = attributes;
    }

    if (flags & SHGFI_SYSICONINDEX) {
        if (ends_ci(name, ".EXE")) info->icon_index = 2;
        else if (ends_ci(name, ".DLL")) info->icon_index = 3;
        else if (ends_ci(name, ".ZIP")) info->icon_index = 4;
        else info->icon_index = 1;

        /*
         * Debe representar el image list del sistema. BlesKernOS todavía no
         * tiene uno compatible, pero un handle estable no nulo es suficiente
         * para aplicaciones que sólo consultan el índice.
         */
        return 1U;
    }

    return info->icon ? (uintptr_t)info->icon : 1U;
}
static uintptr_t WIN32_API shell_SHGetFileInfoW(const uint16_t*path,uint32_t attributes,shell_file_info_w_t*info,uint32_t size,uint32_t flags){char ansi[VFS_MAX_PATH];shell_file_info_a_t value;uintptr_t result;if(!path||!wide_to_ansi(path,ansi,sizeof(ansi))||!info||size<sizeof(*info))return 0;result=shell_SHGetFileInfoA(ansi,attributes,&value,sizeof(value),flags);kmemset(info,0,sizeof(*info));info->icon=value.icon;info->icon_index=value.icon_index;info->attributes=value.attributes;ansi_to_wide(value.display_name,info->display_name,260U);ansi_to_wide(value.type_name,info->type_name,80U);return result;}
static void*WIN32_API shell_FindExecutableA(const char*file,const char*directory UNUSED,char*out){char command[SHELL_COMMAND_CHARS];if(!file||!out)return(void*)(uintptr_t)SE_ERR_FNF;if(!shell_resolve("open",file,NULL,command,sizeof(command))||!command_executable(command,out,VFS_MAX_PATH))return(void*)(uintptr_t)SE_ERR_NOASSOC;return(void*)(uintptr_t)SHELL_SUCCESS;}
static void*WIN32_API shell_FindExecutableW(const uint16_t*file,const uint16_t*directory,uint16_t*out){char f[VFS_MAX_PATH],d[VFS_MAX_PATH],exe[VFS_MAX_PATH];void*result;if(!file||!out||!wide_to_ansi(file,f,sizeof(f)))return(void*)(uintptr_t)SE_ERR_FNF;result=shell_FindExecutableA(f,directory&&wide_to_ansi(directory,d,sizeof(d))?d:NULL,exe);if((uint32_t)(uintptr_t)result>32U)ansi_to_wide(exe,out,VFS_MAX_PATH);return result;}
static uint32_t wide_length(const uint16_t *text) {
    uint32_t length = 0U;
    if (text) while (text[length]) length++;
    return length;
}

/*
 * Derived from Wine's dlls/shcore/main.c, CommandLineToArgvW(), commit
 * 84fe968b936c1b0b656157d7487844eba65c09a7.  The parsing algorithm is
 * LGPL-2.1-or-later; see THIRD_PARTY_LICENSES.md.  It is adapted to the
 * BlesKernOS allocator and uint16_t Win32 strings.
 *
 * The old implementation merely toggled quotes.  That breaks Win9x-era
 * installers that pass an escaped quote or a run of backslashes before one.
 */
static uint16_t **WIN32_API shell_CommandLineToArgvW(
    const uint16_t *command, int *argument_count) {
    const uint16_t *source;
    uint16_t **argv, *destination;
    uint32_t argc, quote_count, slash_count, length;

    if (!argument_count || !command) return NULL;
    *argument_count = 0;
    length = wide_length(command);

    /* First count arguments.  The executable name follows special rules. */
    argc = 1U;
    source = command;
    if (*source == '"') {
        source++;
        while (*source) if (*source++ == '"') break;
    } else {
        while (*source && *source != ' ' && *source != '\t') source++;
    }
    while (*source == ' ' || *source == '\t') source++;
    if (*source) argc++;

    quote_count = slash_count = 0U;
    while (*source) {
        if ((*source == ' ' || *source == '\t') && quote_count == 0U) {
            while (*source == ' ' || *source == '\t') source++;
            if (*source) argc++;
            slash_count = 0U;
        } else if (*source == '\\') {
            slash_count++;
            source++;
        } else if (*source == '"') {
            if ((slash_count & 1U) == 0U) quote_count++;
            source++;
            slash_count = 0U;
            while (*source == '"') {
                quote_count++;
                source++;
            }
            quote_count %= 3U;
            if (quote_count == 2U) quote_count = 0U;
        } else {
            slash_count = 0U;
            source++;
        }
    }

    argv = (uint16_t **)kzalloc((argc + 1U) * sizeof(*argv) +
                                (length + 1U) * sizeof(uint16_t));
    if (!argv) return NULL;

    destination = (uint16_t *)(argv + argc + 1U);
    argv[0] = destination;
    source = command;
    argc = 1U;

    if (*source == '"') {
        source++;
        while (*source && *source != '"') *destination++ = *source++;
        if (*source == '"') source++;
    } else {
        while (*source && *source != ' ' && *source != '\t')
            *destination++ = *source++;
    }
    *destination++ = 0;
    while (*source == ' ' || *source == '\t') source++;
    if (!*source) {
        argv[argc] = NULL;
        *argument_count = (int)argc;
        return argv;
    }

    argv[argc++] = destination;
    quote_count = slash_count = 0U;
    while (*source) {
        if ((*source == ' ' || *source == '\t') && quote_count == 0U) {
            *destination++ = 0;
            slash_count = 0U;
            do { source++; } while (*source == ' ' || *source == '\t');
            if (*source) argv[argc++] = destination;
        } else if (*source == '\\') {
            *destination++ = *source++;
            slash_count++;
        } else if (*source == '"') {
            if ((slash_count & 1U) == 0U) {
                destination -= slash_count / 2U;
                quote_count++;
            } else {
                destination = destination - slash_count / 2U - 1U;
                *destination++ = '"';
            }
            source++;
            slash_count = 0U;
            while (*source == '"') {
                if (++quote_count == 3U) {
                    *destination++ = '"';
                    quote_count = 0U;
                }
                source++;
            }
            if (quote_count == 2U) quote_count = 0U;
        } else {
            *destination++ = *source++;
            slash_count = 0U;
        }
    }
    *destination = 0;
    argv[argc] = NULL;
    *argument_count = (int)argc;
    return argv;
}
static bool shell_copy(const char*from,const char*to){char a[VFS_MAX_PATH],b[VFS_MAX_PATH];void*data=NULL;uint32_t size=0;bool ok;if(!shell_native_path(from,a,sizeof(a))||!shell_native_path(to,b,sizeof(b))||!vfs_read_all(a,&data,&size))return false;ok=vfs_write_all(b,data,size);kfree(data);return ok;}
static int WIN32_API shell_SHFileOperationA(sh_file_op_a_t*op){char from[VFS_MAX_PATH],to[VFS_MAX_PATH];bool ok=false;if(!op||!op->from)return 87;if(op->to&&!shell_native_path(op->to,to,sizeof(to)))return 87;if(!shell_native_path(op->from,from,sizeof(from)))return 87;switch(op->function){case 1:ok=op->to&&vfs_rename(from,to);break;case 2:ok=op->to&&shell_copy(op->from,op->to);break;case 3:ok=vfs_remove(from);break;case 4:ok=op->to&&vfs_rename(from,to);break;}op->aborted=ok?0:1;return ok?0:1;}
static int WIN32_API shell_SHFileOperationW(sh_file_op_w_t*op){sh_file_op_a_t a;char from[VFS_MAX_PATH],to[VFS_MAX_PATH],title[128];if(!op||!op->from||!wide_to_ansi(op->from,from,sizeof(from)))return 87;kmemset(&a,0,sizeof(a));a.hwnd=op->hwnd;a.function=op->function;a.from=from;a.to=op->to&&wide_to_ansi(op->to,to,sizeof(to))?to:NULL;a.flags=op->flags;a.title=op->title&&wide_to_ansi(op->title,title,sizeof(title))?title:NULL;int result=shell_SHFileOperationA(&a);op->aborted=a.aborted;return result;}


/* BLES_WINE_DESKTOP_SHELLFOLDER_20260723 */
#define SHELL_S_OK          ((int32_t)0x00000000L)
#define SHELL_E_NOINTERFACE ((int32_t)0x80004002L)
#define SHELL_E_NOTIMPL     ((int32_t)0x80004001L)
#define SHELL_E_POINTER     ((int32_t)0x80004003L)

/*
 * IMalloc del shell.
 *
 * Las aplicaciones Win9x obtienen este objeto con SHGetMalloc y lo usan para
 * liberar PIDLs devueltos por SHGetSpecialFolderLocation y otras APIs del
 * shell. Debe ser un objeto COM real (puntero a vtable), no un handle opaco:
 * WinZip llama sus metodos directamente.
 */
typedef struct shell_malloc shell_malloc_t;

typedef struct {
    int32_t (WIN32_API *QueryInterface)(
        shell_malloc_t *, const void *, void **);
    uint32_t (WIN32_API *AddRef)(shell_malloc_t *);
    uint32_t (WIN32_API *Release)(shell_malloc_t *);
    void *(WIN32_API *Alloc)(shell_malloc_t *, uint32_t);
    void *(WIN32_API *Realloc)(shell_malloc_t *, void *, uint32_t);
    void (WIN32_API *Free)(shell_malloc_t *, void *);
    uint32_t (WIN32_API *GetSize)(shell_malloc_t *, void *);
    int32_t (WIN32_API *DidAlloc)(shell_malloc_t *, void *);
    void (WIN32_API *HeapMinimize)(shell_malloc_t *);
} shell_imalloc_vtbl_t;

struct shell_malloc {
    const shell_imalloc_vtbl_t *vtbl;
    uint32_t references;
};

static uint32_t WIN32_API shell_malloc_AddRef(shell_malloc_t *self);

static int32_t WIN32_API shell_malloc_QueryInterface(
    shell_malloc_t *self, const void *iid UNUSED, void **object) {
    if (!object) return SHELL_E_POINTER;
    *object = NULL;
    if (!self) return SHELL_E_NOINTERFACE;
    *object = self;
    shell_malloc_AddRef(self);
    return SHELL_S_OK;
}

static uint32_t WIN32_API shell_malloc_AddRef(shell_malloc_t *self) {
    if (!self) return 0U;
    return ++self->references;
}

static uint32_t WIN32_API shell_malloc_Release(shell_malloc_t *self) {
    if (!self) return 0U;
    /* El allocator es un singleton de SHELL32 y nunca se destruye. */
    if (self->references > 1U) self->references--;
    return self->references;
}

static void *WIN32_API shell_malloc_Alloc(
    shell_malloc_t *self UNUSED, uint32_t size) {
    return kmalloc(size);
}

static void *WIN32_API shell_malloc_Realloc(
    shell_malloc_t *self UNUSED, void *memory, uint32_t size) {
    return krealloc(memory, size);
}

static void WIN32_API shell_malloc_Free(
    shell_malloc_t *self UNUSED, void *memory) {
    kfree(memory);
}

static uint32_t WIN32_API shell_malloc_GetSize(
    shell_malloc_t *self UNUSED, void *memory) {
    size_t size = mm_allocation_size(memory);
    return size ? (uint32_t)size : 0xFFFFFFFFU;
}

static int32_t WIN32_API shell_malloc_DidAlloc(
    shell_malloc_t *self UNUSED, void *memory) {
    return mm_allocation_size(memory) ? 1 : 0;
}

static void WIN32_API shell_malloc_HeapMinimize(
    shell_malloc_t *self UNUSED) {
}

static shell_imalloc_vtbl_t shell_malloc_vtbl;

static shell_malloc_t shell_malloc = {
    &shell_malloc_vtbl,
    1U
};

static bool shell_malloc_prepare_ring3_vtbl(void) {
    if (shell_malloc_vtbl.QueryInterface) return true;
#define SHELL_MALLOC_THUNK(field, function) \
    shell_malloc_vtbl.field = (__typeof__(shell_malloc_vtbl.field))(uintptr_t) \
        elf_user_api_thunk("IMalloc::" #field, \
                           (uint32_t)(uintptr_t)&function)
    SHELL_MALLOC_THUNK(QueryInterface, shell_malloc_QueryInterface);
    SHELL_MALLOC_THUNK(AddRef, shell_malloc_AddRef);
    SHELL_MALLOC_THUNK(Release, shell_malloc_Release);
    SHELL_MALLOC_THUNK(Alloc, shell_malloc_Alloc);
    SHELL_MALLOC_THUNK(Realloc, shell_malloc_Realloc);
    SHELL_MALLOC_THUNK(Free, shell_malloc_Free);
    SHELL_MALLOC_THUNK(GetSize, shell_malloc_GetSize);
    SHELL_MALLOC_THUNK(DidAlloc, shell_malloc_DidAlloc);
    SHELL_MALLOC_THUNK(HeapMinimize, shell_malloc_HeapMinimize);
#undef SHELL_MALLOC_THUNK
    return shell_malloc_vtbl.QueryInterface && shell_malloc_vtbl.AddRef &&
           shell_malloc_vtbl.Release && shell_malloc_vtbl.Alloc &&
           shell_malloc_vtbl.Realloc && shell_malloc_vtbl.Free &&
           shell_malloc_vtbl.GetSize && shell_malloc_vtbl.DidAlloc &&
           shell_malloc_vtbl.HeapMinimize;
}

static int32_t WIN32_API shell_SHGetMalloc(void **allocator) {
    if (!allocator) return SHELL_E_POINTER;
    if (!shell_malloc_prepare_ring3_vtbl())
        return (int32_t)0x8007000EU; /* E_OUTOFMEMORY */
    shell_malloc_AddRef(&shell_malloc);
    *allocator = &shell_malloc;
    return SHELL_S_OK;
}

typedef struct shell_desktop_folder shell_desktop_folder_t;

typedef struct {
    int32_t (WIN32_API *QueryInterface)(shell_desktop_folder_t *, const void *, void **);
    uint32_t (WIN32_API *AddRef)(shell_desktop_folder_t *);
    uint32_t (WIN32_API *Release)(shell_desktop_folder_t *);
    int32_t (WIN32_API *ParseDisplayName)(shell_desktop_folder_t *, void *, void *, uint16_t *, uint32_t *, void **, uint32_t *);
    int32_t (WIN32_API *EnumObjects)(shell_desktop_folder_t *, void *, uint32_t, void **);
    int32_t (WIN32_API *BindToObject)(shell_desktop_folder_t *, const void *, void *, const void *, void **);
    int32_t (WIN32_API *BindToStorage)(shell_desktop_folder_t *, const void *, void *, const void *, void **);
    int32_t (WIN32_API *CompareIDs)(shell_desktop_folder_t *, int32_t, const void *, const void *);
    int32_t (WIN32_API *CreateViewObject)(shell_desktop_folder_t *, void *, const void *, void **);
    int32_t (WIN32_API *GetAttributesOf)(shell_desktop_folder_t *, uint32_t, const void **, uint32_t *);
    int32_t (WIN32_API *GetUIObjectOf)(shell_desktop_folder_t *, void *, uint32_t, const void **, const void *, uint32_t *, void **);
    int32_t (WIN32_API *GetDisplayNameOf)(shell_desktop_folder_t *, const void *, uint32_t, void *);
    int32_t (WIN32_API *SetNameOf)(shell_desktop_folder_t *, void *, const void *, const uint16_t *, uint32_t, void **);
} shell_ishellfolder_vtbl_t;

struct shell_desktop_folder {
    const shell_ishellfolder_vtbl_t *vtbl;
    uint32_t references;
};

static int32_t WIN32_API shell_desktop_QueryInterface(
    shell_desktop_folder_t *self, const void *iid UNUSED, void **object) {
    if (!object) return SHELL_E_POINTER;
    *object = self;
    if (!self) return SHELL_E_NOINTERFACE;
    self->references++;
    return SHELL_S_OK;
}

static uint32_t WIN32_API shell_desktop_AddRef(shell_desktop_folder_t *self) {
    if (!self) return 0U;
    return ++self->references;
}

static uint32_t WIN32_API shell_desktop_Release(shell_desktop_folder_t *self) {
    if (!self) return 0U;
    if (self->references > 1U) self->references--;
    return self->references;
}

static int32_t WIN32_API shell_desktop_ParseDisplayName(
    shell_desktop_folder_t *self UNUSED, void *hwnd UNUSED,
    void *bind_context UNUSED, uint16_t *name UNUSED,
    uint32_t *eaten UNUSED, void **pidl, uint32_t *attributes UNUSED) {
    if (pidl) *pidl = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_EnumObjects(
    shell_desktop_folder_t *self UNUSED, void *hwnd UNUSED,
    uint32_t flags UNUSED, void **enumerator) {
    if (enumerator) *enumerator = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_BindToObject(
    shell_desktop_folder_t *self UNUSED, const void *pidl UNUSED,
    void *bind_context UNUSED, const void *iid UNUSED, void **object) {
    if (object) *object = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_BindToStorage(
    shell_desktop_folder_t *self UNUSED, const void *pidl UNUSED,
    void *bind_context UNUSED, const void *iid UNUSED, void **object) {
    if (object) *object = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_CompareIDs(
    shell_desktop_folder_t *self UNUSED, int32_t parameter UNUSED,
    const void *left UNUSED, const void *right UNUSED) {
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_CreateViewObject(
    shell_desktop_folder_t *self UNUSED, void *hwnd UNUSED,
    const void *iid UNUSED, void **object) {
    if (object) *object = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_GetAttributesOf(
    shell_desktop_folder_t *self UNUSED, uint32_t count UNUSED,
    const void **pidls UNUSED, uint32_t *attributes) {
    if (!attributes) return SHELL_E_POINTER;
    return SHELL_S_OK;
}

static int32_t WIN32_API shell_desktop_GetUIObjectOf(
    shell_desktop_folder_t *self UNUSED, void *hwnd UNUSED,
    uint32_t count UNUSED, const void **pidls UNUSED,
    const void *iid UNUSED, uint32_t *reserved UNUSED, void **object) {
    if (object) *object = NULL;
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_GetDisplayNameOf(
    shell_desktop_folder_t *self UNUSED, const void *pidl UNUSED,
    uint32_t flags UNUSED, void *name UNUSED) {
    return SHELL_E_NOTIMPL;
}

static int32_t WIN32_API shell_desktop_SetNameOf(
    shell_desktop_folder_t *self UNUSED, void *hwnd UNUSED,
    const void *pidl UNUSED, const uint16_t *name UNUSED,
    uint32_t flags UNUSED, void **new_pidl) {
    if (new_pidl) *new_pidl = NULL;
    return SHELL_E_NOTIMPL;
}

static shell_ishellfolder_vtbl_t shell_desktop_vtbl;

static shell_desktop_folder_t shell_desktop_folder = {
    &shell_desktop_vtbl,
    1U
};

static bool shell_desktop_prepare_ring3_vtbl(void) {
    if (shell_desktop_vtbl.QueryInterface) return true;
#define SHELL_DESKTOP_THUNK(field, function) \
    shell_desktop_vtbl.field = (__typeof__(shell_desktop_vtbl.field))(uintptr_t) \
        elf_user_api_thunk("IShellFolder::" #field, \
                           (uint32_t)(uintptr_t)&function)
    SHELL_DESKTOP_THUNK(QueryInterface, shell_desktop_QueryInterface);
    SHELL_DESKTOP_THUNK(AddRef, shell_desktop_AddRef);
    SHELL_DESKTOP_THUNK(Release, shell_desktop_Release);
    SHELL_DESKTOP_THUNK(ParseDisplayName, shell_desktop_ParseDisplayName);
    SHELL_DESKTOP_THUNK(EnumObjects, shell_desktop_EnumObjects);
    SHELL_DESKTOP_THUNK(BindToObject, shell_desktop_BindToObject);
    SHELL_DESKTOP_THUNK(BindToStorage, shell_desktop_BindToStorage);
    SHELL_DESKTOP_THUNK(CompareIDs, shell_desktop_CompareIDs);
    SHELL_DESKTOP_THUNK(CreateViewObject, shell_desktop_CreateViewObject);
    SHELL_DESKTOP_THUNK(GetAttributesOf, shell_desktop_GetAttributesOf);
    SHELL_DESKTOP_THUNK(GetUIObjectOf, shell_desktop_GetUIObjectOf);
    SHELL_DESKTOP_THUNK(GetDisplayNameOf, shell_desktop_GetDisplayNameOf);
    SHELL_DESKTOP_THUNK(SetNameOf, shell_desktop_SetNameOf);
#undef SHELL_DESKTOP_THUNK
    return shell_desktop_vtbl.QueryInterface &&
           shell_desktop_vtbl.AddRef && shell_desktop_vtbl.Release &&
           shell_desktop_vtbl.ParseDisplayName &&
           shell_desktop_vtbl.EnumObjects &&
           shell_desktop_vtbl.BindToObject &&
           shell_desktop_vtbl.BindToStorage &&
           shell_desktop_vtbl.CompareIDs &&
           shell_desktop_vtbl.CreateViewObject &&
           shell_desktop_vtbl.GetAttributesOf &&
           shell_desktop_vtbl.GetUIObjectOf &&
           shell_desktop_vtbl.GetDisplayNameOf &&
           shell_desktop_vtbl.SetNameOf;
}

static int32_t WIN32_API shell_SHGetDesktopFolder(void **folder) {
    if (!folder) return SHELL_E_POINTER;
    if (!shell_desktop_prepare_ring3_vtbl())
        return (int32_t)0x8007000EU; /* E_OUTOFMEMORY */
    shell_desktop_AddRef(&shell_desktop_folder);
    *folder = &shell_desktop_folder;
    return SHELL_S_OK;
}


/* BLES_WINE_WINZIP_SHELL_DYNAMIC_APIS_20260723
 *
 * WinZip 7 obtiene estas funciones con GetProcAddress. Wine las exporta desde
 * SHELL32 y mantiene aliases ANSI sin sufijo para varias APIs de Win9x.
 * BlesKernOS aún no posee un servicio global de notificaciones del shell ni
 * accesos directos .LNK, por lo que las operaciones sin consumidor son no-op,
 * pero conservan ABI y valores de salida válidos.
 */
static void WIN32_API shell_SHAddToRecentDocs(uint32_t flags,
                                               const void *item) {
    /* Windows acepta NULL para vaciar la lista. Sin soporte .LNK, conservar la
       llamada como no-op es preferible a devolver una dirección ausente. */
    (void)flags;
    (void)item;
}

static void WIN32_API shell_SHChangeNotify(int32_t event_id,
                                            uint32_t flags,
                                            const void *item1,
                                            const void *item2) {
    /* Wine convierte PATHA/PATHW/PIDL y entrega el evento a los receptores
       registrados. BlesKernOS todavía no expone SHChangeNotifyRegister, por lo
       que no hay receptores a quienes enviar el cambio. */
    (void)event_id;
    (void)flags;
    (void)item1;
    (void)item2;
}

static void WIN32_API shell_SHGetSettings(void *raw_state,
                                          uint32_t mask UNUSED) {
    /* SHELLFLAGSTATE ocupa un DWORD de bitfields. Los valores cero representan
       los defaults clásicos y son una respuesta válida cuando no existe la
       configuración Explorer\\Advanced que Wine consulta en el registro. */
    if (raw_state) *(uint32_t *)raw_state = 0U;
}

static int32_t WIN32_API shell_SHGetSpecialFolderLocation(
    void *owner UNUSED, int32_t folder, void **pidl_out) {
    shell_pidl_t *pidl;
    const char *path;

    if (!pidl_out) return (int32_t)0x80004003U; /* E_POINTER */
    *pidl_out = NULL;

    path = special_folder((uint32_t)folder);
    if (!path) return (int32_t)0x80070057U; /* E_INVALIDARG */

    pidl = (shell_pidl_t *)kzalloc(sizeof(*pidl));
    if (!pidl) return (int32_t)0x8007000EU; /* E_OUTOFMEMORY */

    kstrncpy(pidl->path, path, sizeof(pidl->path) - 1U);
    pidl->path[sizeof(pidl->path) - 1U] = '\0';
    *pidl_out = pidl;
    return 0; /* S_OK */
}

uint32_t win32_shell32_resolve(const char *name){
#define S(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&shell_##api
    S(SHAddToRecentDocs);S(SHChangeNotify);S(SHGetSettings);
    S(SHGetSpecialFolderLocation);S(SHGetMalloc);
    /* Aliases ANSI publicados por Wine/Win95 shell32.spec. */
    if(equal(name,"DragQueryFile"))
        return(uint32_t)(uintptr_t)&shell_DragQueryFileA;
    if(equal(name,"ExtractIconEx"))
        return(uint32_t)(uintptr_t)&shell_ExtractIconExA;
    if(equal(name,"SHBrowseForFolder"))
        return(uint32_t)(uintptr_t)&shell_SHBrowseForFolderA;
    if(equal(name,"SHFileOperation"))
        return(uint32_t)(uintptr_t)&shell_SHFileOperationA;
    if(equal(name,"SHGetPathFromIDList"))
        return(uint32_t)(uintptr_t)&shell_SHGetPathFromIDListA;
    S(SHGetDesktopFolder);
    /* BLES_WINE_SHGETFILEINFO_ALIAS_20260723 */
    if (equal(name, "SHGetFileInfo"))
        return (uint32_t)(uintptr_t)&shell_SHGetFileInfoA;
    /* Win9x exports the undecorated compatibility alias too.  Wine keeps the
     * same alias in shell32.spec: ShellExecuteEx -> ShellExecuteExA. */
    if(equal(name,"ShellExecuteEx"))
        return(uint32_t)(uintptr_t)&shell_ShellExecuteExA;
    S(DragQueryFileA);S(DragQueryFileW);S(DragFinish);S(ShellExecuteA);S(ShellExecuteW);S(ShellExecuteExA);S(ShellExecuteExW);
    S(SHGetSpecialFolderPathA);S(SHGetSpecialFolderPathW);S(SHGetFolderPathA);S(SHGetFolderPathW);S(SHCreateDirectoryExA);S(SHCreateDirectoryExW);
    S(SHBrowseForFolderA);S(SHBrowseForFolderW);S(SHGetPathFromIDListA);S(SHGetPathFromIDListW);S(ILFree);
    S(ExtractIconA);S(ExtractIconW);S(ExtractIconExA);S(ExtractIconExW);S(ExtractAssociatedIconA);S(ExtractAssociatedIconW);
    S(FindExecutableA);S(FindExecutableW);S(CommandLineToArgvW);S(SHFileOperationA);S(SHFileOperationW);S(SHGetFileInfoA);S(SHGetFileInfoW);
#undef S
    return 0;
}

static int WIN32_API shell_ShellAboutA(void *owner, const char *app, const char *other, void *icon);


static int WIN32_API shell_ShellAboutA(
    void *owner,
    const char *app,
    const char *other,
    void *icon
) {
    (void)owner;
    (void)app;
    (void)other;
    (void)icon;
    return 1;
}

uint32_t win32_shell32_resolve_ordinal(uint16_t ordinal){
    /* SHELL32.288 is ShellAboutA on the Win95-compatible export table. */
    if(ordinal==288U)return(uint32_t)(uintptr_t)&shell_ShellAboutA;
    return 0;
}
