/* Stage 6: eventos, mutexes, semaforos, critical sections e interlocked.
 * El mismo PE32 puede ejecutarse en Windows y BlesKernOS. */

typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;

#define WINAPI __attribute__((stdcall))
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define INFINITE 0xFFFFFFFFU
#define WAIT_OBJECT_0 0U
#define WAIT_TIMEOUT 0x102U

typedef DWORD (WINAPI *thread_start_t)(LPVOID parameter);

typedef struct {
    LPVOID DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    DWORD SpinCount;
} CRITICAL_SECTION;

__attribute__((dllimport)) HANDLE WINAPI GetStdHandle(DWORD value);
__attribute__((dllimport)) BOOL WINAPI WriteFile(HANDLE handle,
                                                  const void *buffer,
                                                  DWORD length,
                                                  DWORD *written,
                                                  void *overlapped);
__attribute__((dllimport)) void WINAPI ExitProcess(DWORD code);
__attribute__((dllimport)) void WINAPI Sleep(DWORD milliseconds);
__attribute__((dllimport)) HANDLE WINAPI CreateThread(LPVOID security,
                                                       DWORD stack_size,
                                                       thread_start_t start,
                                                       LPVOID parameter,
                                                       DWORD flags,
                                                       DWORD *thread_id);
__attribute__((dllimport)) DWORD WINAPI WaitForSingleObject(HANDLE handle,
                                                            DWORD milliseconds);
__attribute__((dllimport)) DWORD WINAPI WaitForMultipleObjects(DWORD count,
                                                                HANDLE *handles,
                                                                BOOL wait_all,
                                                                DWORD milliseconds);
__attribute__((dllimport)) BOOL WINAPI GetExitCodeThread(HANDLE handle,
                                                          DWORD *exit_code);
__attribute__((dllimport)) BOOL WINAPI CloseHandle(HANDLE handle);

__attribute__((dllimport)) HANDLE WINAPI CreateEventA(LPVOID security,
                                                       BOOL manual_reset,
                                                       BOOL initial_state,
                                                       LPCSTR name);
__attribute__((dllimport)) BOOL WINAPI SetEvent(HANDLE event);
__attribute__((dllimport)) BOOL WINAPI ResetEvent(HANDLE event);
__attribute__((dllimport)) HANDLE WINAPI CreateMutexA(LPVOID security,
                                                       BOOL initial_owner,
                                                       LPCSTR name);
__attribute__((dllimport)) BOOL WINAPI ReleaseMutex(HANDLE mutex);
__attribute__((dllimport)) HANDLE WINAPI CreateSemaphoreA(LPVOID security,
                                                           LONG initial_count,
                                                           LONG maximum_count,
                                                           LPCSTR name);
__attribute__((dllimport)) BOOL WINAPI ReleaseSemaphore(HANDLE semaphore,
                                                         LONG release_count,
                                                         LONG *previous_count);

__attribute__((dllimport)) void WINAPI InitializeCriticalSection(
    CRITICAL_SECTION *critical);
__attribute__((dllimport)) BOOL WINAPI InitializeCriticalSectionAndSpinCount(
    CRITICAL_SECTION *critical, DWORD spin_count);
__attribute__((dllimport)) void WINAPI DeleteCriticalSection(
    CRITICAL_SECTION *critical);
__attribute__((dllimport)) void WINAPI EnterCriticalSection(
    CRITICAL_SECTION *critical);
__attribute__((dllimport)) BOOL WINAPI TryEnterCriticalSection(
    CRITICAL_SECTION *critical);
__attribute__((dllimport)) void WINAPI LeaveCriticalSection(
    CRITICAL_SECTION *critical);
__attribute__((dllimport)) DWORD WINAPI SetCriticalSectionSpinCount(
    CRITICAL_SECTION *critical, DWORD spin_count);

__attribute__((dllimport)) LONG WINAPI InterlockedIncrement(volatile LONG *value);
__attribute__((dllimport)) LONG WINAPI InterlockedDecrement(volatile LONG *value);
__attribute__((dllimport)) LONG WINAPI InterlockedExchange(volatile LONG *target,
                                                            LONG value);
__attribute__((dllimport)) LONG WINAPI InterlockedExchangeAdd(volatile LONG *target,
                                                               LONG value);
__attribute__((dllimport)) LONG WINAPI InterlockedCompareExchange(
    volatile LONG *target, LONG exchange, LONG compare);

static HANDLE start_event;
static HANDLE gate_mutex;
static HANDLE done_semaphore;
static CRITICAL_SECTION critical;
static volatile LONG protected_counter;
static volatile LONG atomic_counter;

static void print(const char *text, DWORD length) {
    DWORD written = 0U;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, length, &written, 0);
}

static DWORD WINAPI worker(LPVOID parameter) {
    DWORD id = (DWORD)(unsigned long)parameter;
    if (WaitForSingleObject(start_event, INFINITE) != WAIT_OBJECT_0)
        return 10U + id;
    if (WaitForSingleObject(gate_mutex, INFINITE) != WAIT_OBJECT_0)
        return 20U + id;
    if (!ReleaseMutex(gate_mutex)) return 30U + id;

    for (DWORD i = 0; i < 200U; i++) {
        EnterCriticalSection(&critical);
        protected_counter++;
        LeaveCriticalSection(&critical);
    }
    InterlockedIncrement(&atomic_counter);
    if (!ReleaseSemaphore(done_semaphore, 1, 0)) return 40U + id;
    return 100U + id;
}

void entry(void) {
    HANDLE threads[2] = {0, 0};
    HANDLE wait_any[2] = {0, 0};
    HANDLE auto_event = 0;
    DWORD tids[2] = {0, 0};
    DWORD exits[2] = {0, 0};
    volatile LONG interlocked = 0;
    BOOL ok = 1;

    start_event = CreateEventA(0, 1, 0, 0);
    auto_event = CreateEventA(0, 0, 0, 0);
    wait_any[0] = CreateEventA(0, 1, 0, 0);
    wait_any[1] = CreateEventA(0, 1, 1, 0);
    gate_mutex = CreateMutexA(0, 1, 0);
    done_semaphore = CreateSemaphoreA(0, 0, 2, 0);
    if (!start_event || !auto_event || !wait_any[0] || !wait_any[1] ||
        !gate_mutex || !done_semaphore) ok = 0;

    if (ok && !InitializeCriticalSectionAndSpinCount(&critical, 4000U)) ok = 0;
    if (ok && !TryEnterCriticalSection(&critical)) ok = 0;
    if (ok) LeaveCriticalSection(&critical);
    if (ok && SetCriticalSectionSpinCount(&critical, 0U) != 4000U) ok = 0;

    if (ok) threads[0] = CreateThread(0, 0U, worker, (LPVOID)1U, 0U, &tids[0]);
    if (ok) threads[1] = CreateThread(0, 0U, worker, (LPVOID)2U, 0U, &tids[1]);
    if (!threads[0] || !threads[1] || tids[0] == tids[1]) ok = 0;

    if (ok && !SetEvent(start_event)) ok = 0;
    Sleep(20U);
    if (ok && !ReleaseMutex(gate_mutex)) ok = 0;

    if (ok && WaitForMultipleObjects(2U, threads, 1, INFINITE) != WAIT_OBJECT_0)
        ok = 0;
    if (ok && !GetExitCodeThread(threads[0], &exits[0])) ok = 0;
    if (ok && !GetExitCodeThread(threads[1], &exits[1])) ok = 0;
    if (ok && (exits[0] != 101U || exits[1] != 102U)) ok = 0;
    if (ok && protected_counter != 400) ok = 0;
    if (ok && atomic_counter != 2) ok = 0;

    if (ok && WaitForSingleObject(done_semaphore, 0U) != WAIT_OBJECT_0) ok = 0;
    if (ok && WaitForSingleObject(done_semaphore, 0U) != WAIT_OBJECT_0) ok = 0;
    if (ok && WaitForSingleObject(done_semaphore, 0U) != WAIT_TIMEOUT) ok = 0;

    if (ok && !SetEvent(auto_event)) ok = 0;
    if (ok && WaitForSingleObject(auto_event, 0U) != WAIT_OBJECT_0) ok = 0;
    if (ok && WaitForSingleObject(auto_event, 0U) != WAIT_TIMEOUT) ok = 0;

    if (ok && WaitForMultipleObjects(2U, wait_any, 0, 0U) != WAIT_OBJECT_0 + 1U)
        ok = 0;
    if (ok && !ResetEvent(wait_any[1])) ok = 0;
    if (ok && WaitForMultipleObjects(2U, wait_any, 0, 0U) != WAIT_TIMEOUT)
        ok = 0;

    if (ok && InterlockedIncrement(&interlocked) != 1) ok = 0;
    if (ok && InterlockedExchangeAdd(&interlocked, 4) != 1) ok = 0;
    if (ok && InterlockedCompareExchange(&interlocked, 9, 5) != 5) ok = 0;
    if (ok && InterlockedExchange(&interlocked, 3) != 9) ok = 0;
    if (ok && InterlockedDecrement(&interlocked) != 2) ok = 0;

    DeleteCriticalSection(&critical);
    for (DWORD i = 0; i < 2U; i++) {
        if (threads[i] && !CloseHandle(threads[i])) ok = 0;
        if (wait_any[i] && !CloseHandle(wait_any[i])) ok = 0;
    }
    if (start_event && !CloseHandle(start_event)) ok = 0;
    if (auto_event && !CloseHandle(auto_event)) ok = 0;
    if (gate_mutex && !CloseHandle(gate_mutex)) ok = 0;
    if (done_semaphore && !CloseHandle(done_semaphore)) ok = 0;

    if (ok) {
        static const char success[] =
            "Win32 events + mutexes + semaphores + critical sections OK!\r\n";
        print(success, sizeof(success) - 1U);
        ExitProcess(0U);
    } else {
        static const char failure[] = "WIN32 SYNC TEST FAILED\r\n";
        print(failure, sizeof(failure) - 1U);
        ExitProcess(1U);
    }
}
