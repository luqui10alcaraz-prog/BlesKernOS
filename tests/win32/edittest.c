#include <windows.h>

#ifndef EM_GETSEL
#define EM_GETSEL 0x00B0
#endif
#ifndef EM_SETSEL
#define EM_SETSEL 0x00B1
#endif
#ifndef EM_REPLACESEL
#define EM_REPLACESEL 0x00C2
#endif
#ifndef EM_GETLINECOUNT
#define EM_GETLINECOUNT 0x00BA
#define EM_LINEINDEX 0x00BB
#define EM_LINELENGTH 0x00C1
#define EM_GETLINE 0x00C4
#define EM_CANUNDO 0x00C6
#define EM_UNDO 0x00C7
#define EM_SETREADONLY 0x00CF
#endif

static void zero_bytes(void *ptr, unsigned count) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (count--) *p++ = 0;
}

static int same(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void output(const char *text) {
    DWORD written = 0, length = 0;
    while (text[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void entry(void) {
    WNDCLASSA wc;
    HINSTANCE instance = GetModuleHandleA(0);
    HWND window, edit;
    char text[128];
    char line[32];
    DWORD start = 0, end = 0;

    zero_bytes(&wc, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = "BKEditTest";
    if (!RegisterClassA(&wc)) ExitProcess(1);

    window = CreateWindowExA(0, "BKEditTest", "EDIT test",
        WS_OVERLAPPEDWINDOW, 30, 30, 420, 260, 0, 0, instance, 0);
    if (!window) ExitProcess(2);

    edit = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
        8, 8, 380, 200, window, (HMENU)101, instance, 0);
    if (!edit) ExitProcess(3);

    if (!SetWindowTextA(edit, "alpha\r\nbeta\r\ngamma")) ExitProcess(4);
    if (GetWindowTextLengthA(edit) != 18) ExitProcess(5);
    if (SendMessageA(edit, EM_GETLINECOUNT, 0, 0) != 3) ExitProcess(6);
    if (SendMessageA(edit, EM_LINEINDEX, 1, 0) != 7) ExitProcess(7);
    if (SendMessageA(edit, EM_LINELENGTH, 7, 0) != 4) ExitProcess(8);

    zero_bytes(line, sizeof(line));
    *(WORD *)line = sizeof(line) - 1;
    if (SendMessageA(edit, EM_GETLINE, 1, (LPARAM)line) != 4) ExitProcess(9);
    line[4] = 0;
    if (!same(line, "beta")) ExitProcess(10);

    SendMessageA(edit, EM_SETSEL, 7, 11);
    SendMessageA(edit, EM_REPLACESEL, TRUE, (LPARAM)"BETA");
    zero_bytes(text, sizeof(text));
    GetWindowTextA(edit, text, sizeof(text));
    if (!same(text, "alpha\r\nBETA\r\ngamma")) ExitProcess(11);
    if (!SendMessageA(edit, EM_CANUNDO, 0, 0)) ExitProcess(12);
    if (!SendMessageA(edit, EM_UNDO, 0, 0)) ExitProcess(13);

    SendMessageA(edit, EM_SETSEL, 0, 5);
    SendMessageA(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    if (start != 0 || end != 5) ExitProcess(14);
    if (!SendMessageA(edit, WM_COPY, 0, 0)) ExitProcess(15);
    SendMessageA(edit, EM_SETSEL, 18, 18);
    if (!SendMessageA(edit, WM_PASTE, 0, 0)) ExitProcess(16);
    zero_bytes(text, sizeof(text));
    GetWindowTextA(edit, text, sizeof(text));
    if (!same(text, "alpha\r\nbeta\r\ngammaalpha")) ExitProcess(17);

    SendMessageA(edit, EM_SETREADONLY, TRUE, 0);
    SendMessageA(edit, EM_SETSEL, 0, 5);
    SendMessageA(edit, EM_REPLACESEL, TRUE, (LPARAM)"XXXXX");
    zero_bytes(text, sizeof(text));
    GetWindowTextA(edit, text, sizeof(text));
    if (!same(text, "alpha\r\nbeta\r\ngammaalpha")) ExitProcess(18);

    output("Win32 multiline EDIT + selection + undo + clipboard OK!\n");
    DestroyWindow(window);
    ExitProcess(0);
}
