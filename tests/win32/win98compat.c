typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef long LONG;
typedef int BOOL;
typedef void *HANDLE;
typedef unsigned short WCHAR;
#define WINAPI __attribute__((stdcall))
#define STD_OUTPUT_HANDLE ((DWORD)-11)
typedef struct {DWORD size,major,minor,build,platform;char csd[128];} OSVERSIONINFOA;
typedef struct {LONG left,top,right,bottom;} RECT;
typedef struct {DWORD d1;WORD d2,d3;unsigned char d4[8];} GUID;
typedef struct {WORD vt,r1,r2,r3;DWORD value,high;} VARIANT32;
typedef struct {DWORD size,usage,pid,heap,module,threads,parent;LONG priority;DWORD flags;char exe[260];} PROCESSENTRY32A;
__attribute__((dllimport)) HANDLE WINAPI GetStdHandle(DWORD);
__attribute__((dllimport)) BOOL WINAPI WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
__attribute__((dllimport)) void WINAPI ExitProcess(DWORD);
__attribute__((dllimport)) BOOL WINAPI GetVersionExA(OSVERSIONINFOA*);
__attribute__((dllimport)) void* WINAPI GetDesktopWindow(void);
__attribute__((dllimport)) BOOL WINAPI GetClientRect(void*,RECT*);
__attribute__((dllimport)) BOOL WINAPI AdjustWindowRectEx(RECT*,DWORD,BOOL,DWORD);
__attribute__((dllimport)) DWORD WINAPI CoInitialize(void*);
__attribute__((dllimport)) void WINAPI CoUninitialize(void);
__attribute__((dllimport)) void* WINAPI CoTaskMemAlloc(DWORD);
__attribute__((dllimport)) void WINAPI CoTaskMemFree(void*);
__attribute__((dllimport)) WCHAR* WINAPI SysAllocString(const WCHAR*);
__attribute__((dllimport)) DWORD WINAPI SysStringLen(const WCHAR*);
__attribute__((dllimport)) void WINAPI SysFreeString(WCHAR*);
__attribute__((dllimport)) void WINAPI VariantInit(VARIANT32*);
__attribute__((dllimport)) DWORD WINAPI GetFileVersionInfoSizeA(const char*,DWORD*);
__attribute__((dllimport)) DWORD WINAPI timeGetTime(void);
__attribute__((dllimport)) char* WINAPI PathFindFileNameA(const char*);
__attribute__((dllimport)) DWORD WINAPI UuidCreate(GUID*);
__attribute__((dllimport)) BOOL WINAPI ImmReleaseContext(void*,void*);
__attribute__((dllimport)) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD,DWORD);
__attribute__((dllimport)) BOOL WINAPI Process32First(HANDLE,PROCESSENTRY32A*);
__attribute__((dllimport)) BOOL WINAPI Process32Next(HANDLE,PROCESSENTRY32A*);
__attribute__((dllimport)) BOOL WINAPI CloseHandle(HANDLE);
static void out(const char*s){DWORD n=0,l=0;while(s[l])l++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,l,&n,0);}
static void fail(DWORD code){out("[WIN98COMPAT] FAIL\n");ExitProcess(code);}
void entry(void){OSVERSIONINFOA os;RECT r;GUID id;VARIANT32 v;PROCESSENTRY32A process;HANDLE snapshot;DWORD dummy=0;WCHAR word[3]={'O','K',0};WCHAR*b;void*p;os.size=sizeof(os);if(!GetVersionExA(&os)||os.major!=4||os.minor!=10)fail(1);if(!GetDesktopWindow()||!GetClientRect(GetDesktopWindow(),&r)||r.right<=0)fail(2);r.left=0;r.top=0;r.right=100;r.bottom=100;if(!AdjustWindowRectEx(&r,0x00800000U,0,0))fail(3);if(CoInitialize(0)>1U)fail(4);p=CoTaskMemAlloc(64);if(!p)fail(5);CoTaskMemFree(p);b=SysAllocString(word);if(!b||SysStringLen(b)!=2)fail(6);SysFreeString(b);VariantInit(&v);if(v.vt!=0)fail(7);if(GetFileVersionInfoSizeA("C:\\TEST.EXE",&dummy)==0)fail(8);(void)timeGetTime();if(PathFindFileNameA("C:\\WINDOWS\\TEST.EXE")[0]!='T')fail(9);if(UuidCreate(&id)!=0)fail(10);if(!ImmReleaseContext(0,0))fail(11);snapshot=CreateToolhelp32Snapshot(2,0);process.size=sizeof(process);if(snapshot==(HANDLE)-1||!Process32First(snapshot,&process)||!process.pid||!CloseHandle(snapshot))fail(12);CoUninitialize();out("Win95/98 compatibility APIs OK!\n");ExitProcess(0);}
