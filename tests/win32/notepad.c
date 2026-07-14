/* Compile as PE32/i386 without CRT; entry point: entry. */
#include <windows.h>

#define ID_EDIT 100
#define ID_OPEN 101
#define ID_SAVE 102
static HWND edit;

static void load_note(void) {
    char data[1000]; DWORD got=0;
    HANDLE f=CreateFileA("/NOTES.TXT",GENERIC_READ,0,0,OPEN_EXISTING,0,0);
    if(f==INVALID_HANDLE_VALUE)return;
    ReadFile(f,data,sizeof(data)-1,&got,0);data[got]=0;CloseHandle(f);
    SetWindowTextA(edit,data);
}
static void save_note(void) {
    char data[1000];DWORD wrote;int len=GetWindowTextA(edit,data,sizeof(data));
    HANDLE f=CreateFileA("/NOTES.TXT",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    if(f==INVALID_HANDLE_VALUE)return;
    WriteFile(f,data,len,&wrote,0);CloseHandle(f);
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_COMMAND){if(LOWORD(wp)==ID_OPEN)load_note();if(LOWORD(wp)==ID_SAVE)save_note();return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
void entry(void) {
    WNDCLASSA wc={0};MSG msg;HWND hwnd;
    wc.lpfnWndProc=proc;wc.lpszClassName="BlesWinPad";RegisterClassA(&wc);
    hwnd=CreateWindowExA(0,"BlesWinPad","WinPad",WS_OVERLAPPEDWINDOW,60,50,520,360,0,0,0,0);
    edit=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_MULTILINE,8,8,488,260,hwnd,(HMENU)ID_EDIT,0,0);
    CreateWindowExA(0,"BUTTON","Abrir",WS_CHILD|WS_VISIBLE,8,278,80,26,hwnd,(HMENU)ID_OPEN,0,0);
    CreateWindowExA(0,"BUTTON","Guardar",WS_CHILD|WS_VISIBLE,96,278,80,26,hwnd,(HMENU)ID_SAVE,0,0);
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);SetFocus(edit);
    while(GetMessageA(&msg,0,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}
    ExitProcess(0);
}
