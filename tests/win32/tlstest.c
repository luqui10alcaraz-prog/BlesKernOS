/* Prueba Win32 TLS para BlesKernOS.
 *
 * Comprueba:
 *   - IMAGE_TLS_DIRECTORY32 y copia de la plantilla TLS
 *   - callback TLS DLL_PROCESS_ATTACH antes del entry point
 *   - acceso a la tabla TLS mediante FS:[0x2c]
 *   - TlsAlloc/TlsSetValue/TlsGetValue/TlsFree
 *
 * El binario tambien debe poder abrirse en Windows de 32 bits/WoW64.
 */

typedef unsigned char BYTE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void *LPVOID;

#define WINAPI __attribute__((stdcall))
#define NTAPI __attribute__((stdcall))
#define DLL_PROCESS_ATTACH 1U
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFU

__attribute__((dllimport)) HANDLE WINAPI GetStdHandle(DWORD value);
__attribute__((dllimport)) BOOL WINAPI WriteFile(HANDLE handle,
                                                  const void *buffer,
                                                  DWORD length,
                                                  DWORD *written,
                                                  void *overlapped);
__attribute__((dllimport)) void WINAPI ExitProcess(DWORD code);
__attribute__((dllimport)) DWORD WINAPI TlsAlloc(void);
__attribute__((dllimport)) BOOL WINAPI TlsFree(DWORD index);
__attribute__((dllimport)) LPVOID WINAPI TlsGetValue(DWORD index);
__attribute__((dllimport)) BOOL WINAPI TlsSetValue(DWORD index, LPVOID value);

typedef void (NTAPI *tls_callback_t)(LPVOID module, DWORD reason,
                                     LPVOID reserved);

typedef struct {
    DWORD start_raw_data;
    DWORD end_raw_data;
    DWORD address_of_index;
    DWORD address_of_callbacks;
    DWORD size_of_zero_fill;
    DWORD characteristics;
} image_tls_directory32_t;

/* El linker PE fusiona y ordena las secciones con sufijo '$'. */
__attribute__((section(".tls$AAA"), used))
static BYTE tls_template_start[4] = {0x78, 0x56, 0x34, 0x12};

__attribute__((section(".tls$ZZZ"), used))
static BYTE tls_template_end[1];

static DWORD tls_index;
static volatile DWORD tls_callback_seen;

static void **tls_vector(void) {
    void **vector;
    __asm__ volatile ("movl %%fs:0x2c, %0" : "=r"(vector));
    return vector;
}

static BOOL static_tls_is_valid(void) {
    BYTE *block;
    void **vector = tls_vector();
    if (!vector || tls_index >= 64U) return 0;
    block = (BYTE *)vector[tls_index];
    if (!block) return 0;
    return block[0] == 0x78 && block[1] == 0x56 &&
           block[2] == 0x34 && block[3] == 0x12 &&
           block[4] == 0 && block[5] == 0 &&
           block[6] == 0 && block[7] == 0;
}

static void NTAPI tls_callback(LPVOID module, DWORD reason,
                               LPVOID reserved) {
    (void)module;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH && static_tls_is_valid())
        tls_callback_seen = 1U;
}

__attribute__((section(".CRT$XLB"), used))
static tls_callback_t tls_callbacks[2] = {tls_callback, 0};

/* GNU ld/MinGW reconoce _tls_used y crea IMAGE_DIRECTORY_ENTRY_TLS. */
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

void entry(void) {
    DWORD dynamic_index;
    LPVOID expected = (LPVOID)0x13572468U;
    BOOL ok = tls_callback_seen == 1U && static_tls_is_valid();

    dynamic_index = TlsAlloc();
    if (dynamic_index == TLS_OUT_OF_INDEXES) ok = 0;
    if (ok && !TlsSetValue(dynamic_index, expected)) ok = 0;
    if (ok && TlsGetValue(dynamic_index) != expected) ok = 0;
    if (dynamic_index != TLS_OUT_OF_INDEXES && !TlsFree(dynamic_index)) ok = 0;

    if (ok) {
        static const char success[] =
            "TLS static + callback + dynamic API OK on BlesKernOS!\r\n";
        print(success, sizeof(success) - 1U);
        ExitProcess(0U);
    } else {
        static const char failure[] = "TLS TEST FAILED\r\n";
        print(failure, sizeof(failure) - 1U);
        ExitProcess(1U);
    }
}
