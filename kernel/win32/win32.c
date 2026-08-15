#include "win32.h"
bool win32_wine_library_init(void);
bool win32_wine_stage6_is_data_export(const char *dll, const char *name);
uint32_t win32_wine_stage6_resolve(const char *dll, const char *name);
uint32_t win32_wine_stage6_resolve_ordinal(const char *dll, uint16_t ordinal);
bool win32_wine_stage7_is_data_export(const char *dll, const char *name);
uint32_t win32_wine_stage7_resolve(const char *dll, const char *name);
uint32_t win32_wine_stage7_resolve_ordinal(const char *dll, uint16_t ordinal);
bool win32_wine_stage8_is_data_export(const char *dll, const char *name);
uint32_t win32_wine_stage8_resolve(const char *dll, const char *name);
uint32_t win32_wine_stage8_resolve_ordinal(const char *dll, uint16_t ordinal);
bool win32_wine_stage9_is_data_export(const char *dll, const char *name);
uint32_t win32_wine_stage9_resolve(const char *dll, const char *name);
uint32_t win32_wine_stage9_resolve_ordinal(const char *dll, uint16_t ordinal);
uint32_t win32_wine_stage5_resolve(const char *dll, const char *name);
uint32_t win32_wine_stage5_resolve_ordinal(const char *dll, uint16_t ordinal);
uint32_t win32_win95_compat_resolve(const char *dll, const char *name);
uint32_t win32_winzip70_compat_resolve(const char *dll, const char *name);
uint32_t win32_user32_resolve(const char *name);
uint32_t win32_kernel32_resolve(const char *name);
uint32_t win32_msvcrt_resolve(const char *name);
uint32_t win32_gdi32_resolve(const char *name);
uint32_t win32_ntdll_resolve(const char *name);
uint32_t win32_comctl32_resolve(const char *name);
uint32_t win32_comctl32_resolve_ordinal(uint16_t ordinal);
uint32_t win32_comdlg32_resolve(const char *name);
uint32_t win32_advapi32_resolve(const char *name);
uint32_t win32_shell32_resolve(const char *name);
uint32_t win32_shell32_resolve_ordinal(uint16_t ordinal);
uint32_t win32_riched20_resolve(const char *name);
uint32_t win32_ole32_resolve(const char *name);
uint32_t win32_oleaut32_resolve(const char *name);
uint32_t win32_oleaut32_resolve_ordinal(uint16_t ordinal);
uint32_t win32_version_resolve(const char *name);
uint32_t win32_winmm_resolve(const char *name);
uint32_t win32_winmm_resolve_ordinal(uint16_t ordinal);
uint32_t win32_imm32_resolve(const char *name);
uint32_t win32_shlwapi_resolve(const char *name);
uint32_t win32_rpcrt4_resolve(const char *name);
uint32_t win32_winspool_resolve(const char *name);
uint32_t win32_winsock_resolve(const char *name);
uint32_t win32_winsock_resolve_ordinal(uint16_t ordinal);
uint32_t win32_ddraw_resolve(const char *name);
uint32_t win32_dsound_resolve(const char *name);
uint32_t win32_dinput_resolve(const char *name);
uint32_t win32_lz32_resolve(const char *name);
uint32_t win32_wininet_resolve(const char *name);
uint32_t win32_msacm32_resolve(const char *name);
uint32_t win32_avifil32_resolve(const char *name);
uint32_t win32_mpr_resolve(const char *name);
uint32_t win32_wzinet32_resolve(const char *name);
uint32_t __attribute__((stdcall)) win32_kernel32_GetLastError(void);
void __attribute__((stdcall)) win32_kernel32_SetLastError(uint32_t error);

#define WIN32_DYNAMIC_RESOLVER_MAX 16U
typedef struct {
    win32_named_resolver_t named;
    win32_ordinal_resolver_t ordinal;
    win32_data_resolver_t data;
} win32_dynamic_resolver_t;
static win32_dynamic_resolver_t win32_dynamic_resolvers[WIN32_DYNAMIC_RESOLVER_MAX];
static uint32_t win32_dynamic_resolver_count;

bool win32_register_resolver(win32_named_resolver_t named,
                             win32_ordinal_resolver_t ordinal,
                             win32_data_resolver_t data) {
    if (!named && !ordinal && !data) return false;
    for (uint32_t i = 0; i < win32_dynamic_resolver_count; i++) {
        if (win32_dynamic_resolvers[i].named == named &&
            win32_dynamic_resolvers[i].ordinal == ordinal &&
            win32_dynamic_resolvers[i].data == data) return true;
    }
    if (win32_dynamic_resolver_count >= WIN32_DYNAMIC_RESOLVER_MAX) return false;
    win32_dynamic_resolvers[win32_dynamic_resolver_count++] =
        (win32_dynamic_resolver_t){named, ordinal, data};
    return true;
}
static uint8_t upper(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c; }
static bool equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) if (upper((uint8_t)*a++) != upper((uint8_t)*b++)) return false;
    return *a == *b;
}
static bool name_is_read_file(const char *name) {
    return name && upper((uint8_t)name[0]) == 'R' &&
           upper((uint8_t)name[1]) == 'E' && upper((uint8_t)name[2]) == 'A' &&
           upper((uint8_t)name[3]) == 'D' && upper((uint8_t)name[4]) == 'F' &&
           upper((uint8_t)name[5]) == 'I' && upper((uint8_t)name[6]) == 'L' &&
           upper((uint8_t)name[7]) == 'E' && name[8] == '\0';
}
static bool name_is_close_handle(const char *name) {
    return name && upper((uint8_t)name[0]) == 'C' &&
           upper((uint8_t)name[1]) == 'L' && upper((uint8_t)name[2]) == 'O' &&
           upper((uint8_t)name[3]) == 'S' && upper((uint8_t)name[4]) == 'E' &&
           upper((uint8_t)name[5]) == 'H' && upper((uint8_t)name[6]) == 'A' &&
           upper((uint8_t)name[7]) == 'N' && upper((uint8_t)name[8]) == 'D' &&
           upper((uint8_t)name[9]) == 'L' && upper((uint8_t)name[10]) == 'E' &&
           name[11] == '\0';
}
static bool name_is_dde_client_transaction(const char *name) {
    static const char expected[] = "DdeClientTransaction";
    uint32_t i = 0U;
    if (!name) return false;
    while (expected[i] && name[i]) {
        if (upper((uint8_t)expected[i]) != upper((uint8_t)name[i]))
            return false;
        i++;
    }
    return expected[i] == '\0' && name[i] == '\0';
}

/* Minimal Windows/Wine-compatible failure path for a transaction without a
 * valid DDE conversation.  Keeping this core fallback makes the USER32 import
 * available even while the resident compatibility stages are initializing. */
static void *WIN32_API core_DdeClientTransaction(
    uint8_t *data UNUSED, uint32_t size UNUSED, void *conversation UNUSED,
    void *item UNUSED, uint32_t format UNUSED, uint32_t type UNUSED,
    uint32_t timeout UNUSED, uint32_t *result) {
    if (result) *result = 0U;
    return NULL;
}
bool win32_is_builtin_dll(const char *dll) {
    static const char *const names[]={
        "NTDLL.DLL","KERNEL32.DLL","KERNELBASE.DLL","USER32.DLL","USER.EXE","GDI.EXE","MSVCRT.DLL","CRTDLL.DLL","MSVCRT20.DLL","MSVCRT40.DLL",
        "GDI32.DLL","COMCTL32.DLL","COMDLG32.DLL","ADVAPI32.DLL","SHELL32.DLL","RICHED20.DLL","RICHED32.DLL",
        "OLE32.DLL","OLEAUT32.DLL","VERSION.DLL","WINMM.DLL","MMSYSTEM.DLL","IMM32.DLL","SHLWAPI.DLL","RPCRT4.DLL",
        "WINSPOOL.DRV","WINSPOOL.DLL","WSOCK32.DLL","WS2_32.DLL","DDRAW.DLL","DSOUND.DLL","DINPUT.DLL","DINPUT8.DLL",
        "LZ32.DLL","WININET.DLL","MSACM32.DLL","AVIFIL32.DLL","MPR.DLL","WZINET32.DLL"
    };
    for(uint32_t i=0;i<sizeof(names)/sizeof(names[0]);i++)if(equal_ci(dll,names[i]))return true;
    return false;
}
uint32_t win32_resolve_import(const char *dll, const char *name) {
    /* These two functions back the per-thread Win32 error state and must
     * always come from the real KERNEL32 implementation.  Compatibility
     * stages may contain harmless fallback stubs, but allowing those stages
     * to participate before the core resolver can hide or lose GetLastError
     * while loading old SFX installers. */
    if (equal_ci(dll, "KERNEL32.DLL") || equal_ci(dll, "KERNELBASE.DLL")) {
        if (name_is_read_file(name))
            return (uint32_t)(uintptr_t)&win32_kernel32_ReadFile;
        if (name_is_close_handle(name))
            return (uint32_t)(uintptr_t)&win32_kernel32_CloseHandle;
        if (equal_ci(name, "GetLastError"))
            return (uint32_t)(uintptr_t)&win32_kernel32_GetLastError;
        if (equal_ci(name, "SetLastError"))
            return (uint32_t)(uintptr_t)&win32_kernel32_SetLastError;
    }
    if (equal_ci(dll, "USER32.DLL") &&
        name_is_dde_client_transaction(name))
        return (uint32_t)(uintptr_t)&core_DdeClientTransaction;

    /* BLES_WINE_CORE_IMPORT_FIX_20260723_RESOLVER
     * Stages 7/8 contain compatibility fallbacks for these names. Resolve
     * the corrected core implementations first so an old zero/memcpy stub
     * cannot shadow them after wine_library_init() registers its stages. */
    if (equal_ci(dll, "USER32.DLL") &&
        (equal_ci(name, "OemToCharA") ||
         equal_ci(name, "OemToCharW") ||
         equal_ci(name, "OemToCharBuffA") ||
         equal_ci(name, "OemToCharBuffW") ||
         equal_ci(name, "CharToOemA") ||
         equal_ci(name, "CharToOemW") ||
         equal_ci(name, "CharToOemBuffA") ||
         equal_ci(name, "CharToOemBuffW") ||
         equal_ci(name, "AnsiToOemA") ||
         equal_ci(name, "OemToAnsiA") ||
         equal_ci(name, "AnsiToOemBuffA") ||
         equal_ci(name, "OemToAnsiBuffA") ||
         equal_ci(name, "IsIconic") ||
         equal_ci(name, "IsZoomed"))) {
        uint32_t core = win32_user32_resolve(name);
        if (core) return core;
    }
    if ((equal_ci(dll, "KERNEL32.DLL") ||
         equal_ci(dll, "KERNELBASE.DLL")) &&
        equal_ci(name, "SetConsoleCtrlHandler")) {
        uint32_t core = win32_kernel32_resolve(name);
        if (core) return core;
    }

    /*
     * BLES_WINE_CORE_RESOLVER_PRIORITY_20260723
     *
     * Las etapas Wine son fallbacks. Nunca deben ocultar una implementación
     * real del núcleo.
     */
    {
        uint32_t core = 0U;

        if (equal_ci(dll, "NTDLL.DLL"))
            core = win32_ntdll_resolve(name);
        else if (equal_ci(dll, "KERNEL32.DLL") ||
                 equal_ci(dll, "KERNELBASE.DLL"))
            core = win32_kernel32_resolve(name);
        else if (equal_ci(dll, "USER32.DLL"))
            core = win32_user32_resolve(name);
        else if (equal_ci(dll, "MSVCRT.DLL") ||
                 equal_ci(dll, "CRTDLL.DLL") ||
                 equal_ci(dll, "MSVCRT20.DLL") ||
                 equal_ci(dll, "MSVCRT40.DLL"))
            core = win32_msvcrt_resolve(name);
        else if (equal_ci(dll, "GDI32.DLL"))
            core = win32_gdi32_resolve(name);
        else if (equal_ci(dll, "COMCTL32.DLL"))
            core = win32_comctl32_resolve(name);
        else if (equal_ci(dll, "COMDLG32.DLL"))
            core = win32_comdlg32_resolve(name);
        else if (equal_ci(dll, "ADVAPI32.DLL"))
            core = win32_advapi32_resolve(name);
        else if (equal_ci(dll, "SHELL32.DLL"))
            core = win32_shell32_resolve(name);
        else if (equal_ci(dll, "RICHED20.DLL") ||
                 equal_ci(dll, "RICHED32.DLL"))
            core = win32_riched20_resolve(name);
        else if (equal_ci(dll, "OLE32.DLL"))
            core = win32_ole32_resolve(name);
        else if (equal_ci(dll, "OLEAUT32.DLL"))
            core = win32_oleaut32_resolve(name);
        else if (equal_ci(dll, "VERSION.DLL"))
            core = win32_version_resolve(name);
        else if (equal_ci(dll, "WINMM.DLL") ||
                 equal_ci(dll, "MMSYSTEM.DLL"))
            core = win32_winmm_resolve(name);
        else if (equal_ci(dll, "IMM32.DLL"))
            core = win32_imm32_resolve(name);
        else if (equal_ci(dll, "SHLWAPI.DLL"))
            core = win32_shlwapi_resolve(name);
        else if (equal_ci(dll, "RPCRT4.DLL"))
            core = win32_rpcrt4_resolve(name);
        else if (equal_ci(dll, "WINSPOOL.DRV") ||
                 equal_ci(dll, "WINSPOOL.DLL"))
            core = win32_winspool_resolve(name);
        else if (equal_ci(dll, "WSOCK32.DLL") ||
                 equal_ci(dll, "WS2_32.DLL"))
            core = win32_winsock_resolve(name);
        else if (equal_ci(dll, "DDRAW.DLL"))
            core = win32_ddraw_resolve(name);
        else if (equal_ci(dll, "DSOUND.DLL"))
            core = win32_dsound_resolve(name);
        else if (equal_ci(dll, "DINPUT.DLL") ||
                 equal_ci(dll, "DINPUT8.DLL"))
            core = win32_dinput_resolve(name);
        else if (equal_ci(dll, "LZ32.DLL"))
            core = win32_lz32_resolve(name);
        else if (equal_ci(dll, "WININET.DLL"))
            core = win32_wininet_resolve(name);
        else if (equal_ci(dll, "MSACM32.DLL"))
            core = win32_msacm32_resolve(name);
        else if (equal_ci(dll, "AVIFIL32.DLL"))
            core = win32_avifil32_resolve(name);
        else if (equal_ci(dll, "MPR.DLL"))
            core = win32_mpr_resolve(name);

        if (core) return core;
    }

    (void)win32_wine_library_init();
    for (uint32_t i = 0; i < win32_dynamic_resolver_count; i++) {
        uint32_t address = win32_dynamic_resolvers[i].named
            ? win32_dynamic_resolvers[i].named(dll, name) : 0U;
        if (address) return address;
    }

    /* The resident Wine stages are also explicit fallbacks.  Registration is
     * useful for external extensions, but the built-in compatibility surface
     * must never disappear merely because initialization was re-entered while
     * a PE import table was being resolved. */
    uint32_t wine_stage7_address = win32_wine_stage7_resolve(dll, name);
    if (wine_stage7_address) return wine_stage7_address;

    uint32_t wine_stage8_address = win32_wine_stage8_resolve(dll, name);
    if (wine_stage8_address) return wine_stage8_address;

    uint32_t wine_stage9_address = win32_wine_stage9_resolve(dll, name);
    if (wine_stage9_address) return wine_stage9_address;

    uint32_t wine_stage6_address = win32_wine_stage6_resolve(dll, name);
    if (wine_stage6_address) return wine_stage6_address;

    uint32_t wine_stage5_address = win32_wine_stage5_resolve(dll, name);
    if (wine_stage5_address) return wine_stage5_address;

    uint32_t win95_compat_address = win32_win95_compat_resolve(dll, name);
    if (win95_compat_address) return win95_compat_address;

    /* BLES_WINE_WINZIP70_IMPORTS_20260723_RESOLVER
     * Keep the verified wrappers ahead of the core DLL switch: those switches
     * return 0 immediately for an unknown name. */
    uint32_t winzip70_compat_address =
        win32_winzip70_compat_resolve(dll, name);
    if (winzip70_compat_address) return winzip70_compat_address;

    if (equal_ci(dll, "NTDLL.DLL")) return win32_ntdll_resolve(name);
    if (equal_ci(dll, "KERNEL32.DLL") || equal_ci(dll, "KERNELBASE.DLL"))
        return win32_kernel32_resolve(name);
    if (equal_ci(dll, "USER32.DLL")) return win32_user32_resolve(name);
    if (equal_ci(dll, "MSVCRT.DLL") || equal_ci(dll, "CRTDLL.DLL") ||
        equal_ci(dll, "MSVCRT20.DLL") || equal_ci(dll, "MSVCRT40.DLL"))
        return win32_msvcrt_resolve(name);
    if (equal_ci(dll, "GDI32.DLL")) return win32_gdi32_resolve(name);
    if (equal_ci(dll, "COMCTL32.DLL")) return win32_comctl32_resolve(name);
    if (equal_ci(dll, "COMDLG32.DLL")) return win32_comdlg32_resolve(name);
    if (equal_ci(dll, "ADVAPI32.DLL")) return win32_advapi32_resolve(name);
    if (equal_ci(dll, "SHELL32.DLL")) return win32_shell32_resolve(name);
    if (equal_ci(dll, "RICHED20.DLL") || equal_ci(dll, "RICHED32.DLL"))
        return win32_riched20_resolve(name);
    if (equal_ci(dll, "OLE32.DLL")) return win32_ole32_resolve(name);
    if (equal_ci(dll, "OLEAUT32.DLL")) return win32_oleaut32_resolve(name);
    if (equal_ci(dll, "VERSION.DLL")) return win32_version_resolve(name);
    if (equal_ci(dll, "WINMM.DLL") || equal_ci(dll, "MMSYSTEM.DLL"))
        return win32_winmm_resolve(name);
    if (equal_ci(dll, "IMM32.DLL")) return win32_imm32_resolve(name);
    if (equal_ci(dll, "SHLWAPI.DLL")) return win32_shlwapi_resolve(name);
    if (equal_ci(dll, "RPCRT4.DLL")) return win32_rpcrt4_resolve(name);
    if (equal_ci(dll, "WINSPOOL.DRV") || equal_ci(dll, "WINSPOOL.DLL"))
        return win32_winspool_resolve(name);
    if (equal_ci(dll, "WSOCK32.DLL") || equal_ci(dll, "WS2_32.DLL"))
        return win32_winsock_resolve(name);
    if (equal_ci(dll, "DDRAW.DLL")) return win32_ddraw_resolve(name);
    if (equal_ci(dll, "DSOUND.DLL")) return win32_dsound_resolve(name);
    if (equal_ci(dll, "DINPUT.DLL") || equal_ci(dll, "DINPUT8.DLL"))
        return win32_dinput_resolve(name);
    if (equal_ci(dll, "LZ32.DLL")) return win32_lz32_resolve(name);
    if (equal_ci(dll, "WININET.DLL")) return win32_wininet_resolve(name);
    if (equal_ci(dll, "MSACM32.DLL")) return win32_msacm32_resolve(name);
    if (equal_ci(dll, "AVIFIL32.DLL")) return win32_avifil32_resolve(name);
    if (equal_ci(dll, "MPR.DLL")) return win32_mpr_resolve(name);
    if (equal_ci(dll, "WZINET32.DLL")) return win32_wzinet32_resolve(name);
    return 0;
}
uint32_t win32_resolve_ordinal(const char *dll, uint16_t ordinal) {
    (void)win32_wine_library_init();
    for (uint32_t i = 0; i < win32_dynamic_resolver_count; i++) {
        uint32_t address = win32_dynamic_resolvers[i].ordinal
            ? win32_dynamic_resolvers[i].ordinal(dll, ordinal) : 0U;
        if (address) return address;
    }

    uint32_t wine_stage7_address =
        win32_wine_stage7_resolve_ordinal(dll, ordinal);
    if (wine_stage7_address) return wine_stage7_address;

    uint32_t wine_stage8_address =
        win32_wine_stage8_resolve_ordinal(dll, ordinal);
    if (wine_stage8_address) return wine_stage8_address;

    uint32_t wine_stage9_address =
        win32_wine_stage9_resolve_ordinal(dll, ordinal);
    if (wine_stage9_address) return wine_stage9_address;

    uint32_t wine_stage6_address = win32_wine_stage6_resolve_ordinal(dll, ordinal);
    if (wine_stage6_address) return wine_stage6_address;

    uint32_t wine_stage5_address = win32_wine_stage5_resolve_ordinal(dll, ordinal);
    if (wine_stage5_address) return wine_stage5_address;

    if (equal_ci(dll, "SHELL32.DLL"))
        return win32_shell32_resolve_ordinal(ordinal);
    if (equal_ci(dll, "COMCTL32.DLL"))
        return win32_comctl32_resolve_ordinal(ordinal);
    if (equal_ci(dll, "OLEAUT32.DLL"))
        return win32_oleaut32_resolve_ordinal(ordinal);
    if (equal_ci(dll, "WINMM.DLL") || equal_ci(dll, "MMSYSTEM.DLL"))
        return win32_winmm_resolve_ordinal(ordinal);
    if (equal_ci(dll, "WSOCK32.DLL") || equal_ci(dll, "WS2_32.DLL"))
        return win32_winsock_resolve_ordinal(ordinal);
    return 0;
}

bool win32_import_is_data(const char *dll, const char *name) {
    (void)win32_wine_library_init();
    static const char *const crt_data[] = {
        "__initenv", "__winitenv", "_environ", "_wenviron",
        "__argc", "__argv", "__wargv", "_acmdln", "_wcmdln",
        "_fmode", "_commode", "__mb_cur_max", "_iob",
        "_acmdln_dll", "_commode_dll", "_fmode_dll", "_aexit_rtn_dll"
    };
    for (uint32_t i = 0; i < win32_dynamic_resolver_count; i++)
        if (win32_dynamic_resolvers[i].data &&
            win32_dynamic_resolvers[i].data(dll, name)) return true;
    if (win32_wine_stage7_is_data_export(dll, name) ||
        win32_wine_stage8_is_data_export(dll, name) ||
        win32_wine_stage9_is_data_export(dll, name)) return true;
    if (win32_wine_stage6_is_data_export(dll, name)) return true;
    if (!(equal_ci(dll, "MSVCRT.DLL") || equal_ci(dll, "MSVCRT20.DLL") ||
          equal_ci(dll, "MSVCRT40.DLL") || equal_ci(dll, "CRTDLL.DLL") ||
          equal_ci(dll, "MSVCRTD.DLL"))) return false;
    for (uint32_t i = 0; i < sizeof(crt_data) / sizeof(crt_data[0]); i++)
        if (equal_ci(name, crt_data[i])) return true;
    return false;
}
