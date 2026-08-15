#include "win32.h"
#include "../include/types.h"
#include "../include/pe_loader.h"
#include "../include/memory.h"
#include "../string.h"

#define CDERR_NONE 0U
#define CDERR_STRUCTSIZE 1U
#define CDERR_INITIALIZATION 2U
#define CDERR_MEMALLOCFAILURE 9U
#define FNERR_SUBCLASSFAILURE 0x3001U
#define FNERR_INVALIDFILENAME 0x3002U
#define FNERR_BUFFERTOOSMALL 0x3003U
#define PDERR_NODEFAULTPRN 0x1008U
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFU
#define FILE_ATTRIBUTE_DIRECTORY 0x10U
#define OFN_NOVALIDATE 0x00000100U
#define OFN_PATHMUSTEXIST 0x00000800U
#define OFN_FILEMUSTEXIST 0x00001000U
#define OFN_CREATEPROMPT 0x00002000U
#define OFN_EXPLORER 0x00080000U
#define FR_DOWN 0x00000001U
#define FR_WHOLEWORD 0x00000002U
#define FR_MATCHCASE 0x00000004U

typedef struct {
    uint32_t lStructSize; void *hwndOwner,*hInstance; const char *lpstrFilter;
    char *lpstrCustomFilter; uint32_t nMaxCustFilter,nFilterIndex; char *lpstrFile;
    uint32_t nMaxFile; char *lpstrFileTitle; uint32_t nMaxFileTitle;
    const char *lpstrInitialDir,*lpstrTitle; uint32_t Flags;
    uint16_t nFileOffset,nFileExtension; const char *lpstrDefExt;
    int32_t lCustData; void *lpfnHook; const char *lpTemplateName;
    void *pvReserved; uint32_t dwReserved,FlagsEx;
} open_file_name_a_t;
typedef struct {
    uint32_t lStructSize; void *hwndOwner,*hInstance; const uint16_t *lpstrFilter;
    uint16_t *lpstrCustomFilter; uint32_t nMaxCustFilter,nFilterIndex; uint16_t *lpstrFile;
    uint32_t nMaxFile; uint16_t *lpstrFileTitle; uint32_t nMaxFileTitle;
    const uint16_t *lpstrInitialDir,*lpstrTitle; uint32_t Flags;
    uint16_t nFileOffset,nFileExtension; const uint16_t *lpstrDefExt;
    int32_t lCustData; void *lpfnHook; const uint16_t *lpTemplateName;
    void *pvReserved; uint32_t dwReserved,FlagsEx;
} open_file_name_w_t;
typedef struct {uint32_t lStructSize;void*hwndOwner,*hInstance;uint32_t Flags;char*lpstrFindWhat,*lpstrReplaceWith;uint16_t wFindWhatLen,wReplaceWithLen;int32_t lCustData;void*lpfnHook;const char*lpTemplateName;}find_replace_a_t;
typedef struct {uint32_t lStructSize;void*hwndOwner,*hDC,*lpLogFont;int32_t iPointSize;uint32_t Flags,rgbColors;int32_t lCustData;void*lpfnHook;const char*lpTemplateName;void*hInstance;char*lpszStyle;uint16_t nFontType,alignment;int32_t nSizeMin,nSizeMax;}choose_font_a_t;
typedef struct {uint32_t lStructSize;void*hwndOwner,*hDC,*lpLogFont;int32_t iPointSize;uint32_t Flags,rgbColors;int32_t lCustData;void*lpfnHook;const uint16_t*lpTemplateName;void*hInstance;uint16_t*lpszStyle;uint16_t nFontType,alignment;int32_t nSizeMin,nSizeMax;}choose_font_w_t;
typedef struct {uint32_t lStructSize;void*hwndOwner,*hInstance;uint32_t rgbResult,*lpCustColors,Flags;int32_t lCustData;void*lpfnHook;const void*lpTemplateName;}choose_color_t;

typedef uint32_t (WIN32_API *get_current_directory_a_t)(uint32_t,char*);
typedef uint32_t (WIN32_API *get_file_attributes_a_t)(const char*);
typedef uint32_t (WIN32_API *register_window_message_a_t)(const char*);

static uint32_t common_dialog_error;
static bool equal(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool wide_to_ansi(const uint16_t*w,char*out,uint32_t cap){uint32_t i=0;if(!out||!cap)return false;if(!w){out[0]=0;return true;}while(w[i]&&i+1U<cap){out[i]=w[i]<=255U?(char)w[i]:'?';i++;}out[i]=0;return w[i]==0;}
static void ansi_to_wide(const char*a,uint16_t*out,uint32_t cap){uint32_t i=0;if(!out||!cap)return;while(a&&a[i]&&i+1U<cap){out[i]=(uint8_t)a[i];i++;}out[i]=0;}
static bool has_extension(const char*path){const char*base=path,*dot=NULL;if(!path)return false;while(*path){if(*path=='/'||*path=='\\'){base=path+1;dot=NULL;}else if(*path=='.')dot=path;path++;}return dot&&dot>base&&dot[1];}
static void append_default_extension(char*path,uint32_t cap,const char*ext){uint32_t n;if(!path||!cap||!ext||!*ext||has_extension(path))return;while(*ext=='.'||*ext=='*')ext++;n=(uint32_t)kstrlen(path);if(!*ext||n+1U+(uint32_t)kstrlen(ext)>=cap)return;path[n++]='.';kstrcpy(path+n,ext);}
static void to_native_dir(const char*path,char*out,uint32_t cap){uint32_t i=0,n=0;if(!out||!cap)return;if(path&&path[0]&&path[1]==':')i=2;while(path&&path[i]&&n+1U<cap){char c=path[i++];out[n++]=c=='\\'?'/':c;}out[n]=0;if(!out[0])kstrcpy(out,"/");if(out[0]!='/')kstrcpy(out,"/");}
static void to_win_path(const char*path,char*out,uint32_t cap){uint32_t n=0,i=0;if(!out||cap<4U)return;if(path&&path[0]=='/'&&n+3U<cap){out[n++]='C';out[n++]=':';}while(path&&path[i]&&n+1U<cap){out[n++]=path[i]=='/'?'\\':path[i];i++;}out[n]=0;}
static void parent_path(const char*path,char*out,uint32_t cap){uint32_t last=0,n=0;if(!out||!cap)return;for(uint32_t i=0;path&&path[i];i++)if(path[i]=='/'||path[i]=='\\')last=i;while(path&&n<last&&n+1U<cap){out[n]=path[n];n++;}out[n]=0;if(!n)kstrcpy(out,"C:\\");}
static void filter_extension(const char*filter,uint32_t index,char*out,uint32_t cap){uint32_t current=1U;const char*p=filter;out[0]=0;if(!filter||!cap)return;while(*p){p+=(uint32_t)kstrlen(p)+1U;if(!*p)break;if(current==index){const char*pattern=p,*dot=NULL;while(*pattern&&*pattern!=';'){if(*pattern=='.')dot=pattern;pattern++;}if(dot&&dot[1]&&dot[1]!='*'){uint32_t n=0;dot++;while(*dot&&*dot!=';'&&n+1U<cap)out[n++]=*dot++;out[n]=0;}return;}p+=(uint32_t)kstrlen(p)+1U;current++;}}
static void fill_offsets(open_file_name_a_t*d){uint32_t length,base=0,dot=0;if(!d||!d->lpstrFile)return;length=(uint32_t)kstrlen(d->lpstrFile);for(uint32_t i=0;i<length;i++){if(d->lpstrFile[i]=='/'||d->lpstrFile[i]=='\\'){base=i+1U;dot=0;}else if(d->lpstrFile[i]=='.')dot=i+1U;}d->nFileOffset=(uint16_t)base;d->nFileExtension=(uint16_t)dot;if(d->lpstrFileTitle&&d->nMaxFileTitle){kstrncpy(d->lpstrFileTitle,d->lpstrFile+base,d->nMaxFileTitle-1U);d->lpstrFileTitle[d->nMaxFileTitle-1U]=0;}}
static bool path_is_valid(const char*path,bool save,uint32_t flags){get_file_attributes_a_t get_attributes=(get_file_attributes_a_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","GetFileAttributesA");uint32_t attributes;if(!get_attributes)return true;attributes=get_attributes(path);if(!save&&(flags&OFN_FILEMUSTEXIST)&&(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)))return false;if(save&&(flags&OFN_PATHMUSTEXIST)){char parent[512];parent_path(path,parent,sizeof(parent));attributes=get_attributes(parent);if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;}return true;}
static int file_dialog(open_file_name_a_t*d,bool save){char initial[512],native[512],selected[512],ext[32];get_current_directory_a_t get_current;if(!d||d->lStructSize<76U||!d->lpstrFile||d->nMaxFile<2U){common_dialog_error=CDERR_STRUCTSIZE;return 0;}initial[0]=0;if(d->lpstrFile[0])kstrncpy(initial,d->lpstrFile,sizeof(initial)-1U);else if(d->lpstrInitialDir&&*d->lpstrInitialDir)kstrncpy(initial,d->lpstrInitialDir,sizeof(initial)-1U);else{get_current=(get_current_directory_a_t)(uintptr_t)pe_win32_resolve_export("KERNEL32.DLL","GetCurrentDirectoryA");if(get_current)(void)get_current(sizeof(initial),initial);}if(!initial[0])kstrcpy(initial,"C:\\");filter_extension(d->lpstrFilter,d->nFilterIndex?d->nFilterIndex:1U,ext,sizeof(ext));if(!ext[0]&&d->lpstrDefExt)kstrncpy(ext,d->lpstrDefExt,sizeof(ext)-1U);
    if(save){kstrncpy(d->lpstrFile,initial,d->nMaxFile-1U);d->lpstrFile[d->nMaxFile-1U]=0;if(!win32_user_path_dialog(d->lpstrTitle?d->lpstrTitle:"Save As",d->lpstrFile,d->nMaxFile,true)){common_dialog_error=CDERR_NONE;return 0;}append_default_extension(d->lpstrFile,d->nMaxFile,d->lpstrDefExt?d->lpstrDefExt:ext);}else{char directory[512];if(d->lpstrInitialDir&&*d->lpstrInitialDir)kstrncpy(directory,d->lpstrInitialDir,sizeof(directory)-1U);else parent_path(initial,directory,sizeof(directory));to_native_dir(directory,native,sizeof(native));if(!win32_user_file_dialog(d->lpstrTitle?d->lpstrTitle:"Open",native,ext[0]?ext:NULL,selected,sizeof(selected))){common_dialog_error=CDERR_NONE;return 0;}to_win_path(selected,d->lpstrFile,d->nMaxFile);}
    if(!(d->Flags&OFN_NOVALIDATE)&&!path_is_valid(d->lpstrFile,save,d->Flags)){common_dialog_error=FNERR_INVALIDFILENAME;return 0;}fill_offsets(d);common_dialog_error=CDERR_NONE;return 1;}
static int file_dialog_w(open_file_name_w_t*w,bool save){open_file_name_a_t a;char*file;char filter[512],initial[512],title[128],defext[32],filetitle[260];uint32_t fi=0;if(!w||!w->lpstrFile||w->nMaxFile<2U){common_dialog_error=CDERR_STRUCTSIZE;return 0;}file=(char*)kmalloc(w->nMaxFile);if(!file){common_dialog_error=CDERR_MEMALLOCFAILURE;return 0;}wide_to_ansi(w->lpstrFile,file,w->nMaxFile);kmemset(&a,0,sizeof(a));a.lStructSize=sizeof(a);a.hwndOwner=w->hwndOwner;a.hInstance=w->hInstance;a.nFilterIndex=w->nFilterIndex;a.lpstrFile=file;a.nMaxFile=w->nMaxFile;a.Flags=w->Flags;a.lCustData=w->lCustData;a.lpfnHook=w->lpfnHook;a.FlagsEx=w->FlagsEx;
    if(w->lpstrFilter){while(fi+2U<sizeof(filter)){uint16_t c=w->lpstrFilter[fi];filter[fi]=(char)c;fi++;if(!c&&!w->lpstrFilter[fi]){filter[fi++]=0;break;}}a.lpstrFilter=filter;}if(w->lpstrInitialDir&&wide_to_ansi(w->lpstrInitialDir,initial,sizeof(initial)))a.lpstrInitialDir=initial;if(w->lpstrTitle&&wide_to_ansi(w->lpstrTitle,title,sizeof(title)))a.lpstrTitle=title;if(w->lpstrDefExt&&wide_to_ansi(w->lpstrDefExt,defext,sizeof(defext)))a.lpstrDefExt=defext;if(w->lpstrFileTitle&&w->nMaxFileTitle){a.lpstrFileTitle=filetitle;a.nMaxFileTitle=sizeof(filetitle);}
    int result=file_dialog(&a,save);if(result){ansi_to_wide(file,w->lpstrFile,w->nMaxFile);if(w->lpstrFileTitle&&w->nMaxFileTitle)ansi_to_wide(filetitle,w->lpstrFileTitle,w->nMaxFileTitle);w->nFileOffset=a.nFileOffset;w->nFileExtension=a.nFileExtension;}kfree(file);return result;}

static uint32_t WIN32_API comdlg_CommDlgExtendedError(void){return common_dialog_error;}
static int WIN32_API comdlg_GetOpenFileNameA(void*d){return file_dialog((open_file_name_a_t*)d,false);}
static int WIN32_API comdlg_GetSaveFileNameA(void*d){return file_dialog((open_file_name_a_t*)d,true);}
static int WIN32_API comdlg_GetOpenFileNameW(void*d){return file_dialog_w((open_file_name_w_t*)d,false);}
static int WIN32_API comdlg_GetSaveFileNameW(void*d){return file_dialog_w((open_file_name_w_t*)d,true);}
static int WIN32_API comdlg_GetFileTitleA(const char*file,char*title,uint16_t size){const char*base=file;uint32_t len;if(!file||!title||!size)return -1;for(const char*p=file;*p;p++)if(*p=='/'||*p=='\\')base=p+1;len=(uint32_t)kstrlen(base);if(len+1U>size)return -1;kstrcpy(title,base);return 0;}
static int WIN32_API comdlg_GetFileTitleW(const uint16_t*file,uint16_t*title,uint16_t size){char a[512],b[260];if(!file||!title||!wide_to_ansi(file,a,sizeof(a)))return -1;if(comdlg_GetFileTitleA(a,b,sizeof(b)))return -1;if((uint32_t)kstrlen(b)+1U>size)return -1;ansi_to_wide(b,title,size);return 0;}
static int WIN32_API comdlg_ChooseFontA(void*raw){choose_font_a_t*d=(choose_font_a_t*)raw;uint8_t*lf;if(!d||d->lStructSize<60U||!d->lpLogFont){common_dialog_error=CDERR_STRUCTSIZE;return 0;}lf=(uint8_t*)d->lpLogFont;if(*(int32_t*)lf==0)*(int32_t*)lf=-14;if(!lf[28])kstrcpy((char*)lf+28,"Tahoma");d->iPointSize=90;d->nFontType=0x0004U;common_dialog_error=0;return 1;}
static int WIN32_API comdlg_ChooseFontW(void*raw){choose_font_w_t*d=(choose_font_w_t*)raw;uint8_t*lf;uint16_t*face;if(!d||d->lStructSize<60U||!d->lpLogFont){common_dialog_error=CDERR_STRUCTSIZE;return 0;}lf=(uint8_t*)d->lpLogFont;if(*(int32_t*)lf==0)*(int32_t*)lf=-14;face=(uint16_t*)(lf+28U);if(!face[0])ansi_to_wide("Tahoma",face,32U);d->iPointSize=90;d->nFontType=0x0004U;common_dialog_error=0;return 1;}
static int WIN32_API comdlg_ChooseColorA(void*raw){choose_color_t*d=(choose_color_t*)raw;if(!d||d->lStructSize<36U){common_dialog_error=CDERR_STRUCTSIZE;return 0;}if(!d->rgbResult&&d->lpCustColors)d->rgbResult=d->lpCustColors[0];common_dialog_error=0;return 1;}
static int WIN32_API comdlg_ChooseColorW(void*raw){return comdlg_ChooseColorA(raw);}
static int WIN32_API comdlg_PrintDlgA(void*d UNUSED){common_dialog_error=PDERR_NODEFAULTPRN;return 0;}
static int WIN32_API comdlg_PrintDlgW(void*d UNUSED){common_dialog_error=PDERR_NODEFAULTPRN;return 0;}
static int WIN32_API comdlg_PageSetupDlgA(void*d UNUSED){common_dialog_error=PDERR_NODEFAULTPRN;return 0;}
static int WIN32_API comdlg_PageSetupDlgW(void*d UNUSED){common_dialog_error=PDERR_NODEFAULTPRN;return 0;}
static void*find_dialog(void*raw,bool replace){find_replace_a_t*d=(find_replace_a_t*)raw;register_window_message_a_t register_message;uint32_t message;if(!d||d->lStructSize<40U||!d->hwndOwner||!d->lpstrFindWhat||!d->wFindWhatLen){common_dialog_error=CDERR_STRUCTSIZE;return NULL;}register_message=(register_window_message_a_t)(uintptr_t)pe_win32_resolve_export("USER32.DLL","RegisterWindowMessageA");message=register_message?register_message("commdlg_FindReplace"):0U;if(!message){common_dialog_error=CDERR_INITIALIZATION;return NULL;}common_dialog_error=0;return win32_user_find_dialog(replace?"Replace":"Find",d->hwndOwner,message,d,replace);}
static void*WIN32_API comdlg_FindTextA(void*d){return find_dialog(d,false);}
static void*WIN32_API comdlg_ReplaceTextA(void*d){return find_dialog(d,true);}

uint32_t win32_comdlg32_resolve(const char*name){
#define C(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&comdlg_##api
    C(GetOpenFileNameA);C(GetSaveFileNameA);C(GetOpenFileNameW);C(GetSaveFileNameW);C(GetFileTitleA);C(GetFileTitleW);
    C(ChooseFontA);C(ChooseFontW);C(ChooseColorA);C(ChooseColorW);C(PrintDlgA);C(PrintDlgW);C(PageSetupDlgA);C(PageSetupDlgW);
    C(FindTextA);C(ReplaceTextA);C(CommDlgExtendedError);
#undef C
    return 0;
}
