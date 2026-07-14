#include "win32.h"
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
uint32_t win32_riched20_resolve(const char *name);
static uint8_t upper(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c; }
static bool equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) if (upper((uint8_t)*a++) != upper((uint8_t)*b++)) return false;
    return *a == *b;
}
uint32_t win32_resolve_import(const char *dll, const char *name) {
    if (equal_ci(dll, "NTDLL.DLL")) return win32_ntdll_resolve(name);
    if (equal_ci(dll, "KERNEL32.DLL") || equal_ci(dll, "KERNELBASE.DLL"))
        return win32_kernel32_resolve(name);
    if (equal_ci(dll, "USER32.DLL")) return win32_user32_resolve(name);
    if (equal_ci(dll, "MSVCRT.DLL")) return win32_msvcrt_resolve(name);
    if (equal_ci(dll, "GDI32.DLL")) return win32_gdi32_resolve(name);
    if (equal_ci(dll, "COMCTL32.DLL")) return win32_comctl32_resolve(name);
    if (equal_ci(dll, "COMDLG32.DLL")) return win32_comdlg32_resolve(name);
    if (equal_ci(dll, "ADVAPI32.DLL")) return win32_advapi32_resolve(name);
    if (equal_ci(dll, "SHELL32.DLL")) return win32_shell32_resolve(name);
    if (equal_ci(dll, "RICHED20.DLL") || equal_ci(dll, "RICHED32.DLL"))
        return win32_riched20_resolve(name);
    return 0;
}
uint32_t win32_resolve_ordinal(const char *dll, uint16_t ordinal) {
    if (equal_ci(dll, "COMCTL32.DLL"))
        return win32_comctl32_resolve_ordinal(ordinal);
    return 0;
}
