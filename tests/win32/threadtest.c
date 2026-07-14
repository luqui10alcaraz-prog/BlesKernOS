/* Stage 5: hilos Win32 con TEB y TLS aislados.
 * El mismo PE32 puede ejecutarse en Windows y BlesKernOS. */

typedef unsigned char BYTE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;

#define WINAPI __attribute__((stdcall))
#define NTAPI __attribute__((stdcall))
#define DLL_PROCESS_ATTACH 1U
#define DLL_THREAD_ATTACH 2U
#define DLL_THREAD_DETACH 3U
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFU
#define INFINITE 0xFFFFFFFFU
#define WAIT_OBJECT_0 0U

typedef DWORD (WINAPI *thread_start_t)(LPVOID parameter);
typedef void (NTAPI *tls_callback_t)(LPVOID module, DWORD reason,
                                     LPVOID reserved);

__attribute__((dllimport)) HANDLE WINAPI GetStdHandle(DWORD value);
__attribute__((dllimport)) BOOL WINAPI WriteFile(HANDLE handle,
                                                  const void *buffer,
                                                  DWORD length,
                                                  DWORD *written,
                                                  void *overlapped);
__attribute__((dllimport)) void WINAPI ExitProcess(DWORD code);
__attribute__((dllimport)) HANDLE WINAPI CreateThread(LPVOID security,
                                                       DWORD stack_size,
                                                       thread_start_t start,
                                                       LPVOID parameter,
                                                       DWORD flags,
                                                       DWORD *thread_id);
__attribute__((dllimport)) DWORD WINAPI WaitForSingleObject(HANDLE handle,
                                                            DWORD milliseconds);
__attribute__((dllimport)) BOOL WINAPI GetExitCodeThread(HANDLE handle,
                                                          DWORD *exit_code);
__attribute__((dllimport)) BOOL WINAPI CloseHandle(HANDLE handle);
__attribute__((dllimport)) DWORD WINAPI GetCurrentThreadId(void);
__attribute__((dllimport)) DWORD WINAPI GetCurrentProcessId(void);
__attribute__((dllimport)) void WINAPI Sleep(DWORD milliseconds);
__attribute__((dllimport)) DWORD WINAPI TlsAlloc(void);
__attribute__((dllimport)) BOOL WINAPI TlsFree(DWORD index);
__attribute__((dllimport)) LPVOID WINAPI TlsGetValue(DWORD index);
__attribute__((dllimport)) BOOL WINAPI TlsSetValue(DWORD index, LPVOID value);

typedef struct {
    DWORD start_raw_data;
    DWORD end_raw_data;
    DWORD address_of_index;
    DWORD address_of_callbacks;
    DWORD size_of_zero_fill;
    DWORD characteristics;
} image_tls_directory32_t;

__attribute__((section(".tls$AAA"), used))
static BYTE tls_template_start[4] = {0x78, 0x56, 0x34, 0x12};
__attribute__((section(".tls$ZZZ"), used))
static BYTE tls_template_end[1];

static DWORD tls_index;
static DWORD dynamic_index;
static volatile DWORD worker_ok[2];
static volatile DWORD worker_tid[2];
static volatile DWORD worker_pid[2];
static volatile DWORD worker_teb[2];
static volatile DWORD detach_count;

static void **tls_vector(void) {
    void **vector;
    __asm__ volatile ("movl %%fs:0x2c, %0" : "=r"(vector));
    return vector;
}

static BYTE *static_tls_block(void) {
    void **vector = tls_vector();
    if (!vector || tls_index >= 64U) return 0;
    return (BYTE *)vector[tls_index];
}

static void NTAPI tls_callback(LPVOID module, DWORD reason,
                               LPVOID reserved) {
    BYTE *block = static_tls_block();
    (void)module;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH && block) block[4] = 0x11;
    if (reason == DLL_THREAD_ATTACH && block) block[4] = 0xA5;
    if (reason == DLL_THREAD_DETACH) detach_count++;
}

__attribute__((section(".CRT$XLB"), used))
static tls_callback_t tls_callbacks[2] = {tls_callback, 0};

__attribute__((section(".rdata$T"), used))
const image_tls_directory32_t _tls_used = {
    (DWORD)(unsigned long)tls_template_start,
    (DWORD)(unsigned long)tls_template_end,
    (DWORD)(unsigned long)&tls_index,
    (DWORD)(unsigned long)tls_callbacks,
    8U,
    0U
};

static void print(const char *text, DWORD length) {
    DWORD written = 0U;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

static DWORD WINAPI worker(LPVOID parameter) {
    DWORD id = (DWORD)(unsigned long)parameter;
    DWORD slot = id - 1U;
    BYTE *block = static_tls_block();
    LPVOID expected = (LPVOID)(unsigned long)(0x11110000U + id);
    BOOL ok = block && block[0] == 0x78 && block[1] == 0x56 &&
              block[2] == 0x34 && block[3] == 0x12 && block[4] == 0xA5;

    worker_tid[slot] = GetCurrentThreadId();
    worker_pid[slot] = GetCurrentProcessId();
    __asm__ volatile ("movl %%fs:0x18, %0" : "=r"(worker_teb[slot]));
    if (ok) block[0] = (BYTE)id;
    if (ok && !TlsSetValue(dynamic_index, expected)) ok = 0;
    Sleep(20U);
    if (ok && block[0] != (BYTE)id) ok = 0;
    if (ok && TlsGetValue(dynamic_index) != expected) ok = 0;
    worker_ok[slot] = ok ? 1U : 0U;
    return 100U + id;
}

void entry(void) {
    HANDLE first = 0;
    HANDLE second = 0;
    DWORD first_tid = 0U, second_tid = 0U;
    DWORD first_exit = 0U, second_exit = 0U;
    BYTE *main_block = static_tls_block();
    DWORD main_pid = GetCurrentProcessId();
    DWORD main_teb = 0U;
    BOOL ok;
    __asm__ volatile ("movl %%fs:0x18, %0" : "=r"(main_teb));
    ok = main_block && main_block[0] == 0x78 && main_block[4] == 0x11;

    dynamic_index = TlsAlloc();
    if (dynamic_index == TLS_OUT_OF_INDEXES) ok = 0;
    if (ok) first = CreateThread(0, 0U, worker, (LPVOID)1U, 0U, &first_tid);
    if (ok) second = CreateThread(0, 0U, worker, (LPVOID)2U, 0U, &second_tid);
    if (!first || !second || first_tid == second_tid) ok = 0;

    if (ok && WaitForSingleObject(first, INFINITE) != WAIT_OBJECT_0) ok = 0;
    if (ok && WaitForSingleObject(second, INFINITE) != WAIT_OBJECT_0) ok = 0;
    if (ok && !GetExitCodeThread(first, &first_exit)) ok = 0;
    if (ok && !GetExitCodeThread(second, &second_exit)) ok = 0;
    if (ok && (first_exit != 101U || second_exit != 102U)) ok = 0;
    if (ok && (!worker_ok[0] || !worker_ok[1])) ok = 0;
    if (ok && (!worker_tid[0] || !worker_tid[1] ||
               worker_tid[0] == worker_tid[1])) ok = 0;
    if (ok && (worker_pid[0] != main_pid || worker_pid[1] != main_pid)) ok = 0;
    if (ok && (!worker_teb[0] || !worker_teb[1] ||
               worker_teb[0] == worker_teb[1] ||
               worker_teb[0] == main_teb || worker_teb[1] == main_teb)) ok = 0;
    if (ok && (!main_block || main_block[0] != 0x78)) ok = 0;
    if (ok && detach_count != 2U) ok = 0;

    if (first && !CloseHandle(first)) ok = 0;
    if (second && !CloseHandle(second)) ok = 0;
    if (dynamic_index != TLS_OUT_OF_INDEXES && !TlsFree(dynamic_index)) ok = 0;

    if (ok) {
        static const char success[] =
            "CreateThread + per-thread TEB/TLS OK on BlesKernOS!\r\n";
        print(success, sizeof(success) - 1U);
        ExitProcess(0U);
    } else {
        static const char failure[] = "WIN32 THREAD TEST FAILED\r\n";
        print(failure, sizeof(failure) - 1U);
        ExitProcess(1U);
    }
}
