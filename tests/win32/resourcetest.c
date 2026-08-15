#include <windows.h>
#include "resource_ids.h"

static void write_line(const char *text) {
    DWORD written = 0;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD length = 0;
    while (text[length]) length++;
    WriteFile(out, text, length, &written, 0);
}

void entry(void) {
    HMODULE module = GetModuleHandleA(0);
    HICON icon, icon_copy, icon_small;
    char text[96];
    WCHAR wide[96];
    HRSRC raw = FindResourceA(module, MAKEINTRESOURCEA(IDB_STAGE7), RT_BITMAP);
    HGLOBAL loaded = raw ? LoadResource(module, raw) : 0;
    if (!raw || !loaded || !LockResource(loaded) || !SizeofResource(module, raw) ||
        !LoadStringA(module, IDS_STAGE7, text, sizeof(text)) ||
        !LoadStringW(module, IDS_STAGE7, wide, sizeof(wide) / sizeof(wide[0])) ||
        !(icon = LoadIconA(module, MAKEINTRESOURCEA(IDI_STAGE7))) ||
        !LoadBitmapA(module, MAKEINTRESOURCEA(IDB_STAGE7))) {
        write_line("Stage 7 resource test FAILED!\r\n");
        ExitProcess(1);
    }
    icon_copy = CopyIcon(icon);
    icon_small = (HICON)LoadImageA(module, MAKEINTRESOURCEA(IDI_STAGE7),
                                  IMAGE_ICON, 16, 16, 0);
    if (!icon_copy || !icon_small || !DestroyIcon(icon_copy) ||
        !DestroyIcon(icon_small)) {
        write_line("Stage 7 icon decoding test FAILED!\r\n");
        ExitProcess(2);
    }
    write_line(text);
    write_line("\r\n");
    ExitProcess(0);
}
