/* BlesKernOS WINE.A: broad Win95/98 USER32/KERNEL32 fallback layer. */
#include "win32.h"
#include "../include/types.h"
#include "../include/api.h"
extern uint32_t win32_user32_resolve(const char*);
extern uint32_t win32_kernel32_resolve(const char*);
extern uint32_t win32_wine_stage5_resolve(const char*,const char*);
extern uint32_t win32_wine_stage6_resolve(const char*,const char*);
extern uint32_t win32_win95_compat_resolve(const char*,const char*);
extern bool win32_register_resolver(uint32_t(*)(const char*,const char*),uint32_t(*)(const char*,uint16_t),bool(*)(const char*,const char*));
typedef struct{int32_t left,top,right,bottom;}s9_rect_t;
typedef struct{uint32_t length,major,minor,build,platform;char service_pack[128];}s9_osversioninfoa_t;
typedef struct{uint32_t length,major,minor,build,platform;uint16_t service_pack[128];}s9_osversioninfow_t;
typedef struct{uint16_t arch,reserved;uint32_t page_size,min_addr,max_addr,mask,cpu_count,cpu_type,granularity;uint16_t level,revision;}s9_system_info_t;
typedef struct{uint32_t length,load,total_phys,avail_phys,total_page,avail_page,total_virtual,avail_virtual;}s9_memory_status_t;
typedef uint32_t(*s9_any_fn_t)(void);typedef struct{const char*name;s9_any_fn_t fn;}s9_export_t;
static uint8_t s9_up(uint8_t c){return c>='a'&&c<='z'?(uint8_t)(c-32):c;}
static bool s9_eq(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b)if(s9_up((uint8_t)*a++)!=s9_up((uint8_t)*b++))return false;return *a==*b;}
static uint32_t s9_len(const char*s){uint32_t n=0;if(s)while(s[n])n++;return n;}
static uint32_t s9_cpa(char*d,uint32_t cap,const char*s){uint32_t n=s9_len(s),c;if(!d||!cap)return n;c=n<cap-1?n:cap-1;for(uint32_t i=0;i<c;i++)d[i]=s[i];d[c]=0;return c;}
static uint32_t s9_cpw(uint16_t*d,uint32_t cap,const char*s){uint32_t n=s9_len(s),c;if(!d||!cap)return n;c=n<cap-1?n:cap-1;for(uint32_t i=0;i<c;i++)d[i]=(uint8_t)s[i];d[c]=0;return c;}
static uint32_t WIN32_API s9_true0(void){return 1U;}
static uint32_t WIN32_API s9_zero0(void){return 0U;}
static uint32_t WIN32_API s9_true1(uint32_t a0 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero1(uint32_t a0 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true2(uint32_t a0 UNUSED,uint32_t a1 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero2(uint32_t a0 UNUSED,uint32_t a1 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true3(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero3(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true4(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero4(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true5(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero5(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true6(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero6(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true7(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero7(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true8(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED,uint32_t a7 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero8(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED,uint32_t a7 UNUSED){return 0U;}
static uint32_t WIN32_API s9_true9(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED,uint32_t a7 UNUSED,uint32_t a8 UNUSED){return 1U;}
static uint32_t WIN32_API s9_true10(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED,uint32_t a7 UNUSED,uint32_t a8 UNUSED,uint32_t a9 UNUSED){return 1U;}
static uint32_t WIN32_API s9_zero10(uint32_t a0 UNUSED,uint32_t a1 UNUSED,uint32_t a2 UNUSED,uint32_t a3 UNUSED,uint32_t a4 UNUSED,uint32_t a5 UNUSED,uint32_t a6 UNUSED,uint32_t a7 UNUSED,uint32_t a8 UNUSED,uint32_t a9 UNUSED){return 0U;}
static uint32_t WIN32_API s9_DrawFrameControl(void*d UNUSED,s9_rect_t*r,uint32_t t UNUSED,uint32_t s UNUSED){return r!=NULL;}
static int32_t WIN32_API s9_DialogBoxIndirectParamA(void*i UNUSED,const void*t UNUSED,void*o,uint32_t(WIN32_API*p)(void*,uint32_t,uint32_t,int32_t),int32_t x){if(p)p(o,0x110,0,x);return p?1:-1;}
static int32_t WIN32_API s9_DialogBoxIndirectParamW(void*i,const void*t,void*o,uint32_t(WIN32_API*p)(void*,uint32_t,uint32_t,int32_t),int32_t x){return s9_DialogBoxIndirectParamA(i,t,o,p,x);}
static uint32_t WIN32_API s9_EnumThreadWindows(uint32_t t UNUSED,int(WIN32_API*c)(void*,int32_t) UNUSED,int32_t p UNUSED){return 1;}
static uint32_t WIN32_API s9_EnumWindows(int(WIN32_API*c)(void*,int32_t) UNUSED,int32_t p UNUSED){return 1;}
static uint32_t WIN32_API s9_GetWindowThreadProcessId(void*w UNUSED,uint32_t*p){if(p)*p=bk_sys_getpid();return bk_sys_getpid();}
static uint32_t WIN32_API s9_GetVersionExA(s9_osversioninfoa_t*i){if(!i||i->length<20)return 0;i->major=4;i->minor=10;i->build=0x7ce;i->platform=1;i->service_pack[0]=0;return 1;}
static uint32_t WIN32_API s9_GetVersionExW(s9_osversioninfow_t*i){if(!i||i->length<20)return 0;i->major=4;i->minor=10;i->build=0x7ce;i->platform=1;i->service_pack[0]=0;return 1;}
static void WIN32_API s9_GetSystemInfo(s9_system_info_t*i){uint32_t c;if(!i)return;uint8_t*p=(uint8_t*)i;for(uint32_t n=0;n<sizeof(*i);n++)p[n]=0;c=bk_proc_cpu_count();if(!c)c=1;if(c>32)c=32;i->page_size=4096;i->min_addr=0x10000;i->max_addr=0x7ffeffff;i->mask=c==32?0xFFFFFFFFU:((1U<<c)-1U);i->cpu_count=c;i->cpu_type=586;i->granularity=65536;i->level=5;}
static void WIN32_API s9_GlobalMemoryStatus(s9_memory_status_t*i){if(!i)return;uint32_t l=i->length;uint8_t*p=(uint8_t*)i;for(uint32_t n=0;n<sizeof(*i);n++)p[n]=0;i->length=l?l:sizeof(*i);i->load=25;i->total_phys=128U<<20;i->avail_phys=96U<<20;i->total_page=i->total_phys;i->avail_page=i->avail_phys;i->total_virtual=0x70000000;i->avail_virtual=0x60000000;}
static int32_t WIN32_API s9_MulDiv(int32_t a,int32_t b,int32_t c){return c?(int32_t)(((int64_t)a*b)/c):-1;}
static int32_t WIN32_API s9_InterlockedIncrement(volatile int32_t*v){return __sync_add_and_fetch(v,1);}
static int32_t WIN32_API s9_InterlockedDecrement(volatile int32_t*v){return __sync_sub_and_fetch(v,1);}
static int32_t WIN32_API s9_InterlockedExchange(volatile int32_t*v,int32_t n){return __sync_lock_test_and_set(v,n);}
static int32_t WIN32_API s9_InterlockedExchangeAdd(volatile int32_t*v,int32_t n){return __sync_fetch_and_add(v,n);}
static int32_t WIN32_API s9_InterlockedCompareExchange(volatile int32_t*v,int32_t e,int32_t c){return __sync_val_compare_and_swap(v,c,e);}
static uint32_t WIN32_API s9_QueryPerformanceFrequency(uint64_t*v){if(v)*v=1000;return v!=NULL;}
static uint32_t WIN32_API s9_QueryPerformanceCounter(uint64_t*v){if(!v)return 0;uint32_t h=bk_sys_tick_frequency();*v=h?((uint64_t)bk_sys_ticks()*1000)/h:bk_sys_ticks();return 1;}
static uint32_t WIN32_API s9_GetComputerNameA(char*b,uint32_t*n){const char*s="BLESKERNOS";uint32_t z=s9_len(s);if(!n)return 0;if(!b||*n<=z){*n=z+1;return 0;}s9_cpa(b,*n,s);*n=z;return 1;}
static uint32_t WIN32_API s9_GetComputerNameW(uint16_t*b,uint32_t*n){const char*s="BLESKERNOS";uint32_t z=s9_len(s);if(!n)return 0;if(!b||*n<=z){*n=z+1;return 0;}s9_cpw(b,*n,s);*n=z;return 1;}
static uint32_t WIN32_API s9_GetWindowsDirectoryA(char*b,uint32_t n){return s9_cpa(b,n,"/SYSTEM/WIN32");}
static uint32_t WIN32_API s9_GetWindowsDirectoryW(uint16_t*b,uint32_t n){return s9_cpw(b,n,"/SYSTEM/WIN32");}
static uint32_t WIN32_API s9_GetSystemDirectoryA(char*b,uint32_t n){return s9_cpa(b,n,"/SYSTEM/LIBS/WINE");}
static uint32_t WIN32_API s9_GetSystemDirectoryW(uint16_t*b,uint32_t n){return s9_cpw(b,n,"/SYSTEM/LIBS/WINE");}
static uint32_t WIN32_API s9_GetTempPathA(uint32_t n,char*b){return s9_cpa(b,n,"/TEMP/");}
static uint32_t WIN32_API s9_GetTempPathW(uint32_t n,uint16_t*b){return s9_cpw(b,n,"/TEMP/");}
static uint32_t WIN32_API s9_GetExitCodeProcess(void*h UNUSED,uint32_t*c){if(c)*c=259;return c!=NULL;}
static uint32_t WIN32_API s9_GetExitCodeThread(void*h UNUSED,uint32_t*c){if(c)*c=259;return c!=NULL;}
static uint32_t WIN32_API s9_SystemParametersInfoA(uint32_t a,uint32_t p UNUSED,void*o,uint32_t f UNUSED){if(a==0x30&&o){s9_rect_t*r=o;r->left=0;r->top=0;r->right=800;r->bottom=570;}else if(a==1&&o)*(uint32_t*)o=1;else if(a==0xe&&o)*(uint32_t*)o=300;return 1;}
static uint32_t WIN32_API s9_SystemParametersInfoW(uint32_t a,uint32_t p,void*o,uint32_t f){return s9_SystemParametersInfoA(a,p,o,f);}
static uint32_t WIN32_API s9_AdjustWindowRect(s9_rect_t*r,uint32_t s UNUSED,uint32_t m){if(!r)return 0;r->left-=4;r->right+=4;r->top-=m?24:20;r->bottom+=4;return 1;}
static uint32_t WIN32_API s9_AdjustWindowRectEx(s9_rect_t*r,uint32_t s,uint32_t m,uint32_t e UNUSED){return s9_AdjustWindowRect(r,s,m);}
static uint32_t WIN32_API s9_SetRectEmpty(s9_rect_t*r){if(!r)return 0;r->left=r->top=r->right=r->bottom=0;return 1;}
static uint32_t WIN32_API s9_IsRectEmpty(const s9_rect_t*r){return !r||r->left>=r->right||r->top>=r->bottom;}
static uint32_t WIN32_API s9_EqualRect(const s9_rect_t*a,const s9_rect_t*b){return a&&b&&a->left==b->left&&a->top==b->top&&a->right==b->right&&a->bottom==b->bottom;}
static uint32_t WIN32_API s9_PtInRect(const s9_rect_t*r,int32_t x,int32_t y){return r&&x>=r->left&&x<r->right&&y>=r->top&&y<r->bottom;}
static void s9_cpp_terminate(void){}
static const s9_export_t s9_user[]={
{"DrawFrameControl",(s9_any_fn_t)(uintptr_t)&s9_DrawFrameControl},{"DialogBoxIndirectParamA",(s9_any_fn_t)(uintptr_t)&s9_DialogBoxIndirectParamA},{"DialogBoxIndirectParamW",(s9_any_fn_t)(uintptr_t)&s9_DialogBoxIndirectParamW},{"EnumThreadWindows",(s9_any_fn_t)(uintptr_t)&s9_EnumThreadWindows},{"EnumWindows",(s9_any_fn_t)(uintptr_t)&s9_EnumWindows},{"GetWindowThreadProcessId",(s9_any_fn_t)(uintptr_t)&s9_GetWindowThreadProcessId},{"SystemParametersInfoA",(s9_any_fn_t)(uintptr_t)&s9_SystemParametersInfoA},{"SystemParametersInfoW",(s9_any_fn_t)(uintptr_t)&s9_SystemParametersInfoW},{"AdjustWindowRect",(s9_any_fn_t)(uintptr_t)&s9_AdjustWindowRect},{"AdjustWindowRectEx",(s9_any_fn_t)(uintptr_t)&s9_AdjustWindowRectEx},{"SetRectEmpty",(s9_any_fn_t)(uintptr_t)&s9_SetRectEmpty},{"IsRectEmpty",(s9_any_fn_t)(uintptr_t)&s9_IsRectEmpty},{"EqualRect",(s9_any_fn_t)(uintptr_t)&s9_EqualRect},{"PtInRect",(s9_any_fn_t)(uintptr_t)&s9_PtInRect},
    {"AdjustWindowRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"AdjustWindowRectEx",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"AnyPopup",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"AppendMenuA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"AppendMenuW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"BringWindowToTop",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"CallMsgFilterA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CallMsgFilterW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CallNextHookEx",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"CallWindowProcA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"CallWindowProcW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"ChangeClipboardChain",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"CheckDlgButton",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CheckMenuRadioItem",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"CheckRadioButton",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"ChildWindowFromPoint",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"ClientToScreen",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ClipCursor",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"CloseClipboard",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"CloseDesktop",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"CloseWindow",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"CloseWindowStation",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"CopyAcceleratorTableA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CopyAcceleratorTableW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CopyIcon",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"CopyImage",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"CreateAcceleratorTableA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"CreateAcceleratorTableW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"CreateCursor",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"CreateDesktopA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"CreateDesktopW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"CreateIcon",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"CreateIconFromResource",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"CreateIconFromResourceEx",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"CreateMenu",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"CreatePopupMenu",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"DefDlgProcA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DefDlgProcW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DefFrameProcA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"DefFrameProcW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"DefMDIChildProcA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"DefMDIChildProcW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"DeferWindowPos",(s9_any_fn_t)(uintptr_t)&s9_true8},
    {"DeleteMenu",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"DestroyAcceleratorTable",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DestroyCaret",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"DestroyCursor",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DestroyIcon",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DestroyMenu",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DlgDirListA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"DlgDirListComboBoxA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"DlgDirListComboBoxW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"DlgDirListW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"DragDetect",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"DrawAnimatedRects",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"DrawCaption",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DrawEdge",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DrawFocusRect",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"DrawFrameControl",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DrawIcon",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"DrawIconEx",(s9_any_fn_t)(uintptr_t)&s9_true9},
    {"DrawMenuBar",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DrawStateA",(s9_any_fn_t)(uintptr_t)&s9_true10},
    {"DrawStateW",(s9_any_fn_t)(uintptr_t)&s9_true10},
    {"DrawTextA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"DrawTextExA",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"DrawTextExW",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"DrawTextW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"EmptyClipboard",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"EnableMenuItem",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnableScrollBar",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnableWindow",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"EndDeferWindowPos",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"EndMenu",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"EnumChildWindows",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumDesktopWindows",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"EnumDesktopsA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"EnumDesktopsW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"EnumDisplaySettingsA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumDisplaySettingsW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumPropsA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"EnumPropsExA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"EnumPropsExW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"EnumPropsW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"EnumThreadWindows",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"FindWindowA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"FindWindowExA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"FindWindowExW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"FindWindowW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetActiveWindow",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetAsyncKeyState",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetCapture",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCaretBlinkTime",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCaretPos",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetClassInfoA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClassInfoW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClassLongA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetClassLongW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetClassNameA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClassNameW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClassWord",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetClipCursor",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetClipboardData",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetClipboardFormatNameA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClipboardFormatNameW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetClipboardOwner",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetClipboardViewer",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCursor",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCursorPos",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetDesktopWindow",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetDialogBaseUnits",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetDlgItemInt",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetDlgItemTextA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetDlgItemTextW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetFocus",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetForegroundWindow",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetIconInfo",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetInputState",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetKeyNameTextA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetKeyNameTextW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetKeyState",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetKeyboardLayoutList",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetMenuCheckMarkDimensions",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetMenuDefaultItem",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetMenuItemCount",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetMenuItemID",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetMenuItemInfoA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetMenuItemInfoW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetMenuItemRect",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetMenuState",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetMenuStringA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"GetMenuStringW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"GetMessageExtraInfo",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetMessagePos",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetMessageTime",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetNextDlgGroupItem",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetNextDlgTabItem",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetOpenClipboardWindow",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetParent",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetPriorityClipboardFormat",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetProcessWindowStation",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetPropA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetPropW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetQueueStatus",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetScrollInfo",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetScrollPos",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetShellWindow",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetSubMenu",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetSystemMenu",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetThreadDesktop",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetTopWindow",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetUpdateRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetUpdateRgn",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetUserObjectInformationA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetUserObjectInformationW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetWindow",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetWindowDC",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetWindowLongA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetWindowLongW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetWindowPlacement",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetWindowRgn",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetWindowTextLengthA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetWindowTextLengthW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetWindowThreadProcessId",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GrayStringA",(s9_any_fn_t)(uintptr_t)&s9_true9},
    {"GrayStringW",(s9_any_fn_t)(uintptr_t)&s9_true9},
    {"HideCaret",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"HiliteMenuItem",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"InsertMenuA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"InsertMenuItemA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"InsertMenuItemW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"InsertMenuW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"IntersectRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"InvalidateRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"InvalidateRgn",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"InvertRect",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"IsChild",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"IsClipboardFormatAvailable",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsDialogMessageA",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"IsDialogMessageW",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"IsIconic",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsMenu",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsRectEmpty",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsWindow",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsWindowEnabled",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsWindowUnicode",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsWindowVisible",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsZoomed",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"KillTimer",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"LoadImageA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"LoadImageW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"LoadMenuA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"LoadMenuIndirectA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"LoadMenuIndirectW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"LoadMenuW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"LoadStringA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"LoadStringW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"LockWindowUpdate",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"MapVirtualKeyExA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"MapVirtualKeyExW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"MapWindowPoints",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"MessageBeep",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"MessageBoxExA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"MessageBoxExW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"ModifyMenuA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ModifyMenuW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"MoveWindow",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"MsgWaitForMultipleObjects",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"OemKeyScan",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"OpenClipboard",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"OpenIcon",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"OpenInputDesktop",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"PackDDElParam",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"PaintDesktop",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"PeekMessageA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"PeekMessageW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"PostMessageA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"PostMessageW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"PostThreadMessageA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"PostThreadMessageW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"PtInRect",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"RegisterClipboardFormatA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"RegisterClipboardFormatW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"RegisterHotKey",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"RegisterWindowMessageA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"RegisterWindowMessageW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"ReleaseCapture",(s9_any_fn_t)(uintptr_t)&s9_true0},
    {"ReleaseDC",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"RemoveMenu",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"RemovePropA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"RemovePropW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"ReplyMessage",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ScreenToClient",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ScrollDC",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"ScrollWindow",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ScrollWindowEx",(s9_any_fn_t)(uintptr_t)&s9_true8},
    {"SendDlgItemMessageA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"SendDlgItemMessageW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"SendMessageA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SendMessageCallbackA",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"SendMessageCallbackW",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"SendMessageTimeoutA",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"SendMessageTimeoutW",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"SendMessageW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SendNotifyMessageA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SendNotifyMessageW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetActiveWindow",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetCapture",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetCaretBlinkTime",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetClassLongA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetClassLongW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetClipboardViewer",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetCursor",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetCursorPos",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetDlgItemInt",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"SetFocus",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetForegroundWindow",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetKeyboardState",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetMenu",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetMenuContextHelpId",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetMenuDefaultItem",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"SetMenuItemBitmaps",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"SetMenuItemInfoA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"SetMenuItemInfoW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"SetParent",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetProcessWindowStation",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetPropA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetPropW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetRect",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"SetScrollInfo",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetScrollPos",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetScrollRange",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"SetThreadDesktop",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetTimer",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetUserObjectInformationA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"SetUserObjectInformationW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"SetWindowContextHelpId",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetWindowLongA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetWindowLongW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetWindowPos",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"SetWindowRgn",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"SetWindowsHookA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetWindowsHookExA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetWindowsHookExW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetWindowsHookW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"ShowCaret",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ShowCursor",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"ShowOwnedPopups",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ShowScrollBar",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"ShowWindow",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ShowWindowAsync",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SubtractRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SwapMouseButton",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SystemParametersInfoA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"SystemParametersInfoW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"TabbedTextOutA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"TabbedTextOutW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"TileWindows",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"ToAscii",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"ToAsciiEx",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"TrackMouseEvent",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"TrackPopupMenu",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"TrackPopupMenuEx",(s9_any_fn_t)(uintptr_t)&s9_true6},
    {"TranslateAcceleratorA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"TranslateAcceleratorW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"UnhookWindowsHookEx",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"UnionRect",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"UnloadKeyboardLayout",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"UnpackDDElParam",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"UnregisterHotKey",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"UpdateWindow",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ValidateRect",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ValidateRgn",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"VkKeyScanA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"VkKeyScanExA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"VkKeyScanExW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"VkKeyScanW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"WaitForInputIdle",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"WaitMessage",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"WinHelpA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"WinHelpW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"WindowFromDC",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"WindowFromPoint",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"keybd_event",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"mouse_event",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"wvsprintfA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"wvsprintfW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
};
static const s9_export_t s9_kernel[]={
{"GetVersionExA",(s9_any_fn_t)(uintptr_t)&s9_GetVersionExA},{"GetVersionExW",(s9_any_fn_t)(uintptr_t)&s9_GetVersionExW},{"GetSystemInfo",(s9_any_fn_t)(uintptr_t)&s9_GetSystemInfo},{"GlobalMemoryStatus",(s9_any_fn_t)(uintptr_t)&s9_GlobalMemoryStatus},{"MulDiv",(s9_any_fn_t)(uintptr_t)&s9_MulDiv},{"InterlockedIncrement",(s9_any_fn_t)(uintptr_t)&s9_InterlockedIncrement},{"InterlockedDecrement",(s9_any_fn_t)(uintptr_t)&s9_InterlockedDecrement},{"InterlockedExchange",(s9_any_fn_t)(uintptr_t)&s9_InterlockedExchange},{"InterlockedExchangeAdd",(s9_any_fn_t)(uintptr_t)&s9_InterlockedExchangeAdd},{"InterlockedCompareExchange",(s9_any_fn_t)(uintptr_t)&s9_InterlockedCompareExchange},{"QueryPerformanceFrequency",(s9_any_fn_t)(uintptr_t)&s9_QueryPerformanceFrequency},{"QueryPerformanceCounter",(s9_any_fn_t)(uintptr_t)&s9_QueryPerformanceCounter},{"GetComputerNameA",(s9_any_fn_t)(uintptr_t)&s9_GetComputerNameA},{"GetComputerNameW",(s9_any_fn_t)(uintptr_t)&s9_GetComputerNameW},{"GetWindowsDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_GetWindowsDirectoryA},{"GetWindowsDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_GetWindowsDirectoryW},{"GetSystemDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_GetSystemDirectoryA},{"GetSystemDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_GetSystemDirectoryW},{"GetTempPathA",(s9_any_fn_t)(uintptr_t)&s9_GetTempPathA},{"GetTempPathW",(s9_any_fn_t)(uintptr_t)&s9_GetTempPathW},{"GetExitCodeProcess",(s9_any_fn_t)(uintptr_t)&s9_GetExitCodeProcess},{"GetExitCodeThread",(s9_any_fn_t)(uintptr_t)&s9_GetExitCodeThread},
    {"AddAtomA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"AddAtomW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"AllocConsole",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"AreFileApisANSI",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"Beep",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"BeginUpdateResourceA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"BeginUpdateResourceW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"BuildCommDCBA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"BuildCommDCBAndTimeoutsA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"BuildCommDCBAndTimeoutsW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"BuildCommDCBW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"ClearCommBreak",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ClearCommError",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CloseHandle",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"CommConfigDialogA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CommConfigDialogW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CompareFileTime",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ConnectNamedPipe",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"ContinueDebugEvent",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"ConvertDefaultLocale",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"CopyFileA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CopyFileW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CreateConsoleScreenBuffer",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"CreateDirectoryExA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CreateDirectoryExW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"CreateEventA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"CreateEventW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"CreateFileA",(s9_any_fn_t)(uintptr_t)&s9_zero7},
    {"CreateFileMappingA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"CreateFileMappingW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"CreateFileW",(s9_any_fn_t)(uintptr_t)&s9_zero7},
    {"CreateMailslotA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"CreateMailslotW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"CreateMutexA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CreateMutexW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"CreateNamedPipeA",(s9_any_fn_t)(uintptr_t)&s9_zero8},
    {"CreateNamedPipeW",(s9_any_fn_t)(uintptr_t)&s9_zero8},
    {"CreateProcessA",(s9_any_fn_t)(uintptr_t)&s9_zero10},
    {"CreateProcessW",(s9_any_fn_t)(uintptr_t)&s9_zero10},
    {"CreateRemoteThread",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"CreateSemaphoreA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"CreateSemaphoreW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"DebugActiveProcess",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"DefineDosDeviceA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"DefineDosDeviceW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"DeleteCriticalSection",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DeleteFileA",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DeleteFileW",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DeviceIoControl",(s9_any_fn_t)(uintptr_t)&s9_true8},
    {"DisableThreadLibraryCalls",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DisconnectNamedPipe",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"DosDateTimeToFileTime",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"DuplicateHandle",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"EndUpdateResourceA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EndUpdateResourceW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnterCriticalSection",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"EnumDateFormatsA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumDateFormatsW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumResourceTypesA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumResourceTypesW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumSystemCodePagesA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumSystemCodePagesW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumSystemLocalesA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumSystemLocalesW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumTimeFormatsA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EnumTimeFormatsW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"EscapeCommFunction",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"ExitProcess",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ExitThread",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FileTimeToDosDateTime",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"FileTimeToLocalFileTime",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"FileTimeToSystemTime",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"FillConsoleOutputAttribute",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"FillConsoleOutputCharacterA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"FillConsoleOutputCharacterW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"FindAtomA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"FindAtomW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"FindClose",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FindCloseChangeNotification",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FindFirstChangeNotificationA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"FindFirstChangeNotificationW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"FindNextChangeNotification",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FlushConsoleInputBuffer",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FlushFileBuffers",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FlushInstructionCache",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"FlushViewOfFile",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"FoldStringA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"FoldStringW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"FormatMessageA",(s9_any_fn_t)(uintptr_t)&s9_zero7},
    {"FormatMessageW",(s9_any_fn_t)(uintptr_t)&s9_zero7},
    {"FreeConsole",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"FreeEnvironmentStringsA",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FreeEnvironmentStringsW",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FreeLibrary",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"FreeLibraryAndExitThread",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetACP",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetAtomNameA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetAtomNameW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetBinaryTypeA",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetBinaryTypeW",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCPInfo",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetCPInfoExA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetCPInfoExW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetCommConfig",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetCommMask",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCommModemStatus",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCommProperties",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCommState",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCommTimeouts",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetCommandLineA",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCommandLineW",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetComputerNameA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetComputerNameW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetConsoleCP",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetConsoleCursorInfo",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetConsoleMode",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetConsoleOutputCP",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetConsoleScreenBufferInfo",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetConsoleTitleA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetConsoleTitleW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetCurrentDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetCurrentDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetCurrentProcess",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCurrentProcessId",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCurrentThread",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetCurrentThreadId",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetDateFormatA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetDateFormatW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetDefaultCommConfigA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetDefaultCommConfigW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetDriveTypeA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetDriveTypeW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetEnvironmentStringsA",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetEnvironmentStringsW",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetEnvironmentVariableA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetEnvironmentVariableW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetExitCodeProcess",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetExitCodeThread",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetFileAttributesA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetFileAttributesW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetFileInformationByHandle",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetFileSize",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetFileTime",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetFileType",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetFullPathNameA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetFullPathNameW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetHandleInformation",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"GetLargestConsoleWindowSize",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetLastError",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetLocaleInfoA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"GetLocaleInfoW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"GetLogicalDriveStringsA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetLogicalDriveStringsW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetLogicalDrives",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetLongPathNameA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetLongPathNameW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetModuleFileNameA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetModuleFileNameW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetModuleHandleA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetModuleHandleW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetNamedPipeInfo",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"GetOEMCP",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetPriorityClass",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetPrivateProfileIntA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetPrivateProfileIntW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"GetPrivateProfileSectionA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetPrivateProfileSectionNamesA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetPrivateProfileSectionNamesW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetPrivateProfileSectionW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetPrivateProfileStringA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetPrivateProfileStringW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetProcAddress",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetProcessAffinityMask",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetProcessHeap",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetProcessTimes",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"GetProcessVersion",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetProfileIntA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetProfileIntW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetProfileStringA",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"GetProfileStringW",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"GetShortPathNameA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetShortPathNameW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"GetStartupInfoA",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetStartupInfoW",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetSystemDefaultLCID",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetSystemDefaultLangID",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetSystemDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetSystemDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetSystemInfo",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetSystemPowerStatus",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetSystemTime",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetSystemTimeAdjustment",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"GetSystemTimeAsFileTime",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetTempFileNameA",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetTempFileNameW",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"GetTempPathA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetTempPathW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetThreadLocale",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetThreadPriority",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"GetThreadTimes",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"GetTickCount",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetTimeFormatA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetTimeFormatW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"GetTimeZoneInformation",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"GetUserDefaultLCID",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetUserDefaultLangID",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetVersion",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"GetVolumeInformationA",(s9_any_fn_t)(uintptr_t)&s9_true8},
    {"GetVolumeInformationW",(s9_any_fn_t)(uintptr_t)&s9_true8},
    {"GetWindowsDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GetWindowsDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"GlobalMemoryStatus",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"HeapAlloc",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"HeapCompact",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"HeapDestroy",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"HeapFree",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"HeapLock",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"HeapReAlloc",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"HeapSize",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"HeapUnlock",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"InitializeCriticalSection",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"InterlockedCompareExchange",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"InterlockedDecrement",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"InterlockedExchange",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"InterlockedExchangeAdd",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"InterlockedIncrement",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"IsBadCodePtr",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsBadHugeReadPtr",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsBadHugeWritePtr",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsBadReadPtr",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsBadStringPtrA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsBadStringPtrW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsBadWritePtr",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"IsDBCSLeadByte",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"IsValidCodePage",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"LCMapStringA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"LCMapStringW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"LeaveCriticalSection",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"LoadLibraryA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"LoadLibraryExA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"LoadLibraryExW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"LoadLibraryW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"LocalFileTimeToFileTime",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"LockFile",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"LockFileEx",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"MapViewOfFile",(s9_any_fn_t)(uintptr_t)&s9_zero5},
    {"MoveFileA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"MoveFileExA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"MoveFileExW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"MoveFileW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"MulDiv",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"MultiByteToWideChar",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"OpenEventA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenEventW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenFile",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenFileMappingA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenFileMappingW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenMutexA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenMutexW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenProcess",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenSemaphoreA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenSemaphoreW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"OpenThreadToken",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"PulseEvent",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"QueryPerformanceCounter",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"QueryPerformanceFrequency",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ReadConsoleA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadConsoleInputA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadConsoleInputW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadConsoleOutputCharacterA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadConsoleOutputCharacterW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadConsoleW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReadFile",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"ReleaseMutex",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ReleaseSemaphore",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"RemoveDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"RemoveDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ResetEvent",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"ResumeThread",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SearchPathA",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"SearchPathW",(s9_any_fn_t)(uintptr_t)&s9_zero6},
    {"SetCommBreak",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetCommConfig",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetComputerNameA",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetComputerNameW",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleActiveScreenBuffer",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetConsoleCP",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleCtrlHandler",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleCursorInfo",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleCursorPosition",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleMode",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleOutputCP",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleScreenBufferSize",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleTextAttribute",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetConsoleTitleA",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetConsoleTitleW",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetConsoleWindowInfo",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"SetCurrentDirectoryA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetCurrentDirectoryW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetDefaultCommConfigA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetDefaultCommConfigW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetEndOfFile",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetEnvironmentVariableA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetEnvironmentVariableW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"SetErrorMode",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetEvent",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetFileApisToANSI",(s9_any_fn_t)(uintptr_t)&s9_true0},
    {"SetFileApisToOEM",(s9_any_fn_t)(uintptr_t)&s9_true0},
    {"SetFileAttributesA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"SetFileAttributesW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"SetFilePointer",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"SetFileTime",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"SetHandleCount",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"SetHandleInformation",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetLastError",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetLocalTime",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetNamedPipeHandleState",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"SetPriorityClass",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetProcessAffinityMask",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetProcessShutdownParameters",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetStdHandle",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetSystemTime",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SetSystemTimeAdjustment",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetThreadContext",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetThreadLocale",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetThreadPriority",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SetTimeZoneInformation",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"Sleep",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"SleepEx",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SystemTimeToFileTime",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"SystemTimeToTzSpecificLocalTime",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"TerminateProcess",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"TerminateThread",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"TlsAlloc",(s9_any_fn_t)(uintptr_t)&s9_zero0},
    {"TlsFree",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"TlsGetValue",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"TlsSetValue",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"TransactNamedPipe",(s9_any_fn_t)(uintptr_t)&s9_true7},
    {"UnlockFile",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"UnlockFileEx",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"UnmapViewOfFile",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"VerLanguageNameA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"VerLanguageNameW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"VirtualAlloc",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"VirtualFree",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"VirtualLock",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"VirtualProtect",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"VirtualProtectEx",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"VirtualQuery",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"VirtualQueryEx",(s9_any_fn_t)(uintptr_t)&s9_zero4},
    {"VirtualUnlock",(s9_any_fn_t)(uintptr_t)&s9_true1},
    {"WaitForMultipleObjects",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"WaitForSingleObject",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"WaitForSingleObjectEx",(s9_any_fn_t)(uintptr_t)&s9_true2},
    {"WideCharToMultiByte",(s9_any_fn_t)(uintptr_t)&s9_zero8},
    {"WinExec",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"WriteConsoleA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteConsoleInputA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteConsoleInputW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteConsoleOutputCharacterA",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteConsoleOutputCharacterW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteConsoleW",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WriteFile",(s9_any_fn_t)(uintptr_t)&s9_true5},
    {"WritePrivateProfileSectionA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"WritePrivateProfileSectionW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"WritePrivateProfileStringA",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"WritePrivateProfileStringW",(s9_any_fn_t)(uintptr_t)&s9_true4},
    {"WriteProfileStringA",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"WriteProfileStringW",(s9_any_fn_t)(uintptr_t)&s9_true3},
    {"lstrcatA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcatW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcmpA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcmpW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcmpiA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcmpiW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcpyA",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcpyW",(s9_any_fn_t)(uintptr_t)&s9_zero2},
    {"lstrcpynA",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"lstrcpynW",(s9_any_fn_t)(uintptr_t)&s9_zero3},
    {"lstrlenA",(s9_any_fn_t)(uintptr_t)&s9_zero1},
    {"lstrlenW",(s9_any_fn_t)(uintptr_t)&s9_zero1},
};

/* WINE_STAGE9_SAFE_ABI
 *
 * Stage 9 deja de publicar la tabla genérica completa. Un wrapper stdcall con
 * un ret N incorrecto desplaza el ESP de Ring 3. Solo se exponen firmas que
 * fueron verificadas.
 */
static int32_t WIN32_API s9_safe_DialogBoxIndirectParamA(
    void *instance UNUSED, const void *template_data UNUSED,
    void *owner UNUSED,
    uint32_t (WIN32_API *dialog_proc)(void *, uint32_t, uint32_t, int32_t)
        UNUSED,
    int32_t parameter UNUSED) {
    /* Un callback PE no puede ejecutarse directamente en Ring 0. */
    return -1;
}

static int32_t WIN32_API s9_safe_DialogBoxIndirectParamW(
    void *instance, const void *template_data, void *owner,
    uint32_t (WIN32_API *dialog_proc)(void *, uint32_t, uint32_t, int32_t),
    int32_t parameter) {
    return s9_safe_DialogBoxIndirectParamA(
        instance, template_data, owner, dialog_proc, parameter);
}

#define S9_SAFE_EXPORT(export_name, function_name) \
    if (s9_eq(name, export_name)) \
        return (uint32_t)(uintptr_t)&function_name

static uint32_t s9_safe_user_resolve(const char *name) {
    S9_SAFE_EXPORT("DrawFrameControl", s9_DrawFrameControl);
    S9_SAFE_EXPORT("DialogBoxIndirectParamA",
                   s9_safe_DialogBoxIndirectParamA);
    S9_SAFE_EXPORT("DialogBoxIndirectParamW",
                   s9_safe_DialogBoxIndirectParamW);
    S9_SAFE_EXPORT("EnumThreadWindows", s9_EnumThreadWindows);
    S9_SAFE_EXPORT("EnumWindows", s9_EnumWindows);
    S9_SAFE_EXPORT("GetWindowThreadProcessId",
                   s9_GetWindowThreadProcessId);
    S9_SAFE_EXPORT("SystemParametersInfoA", s9_SystemParametersInfoA);
    S9_SAFE_EXPORT("SystemParametersInfoW", s9_SystemParametersInfoW);
    S9_SAFE_EXPORT("AdjustWindowRect", s9_AdjustWindowRect);
    S9_SAFE_EXPORT("AdjustWindowRectEx", s9_AdjustWindowRectEx);
    S9_SAFE_EXPORT("SetRectEmpty", s9_SetRectEmpty);
    S9_SAFE_EXPORT("IsRectEmpty", s9_IsRectEmpty);
    S9_SAFE_EXPORT("EqualRect", s9_EqualRect);
    S9_SAFE_EXPORT("PtInRect", s9_PtInRect);

    /* Firmas USER32 Win95/98 verificadas. */
    S9_SAFE_EXPORT("ShowCursor", s9_true1);
    S9_SAFE_EXPORT("DrawEdge", s9_true4);
    S9_SAFE_EXPORT("IsIconic", s9_zero1);
    return 0;
}

static uint32_t s9_safe_kernel_resolve(const char *name) {
    S9_SAFE_EXPORT("GetVersionExA", s9_GetVersionExA);
    S9_SAFE_EXPORT("GetVersionExW", s9_GetVersionExW);
    S9_SAFE_EXPORT("GetSystemInfo", s9_GetSystemInfo);
    S9_SAFE_EXPORT("GlobalMemoryStatus", s9_GlobalMemoryStatus);
    S9_SAFE_EXPORT("MulDiv", s9_MulDiv);
    S9_SAFE_EXPORT("InterlockedIncrement", s9_InterlockedIncrement);
    S9_SAFE_EXPORT("InterlockedDecrement", s9_InterlockedDecrement);
    S9_SAFE_EXPORT("InterlockedExchange", s9_InterlockedExchange);
    S9_SAFE_EXPORT("InterlockedExchangeAdd", s9_InterlockedExchangeAdd);
    S9_SAFE_EXPORT("InterlockedCompareExchange",
                   s9_InterlockedCompareExchange);
    S9_SAFE_EXPORT("QueryPerformanceFrequency",
                   s9_QueryPerformanceFrequency);
    S9_SAFE_EXPORT("QueryPerformanceCounter", s9_QueryPerformanceCounter);
    S9_SAFE_EXPORT("GetComputerNameA", s9_GetComputerNameA);
    S9_SAFE_EXPORT("GetComputerNameW", s9_GetComputerNameW);
    S9_SAFE_EXPORT("GetWindowsDirectoryA", s9_GetWindowsDirectoryA);
    S9_SAFE_EXPORT("GetWindowsDirectoryW", s9_GetWindowsDirectoryW);
    S9_SAFE_EXPORT("GetSystemDirectoryA", s9_GetSystemDirectoryA);
    S9_SAFE_EXPORT("GetSystemDirectoryW", s9_GetSystemDirectoryW);
    S9_SAFE_EXPORT("GetTempPathA", s9_GetTempPathA);
    S9_SAFE_EXPORT("GetTempPathW", s9_GetTempPathW);
    S9_SAFE_EXPORT("GetExitCodeProcess", s9_GetExitCodeProcess);
    S9_SAFE_EXPORT("GetExitCodeThread", s9_GetExitCodeThread);

    /* Firmas usadas por WinRAR/Win95 y verificadas. */
    S9_SAFE_EXPORT("SetHandleCount", s9_zero1);
    S9_SAFE_EXPORT("DosDateTimeToFileTime", s9_true3);
    S9_SAFE_EXPORT("FileTimeToDosDateTime", s9_true3);
    S9_SAFE_EXPORT("SetFileTime", s9_true4);
    S9_SAFE_EXPORT("SetEnvironmentVariableA", s9_true2);
    S9_SAFE_EXPORT("SetEnvironmentVariableW", s9_true2);
    S9_SAFE_EXPORT("WritePrivateProfileSectionA", s9_true3);
    S9_SAFE_EXPORT("WritePrivateProfileSectionW", s9_true3);
    return 0;
}

#undef S9_SAFE_EXPORT

static uint32_t s9_find(const s9_export_t*t,uint32_t n,const char*s){for(uint32_t i=0;i<n;i++)if(s9_eq(s,t[i].name))return(uint32_t)(uintptr_t)t[i].fn;return 0;}
static uint32_t s9_old(const char*d,const char*n){uint32_t a=win32_wine_stage6_resolve(d,n);if(a)return a;a=win32_wine_stage5_resolve(d,n);if(a)return a;if(s9_eq(d,"USER32.DLL"))a=win32_user32_resolve(n);else if(s9_eq(d,"KERNEL32.DLL")||s9_eq(d,"KERNELBASE.DLL"))a=win32_kernel32_resolve(n);if(a)return a;return win32_win95_compat_resolve(d,n);}
uint32_t win32_wine_stage9_resolve(const char *d,
                                             const char *n) {
    if (!d || !n || s9_old(d, n)) return 0;
    if (s9_eq(d, "USER32.DLL"))
        return s9_safe_user_resolve(n);
    if (s9_eq(d, "KERNEL32.DLL") || s9_eq(d, "KERNELBASE.DLL"))
        return s9_safe_kernel_resolve(n);
    if ((s9_eq(d, "MSVCRT20.DLL") ||
         s9_eq(d, "MSVCRT40.DLL") ||
         s9_eq(d, "MSVCRT.DLL") ||
         s9_eq(d, "CRTDLL.DLL")) &&
        s9_eq(n, "?terminate@@YAXXZ"))
        return (uint32_t)(uintptr_t)&s9_cpp_terminate;
    return 0;
}

uint32_t win32_wine_stage9_resolve_ordinal(const char*d UNUSED,uint16_t o UNUSED){return 0;}
bool win32_wine_stage9_is_data_export(const char*d UNUSED,const char*n UNUSED){return false;}
bool win32_wine_stage9_init(void){return win32_register_resolver(win32_wine_stage9_resolve,win32_wine_stage9_resolve_ordinal,win32_wine_stage9_is_data_export);}
