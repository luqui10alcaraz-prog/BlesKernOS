#include <windows.h>
#include "resource_ids.h"

static INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    (void)wparam; (void)lparam;
    if (message == WM_INITDIALOG) {
        char name[32];
        SetDlgItemTextA(hwnd, IDC_NAME, "Lucas");
        CheckDlgButton(hwnd, IDC_REMEMBER, BST_CHECKED);
        if (!GetDlgItem(hwnd, IDC_NAME) ||
            GetDlgItemTextA(hwnd, IDC_NAME, name, sizeof(name)) != 5 ||
            name[0] != 'L' ||
            IsDlgButtonChecked(hwnd, IDC_REMEMBER) != BST_CHECKED) {
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        EndDialog(hwnd, IDOK);
        return TRUE;
    }
    return FALSE;
}

static void write_line(const char *text) {
    DWORD written = 0, length = 0;
    while (text[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

void entry(void) {
    HINSTANCE instance = GetModuleHandleA(0);
    INT_PTR result = DialogBoxParamA(instance, MAKEINTRESOURCEA(IDD_STAGE7),
                                     0, dialog_proc, 0);
    if (result != IDOK) {
        write_line("Stage 7 dialog test FAILED!\r\n");
        ExitProcess(1);
    }
    write_line("RT_DIALOG + controls + dialog APIs OK on BlesKernOS!\r\n");
    ExitProcess(0);
}
