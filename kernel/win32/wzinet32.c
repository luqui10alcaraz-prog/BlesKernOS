#include "win32.h"
#include "../stdio.h"

/*
 * WinZip 7 loads WZINET32 dynamically to integrate with Internet Explorer
 * and Netscape.  It was an optional WinZip add-on, not a Windows component.
 *
 * BlesKernOS deliberately does not ship that historical binary.  These tiny
 * replacements preserve the add-on's "not installed" behaviour: downloads
 * are never claimed by WinZip and the obsolete configuration commands are
 * harmless no-ops.  No network code, browser hooks, or third-party DLL code
 * is included here.
 */
static int WIN32_API wzinet_IBSIsDownload(const char *path UNUSED) {
    return 0;
}

static int WIN32_API wzinet_IBSUninstall(void *owner UNUSED) {
    return 1;
}

static int WIN32_API wzinet_IBSConfig(void *owner UNUSED) {
    return 1;
}

static uint8_t wzinet_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static bool wzinet_equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (wzinet_upper((uint8_t)*a++) != wzinet_upper((uint8_t)*b++))
            return false;
    }
    return *a == '\0' && *b == '\0';
}

uint32_t win32_wzinet32_resolve(const char *name) {
    if (wzinet_equal_ci(name, "IBSIsDownload")) {
        kprintf("[WIN32:wzinet] GetProcAddress IBSIsDownload\\n");
        return (uint32_t)(uintptr_t)&wzinet_IBSIsDownload;
    }
    if (wzinet_equal_ci(name, "IBSUninstall")) {
        kprintf("[WIN32:wzinet] GetProcAddress IBSUninstall\\n");
        return (uint32_t)(uintptr_t)&wzinet_IBSUninstall;
    }
    if (wzinet_equal_ci(name, "IBSConfig")) {
        kprintf("[WIN32:wzinet] GetProcAddress IBSConfig\\n");
        return (uint32_t)(uintptr_t)&wzinet_IBSConfig;
    }
    return 0U;
}
