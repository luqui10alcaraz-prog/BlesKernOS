/*
 * BlesKernOS Win32/Wine compatibility port - Stage 7.
 *
 * Legacy USER32, console KERNEL32 and MSVCRT20/40 compatibility surface.
 * Kept in kernel/win32/ until the Win32 subsystem is split into modules.
 */
#include "win32.h"
#include "../include/types.h"
#include "../include/api.h"
#include "../include/driver.h"
#include "../stdio.h"
#include "../stdlib.h"
#include "../string.h"

extern uint32_t win32_user32_resolve(const char *name);
extern uint32_t win32_kernel32_resolve(const char *name);

#define S7_ERROR_SUCCESS 0U
#define S7_ERROR_INVALID_PARAMETER 87U
#define S7_INVALID_FILE_ATTRIBUTES 0xFFFFFFFFU

static uint8_t s7_up(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}
static bool s7_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (s7_up((uint8_t)*a++) != s7_up((uint8_t)*b++)) return false;
    }
    return *a == *b;
}
static uint32_t s7_len(const char *s) {
    uint32_t n = 0U;
    if (s) while (s[n]) n++;
    return n;
}
static uint32_t s7_wlen(const uint16_t *s) {
    uint32_t n = 0U;
    if (s) while (s[n]) n++;
    return n;
}
static char *s7_copy(char *d, const char *s) {
    char *r = d;
    if (!d) return NULL;
    if (!s) { *d = '\0'; return r; }
    while ((*d++ = *s++)) {}
    return r;
}
static uint16_t *s7_wcopy(uint16_t *d, const uint16_t *s) {
    uint16_t *r = d;
    if (!d) return NULL;
    if (!s) { *d = 0; return r; }
    while ((*d++ = *s++)) {}
    return r;
}
static uint32_t s7_ansi_to_wide(const char *src, uint16_t *dst, uint32_t cap) {
    uint32_t n = 0U;
    if (!src) return 0U;
    while (src[n]) {
        if (dst && n + 1U < cap) dst[n] = (uint8_t)src[n];
        n++;
    }
    if (dst && cap) dst[n < cap ? n : cap - 1U] = 0;
    return n;
}
static uint32_t s7_wide_to_ansi(const uint16_t *src, char *dst, uint32_t cap) {
    uint32_t n = 0U;
    if (!src) return 0U;
    while (src[n]) {
        if (dst && n + 1U < cap) dst[n] = src[n] <= 255U ? (char)src[n] : '?';
        n++;
    }
    if (dst && cap) dst[n < cap ? n : cap - 1U] = '\0';
    return n;
}

/* ------------------------------------------------------------------------- */
/* USER32 legacy dialog, OEM and scrollbar APIs.                             */
/* ------------------------------------------------------------------------- */

typedef int (WIN32_API *s7_check_dlg_fn)(void *, int, uint32_t);
typedef void *(WIN32_API *s7_get_dlg_fn)(void *, int);

static int WIN32_API s7_CheckRadioButton(void *dialog, int first, int last,
                                         int selected) {
    s7_check_dlg_fn check = (s7_check_dlg_fn)(uintptr_t)
        win32_user32_resolve("CheckDlgButton");
    if (!check || first > last) return 0;
    for (int id = first; id <= last; id++)
        (void)check(dialog, id, id == selected ? 1U : 0U);
    return 1;
}

static uint8_t s7_ansi_to_oem_byte(uint8_t c) {
    switch (c) {
        case 0xC1: return 0xB5; case 0xC9: return 0x90; case 0xCD: return 0xD6;
        case 0xD3: return 0xE0; case 0xDA: return 0xE9; case 0xD1: return 0xA5;
        case 0xDC: return 0x9A; case 0xE1: return 0xA0; case 0xE9: return 0x82;
        case 0xED: return 0xA1; case 0xF3: return 0xA2; case 0xFA: return 0xA3;
        case 0xF1: return 0xA4; case 0xFC: return 0x81; case 0xC7: return 0x80;
        case 0xE7: return 0x87; default: return c;
    }
}
static uint8_t s7_oem_to_ansi_byte(uint8_t c) {
    switch (c) {
        case 0xB5: return 0xC1; case 0x90: return 0xC9; case 0xD6: return 0xCD;
        case 0xE0: return 0xD3; case 0xE9: return 0xDA; case 0xA5: return 0xD1;
        case 0x9A: return 0xDC; case 0xA0: return 0xE1; case 0x82: return 0xE9;
        case 0xA1: return 0xED; case 0xA2: return 0xF3; case 0xA3: return 0xFA;
        case 0xA4: return 0xF1; case 0x81: return 0xFC; case 0x80: return 0xC7;
        case 0x87: return 0xE7; default: return c;
    }
}
static int WIN32_API s7_CharToOemBuffA(const char *src, char *dst,
                                       uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++) dst[i] = (char)s7_ansi_to_oem_byte((uint8_t)src[i]);
    return 1;
}
static int WIN32_API s7_OemToCharBuffA(const char *src, char *dst,
                                       uint32_t length) {
    if (!src || !dst) return 0;
    for (uint32_t i = 0; i < length; i++) dst[i] = (char)s7_oem_to_ansi_byte((uint8_t)src[i]);
    return 1;
}
static int WIN32_API s7_CharToOemA(const char *src, char *dst) {
    if (!src || !dst) return 0;
    return s7_CharToOemBuffA(src, dst, s7_len(src) + 1U);
}
static int WIN32_API s7_OemToCharA(const char *src, char *dst) {
    if (!src || !dst) return 0;
    return s7_OemToCharBuffA(src, dst, s7_len(src) + 1U);
}
static int WIN32_API s7_CharToOemW(const uint16_t *src, char *dst) {
    uint32_t i = 0U;
    if (!src || !dst) return 0;
    while (src[i]) { dst[i] = (char)s7_ansi_to_oem_byte(src[i] <= 255U ? (uint8_t)src[i] : '?'); i++; }
    dst[i] = '\0'; return 1;
}
static int WIN32_API s7_OemToCharW(const char *src, uint16_t *dst) {
    uint32_t i = 0U;
    if (!src || !dst) return 0;
    while (src[i]) { dst[i] = s7_oem_to_ansi_byte((uint8_t)src[i]); i++; }
    dst[i] = 0; return 1;
}
static const uint16_t *WIN32_API s7_CharNextW(const uint16_t *text) {
    return text && *text ? text + 1 : text;
}
static const uint16_t *WIN32_API s7_CharPrevW(const uint16_t *start,
                                              const uint16_t *current) {
    return !start || !current || current <= start ? start : current - 1;
}
static const uint16_t *WIN32_API s7_CharNextExW(uint16_t cp UNUSED,
                                                const uint16_t *text,
                                                uint32_t flags UNUSED) {
    return s7_CharNextW(text);
}
static const uint16_t *WIN32_API s7_CharPrevExW(uint16_t cp UNUSED,
                                                const uint16_t *start,
                                                const uint16_t *current,
                                                uint32_t flags UNUSED) {
    return s7_CharPrevW(start, current);
}
static int s7_alpha(uint32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80U;
}
static int s7_digit(uint32_t c) { return c >= '0' && c <= '9'; }
static int WIN32_API s7_IsCharAlphaNumericA(char c) { return s7_alpha((uint8_t)c) || s7_digit((uint8_t)c); }
static int WIN32_API s7_IsCharAlphaNumericW(uint16_t c) { return s7_alpha(c) || s7_digit(c); }
static int WIN32_API s7_IsCharAlphaW(uint16_t c) { return s7_alpha(c); }
static int WIN32_API s7_IsCharUpperW(uint16_t c) { return c >= 'A' && c <= 'Z'; }
static int WIN32_API s7_IsCharLowerW(uint16_t c) { return c >= 'a' && c <= 'z'; }

static void *WIN32_API s7_GetNextDlgTabItem(void *dialog, void *control,
                                            int previous UNUSED) {
    if (control) return control;
    s7_get_dlg_fn get = (s7_get_dlg_fn)(uintptr_t)win32_user32_resolve("GetDlgItem");
    return get ? get(dialog, 1) : NULL;
}
static void *WIN32_API s7_GetNextDlgGroupItem(void *dialog, void *control,
                                              int previous) {
    return s7_GetNextDlgTabItem(dialog, control, previous);
}

typedef struct { int32_t left, top, right, bottom; } s7_rect_t;
static int WIN32_API s7_CopyRect(s7_rect_t *dst, const s7_rect_t *src) {
    if (!dst || !src) return 0;
    *dst = *src;
    return 1;
}
static int WIN32_API s7_IntersectRect(s7_rect_t *dst, const s7_rect_t *a,
                                      const s7_rect_t *b) {
    if (!dst || !a || !b) return 0;
    dst->left = a->left > b->left ? a->left : b->left;
    dst->top = a->top > b->top ? a->top : b->top;
    dst->right = a->right < b->right ? a->right : b->right;
    dst->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (dst->right <= dst->left || dst->bottom <= dst->top) {
        dst->left = dst->top = dst->right = dst->bottom = 0; return 0;
    }
    return 1;
}
static int WIN32_API s7_UnionRect(s7_rect_t *dst, const s7_rect_t *a,
                                  const s7_rect_t *b) {
    if (!dst || !a || !b) return 0;
    dst->left = a->left < b->left ? a->left : b->left;
    dst->top = a->top < b->top ? a->top : b->top;
    dst->right = a->right > b->right ? a->right : b->right;
    dst->bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    return dst->right > dst->left && dst->bottom > dst->top;
}

typedef struct { bool used, visible; void *hwnd; int bar, minimum, maximum, position; } s7_scroll_t;
static s7_scroll_t s7_scrolls[32];
static s7_scroll_t *s7_scroll(void *hwnd, int bar, bool create) {
    s7_scroll_t *free_slot = NULL;
    for (uint32_t i = 0; i < 32U; i++) {
        if (s7_scrolls[i].used && s7_scrolls[i].hwnd == hwnd && s7_scrolls[i].bar == bar)
            return &s7_scrolls[i];
        if (!s7_scrolls[i].used && !free_slot) free_slot = &s7_scrolls[i];
    }
    if (!create || !free_slot) return NULL;
    *free_slot = (s7_scroll_t){true, true, hwnd, bar, 0, 100, 0};
    return free_slot;
}
static int WIN32_API s7_SetScrollRange(void *hwnd, int bar, int min, int max,
                                       int redraw UNUSED) {
    s7_scroll_t *s = s7_scroll(hwnd, bar, true); if (!s || min > max) return 0;
    s->minimum = min; s->maximum = max;
    if (s->position < min) s->position = min;
    if (s->position > max) s->position = max;
    return 1;
}
static int WIN32_API s7_GetScrollRange(void *hwnd, int bar, int *min, int *max) {
    s7_scroll_t *s = s7_scroll(hwnd, bar, false); if (!s) return 0;
    if (min) *min = s->minimum;
    if (max) *max = s->maximum;
    return 1;
}
static int WIN32_API s7_SetScrollPos(void *hwnd, int bar, int pos,
                                     int redraw UNUSED) {
    s7_scroll_t *s = s7_scroll(hwnd, bar, true); int old;
    if (!s) return 0;
    old = s->position;
    s->position = pos < s->minimum ? s->minimum : (pos > s->maximum ? s->maximum : pos);
    return old;
}
static int WIN32_API s7_GetScrollPos(void *hwnd, int bar) {
    s7_scroll_t *s = s7_scroll(hwnd, bar, false); return s ? s->position : 0;
}
static int WIN32_API s7_ShowScrollBar(void *hwnd, int bar, int show) {
    s7_scroll_t *s = s7_scroll(hwnd, bar, true); if (!s) return 0; s->visible = show != 0; return 1;
}

/* ------------------------------------------------------------------------- */
/* KERNEL32 console and pointer-probe APIs.                                  */
/* ------------------------------------------------------------------------- */

typedef int (WIN32_API *s7_rw_fn)(void *, void *, uint32_t, uint32_t *, void *);
typedef void *(WIN32_API *s7_get_std_fn)(uint32_t);
typedef void *(WIN32_API *s7_get_desktop_fn)(void);
static char s7_console_title[128] = "BlesKernOS Console";
static uint16_t s7_console_attr = 0x0007U;
static void *s7_console_handlers[8];
static uint32_t s7_console_handler_count;

static int WIN32_API s7_SetConsoleCtrlHandler(void *handler, int add) {
    if (!handler) return 1;
    if (add) {
        for (uint32_t i = 0; i < s7_console_handler_count; i++)
            if (s7_console_handlers[i] == handler) return 1;
        if (s7_console_handler_count >= 8U) return 0;
        s7_console_handlers[s7_console_handler_count++] = handler;
        return 1;
    }
    for (uint32_t i = 0; i < s7_console_handler_count; i++) {
        if (s7_console_handlers[i] == handler) {
            for (; i + 1U < s7_console_handler_count; i++)
                s7_console_handlers[i] = s7_console_handlers[i + 1U];
            s7_console_handler_count--; return 1;
        }
    }
    return 0;
}
static int WIN32_API s7_GenerateConsoleCtrlEvent(uint32_t event UNUSED,
                                                  uint32_t group UNUSED) {
    return 1;
}
static uint32_t WIN32_API s7_GetConsoleTitleA(char *buffer, uint32_t capacity) {
    uint32_t n = s7_len(s7_console_title);
    if (buffer && capacity) {
        uint32_t copy = n < capacity - 1U ? n : capacity - 1U;
        for (uint32_t i = 0; i < copy; i++) buffer[i] = s7_console_title[i];
        buffer[copy] = '\0';
    }
    return n;
}
static uint32_t WIN32_API s7_GetConsoleTitleW(uint16_t *buffer, uint32_t capacity) {
    return s7_ansi_to_wide(s7_console_title, buffer, capacity);
}
static int WIN32_API s7_SetConsoleTitleA(const char *title) {
    uint32_t i = 0U; if (!title) return 0;
    while (title[i] && i + 1U < sizeof(s7_console_title)) { s7_console_title[i] = title[i]; i++; }
    s7_console_title[i] = '\0'; return 1;
}
static int WIN32_API s7_SetConsoleTitleW(const uint16_t *title) {
    char ansi[128]; if (!title) return 0; s7_wide_to_ansi(title, ansi, sizeof(ansi)); return s7_SetConsoleTitleA(ansi);
}
static int WIN32_API s7_GetNumberOfConsoleInputEvents(void *handle UNUSED,
                                                       uint32_t *events) {
    if (!events) return 0;
    *events = 0U;
    return 1;
}
static uint32_t WIN32_API s7_GetLargestConsoleWindowSize(void *handle UNUSED) {
    return (25U << 16) | 80U;
}
typedef struct PACKED { int16_t x, y; } s7_coord_t;
typedef struct PACKED { int16_t left, top, right, bottom; } s7_small_rect_t;
typedef struct PACKED {
    s7_coord_t size, cursor; uint16_t attributes; s7_small_rect_t window;
    s7_coord_t maximum;
} s7_console_info_t;
typedef struct PACKED { uint32_t size; int visible; } s7_cursor_info_t;
static int WIN32_API s7_GetConsoleScreenBufferInfo(void *handle UNUSED,
                                                    s7_console_info_t *info) {
    if (!info) return 0;
    memset(info, 0, sizeof(*info));
    info->size = (s7_coord_t){80, 25}; info->attributes = s7_console_attr;
    info->window = (s7_small_rect_t){0, 0, 79, 24}; info->maximum = (s7_coord_t){80, 25};
    return 1;
}
static int WIN32_API s7_SetConsoleCursorPosition(void *handle UNUSED,
                                                 uint32_t coord UNUSED) { return 1; }
static int WIN32_API s7_SetConsoleTextAttribute(void *handle UNUSED,
                                                uint16_t attr) { s7_console_attr = attr; return 1; }
static int WIN32_API s7_GetConsoleCursorInfo(void *handle UNUSED,
                                             s7_cursor_info_t *info) {
    if (!info) return 0;
    info->size = 25U;
    info->visible = 1;
    return 1;
}
static int WIN32_API s7_SetConsoleCursorInfo(void *handle UNUSED,
                                             const s7_cursor_info_t *info) { return info != NULL; }
static int WIN32_API s7_SetConsoleScreenBufferSize(void *handle UNUSED,
                                                   uint32_t size UNUSED) { return 1; }
static int WIN32_API s7_SetConsoleWindowInfo(void *handle UNUSED, int absolute UNUSED,
                                             const s7_small_rect_t *rect) { return rect != NULL; }
static int WIN32_API s7_SetConsoleActiveScreenBuffer(void *handle) { return handle != NULL; }
static void *WIN32_API s7_CreateConsoleScreenBuffer(uint32_t access UNUSED,
                                                    uint32_t share UNUSED,
                                                    void *security UNUSED,
                                                    uint32_t flags UNUSED,
                                                    void *data UNUSED) {
    s7_get_std_fn get = (s7_get_std_fn)(uintptr_t)win32_kernel32_resolve("GetStdHandle");
    return get ? get((uint32_t)-11) : (void *)(uintptr_t)1U;
}
static int WIN32_API s7_WriteConsoleA(void *handle, const char *buffer,
                                      uint32_t length, uint32_t *written,
                                      void *reserved UNUSED) {
    if (!handle || (!buffer && length)) return 0;
    return win32_file_write(handle, buffer, length, written) ? 1 : 0;
}
static int WIN32_API s7_WriteConsoleW(void *handle, const uint16_t *buffer,
                                      uint32_t length, uint32_t *written,
                                      void *reserved UNUSED) {
    char chunk[256]; uint32_t done = 0U;
    if (!handle || (!buffer && length)) return 0;
    while (done < length) {
        uint32_t part = length - done > sizeof(chunk) ? sizeof(chunk) : length - done;
        uint32_t bytes = 0U;
        for (uint32_t i = 0; i < part; i++) chunk[i] = buffer[done + i] <= 255U ? (char)buffer[done + i] : '?';
        if (!win32_file_write(handle, chunk, part, &bytes)) break;
        done += bytes; if (bytes < part) break;
    }
    if (written) *written = done;
    return done == length;
}
static int WIN32_API s7_ReadConsoleA(void *handle, char *buffer, uint32_t length,
                                     uint32_t *read, void *control) {
    s7_rw_fn fn = (s7_rw_fn)(uintptr_t)win32_kernel32_resolve("ReadFile");
    return fn ? fn(handle, buffer, length, read, control) : 0;
}
static int WIN32_API s7_ReadConsoleW(void *handle, uint16_t *buffer, uint32_t length,
                                     uint32_t *read, void *control) {
    char chunk[256]; uint32_t want = length > sizeof(chunk) ? sizeof(chunk) : length, got = 0U;
    if (!buffer) return 0;
    if (!s7_ReadConsoleA(handle, chunk, want, &got, control)) return 0;
    for (uint32_t i = 0; i < got; i++) buffer[i] = (uint8_t)chunk[i];
    if (read) *read = got;
    return 1;
}
static int WIN32_API s7_FillConsoleOutputCharacterA(void *h UNUSED, char c UNUSED,
                                                    uint32_t n, uint32_t coord UNUSED,
                                                    uint32_t *written) { if (written) *written = n; return 1; }
static int WIN32_API s7_FillConsoleOutputCharacterW(void *h UNUSED, uint16_t c UNUSED,
                                                    uint32_t n, uint32_t coord UNUSED,
                                                    uint32_t *written) { if (written) *written = n; return 1; }
static int WIN32_API s7_FillConsoleOutputAttribute(void *h UNUSED, uint16_t a UNUSED,
                                                   uint32_t n, uint32_t coord UNUSED,
                                                   uint32_t *written) { if (written) *written = n; return 1; }
static int WIN32_API s7_WriteConsoleOutputCharacterA(void *h, const char *b,
                                                     uint32_t n, uint32_t coord UNUSED,
                                                     uint32_t *written) {
    return s7_WriteConsoleA(h, b, n, written, NULL);
}
static int WIN32_API s7_WriteConsoleOutputCharacterW(void *h, const uint16_t *b,
                                                     uint32_t n, uint32_t coord UNUSED,
                                                     uint32_t *written) {
    return s7_WriteConsoleW(h, b, n, written, NULL);
}
static int WIN32_API s7_ReadConsoleOutputCharacterA(void *h UNUSED, char *b,
                                                    uint32_t n, uint32_t coord UNUSED,
                                                    uint32_t *read) {
    if (b && n) memset(b, ' ', n);
    if (read) *read = n;
    return b != NULL || n == 0U;
}
static int WIN32_API s7_ReadConsoleOutputCharacterW(void *h UNUSED, uint16_t *b,
                                                    uint32_t n, uint32_t coord UNUSED,
                                                    uint32_t *read) {
    if (b) for (uint32_t i = 0; i < n; i++) b[i] = ' ';
    if (read) *read = n;
    return b != NULL || n == 0U;
}
static void *WIN32_API s7_GetConsoleWindow(void) {
    s7_get_desktop_fn fn = (s7_get_desktop_fn)(uintptr_t)win32_user32_resolve("GetDesktopWindow");
    return fn ? fn() : NULL;
}
static int WIN32_API s7_IsBadReadPtr(const void *ptr, uint32_t size) { return size && !ptr; }
static int WIN32_API s7_IsBadWritePtr(void *ptr, uint32_t size) { return size && !ptr; }
static int WIN32_API s7_IsBadCodePtr(void *ptr) { return ptr == NULL; }
static int WIN32_API s7_IsBadStringPtrA(const char *ptr, uint32_t max UNUSED) { return ptr == NULL; }
static int WIN32_API s7_IsBadStringPtrW(const uint16_t *ptr, uint32_t max UNUSED) { return ptr == NULL; }

/* ------------------------------------------------------------------------- */
/* MSVCRT20/40 time, strings, sorting, environment and path helpers.          */
/* ------------------------------------------------------------------------- */

typedef struct {
    int32_t tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
} s7_tm_t;
static s7_tm_t s7_tm_buffer;
static char s7_time_text[32];
static uint32_t s7_rand_state = 1U;
static int32_t s7_timezone;
static int32_t s7_daylight;
static int32_t s7_dstbias = -3600;
static char s7_tz_utc[] = "UTC";
static char *s7_tzname[2] = {s7_tz_utc, s7_tz_utc};
static char s7_env_custom[8][96];
static bool s7_env_used[8];

static bool s7_leap(uint32_t y) { return (y % 4U == 0U && y % 100U != 0U) || y % 400U == 0U; }
static uint32_t s7_days_year(uint32_t y) { return s7_leap(y) ? 366U : 365U; }
static uint32_t s7_month_days(uint32_t y, uint32_t m) {
    static const uint8_t d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return m == 2U ? d[1] + (s7_leap(y) ? 1U : 0U) : (m >= 1U && m <= 12U ? d[m - 1U] : 0U);
}
static uint32_t s7_days_before(uint32_t y, uint32_t m, uint32_t d) {
    uint32_t days = 0U;
    for (uint32_t year = 1970U; year < y; year++) days += s7_days_year(year);
    for (uint32_t month = 1U; month < m; month++) days += s7_month_days(y, month);
    return days + (d ? d - 1U : 0U);
}
static int32_t s7_epoch(uint32_t y, uint32_t m, uint32_t d,
                        uint32_t h, uint32_t min, uint32_t sec) {
    uint64_t v;
    if (y < 1970U || m < 1U || m > 12U || d < 1U || d > s7_month_days(y, m)) return -1;
    v = (uint64_t)s7_days_before(y, m, d) * 86400ULL + h * 3600U + min * 60U + sec;
    return v > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int32_t)v;
}
static int32_t s7_time(int32_t *out) {
    rtc_datetime_t now; int32_t value = 0;
    if (bk_time_datetime(&now))
        value = s7_epoch(now.date.year, now.date.month, now.date.day,
                         now.time.hour, now.time.minute, now.time.second);
    if (out) *out = value;
    return value;
}
static s7_tm_t *s7_gmtime(const int32_t *value) {
    uint32_t seconds, days, year = 1970U, month = 1U, yday;
    if (!value || *value < 0) return NULL;
    seconds = (uint32_t)*value; days = seconds / 86400U; seconds %= 86400U;
    yday = days;
    while (days >= s7_days_year(year)) { days -= s7_days_year(year); year++; }
    yday = days;
    while (month <= 12U && days >= s7_month_days(year, month)) { days -= s7_month_days(year, month); month++; }
    s7_tm_buffer.tm_sec = (int32_t)(seconds % 60U); seconds /= 60U;
    s7_tm_buffer.tm_min = (int32_t)(seconds % 60U); s7_tm_buffer.tm_hour = (int32_t)(seconds / 60U);
    s7_tm_buffer.tm_mday = (int32_t)days + 1; s7_tm_buffer.tm_mon = (int32_t)month - 1;
    s7_tm_buffer.tm_year = (int32_t)year - 1900; s7_tm_buffer.tm_yday = (int32_t)yday;
    s7_tm_buffer.tm_wday = (int32_t)(((uint32_t)*value / 86400U + 4U) % 7U); s7_tm_buffer.tm_isdst = 0;
    return &s7_tm_buffer;
}
static s7_tm_t *s7_localtime(const int32_t *value) { return s7_gmtime(value); }
static int32_t s7_mktime(s7_tm_t *tm) {
    int32_t v;
    if (!tm) return -1;
    v = s7_epoch((uint32_t)(tm->tm_year + 1900), (uint32_t)(tm->tm_mon + 1),
                 (uint32_t)tm->tm_mday, (uint32_t)tm->tm_hour,
                 (uint32_t)tm->tm_min, (uint32_t)tm->tm_sec);
    if (v >= 0) { s7_tm_t *n = s7_gmtime(&v); if (n) *tm = *n; }
    return v;
}
static double s7_difftime(int32_t a, int32_t b) { return (double)(a - b); }
static int32_t s7_clock(void) {
    uint32_t hz = bk_sys_tick_frequency();
    return hz ? (int32_t)(((uint64_t)bk_sys_ticks() * 1000U) / hz) : (int32_t)bk_sys_ticks();
}
static char *s7_asctime(const s7_tm_t *tm) {
    static const char *wd[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mo[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    if (!tm) return NULL;
    snprintf(s7_time_text, sizeof(s7_time_text), "%s %s %2d %02d:%02d:%02d %04d\n",
             wd[(uint32_t)tm->tm_wday % 7U], mo[(uint32_t)tm->tm_mon % 12U],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
    return s7_time_text;
}
static char *s7_ctime(const int32_t *value) { s7_tm_t *tm = s7_localtime(value); return tm ? s7_asctime(tm) : NULL; }
static bool s7_append(char *out, uint32_t cap, uint32_t *pos, const char *text) {
    if (!out || !pos || !text) return false;
    while (*text) { if (*pos + 1U >= cap) return false; out[(*pos)++] = *text++; }
    out[*pos] = '\0'; return true;
}
static bool s7_append_num(char *out, uint32_t cap, uint32_t *pos, int value, int width) {
    char temp[16]; snprintf(temp, sizeof(temp), width == 4 ? "%04d" : "%02d", value);
    return s7_append(out, cap, pos, temp);
}
static size_t s7_strftime(char *out, size_t cap, const char *fmt, const s7_tm_t *tm) {
    static const char *wd[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char *mo[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    uint32_t p = 0U;
    if (!out || !cap || !fmt || !tm) return 0U;
    out[0] = '\0';
    while (*fmt) {
        if (*fmt != '%') { char t[2] = {*fmt++, 0}; if (!s7_append(out, cap, &p, t)) return 0U; continue; }
        fmt++; if (!*fmt) break;
        switch (*fmt++) {
            case '%': if (!s7_append(out, cap, &p, "%")) return 0U; break;
            case 'Y': if (!s7_append_num(out, cap, &p, tm->tm_year + 1900, 4)) return 0U; break;
            case 'y': if (!s7_append_num(out, cap, &p, (tm->tm_year + 1900) % 100, 2)) return 0U; break;
            case 'm': if (!s7_append_num(out, cap, &p, tm->tm_mon + 1, 2)) return 0U; break;
            case 'd': if (!s7_append_num(out, cap, &p, tm->tm_mday, 2)) return 0U; break;
            case 'H': if (!s7_append_num(out, cap, &p, tm->tm_hour, 2)) return 0U; break;
            case 'M': if (!s7_append_num(out, cap, &p, tm->tm_min, 2)) return 0U; break;
            case 'S': if (!s7_append_num(out, cap, &p, tm->tm_sec, 2)) return 0U; break;
            case 'a': { char t[4]; for (int i=0;i<3;i++) t[i]=wd[(uint32_t)tm->tm_wday%7U][i]; t[3]=0; if(!s7_append(out,cap,&p,t))return 0U; break; }
            case 'A': if (!s7_append(out, cap, &p, wd[(uint32_t)tm->tm_wday % 7U])) return 0U; break;
            case 'b': { char t[4]; for (int i=0;i<3;i++) t[i]=mo[(uint32_t)tm->tm_mon%12U][i]; t[3]=0; if(!s7_append(out,cap,&p,t))return 0U; break; }
            case 'B': if (!s7_append(out, cap, &p, mo[(uint32_t)tm->tm_mon % 12U])) return 0U; break;
            case 'c': { char t[32]; snprintf(t,sizeof(t),"%02d/%02d/%04d %02d:%02d:%02d",tm->tm_mon+1,tm->tm_mday,tm->tm_year+1900,tm->tm_hour,tm->tm_min,tm->tm_sec); if(!s7_append(out,cap,&p,t))return 0U; break; }
            default: { char t[3] = {'%', fmt[-1], 0}; if (!s7_append(out, cap, &p, t)) return 0U; break; }
        }
    }
    return p;
}
static void s7_srand(uint32_t seed) { s7_rand_state = seed ? seed : 1U; }
static int s7_rand(void) { s7_rand_state = s7_rand_state * 214013U + 2531011U; return (int)((s7_rand_state >> 16) & 0x7FFFU); }
typedef int (*s7_compare_fn)(const void *, const void *);
static void s7_qsort(void *base, size_t count, size_t width, s7_compare_fn cmp) {
    uint8_t *data = (uint8_t *)base;
    if (!data || !cmp || width == 0U || count < 2U) return;
    for (size_t gap = count / 2U; gap; gap /= 2U) {
        for (size_t i = gap; i < count; i++) {
            size_t j = i;
            while (j >= gap && cmp(data + (j - gap) * width, data + j * width) > 0) {
                for (size_t k = 0; k < width; k++) { uint8_t t=data[(j-gap)*width+k]; data[(j-gap)*width+k]=data[j*width+k]; data[j*width+k]=t; }
                j -= gap;
            }
        }
    }
}
static void *s7_bsearch(const void *key, const void *base, size_t count,
                        size_t width, s7_compare_fn cmp) {
    size_t low = 0U, high = count; const uint8_t *data = (const uint8_t *)base;
    if (!key || !base || !cmp || !width) return NULL;
    while (low < high) { size_t mid = low + (high-low)/2U; int c = cmp(key, data + mid*width); if (!c) return (void *)(data + mid*width); if (c < 0) high = mid; else low = mid + 1U; }
    return NULL;
}
static int s7_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c; }
static int s7_toupper(int c) { return c >= 'a' && c <= 'z' ? c - ('a'-'A') : c; }
static int s7_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static int s7_islower(int c) { return c >= 'a' && c <= 'z'; }
static int s7_isalpha_c(int c) { return s7_isupper(c) || s7_islower(c); }
static int s7_isxdigit(int c) { return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'); }
static int s7_iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
static int s7_ispunct(int c) { return c >= 0x21 && c <= 0x7E && !s7_isalpha_c(c) && !(c>='0'&&c<='9'); }
static int s7_stricmp(const char *a, const char *b) {
    uint8_t x, y; if (!a || !b) return a ? 1 : (b ? -1 : 0);
    do { x=(uint8_t)s7_tolower((uint8_t)*a++); y=(uint8_t)s7_tolower((uint8_t)*b++); } while(x&&x==y);
    return (int)x-(int)y;
}
static int s7_strnicmp(const char *a, const char *b, size_t n) {
    while (n--) { int x=s7_tolower((uint8_t)*a++), y=s7_tolower((uint8_t)*b++); if(x!=y||!x)return x-y; } return 0;
}
static char *s7_strdup(const char *s) { uint32_t n; char *d; if(!s)return NULL;n=s7_len(s)+1U;d=(char*)malloc(n);if(d)memcpy(d,s,n);return d; }
static char *s7_strupr(char *s) { if(s)for(char*p=s;*p;p++)*p=(char)s7_toupper((uint8_t)*p);return s; }
static char *s7_strlwr(char *s) { if(s)for(char*p=s;*p;p++)*p=(char)s7_tolower((uint8_t)*p);return s; }
static uint32_t s7_strtoul(const char *s, char **end, int base) {
    uint32_t value=0U; const char*p=s; if(!p)return 0U; while(*p==' '||*p=='\t')p++; if(base==0){base=10;if(p[0]=='0'&&(p[1]=='x'||p[1]=='X')){base=16;p+=2;}}
    while(*p){int d=*p>='0'&&*p<='9'?*p-'0':(*p>='a'&&*p<='z'?*p-'a'+10:(*p>='A'&&*p<='Z'?*p-'A'+10:-1));if(d<0||d>=base)break;value=value*(uint32_t)base+(uint32_t)d;p++;}if(end)*end=(char*)p;return value;
}
static double s7_strtod(const char *s, char **end) { const char*p=s;if(end){while(*p&&((*p>='0'&&*p<='9')||*p=='+'||*p=='-'||*p=='.'||*p=='e'||*p=='E'))p++;*end=(char*)p;}return atof(s); }
static char *s7_ultoa(uint32_t value, char *out, int radix) {
    char temp[34]; uint32_t n=0U; if(!out||radix<2||radix>36)return NULL; do{uint32_t d=value%(uint32_t)radix;temp[n++]=(char)(d<10?'0'+d:'a'+d-10);value/=(uint32_t)radix;}while(value);for(uint32_t i=0;i<n;i++)out[i]=temp[n-1U-i];out[n]='\0';return out;
}
static char *s7_ltoa(int32_t value, char *out, int radix) { if(radix==10&&value<0){*out='-';s7_ultoa((uint32_t)(-(int64_t)value),out+1,radix);return out;}return s7_ultoa((uint32_t)value,out,radix); }
static char *s7_itoa(int value, char *out, int radix) { return s7_ltoa(value,out,radix); }

static char *s7_getenv(const char *name) {
    static char path[]="/SYSTEM/PROGRAMS"; static char temp[]="/TEMP"; static char windir[]="/SYSTEM"; static char comspec[]="/SYSTEM/PROGRAMS/SHELL.BEX";
    if(!name)return NULL;
    for(uint32_t i=0;i<8U;i++)if(s7_env_used[i]){char*eq=s7_env_custom[i];while(*eq&&*eq!='=')eq++;if(*eq=='='){uint32_t n=(uint32_t)(eq-s7_env_custom[i]);if(s7_len(name)==n&&s7_strnicmp(name,s7_env_custom[i],n)==0)return eq+1;}}
    if (s7_eq(name, "PATH")) return path;
    if (s7_eq(name, "TEMP") || s7_eq(name, "TMP")) return temp;
    if (s7_eq(name, "WINDIR")) return windir;
    if (s7_eq(name, "COMSPEC")) return comspec;
    return NULL;
}
static int s7_putenv(const char *entry) {
    const char *eq=entry; uint32_t n;if(!entry)return -1;while(*eq&&*eq!='=')eq++;if(*eq!='=')return -1;n=s7_len(entry);if(n>=96U)return -1;
    for (uint32_t i = 0; i < 8U; i++) {
        if (!s7_env_used[i]) {
            s7_copy(s7_env_custom[i], entry);
            s7_env_used[i] = true;
            return 0;
        }
    }
    return -1;
}
static void s7_splitpath(const char *path,char *drive,char *dir,char *fname,char *ext){const char*slash=NULL,*dot=NULL,*p;if(drive){drive[0]='\0';if(path&&path[1]==':'){drive[0]=path[0];drive[1]=':';drive[2]='\0';}}if(!path){if(dir)*dir=0;if(fname)*fname=0;if(ext)*ext=0;return;}for(p=path;*p;p++){if(*p=='/'||*p=='\\'){slash=p;dot=NULL;}else if(*p=='.')dot=p;}if(dir){const char*start=path+(path[1]==':'?2:0);uint32_t n=slash?(uint32_t)(slash-start+1):0;for(uint32_t i=0;i<n;i++)dir[i]=start[i];dir[n]=0;}const char*base=slash?slash+1:path+(path[1]==':'?2:0);if(fname){uint32_t n=(uint32_t)((dot&&dot>=base?dot:p)-base);for(uint32_t i=0;i<n;i++)fname[i]=base[i];fname[n]=0;}if(ext)s7_copy(ext,dot&&dot>=base?dot:"");}
static void s7_makepath(char *out,const char *drive,const char *dir,const char *fname,const char *ext){uint32_t p=0U;if(!out)return;out[0]=0;const char*parts[4]={drive,dir,fname,ext};for(int x=0;x<4;x++){const char*s=parts[x];if(!s)continue;if(x==3&&*s&&*s!='.')out[p++]='.';while(*s&&p+1U<260U)out[p++]=*s++;if(x==0&&p&&out[p-1]!=':')out[p++]=':';if(x==1&&p&&out[p-1]!='/'&&out[p-1]!='\\')out[p++]='/';}out[p]=0;}
typedef uint32_t (WIN32_API *s7_fullpath_fn)(const char*,uint32_t,char*,char**);
static char *s7_fullpath(char *out,const char *path,size_t cap){s7_fullpath_fn fn=(s7_fullpath_fn)(uintptr_t)win32_kernel32_resolve("GetFullPathNameA");char*part=NULL;if(!path)return NULL;if(!out){cap=260U;out=(char*)malloc(cap);if(!out)return NULL;}if(fn&&fn(path,cap,out,&part))return out;s7_copy(out,path);return out;}
typedef uint32_t (WIN32_API *s7_attr_fn)(const char*);
static int s7_access(const char *path,int mode UNUSED){s7_attr_fn fn=(s7_attr_fn)(uintptr_t)win32_kernel32_resolve("GetFileAttributesA");return fn&&fn(path)!=S7_INVALID_FILE_ATTRIBUTES?0:-1;}
typedef int (WIN32_API *s7_delete_fn)(const char*);
static int s7_unlink(const char *path){s7_delete_fn fn=(s7_delete_fn)(uintptr_t)win32_kernel32_resolve("DeleteFileA");return fn&&fn(path)?0:-1;}
static int s7_rmdir(const char *path){s7_delete_fn fn=(s7_delete_fn)(uintptr_t)win32_kernel32_resolve("RemoveDirectoryA");return fn&&fn(path)?0:-1;}

static uint16_t *s7_wcscpy(uint16_t*d,const uint16_t*s){return s7_wcopy(d,s);}static uint16_t*s7_wcsncpy(uint16_t*d,const uint16_t*s,size_t n){size_t i=0;if(!d)return NULL;for(;i<n&&s&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}static uint16_t*s7_wcscat(uint16_t*d,const uint16_t*s){uint32_t n=s7_wlen(d);s7_wcopy(d+n,s);return d;}static int s7_wcscmp(const uint16_t*a,const uint16_t*b){while(*a&&*a==*b){a++;b++;}return(int)*a-(int)*b;}static int s7_wcsncmp(const uint16_t*a,const uint16_t*b,size_t n){while(n--){int d=(int)*a-(int)*b;if(d||!*a)return d;a++;b++;}return 0;}static uint16_t*s7_wcschr(const uint16_t*s,int c){if(!s)return NULL;while(*s!=(uint16_t)c){if(!*s++)return NULL;}return(uint16_t*)s;}static uint16_t*s7_wcsrchr(const uint16_t*s,int c){const uint16_t*r=NULL;if(!s)return NULL;do{if(*s==(uint16_t)c)r=s;}while(*s++);return(uint16_t*)r;}

uint32_t win32_wine_stage7_resolve(const char *dll, const char *name) {
    if (!dll || !name) return 0U;
    if (s7_eq(dll,"USER32.DLL")) {
#define S7U(n) if(s7_eq(name,#n))return(uint32_t)(uintptr_t)&s7_##n
        S7U(CheckRadioButton);S7U(CharToOemA);S7U(OemToCharA);S7U(CharToOemW);S7U(OemToCharW);
        S7U(CharToOemBuffA);S7U(OemToCharBuffA);S7U(CharNextW);S7U(CharPrevW);S7U(CharNextExW);S7U(CharPrevExW);
        S7U(IsCharAlphaNumericA);S7U(IsCharAlphaNumericW);S7U(IsCharAlphaW);S7U(IsCharUpperW);S7U(IsCharLowerW);
        S7U(GetNextDlgTabItem);S7U(GetNextDlgGroupItem);S7U(CopyRect);S7U(IntersectRect);S7U(UnionRect);
        S7U(SetScrollRange);S7U(GetScrollRange);S7U(SetScrollPos);S7U(GetScrollPos);S7U(ShowScrollBar);
#undef S7U
        if(s7_eq(name,"AnsiToOemA"))return(uint32_t)(uintptr_t)&s7_CharToOemA;
        if(s7_eq(name,"OemToAnsiA"))return(uint32_t)(uintptr_t)&s7_OemToCharA;
        if(s7_eq(name,"AnsiToOemBuffA"))return(uint32_t)(uintptr_t)&s7_CharToOemBuffA;
        if(s7_eq(name,"OemToAnsiBuffA"))return(uint32_t)(uintptr_t)&s7_OemToCharBuffA;
    }
    if (s7_eq(dll,"KERNEL32.DLL") || s7_eq(dll,"KERNELBASE.DLL")) {
#define S7K(n) if(s7_eq(name,#n))return(uint32_t)(uintptr_t)&s7_##n
        S7K(SetConsoleCtrlHandler);S7K(GenerateConsoleCtrlEvent);S7K(GetConsoleTitleA);S7K(GetConsoleTitleW);S7K(SetConsoleTitleA);S7K(SetConsoleTitleW);
        S7K(GetNumberOfConsoleInputEvents);S7K(GetLargestConsoleWindowSize);S7K(GetConsoleScreenBufferInfo);S7K(SetConsoleCursorPosition);S7K(SetConsoleTextAttribute);
        S7K(GetConsoleCursorInfo);S7K(SetConsoleCursorInfo);S7K(SetConsoleScreenBufferSize);S7K(SetConsoleWindowInfo);S7K(SetConsoleActiveScreenBuffer);S7K(CreateConsoleScreenBuffer);
        S7K(WriteConsoleA);S7K(WriteConsoleW);S7K(ReadConsoleA);S7K(ReadConsoleW);S7K(FillConsoleOutputCharacterA);S7K(FillConsoleOutputCharacterW);
        S7K(FillConsoleOutputAttribute);S7K(WriteConsoleOutputCharacterA);S7K(WriteConsoleOutputCharacterW);S7K(ReadConsoleOutputCharacterA);S7K(ReadConsoleOutputCharacterW);S7K(GetConsoleWindow);
        S7K(IsBadReadPtr);S7K(IsBadWritePtr);S7K(IsBadCodePtr);S7K(IsBadStringPtrA);S7K(IsBadStringPtrW);
#undef S7K
    }
    if (s7_eq(dll,"MSVCRT.DLL") || s7_eq(dll,"MSVCRT20.DLL") ||
        s7_eq(dll,"MSVCRT40.DLL") || s7_eq(dll,"CRTDLL.DLL") || s7_eq(dll,"MSVCRTD.DLL")) {
#define S7C(n) if(s7_eq(name,#n))return(uint32_t)(uintptr_t)&s7_##n
        S7C(gmtime);S7C(localtime);S7C(time);S7C(mktime);S7C(difftime);S7C(clock);S7C(asctime);S7C(ctime);S7C(strftime);
        S7C(rand);S7C(srand);S7C(qsort);S7C(bsearch);S7C(tolower);S7C(toupper);S7C(isupper);S7C(islower);S7C(isxdigit);S7C(iscntrl);S7C(ispunct);
        S7C(strtoul);S7C(strtod);S7C(getenv);S7C(wcscpy);S7C(wcsncpy);S7C(wcscat);S7C(wcscmp);S7C(wcsncmp);S7C(wcschr);S7C(wcsrchr);
#undef S7C
        if(s7_eq(name,"_stricmp")||s7_eq(name,"stricmp")||s7_eq(name,"_strcmpi"))return(uint32_t)(uintptr_t)&s7_stricmp;
        if(s7_eq(name,"_strnicmp")||s7_eq(name,"strnicmp"))return(uint32_t)(uintptr_t)&s7_strnicmp;
        if(s7_eq(name,"_strdup")||s7_eq(name,"strdup"))return(uint32_t)(uintptr_t)&s7_strdup;
        if (s7_eq(name, "_strupr")) return (uint32_t)(uintptr_t)&s7_strupr;
        if (s7_eq(name, "_strlwr")) return (uint32_t)(uintptr_t)&s7_strlwr;
        if (s7_eq(name, "_itoa")) return (uint32_t)(uintptr_t)&s7_itoa;
        if (s7_eq(name, "_ltoa")) return (uint32_t)(uintptr_t)&s7_ltoa;
        if (s7_eq(name, "_ultoa")) return (uint32_t)(uintptr_t)&s7_ultoa;
        if(s7_eq(name,"_putenv")||s7_eq(name,"putenv"))return(uint32_t)(uintptr_t)&s7_putenv;
        if (s7_eq(name, "_splitpath")) return (uint32_t)(uintptr_t)&s7_splitpath;
        if (s7_eq(name, "_makepath")) return (uint32_t)(uintptr_t)&s7_makepath;
        if (s7_eq(name, "_fullpath")) return (uint32_t)(uintptr_t)&s7_fullpath;
        if (s7_eq(name, "_access")) return (uint32_t)(uintptr_t)&s7_access;
        if (s7_eq(name, "_unlink")) return (uint32_t)(uintptr_t)&s7_unlink;
        if (s7_eq(name, "_rmdir")) return (uint32_t)(uintptr_t)&s7_rmdir;
        if (s7_eq(name, "_timezone")) return (uint32_t)(uintptr_t)&s7_timezone;
        if (s7_eq(name, "_daylight")) return (uint32_t)(uintptr_t)&s7_daylight;
        if (s7_eq(name, "_dstbias")) return (uint32_t)(uintptr_t)&s7_dstbias;
        if (s7_eq(name, "_tzname")) return (uint32_t)(uintptr_t)&s7_tzname;
    }
    return 0U;
}

uint32_t win32_wine_stage7_resolve_ordinal(const char *dll UNUSED,
                                            uint16_t ordinal UNUSED) {
    return 0U;
}

bool win32_wine_stage7_is_data_export(const char *dll, const char *name) {
    if (!dll || !name) return false;
    if (!(s7_eq(dll,"MSVCRT.DLL") || s7_eq(dll,"MSVCRT20.DLL") ||
          s7_eq(dll,"MSVCRT40.DLL") || s7_eq(dll,"CRTDLL.DLL") ||
          s7_eq(dll,"MSVCRTD.DLL"))) return false;
    return s7_eq(name,"_timezone") || s7_eq(name,"_daylight") ||
           s7_eq(name,"_dstbias") || s7_eq(name,"_tzname");
}


bool win32_wine_stage7_init(void) {
    return win32_register_resolver(win32_wine_stage7_resolve,
                                   win32_wine_stage7_resolve_ordinal,
                                   win32_wine_stage7_is_data_export);
}
