/* BlesKernOS Win32/Wine compatibility port - Stage 5. */
#include "win32.h"
#include "process.h"
#include "../include/types.h"
#include "../include/memory.h"

#define ERROR_SUCCESS 0U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define ERROR_INVALID_PARAMETER 87U
#define SD_REVISION 1U
#define CLASS_E_CLASSNOTAVAILABLE ((int32_t)0x80040111U)

extern uint32_t win32_kernel32_resolve(const char *name);
extern uint32_t win32_user32_resolve(const char *name);
extern uint32_t win32_ntdll_resolve(const char *name);

static uint8_t up(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c; }
static bool eq(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b)if(up((uint8_t)*a++)!=up((uint8_t)*b++))return false;return *a==*b;}
static void seterr(uint32_t e){win32_process_set_last_error(e);}
static void *move_mem(void*d,const void*s,uint32_t n){uint8_t*o=d;const uint8_t*i=s;if(!o||!i||o==i)return d;if(o<i)for(uint32_t x=0;x<n;x++)o[x]=i[x];else for(uint32_t x=n;x;x--)o[x-1]=i[x-1];return d;}

/* KERNEL32 */
typedef int(WIN32_API*mkdir_a_fn)(const char*,void*);
typedef int(WIN32_API*mkdir_w_fn)(const uint16_t*,void*);
static int WIN32_API p_CreateDirectoryExA(const char*t UNUSED,const char*n,void*s){mkdir_a_fn f=(mkdir_a_fn)(uintptr_t)win32_kernel32_resolve("CreateDirectoryA");if(!f||!n){seterr(ERROR_INVALID_PARAMETER);return 0;}return f(n,s);}
static int WIN32_API p_CreateDirectoryExW(const uint16_t*t UNUSED,const uint16_t*n,void*s){mkdir_w_fn f=(mkdir_w_fn)(uintptr_t)win32_kernel32_resolve("CreateDirectoryW");if(!f||!n){seterr(ERROR_INVALID_PARAMETER);return 0;}return f(n,s);}
static void WIN32_API p_RtlZeroMemory(void*d,uint32_t n){if(d&&n)kmemset(d,0,n);}
static void WIN32_API p_RtlFillMemory(void*d,uint32_t n,uint8_t v){if(d&&n)kmemset(d,v,n);}
static void WIN32_API p_RtlMoveMemory(void*d,const void*s,uint32_t n){(void)move_mem(d,s,n);}
static void*WIN32_API p_RtlSecureZeroMemory(void*d,uint32_t n){volatile uint8_t*p=d;if(p)while(n--)*p++=0;return d;}
static int WIN32_API p_GetCommModemStatus(void*h UNUSED,uint32_t*s){if(!s){seterr(ERROR_INVALID_PARAMETER);return 0;}*s=0;seterr(0);return 1;}
static int WIN32_API p_comm_true(void*h UNUSED,uint32_t a UNUSED){seterr(0);return 1;}
static int WIN32_API p_SetupComm(void*h UNUSED,uint32_t inq UNUSED,uint32_t outq UNUSED){seterr(0);return 1;}
static int WIN32_API p_GetCommMask(void*h UNUSED,uint32_t*m){if(!m)return 0;*m=0;return 1;}
static int WIN32_API p_GetCommTimeouts(void*h UNUSED,void*t){if(!t)return 0;kmemset(t,0,20);return 1;}
static int WIN32_API p_SetCommTimeouts(void*h UNUSED,const void*t){return t!=NULL;}
static int WIN32_API p_ClearCommError(void*h UNUSED,uint32_t*e,void*s){if(e)*e=0;if(s)kmemset(s,0,20);return 1;}

/* USER32 */
typedef void*(WIN32_API*get_window_fn)(void);
static void*WIN32_API p_GetShellWindow(void){get_window_fn f=(get_window_fn)(uintptr_t)win32_user32_resolve("GetDesktopWindow");return f?f():NULL;}
static const char*WIN32_API p_CharNextA(const char*p){return p&&*p?p+1:p;}
static const char*WIN32_API p_CharPrevA(const char*s,const char*p){return !s||!p||p<=s?s:p-1;}
static const char*WIN32_API p_CharNextExA(uint16_t cp UNUSED,const char*p,uint32_t fl UNUSED){return p_CharNextA(p);}
static const char*WIN32_API p_CharPrevExA(uint16_t cp UNUSED,const char*s,const char*p,uint32_t fl UNUSED){return p_CharPrevA(s,p);}
static int WIN32_API p_ExitWindowsEx(uint32_t f UNUSED,uint32_t r UNUSED){seterr(0);return 1;}

#define DDE_STR_MAGIC 0x44535452U
typedef struct{uint32_t magic,length;char text[1];}dde_str_t;
static uint32_t dde_instance=1;
static uint32_t WIN32_API p_DdeInitializeA(uint32_t*i,void*c UNUSED,uint32_t f UNUSED,uint32_t r UNUSED){if(!i)return 0x4006U;*i=dde_instance++;return 0;}
static uint32_t WIN32_API p_DdeInitializeW(uint32_t*i,void*c,uint32_t f,uint32_t r){return p_DdeInitializeA(i,c,f,r);}
static int WIN32_API p_DdeUninitialize(uint32_t i UNUSED){return 1;}
static void*WIN32_API p_DdeCreateStringHandleA(uint32_t i UNUSED,const char*t,int cp UNUSED){uint32_t n;dde_str_t*h;if(!t)return NULL;n=(uint32_t)kstrlen(t);h=kmalloc(sizeof(*h)+n);if(!h)return NULL;h->magic=DDE_STR_MAGIC;h->length=n;kmemcpy(h->text,t,n+1);return h;}
static void*WIN32_API p_DdeCreateStringHandleW(uint32_t i,const uint16_t*t,int cp){char b[260];uint32_t n=0;if(!t)return NULL;while(t[n]&&n+1<sizeof(b)){b[n]=t[n]<=255?(char)t[n]:'?';n++;}b[n]=0;return p_DdeCreateStringHandleA(i,b,cp);}
static int WIN32_API p_DdeFreeStringHandle(uint32_t i UNUSED,void*v){dde_str_t*h=v;if(!h||h->magic!=DDE_STR_MAGIC)return 0;h->magic=0;kfree(h);return 1;}
static int WIN32_API p_DdeKeepStringHandle(uint32_t i UNUSED,void*h){return h!=NULL;}
static uint32_t WIN32_API p_DdeQueryStringA(uint32_t i UNUSED,void*v,char*b,uint32_t cap,int cp UNUSED){dde_str_t*h=v;uint32_t n;if(!h||h->magic!=DDE_STR_MAGIC)return 0;if(!b||!cap)return h->length;n=h->length<cap-1?h->length:cap-1;kmemcpy(b,h->text,n);b[n]=0;return n;}
static uint32_t WIN32_API p_DdeQueryStringW(uint32_t i,void*v,uint16_t*b,uint32_t cap,int cp){char a[260];uint32_t n=p_DdeQueryStringA(i,v,a,sizeof(a),cp),c;if(!b||!cap)return n;c=n<cap-1?n:cap-1;for(uint32_t x=0;x<c;x++)b[x]=(uint8_t)a[x];b[c]=0;return c;}
static int WIN32_API p_DdeCmpStringHandles(void*a,void*b){dde_str_t*x=a,*y=b;if(x==y)return 0;if(!x||x->magic!=DDE_STR_MAGIC)return-1;if(!y||y->magic!=DDE_STR_MAGIC)return 1;return kstrcmp(x->text,y->text);}
static void*WIN32_API p_DdeConnect(uint32_t i UNUSED,void*s UNUSED,void*t UNUSED,void*c UNUSED){return NULL;}
static int WIN32_API p_DdeDisconnect(void*c UNUSED){return 1;}
static int WIN32_API p_DdeDisconnectList(void*l UNUSED){return 1;}
static uint32_t WIN32_API p_DdeGetLastError(uint32_t i UNUSED){return 0;}
static void*WIN32_API p_DdeNameService(uint32_t i UNUSED,void*s UNUSED,void*r UNUSED,uint32_t c UNUSED){return(void*)(uintptr_t)1;}
static int WIN32_API p_DdePostAdvise(uint32_t i UNUSED,void*t UNUSED,void*x UNUSED){return 1;}
static int WIN32_API p_DdeEnableCallback(uint32_t i UNUSED,void*c UNUSED,uint32_t f UNUSED){return 1;}

/* ADVAPI32 */
typedef struct PACKED{uint8_t rev,sbz;uint16_t control;void*owner,*group,*sacl,*dacl;}security_desc_t;
typedef struct PACKED{uint8_t rev,sbz;uint16_t size,count,sbz2;}acl_t;
static int WIN32_API p_InitializeSecurityDescriptor(void*v,uint32_t r){security_desc_t*s=v;if(!s||r!=SD_REVISION){seterr(ERROR_INVALID_PARAMETER);return 0;}kmemset(s,0,sizeof(*s));s->rev=SD_REVISION;seterr(0);return 1;}
static int WIN32_API p_IsValidSecurityDescriptor(const void*v){const security_desc_t*s=v;return s&&s->rev==SD_REVISION;}
static uint32_t WIN32_API p_GetSecurityDescriptorLength(const void*v){return p_IsValidSecurityDescriptor(v)?sizeof(security_desc_t):0;}
static int WIN32_API p_SetSecurityDescriptorDacl(void*v,int present,void*d,int def UNUSED){security_desc_t*s=v;if(!p_IsValidSecurityDescriptor(s))return 0;s->dacl=present?d:NULL;s->control=present?(uint16_t)(s->control|4):(uint16_t)(s->control&~4);return 1;}
static int WIN32_API p_GetSecurityDescriptorDacl(const void*v,int*present,void**d,int*def){const security_desc_t*s=v;if(!p_IsValidSecurityDescriptor(s))return 0;if(present)*present=(s->control&4)!=0;if(d)*d=s->dacl;if(def)*def=0;return 1;}
static int WIN32_API p_SetSecurityDescriptorSacl(void*v,int present,void*d,int def UNUSED){security_desc_t*s=v;if(!p_IsValidSecurityDescriptor(s))return 0;s->sacl=present?d:NULL;s->control=present?(uint16_t)(s->control|0x10):(uint16_t)(s->control&~0x10);return 1;}
static int WIN32_API p_SetSecurityDescriptorOwner(void*v,void*o,int def UNUSED){security_desc_t*s=v;if(!p_IsValidSecurityDescriptor(s))return 0;s->owner=o;return 1;}
static int WIN32_API p_SetSecurityDescriptorGroup(void*v,void*g,int def UNUSED){security_desc_t*s=v;if(!p_IsValidSecurityDescriptor(s))return 0;s->group=g;return 1;}
static int WIN32_API p_InitializeAcl(void*v,uint32_t n,uint32_t r){acl_t*a=v;if(!a||n<sizeof(*a)||!r){seterr(ERROR_INVALID_PARAMETER);return 0;}kmemset(a,0,n);a->rev=(uint8_t)r;a->size=(uint16_t)n;return 1;}
static int WIN32_API p_IsValidAcl(const void*v){const acl_t*a=v;return a&&a->rev&&a->size>=sizeof(*a);}
static int WIN32_API p_OpenProcessToken(void*p UNUSED,uint32_t a UNUSED,void**t){if(!t)return 0;*t=(void*)(uintptr_t)0x7A000001U;return 1;}
static int WIN32_API p_OpenThreadToken(void*p UNUSED,uint32_t a UNUSED,int self UNUSED,void**t){if(!t)return 0;*t=(void*)(uintptr_t)0x7A000002U;return 1;}
static int WIN32_API p_GetTokenInformation(void*t UNUSED,uint32_t c UNUSED,void*b,uint32_t n,uint32_t*need){if(need)*need=4;if(!b||n<4)return 0;*(uint32_t*)b=0;return 1;}
static int WIN32_API p_AdjustTokenPrivileges(void*t UNUSED,int d UNUSED,void*n UNUSED,uint32_t c UNUSED,void*o UNUSED,uint32_t*need){if(need)*need=0;return 1;}
static int WIN32_API p_LookupPrivilegeValueA(const char*s UNUSED,const char*n UNUSED,void*l){if(!l)return 0;kmemset(l,0,8);return 1;}
static int WIN32_API p_LookupPrivilegeValueW(const uint16_t*s UNUSED,const uint16_t*n UNUSED,void*l){return p_LookupPrivilegeValueA(NULL,NULL,l);}

/* SHDOCVW */
typedef int(WIN32_API*msgbox_fn)(void*,const char*,const char*,uint32_t);
static uint32_t WIN32_API p_IEWinMain(char*cmd,int show UNUSED){msgbox_fn f=(msgbox_fn)(uintptr_t)win32_user32_resolve("MessageBoxA");if(f)f(NULL,cmd&&*cmd?cmd:"Internet Explorer cargado mediante SHDOCVW.","BlesKernOS Internet",0x40);return 0;}
static int32_t WIN32_API p_DllCanUnloadNow(void){return 0;}
static int32_t WIN32_API p_DllGetClassObject(const void*c UNUSED,const void*i UNUSED,void**o){if(o)*o=NULL;return CLASS_E_CLASSNOTAVAILABLE;}
static int32_t WIN32_API p_DllRegisterServer(void){return 0;}
static int32_t WIN32_API p_DllUnregisterServer(void){return 0;}
static int32_t WIN32_API p_DllInstall(int i UNUSED,const uint16_t*c UNUSED){return 0;}
static uint32_t WIN32_API p_WinList_Init(void){return 0;}
static int WIN32_API p_ShellDDEInit(int s UNUSED){return 1;}
static uint32_t WIN32_API p_RunInstallUninstallStubs(void){return 0;}
static uint32_t WIN32_API p_RunInstallUninstallStubs2(int a UNUSED){return 0;}

/* TAPI32: zero-device virtual provider. */
static uint32_t tapi_handle=0x74000001U;
static int32_t tapi_struct(void*v){uint32_t n;if(!v)return(int32_t)0x8000004BU;n=*(uint32_t*)v;if(n<12||n>65536)return(int32_t)0x8000004BU;kmemset((uint8_t*)v+4,0,n-4);((uint32_t*)v)[0]=n;((uint32_t*)v)[1]=12;((uint32_t*)v)[2]=12;return 0;}
static int32_t WIN32_API p_lineInitializeA(void**a,void*i UNUSED,void*c UNUSED,const char*n UNUSED,uint32_t*d){if(!a||!d)return(int32_t)0x8000004BU;*a=(void*)(uintptr_t)tapi_handle++;*d=0;return 0;}
static int32_t WIN32_API p_lineInitializeW(void**a,void*i,void*c,const uint16_t*n UNUSED,uint32_t*d){return p_lineInitializeA(a,i,c,NULL,d);}
static int32_t WIN32_API p_lineInitializeExA(void**a,void*i,void*c,const char*n,uint32_t*d,uint32_t*v,void*p UNUSED){int32_t r=p_lineInitializeA(a,i,c,n,d);if(!r&&v)*v=0x20000;return r;}
static int32_t WIN32_API p_lineInitializeExW(void**a,void*i,void*c,const uint16_t*n,uint32_t*d,uint32_t*v,void*p UNUSED){int32_t r=p_lineInitializeW(a,i,c,n,d);if(!r&&v)*v=0x20000;return r;}
static int32_t WIN32_API p_lineShutdown(void*a UNUSED){return 0;}
static int32_t WIN32_API p_lineNegotiateAPIVersion(void*a UNUSED,uint32_t d UNUSED,uint32_t lo,uint32_t hi,uint32_t*v,void*e UNUSED){uint32_t x=hi>0x20000?0x20000:hi;if(x<lo)return(int32_t)0x80000048U;if(v)*v=x;return 0;}
static int32_t WIN32_API p_lineGetDevCapsA(void*a UNUSED,uint32_t d UNUSED,uint32_t v UNUSED,uint32_t e UNUSED,void*c){return tapi_struct(c);}
static int32_t WIN32_API p_lineGetDevCapsW(void*a,uint32_t d,uint32_t v,uint32_t e,void*c){return p_lineGetDevCapsA(a,d,v,e,c);}
static int32_t WIN32_API p_lineOpen(void*a UNUSED,uint32_t d UNUSED,void**l,uint32_t v UNUSED,uint32_t e UNUSED,uintptr_t ci UNUSED,uint32_t p UNUSED,uint32_t m UNUSED,void*cp UNUSED){if(!l)return(int32_t)0x8000004BU;*l=(void*)(uintptr_t)tapi_handle++;return 0;}
static int32_t WIN32_API p_lineClose(void*l UNUSED){return 0;}
static int32_t WIN32_API p_lineMakeCallA(void*l UNUSED,void**c,const char*d UNUSED,uint32_t co UNUSED,void*p UNUSED){if(c)*c=(void*)(uintptr_t)tapi_handle++;return 0;}
static int32_t WIN32_API p_lineMakeCallW(void*l,void**c,const uint16_t*d UNUSED,uint32_t co,void*p){return p_lineMakeCallA(l,c,NULL,co,p);}
static int32_t WIN32_API p_lineDrop(void*c UNUSED,const char*u UNUSED,uint32_t n UNUSED){return 0;}
static int32_t WIN32_API p_lineDeallocateCall(void*c UNUSED){return 0;}
static int32_t WIN32_API p_tapiRequestMakeCallA(const char*d UNUSED,const char*a UNUSED,const char*c UNUSED,const char*m UNUSED){return 0;}
static int32_t WIN32_API p_tapiRequestMakeCallW(const uint16_t*d UNUSED,const uint16_t*a UNUSED,const uint16_t*c UNUSED,const uint16_t*m UNUSED){return 0;}

uint32_t win32_wine_stage5_resolve(const char*dll,const char*name){
 if(!dll||!name)return 0;
 if(eq(dll,"KERNEL32.DLL")||eq(dll,"KERNELBASE.DLL")){
  if(eq(name,"GetEnvironmentStrings"))return win32_kernel32_resolve("GetEnvironmentStringsA");
  if(eq(name,"FreeEnvironmentStrings"))return win32_kernel32_resolve("FreeEnvironmentStringsA");
  if(eq(name,"RtlUnwind"))return win32_ntdll_resolve("RtlUnwind");
  if(eq(name,"RtlUnwindEx"))return win32_ntdll_resolve("RtlUnwindEx");
#define X(n) if(eq(name,#n))return(uint32_t)(uintptr_t)&p_##n
  X(CreateDirectoryExA);X(CreateDirectoryExW);X(RtlZeroMemory);X(RtlFillMemory);X(RtlMoveMemory);X(RtlSecureZeroMemory);X(GetCommModemStatus);X(GetCommMask);X(GetCommTimeouts);X(SetCommTimeouts);X(ClearCommError);
#undef X
  if(eq(name,"RtlCopyMemory")||eq(name,"CopyMemory")||eq(name,"MoveMemory"))return(uint32_t)(uintptr_t)&p_RtlMoveMemory;
  if(eq(name,"ZeroMemory"))return(uint32_t)(uintptr_t)&p_RtlZeroMemory;
  if(eq(name,"SetupComm"))return(uint32_t)(uintptr_t)&p_SetupComm;
  if(eq(name,"SetCommState")||eq(name,"PurgeComm")||eq(name,"EscapeCommFunction")||eq(name,"SetCommMask"))return(uint32_t)(uintptr_t)&p_comm_true;
 }
 if(eq(dll,"USER32.DLL")){
#define X(n) if(eq(name,#n))return(uint32_t)(uintptr_t)&p_##n
  X(GetShellWindow);X(CharNextA);X(CharPrevA);X(CharNextExA);X(CharPrevExA);X(ExitWindowsEx);X(DdeInitializeA);X(DdeInitializeW);X(DdeUninitialize);X(DdeCreateStringHandleA);X(DdeCreateStringHandleW);X(DdeFreeStringHandle);X(DdeKeepStringHandle);X(DdeQueryStringA);X(DdeQueryStringW);X(DdeCmpStringHandles);X(DdeConnect);X(DdeDisconnect);X(DdeDisconnectList);X(DdeGetLastError);X(DdeNameService);X(DdePostAdvise);X(DdeEnableCallback);
#undef X
 }
 if(eq(dll,"ADVAPI32.DLL")){
#define X(n) if(eq(name,#n))return(uint32_t)(uintptr_t)&p_##n
  X(InitializeSecurityDescriptor);X(IsValidSecurityDescriptor);X(GetSecurityDescriptorLength);X(SetSecurityDescriptorDacl);X(GetSecurityDescriptorDacl);X(SetSecurityDescriptorSacl);X(SetSecurityDescriptorOwner);X(SetSecurityDescriptorGroup);X(InitializeAcl);X(IsValidAcl);X(OpenProcessToken);X(OpenThreadToken);X(GetTokenInformation);X(AdjustTokenPrivileges);X(LookupPrivilegeValueA);X(LookupPrivilegeValueW);
#undef X
 }
 if(eq(dll,"SHDOCVW.DLL")){
#define X(n) if(eq(name,#n))return(uint32_t)(uintptr_t)&p_##n
  X(IEWinMain);X(DllCanUnloadNow);X(DllGetClassObject);X(DllRegisterServer);X(DllUnregisterServer);X(DllInstall);X(WinList_Init);X(ShellDDEInit);X(RunInstallUninstallStubs);X(RunInstallUninstallStubs2);
#undef X
 }
 if(eq(dll,"TAPI32.DLL")){
#define X(n) if(eq(name,#n))return(uint32_t)(uintptr_t)&p_##n
  X(lineInitializeA);X(lineInitializeW);X(lineInitializeExA);X(lineInitializeExW);X(lineShutdown);X(lineNegotiateAPIVersion);X(lineGetDevCapsA);X(lineGetDevCapsW);X(lineOpen);X(lineClose);X(lineMakeCallA);X(lineMakeCallW);X(lineDrop);X(lineDeallocateCall);X(tapiRequestMakeCallA);X(tapiRequestMakeCallW);
#undef X
  if(eq(name,"lineInitialize"))return(uint32_t)(uintptr_t)&p_lineInitializeA;
  if(eq(name,"lineGetDevCaps"))return(uint32_t)(uintptr_t)&p_lineGetDevCapsA;
  if(eq(name,"lineMakeCall"))return(uint32_t)(uintptr_t)&p_lineMakeCallA;
  if(eq(name,"tapiRequestMakeCall"))return(uint32_t)(uintptr_t)&p_tapiRequestMakeCallA;
 }
 return 0;
}

uint32_t win32_wine_stage5_resolve_ordinal(const char*dll,uint16_t ordinal){if(!dll||!eq(dll,"SHDOCVW.DLL"))return 0;switch(ordinal){case 101:return(uint32_t)(uintptr_t)&p_IEWinMain;case 110:return(uint32_t)(uintptr_t)&p_WinList_Init;case 118:return(uint32_t)(uintptr_t)&p_ShellDDEInit;case 125:return(uint32_t)(uintptr_t)&p_RunInstallUninstallStubs;case 130:return(uint32_t)(uintptr_t)&p_RunInstallUninstallStubs2;default:return 0;}}
