typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;

#define WINAPI __attribute__((stdcall))
#define DLLIMPORT __attribute__((dllimport))
#define STD_OUTPUT_HANDLE ((DWORD)-11)

DLLIMPORT HANDLE WINAPI GetStdHandle(DWORD handle);
DLLIMPORT BOOL WINAPI WriteFile(HANDLE file, const void *buffer, DWORD length,
                                DWORD *written, void *overlapped);
DLLIMPORT void WINAPI ExitProcess(DWORD status);

void mainCRTStartup(void) {
    static const char message[] = "Hello from Win32 on BlesKernOS!\r\n";
    DWORD written = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

    WriteFile(output, message, sizeof(message) - 1, &written, 0);
    ExitProcess(0);
    for (;;) {}
}
