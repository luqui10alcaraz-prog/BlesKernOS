#include <windows.h>
#include "resource_ids.h"

static volatile int command_seen;

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    (void)lparam;
    if (message == WM_COMMAND && LOWORD(wparam) == IDM_EXIT) {
        command_seen = 1;
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static void write_line(const char *text) {
    DWORD written = 0, length = 0;
    while (text[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

void entry(void) {
    HINSTANCE instance = GetModuleHandleA(0);
    WNDCLASSA cls;
    for (unsigned i = 0; i < sizeof(cls); i++) ((unsigned char *)&cls)[i] = 0;
    cls.lpfnWndProc = window_proc;
    cls.hInstance = instance;
    cls.lpszClassName = "Stage7MenuWindow";
    if (!RegisterClassA(&cls)) ExitProcess(1);
    HWND hwnd = CreateWindowExA(0, cls.lpszClassName, "Resource menu",
                                WS_OVERLAPPEDWINDOW, 80, 60, 360, 220,
                                0, 0, instance, 0);
    HMENU menu = LoadMenuA(instance, MAKEINTRESOURCEA(IDM_STAGE7));
    if (!hwnd || !menu || !SetMenu(hwnd, menu) || GetMenu(hwnd) != menu) {
        write_line("Stage 7 menu test FAILED!\r\n");
        ExitProcess(2);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SendMessageA(hwnd, WM_COMMAND, IDM_EXIT, 0);
    if (!command_seen) {
        write_line("Stage 7 menu command FAILED!\r\n");
        ExitProcess(3);
    }
    write_line("PE menu resource + WM_COMMAND OK on BlesKernOS!\r\n");
    ExitProcess(0);
}
