#include <windows.h>

#define IDR_MENU 401
#define IDR_ACCEL 402
#define IDM_TEST 5001

static volatile int command_seen;

/* Keep this PE test fully freestanding.  Using ZeroMemory expands to memset
 * with MinGW headers, but the test deliberately links with -nostdlib.
 * volatile prevents GCC from recognizing this as a library memset call. */
static __attribute__((noinline)) void zero_bytes(void *ptr, unsigned count){
    volatile unsigned char *p=(volatile unsigned char*)ptr;
    while(count--)*p++=0;
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_COMMAND&&LOWORD(wp)==IDM_TEST){command_seen=1;return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static void output(const char*s){DWORD n=0,l=0;while(s[l])l++;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,l,&n,0);}
static void fail(const char *step, UINT code){
    output("[WCCOMPAT] FAIL: ");
    output(step);
    output("\n");
    ExitProcess(code);
}
void entry(void){
    char value[32]; WNDCLASSA wc; MSG msg; RECT r; PAINTSTRUCT ps;
    HINSTANCE inst; HMENU menu; HACCEL accel; HWND hwnd;
    output("[WCCOMPAT] entry\n");
    inst=GetModuleHandleA(0);
    if(!inst)fail("GetModuleHandleA",20);
    output("[WCCOMPAT] profile write\n");
    if(!WriteProfileStringA("SciCalc","layout","1"))fail("WriteProfileStringA",1);
    output("[WCCOMPAT] profile read\n");
    if(GetProfileStringA("SciCalc","layout","0",value,sizeof(value))!=1||value[0]!='1')fail("GetProfileStringA",2);
    zero_bytes(&wc,sizeof(wc));wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName="WineCalcCompat";
    output("[WCCOMPAT] window class\n");
    if(!RegisterClassA(&wc))fail("RegisterClassA",3);
    hwnd=CreateWindowExA(0,"WineCalcCompat","WineCalc compatibility",WS_OVERLAPPEDWINDOW,40,40,300,180,0,0,inst,0);
    if(!hwnd)fail("CreateWindowExA",4);
    output("[WCCOMPAT] menu + accelerator\n");
    menu=LoadMenuA(inst,MAKEINTRESOURCEA(IDR_MENU));if(!menu||!SetMenu(hwnd,menu))fail("LoadMenuA/SetMenu",5);
    CheckMenuItem(menu,IDM_TEST,MF_BYCOMMAND|MF_CHECKED);
    accel=LoadAcceleratorsA(inst,MAKEINTRESOURCEA(IDR_ACCEL));if(!accel)fail("LoadAcceleratorsA",6);
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);InvalidateRect(hwnd,0,TRUE);
    output("[WCCOMPAT] window APIs\n");
    if(!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0)fail("GetClientRect",7);
    EnableWindow(hwnd,FALSE);EnableWindow(hwnd,TRUE);
    output("[WCCOMPAT] memory GDI\n");
    HDC dc=BeginPaint(hwnd,&ps);HDC mem=CreateCompatibleDC(dc);HBITMAP bm=CreateCompatibleBitmap(dc,32,32);
    if(!dc||!mem||!bm)fail("BeginPaint/CreateCompatibleDC/CreateCompatibleBitmap",8);SelectObject(mem,bm);SetPixel(mem,1,1,RGB(255,0,0));
    if(!BitBlt(dc,4,4,32,32,mem,0,0,SRCCOPY))fail("BitBlt",9);EndPaint(hwnd,&ps);DeleteDC(mem);DeleteObject(bm);
    zero_bytes(&msg,sizeof(msg));msg.hwnd=hwnd;msg.message=WM_KEYDOWN;msg.wParam='T';
    output("[WCCOMPAT] accelerator\n");
    if(!TranslateAcceleratorA(hwnd,accel,&msg)||!command_seen)fail("TranslateAcceleratorA/WM_COMMAND",10);
    msg.message=WM_KEYDOWN;msg.wParam=VK_TAB;IsDialogMessageA(hwnd,&msg);
    output("WineCalc target APIs OK on BlesKernOS!\n");
    DestroyAcceleratorTable(accel);DestroyMenu(menu);DestroyWindow(hwnd);ExitProcess(0);
}
