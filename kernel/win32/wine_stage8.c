/*
 * BlesKernOS Win32/Wine compatibility port - Stage 8.
 *
 * Adds the PE32 compatibility functions that blocked the current Win95
 * applications, plus a wider set of harmless legacy USER32/KERNEL32/MSVCRT20
 * helpers.  Kept as an external resident .DVR so kernel.bin does not grow.
 */
#include "win32.h"
#include "../include/types.h"
#include "../include/api.h"
#include "../include/driver.h"
#include "../string.h"
#include "../stdio.h"

extern uint32_t win32_user32_resolve(const char *name);
extern uint32_t win32_kernel32_resolve(const char *name);
extern bool win32_register_resolver(
    uint32_t (*named)(const char *, const char *),
    uint32_t (*ordinal)(const char *, uint16_t),
    bool (*data)(const char *, const char *));

#define S8_CP_ACP 0U
#define S8_CP_OEMCP 1U
#define S8_TIME_ZONE_ID_UNKNOWN 0U
#define S8_LANG_ENGLISH_US 0x0409U
#define S8_GFSR_SYSTEMRESOURCES 0x0000U
#define S8_GFSR_GDIRESOURCES 0x0001U
#define S8_GFSR_USERRESOURCES 0x0002U
#define S8_DDE_MAGIC 0x38454444U
#define S8_MAX_DDE_DATA 16U
#define S8_MAX_CONTEXT 32U

static uint8_t s8_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static bool s8_equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (s8_upper((uint8_t)*a++) != s8_upper((uint8_t)*b++)) return false;
    }
    return *a == *b;
}

static uint32_t s8_strlen(const char *text) {
    uint32_t length = 0U;
    if (text) while (text[length]) length++;
    return length;
}

static uint32_t s8_wstrlen(const uint16_t *text) {
    uint32_t length = 0U;
    if (text) while (text[length]) length++;
    return length;
}

static uint32_t s8_copy_a(char *dst, uint32_t capacity, const char *src) {
    uint32_t length = s8_strlen(src);
    uint32_t count;
    if (!dst || capacity == 0U) return length;
    count = length < capacity - 1U ? length : capacity - 1U;
    for (uint32_t i = 0; i < count; i++) dst[i] = src[i];
    dst[count] = '\0';
    return count;
}

static uint32_t s8_copy_w(uint16_t *dst, uint32_t capacity,
                          const uint16_t *src) {
    uint32_t length = s8_wstrlen(src);
    uint32_t count;
    if (!dst || capacity == 0U) return length;
    count = length < capacity - 1U ? length : capacity - 1U;
    for (uint32_t i = 0; i < count; i++) dst[i] = src[i];
    dst[count] = 0;
    return count;
}

static uint32_t s8_ansi_to_wide(const char *src, uint16_t *dst,
                                uint32_t capacity) {
    uint32_t length = s8_strlen(src);
    uint32_t count;
    if (!dst || capacity == 0U) return length;
    count = length < capacity - 1U ? length : capacity - 1U;
    for (uint32_t i = 0; i < count; i++) dst[i] = (uint8_t)src[i];
    dst[count] = 0;
    return count;
}

static uint32_t s8_wide_to_ansi(const uint16_t *src, char *dst,
                                uint32_t capacity) {
    uint32_t length = s8_wstrlen(src);
    uint32_t count;
    if (!dst || capacity == 0U) return length;
    count = length < capacity - 1U ? length : capacity - 1U;
    for (uint32_t i = 0; i < count; i++)
        dst[i] = src[i] <= 255U ? (char)src[i] : '?';
    dst[count] = '\0';
    return count;
}

static bool s8_is_crt(const char *dll) {
    return s8_equal_ci(dll, "MSVCRT.DLL") ||
           s8_equal_ci(dll, "MSVCRT20.DLL") ||
           s8_equal_ci(dll, "MSVCRT40.DLL") ||
           s8_equal_ci(dll, "MSVCRTD.DLL") ||
           s8_equal_ci(dll, "CRTDLL.DLL");
}

/* ------------------------------------------------------------------------- */
/* USER32: dialog geometry, popup state, MDI helpers and DDE data handles.    */
/* ------------------------------------------------------------------------- */

typedef struct {
    int32_t left, top, right, bottom;
} s8_rect_t;

typedef struct {
    int32_t x, y;
} s8_point_t;

typedef uint32_t (WIN32_API *s8_get_dialog_units_fn)(void);
typedef int (WIN32_API *s8_show_window_fn)(void *, int);
typedef int (WIN32_API *s8_def_window_proc_fn)(void *, uint32_t,
                                               uintptr_t, intptr_t);
typedef uint32_t (WIN32_API *s8_window_thread_fn)(void *, uint32_t *);
typedef void *(WIN32_API *s8_get_parent_fn)(void *);
typedef int (WIN32_API *s8_get_window_rect_fn)(void *, s8_rect_t *);
typedef int (WIN32_API *s8_screen_to_client_fn)(void *, s8_point_t *);

static int WIN32_API s8_MapDialogRect(void *dialog UNUSED, s8_rect_t *rect) {
    uint32_t units = (16U << 16) | 8U;
    uint32_t horizontal;
    uint32_t vertical;
    s8_get_dialog_units_fn get_units =
        (s8_get_dialog_units_fn)(uintptr_t)
            win32_user32_resolve("GetDialogBaseUnits");

    if (!rect) return 0;
    if (get_units) units = get_units();
    horizontal = units & 0xFFFFU;
    vertical = units >> 16;
    if (!horizontal) horizontal = 8U;
    if (!vertical) vertical = 16U;

    rect->left = (rect->left * (int32_t)horizontal) / 4;
    rect->right = (rect->right * (int32_t)horizontal) / 4;
    rect->top = (rect->top * (int32_t)vertical) / 8;
    rect->bottom = (rect->bottom * (int32_t)vertical) / 8;
    return 1;
}

static void *WIN32_API s8_GetLastActivePopup(void *window) {
    return window;
}

static void *WIN32_API s8_GetWindow(void *window, uint32_t command) {
    s8_get_parent_fn get_parent;
    /* GW_OWNER is the only relation we can represent reliably for now. */
    if (command != 4U) return NULL;
    get_parent = (s8_get_parent_fn)(uintptr_t)
        win32_user32_resolve("GetParent");
    return get_parent ? get_parent(window) : NULL;
}

static void *WIN32_API s8_GetNextWindow(void *window, uint32_t command) {
    return s8_GetWindow(window, command);
}

static uint32_t WIN32_API s8_GetWindowTask(void *window) {
    s8_window_thread_fn get_thread = (s8_window_thread_fn)(uintptr_t)
        win32_user32_resolve("GetWindowThreadProcessId");
    return get_thread ? get_thread(window, NULL) : 0U;
}

static int WIN32_API s8_IsIconic(void *window UNUSED) { return 0; }
static int WIN32_API s8_IsZoomed(void *window UNUSED) { return 0; }

static int WIN32_API s8_OpenIcon(void *window) {
    s8_show_window_fn show = (s8_show_window_fn)(uintptr_t)
        win32_user32_resolve("ShowWindow");
    return show ? show(window, 9) : (window != NULL);
}

static int WIN32_API s8_CloseWindow(void *window) {
    s8_show_window_fn show = (s8_show_window_fn)(uintptr_t)
        win32_user32_resolve("ShowWindow");
    return show ? show(window, 6) : (window != NULL);
}

static int WIN32_API s8_FlashWindow(void *window, int invert UNUSED) {
    return window != NULL;
}

static int WIN32_API s8_LockWindowUpdate(void *window UNUSED) { return 1; }

static int WIN32_API s8_GetWindowRgn(void *window UNUSED,
                                     void *region UNUSED) { return 0; }
static int WIN32_API s8_SetWindowRgn(void *window UNUSED,
                                     void *region UNUSED,
                                     int redraw UNUSED) { return 1; }

static uint32_t WIN32_API s8_ArrangeIconicWindows(void *parent UNUSED) {
    return 0U;
}

static uint16_t WIN32_API s8_CascadeWindows(void *parent UNUSED,
                                            uint32_t how UNUSED,
                                            const s8_rect_t *rect UNUSED,
                                            uint32_t count UNUSED,
                                            void **children UNUSED) {
    return 0U;
}

static uint16_t WIN32_API s8_TileWindows(void *parent UNUSED,
                                         uint32_t how UNUSED,
                                         const s8_rect_t *rect UNUSED,
                                         uint32_t count UNUSED,
                                         void **children UNUSED) {
    return 0U;
}

static int WIN32_API s8_AnyPopup(void) { return 0; }

static int WIN32_API s8_DefFrameProcA(void *window, void *mdi UNUSED,
                                      uint32_t message, uintptr_t wparam,
                                      intptr_t lparam) {
    s8_def_window_proc_fn proc = (s8_def_window_proc_fn)(uintptr_t)
        win32_user32_resolve("DefWindowProcA");
    return proc ? proc(window, message, wparam, lparam) : 0;
}

static int WIN32_API s8_DefFrameProcW(void *window, void *mdi,
                                      uint32_t message, uintptr_t wparam,
                                      intptr_t lparam) {
    return s8_DefFrameProcA(window, mdi, message, wparam, lparam);
}

static int WIN32_API s8_DefMDIChildProcA(void *window, uint32_t message,
                                         uintptr_t wparam, intptr_t lparam) {
    return s8_DefFrameProcA(window, NULL, message, wparam, lparam);
}

static int WIN32_API s8_DefMDIChildProcW(void *window, uint32_t message,
                                         uintptr_t wparam, intptr_t lparam) {
    return s8_DefMDIChildProcA(window, message, wparam, lparam);
}

static int WIN32_API s8_TranslateMDISysAccel(void *client UNUSED,
                                             void *message UNUSED) {
    return 0;
}

typedef struct {
    bool used;
    void *object;
    uint32_t value;
} s8_context_slot_t;

static s8_context_slot_t s8_window_context[S8_MAX_CONTEXT];
static s8_context_slot_t s8_menu_context[S8_MAX_CONTEXT];

static uint32_t s8_context_get(s8_context_slot_t *slots, void *object) {
    for (uint32_t i = 0; i < S8_MAX_CONTEXT; i++)
        if (slots[i].used && slots[i].object == object) return slots[i].value;
    return 0U;
}

static int s8_context_set(s8_context_slot_t *slots, void *object,
                          uint32_t value) {
    s8_context_slot_t *free_slot = NULL;
    if (!object) return 0;
    for (uint32_t i = 0; i < S8_MAX_CONTEXT; i++) {
        if (slots[i].used && slots[i].object == object) {
            slots[i].value = value;
            return 1;
        }
        if (!slots[i].used && !free_slot) free_slot = &slots[i];
    }
    if (!free_slot) return 0;
    free_slot->used = true;
    free_slot->object = object;
    free_slot->value = value;
    return 1;
}

static uint32_t WIN32_API s8_GetWindowContextHelpId(void *window) {
    return s8_context_get(s8_window_context, window);
}
static int WIN32_API s8_SetWindowContextHelpId(void *window, uint32_t value) {
    return s8_context_set(s8_window_context, window, value);
}
static uint32_t WIN32_API s8_GetMenuContextHelpId(void *menu) {
    return s8_context_get(s8_menu_context, menu);
}
static int WIN32_API s8_SetMenuContextHelpId(void *menu, uint32_t value) {
    return s8_context_set(s8_menu_context, menu, value);
}

static int WIN32_API s8_SetWindowPlacement(void *window UNUSED,
                                           const void *placement) {
    return placement != NULL;
}

/* Minimal DDE data-handle support. Stage 5 already provides string handles. */
typedef struct {
    uint32_t magic;
    uint32_t size;
    uint8_t *data;
} s8_dde_data_t;

static s8_dde_data_t *s8_dde_handles[S8_MAX_DDE_DATA];
static uint32_t s8_dde_success_token = S8_DDE_MAGIC;

static s8_dde_data_t *s8_dde_create(const void *data, uint32_t size) {
    s8_dde_data_t *handle;
    uint32_t slot = S8_MAX_DDE_DATA;
    for (uint32_t i = 0; i < S8_MAX_DDE_DATA; i++) {
        if (!s8_dde_handles[i]) { slot = i; break; }
    }
    if (slot == S8_MAX_DDE_DATA) return NULL;
    handle = (s8_dde_data_t *)bk_sys_alloc_zero(sizeof(*handle));
    if (!handle) return NULL;
    if (size) {
        handle->data = (uint8_t *)bk_sys_alloc(size);
        if (!handle->data) { bk_sys_free(handle); return NULL; }
        if (data) memcpy(handle->data, data, size);
        else memset(handle->data, 0, size);
    }
    handle->magic = S8_DDE_MAGIC;
    handle->size = size;
    s8_dde_handles[slot] = handle;
    return handle;
}

static s8_dde_data_t *s8_dde_valid(void *value) {
    s8_dde_data_t *handle = (s8_dde_data_t *)value;
    if (!handle || handle->magic != S8_DDE_MAGIC) return NULL;
    for (uint32_t i = 0; i < S8_MAX_DDE_DATA; i++)
        if (s8_dde_handles[i] == handle) return handle;
    return NULL;
}

static void *WIN32_API s8_DdeCreateDataHandle(uint32_t instance UNUSED,
                                               const uint8_t *source,
                                               uint32_t size,
                                               uint32_t offset,
                                               void *item UNUSED,
                                               uint32_t format UNUSED,
                                               uint32_t command UNUSED) {
    uint32_t total = size + offset;
    s8_dde_data_t *handle = s8_dde_create(NULL, total);
    if (!handle) return NULL;
    if (source && size) memcpy(handle->data + offset, source, size);
    return handle;
}

static void *WIN32_API s8_DdeClientTransaction(uint8_t *data,
                                                uint32_t size,
                                                void *conversation,
                                                void *item UNUSED,
                                                uint32_t format UNUSED,
                                                uint32_t type UNUSED,
                                                uint32_t timeout UNUSED,
                                                uint32_t *result) {
    s8_dde_data_t *handle;
    if (result) *result = 0U;
    /* Wine/Windows reject a transaction without a conversation.  Returning a
     * success token here made old applications believe that a DDE server had
     * accepted work which was never delivered. */
    if (!conversation) return NULL;
    if (!data || data == (uint8_t *)(uintptr_t)0xFFFFFFFFU || size == 0U)
        return &s8_dde_success_token;
    handle = s8_dde_create(data, size);
    return handle ? (void *)handle : NULL;
}

static uint8_t *WIN32_API s8_DdeAccessData(void *value, uint32_t *size) {
    s8_dde_data_t *handle = s8_dde_valid(value);
    if (!handle) { if (size) *size = 0U; return NULL; }
    if (size) *size = handle->size;
    return handle->data;
}

static int WIN32_API s8_DdeUnaccessData(void *value) {
    return s8_dde_valid(value) != NULL || value == &s8_dde_success_token;
}

static int WIN32_API s8_DdeFreeDataHandle(void *value) {
    s8_dde_data_t *handle = s8_dde_valid(value);
    if (value == &s8_dde_success_token) return 1;
    if (!handle) return 0;
    for (uint32_t i = 0; i < S8_MAX_DDE_DATA; i++) {
        if (s8_dde_handles[i] == handle) {
            s8_dde_handles[i] = NULL;
            break;
        }
    }
    handle->magic = 0U;
    if (handle->data) bk_sys_free(handle->data);
    bk_sys_free(handle);
    return 1;
}

static uint32_t WIN32_API s8_DdeGetData(void *value, uint8_t *destination,
                                        uint32_t maximum, uint32_t offset) {
    s8_dde_data_t *handle = s8_dde_valid(value);
    uint32_t available, copied;
    if (!handle || offset > handle->size) return 0U;
    available = handle->size - offset;
    if (!destination) return available;
    copied = maximum < available ? maximum : available;
    if (copied) memcpy(destination, handle->data + offset, copied);
    return copied;
}

static int WIN32_API s8_DdeAddData(void *value, const uint8_t *source,
                                    uint32_t size, uint32_t offset) {
    s8_dde_data_t *handle = s8_dde_valid(value);
    uint8_t *new_data;
    uint32_t new_size;
    if (!handle || !source) return 0;
    new_size = offset + size;
    if (new_size > handle->size) {
        new_data = (uint8_t *)bk_sys_realloc(handle->data, new_size);
        if (!new_data) return 0;
        if (new_size > handle->size)
            memset(new_data + handle->size, 0, new_size - handle->size);
        handle->data = new_data;
        handle->size = new_size;
    }
    memcpy(handle->data + offset, source, size);
    return 1;
}

static int WIN32_API s8_DdeAbandonTransaction(uint32_t instance UNUSED,
                                               void *conversation UNUSED,
                                               uint32_t transaction UNUSED) {
    return 1;
}
static int WIN32_API s8_DdeSetUserHandle(void *conversation UNUSED,
                                         uint32_t id UNUSED,
                                         uintptr_t user UNUSED) { return 1; }
static void *WIN32_API s8_DdeReconnect(void *conversation) { return conversation; }

/* ------------------------------------------------------------------------- */
/* KERNEL32: profile sections, time zone, console CP and old system queries.  */
/* ------------------------------------------------------------------------- */

typedef int (WIN32_API *s8_write_profile_a_fn)(const char *, const char *,
                                               const char *, const char *);
typedef int (WIN32_API *s8_write_profile_w_fn)(const uint16_t *,
                                               const uint16_t *,
                                               const uint16_t *,
                                               const uint16_t *);
typedef void *(WIN32_API *s8_get_std_handle_fn)(int32_t);

typedef struct {
    int32_t bias;
    uint16_t standard_name[32];
    uint16_t standard_date[8];
    int32_t standard_bias;
    uint16_t daylight_name[32];
    uint16_t daylight_date[8];
    int32_t daylight_bias;
} s8_timezone_information_t;

typedef struct {
    uint32_t length;
    uint32_t memory_load;
    uint32_t total_physical;
    uint32_t available_physical;
    uint32_t total_page_file;
    uint32_t available_page_file;
    uint32_t total_virtual;
    uint32_t available_virtual;
} s8_memory_status_t;

typedef struct {
    uint8_t ac_line_status;
    uint8_t battery_flag;
    uint8_t battery_life_percent;
    uint8_t reserved;
    uint32_t battery_life_time;
    uint32_t battery_full_life_time;
} s8_power_status_t;

typedef struct {
    uint32_t max_char_size;
    uint8_t default_char[2];
    uint8_t lead_byte[12];
} s8_cp_info_t;

typedef struct {
    uint32_t max_char_size;
    uint8_t default_char[2];
    uint8_t lead_byte[12];
    uint16_t unicode_default_char;
    uint32_t code_page;
    char code_page_name[260];
} s8_cp_info_ex_a_t;

typedef struct {
    uint32_t max_char_size;
    uint8_t default_char[2];
    uint8_t lead_byte[12];
    uint16_t unicode_default_char;
    uint32_t code_page;
    uint16_t code_page_name[260];
} s8_cp_info_ex_w_t;

static uint32_t s8_error_mode;
static bool s8_file_apis_ansi = true;
static uint32_t s8_console_input_cp = 437U;
static uint32_t s8_console_output_cp = 437U;
static void *s8_std_handles[3];
static uint32_t s8_handle_flags[16];

static int WIN32_API s8_WritePrivateProfileSectionA(const char *section,
                                                     const char *pairs,
                                                     const char *file) {
    s8_write_profile_a_fn write_string =
        (s8_write_profile_a_fn)(uintptr_t)
            win32_kernel32_resolve("WritePrivateProfileStringA");
    const char *cursor = pairs;
    if (!write_string || !section || !file) return 0;
    if (!pairs) return write_string(section, NULL, NULL, file);
    while (*cursor) {
        const char *equals = cursor;
        char key[128];
        uint32_t key_length;
        while (*equals && *equals != '=') equals++;
        if (*equals == '=') {
            key_length = (uint32_t)(equals - cursor);
            if (key_length >= sizeof(key)) key_length = sizeof(key) - 1U;
            for (uint32_t i = 0; i < key_length; i++) key[i] = cursor[i];
            key[key_length] = '\0';
            if (!write_string(section, key, equals + 1, file)) return 0;
        }
        cursor += s8_strlen(cursor) + 1U;
    }
    return 1;
}

static int WIN32_API s8_WritePrivateProfileSectionW(const uint16_t *section,
                                                     const uint16_t *pairs,
                                                     const uint16_t *file) {
    char section_a[128];
    char file_a[260];
    char pairs_a[1024];
    uint32_t out = 0U;
    const uint16_t *cursor = pairs;
    if (!section || !file) return 0;
    s8_wide_to_ansi(section, section_a, sizeof(section_a));
    s8_wide_to_ansi(file, file_a, sizeof(file_a));
    if (!pairs) return s8_WritePrivateProfileSectionA(section_a, NULL, file_a);
    while (*cursor && out + 2U < sizeof(pairs_a)) {
        uint32_t length = s8_wstrlen(cursor);
        uint32_t copy = length;
        if (copy > sizeof(pairs_a) - out - 2U)
            copy = sizeof(pairs_a) - out - 2U;
        for (uint32_t i = 0; i < copy; i++)
            pairs_a[out + i] = cursor[i] <= 255U ? (char)cursor[i] : '?';
        out += copy;
        pairs_a[out++] = '\0';
        cursor += length + 1U;
    }
    pairs_a[out] = '\0';
    return s8_WritePrivateProfileSectionA(section_a, pairs_a, file_a);
}

static uint32_t WIN32_API s8_GetPrivateProfileSectionA(
    const char *section UNUSED, char *buffer, uint32_t capacity,
    const char *file UNUSED) {
    if (!buffer || capacity < 2U) return 0U;
    buffer[0] = '\0';
    buffer[1] = '\0';
    return 0U;
}

static uint32_t WIN32_API s8_GetPrivateProfileSectionW(
    const uint16_t *section UNUSED, uint16_t *buffer, uint32_t capacity,
    const uint16_t *file UNUSED) {
    if (!buffer || capacity < 2U) return 0U;
    buffer[0] = 0;
    buffer[1] = 0;
    return 0U;
}

static uint32_t WIN32_API s8_GetPrivateProfileSectionNamesA(
    char *buffer, uint32_t capacity, const char *file UNUSED) {
    if (!buffer || capacity < 2U) return 0U;
    buffer[0] = '\0'; buffer[1] = '\0';
    return 0U;
}

static uint32_t WIN32_API s8_GetPrivateProfileSectionNamesW(
    uint16_t *buffer, uint32_t capacity, const uint16_t *file UNUSED) {
    if (!buffer || capacity < 2U) return 0U;
    buffer[0] = 0; buffer[1] = 0;
    return 0U;
}

static int WIN32_API s8_WriteProfileSectionA(const char *section,
                                              const char *pairs) {
    return s8_WritePrivateProfileSectionA(section, pairs, "WIN.INI");
}
static int WIN32_API s8_WriteProfileSectionW(const uint16_t *section,
                                              const uint16_t *pairs) {
    static const uint16_t win_ini[] = {'W','I','N','.','I','N','I',0};
    return s8_WritePrivateProfileSectionW(section, pairs, win_ini);
}
static uint32_t WIN32_API s8_GetProfileSectionA(const char *section,
                                                 char *buffer,
                                                 uint32_t capacity) {
    return s8_GetPrivateProfileSectionA(section, buffer, capacity, "WIN.INI");
}
static uint32_t WIN32_API s8_GetProfileSectionW(const uint16_t *section,
                                                 uint16_t *buffer,
                                                 uint32_t capacity) {
    static const uint16_t win_ini[] = {'W','I','N','.','I','N','I',0};
    return s8_GetPrivateProfileSectionW(section, buffer, capacity, win_ini);
}

static uint32_t WIN32_API s8_GetTimeZoneInformation(
    s8_timezone_information_t *information) {
    static const uint16_t utc[] = {'U','T','C',0};
    if (!information) return 0xFFFFFFFFU;
    memset(information, 0, sizeof(*information));
    s8_copy_w(information->standard_name, 32U, utc);
    s8_copy_w(information->daylight_name, 32U, utc);
    return S8_TIME_ZONE_ID_UNKNOWN;
}

static int WIN32_API s8_SetTimeZoneInformation(
    const s8_timezone_information_t *information) {
    return information != NULL;
}

static uint32_t WIN32_API s8_SetErrorMode(uint32_t mode) {
    uint32_t old = s8_error_mode;
    s8_error_mode = mode;
    return old;
}
static uint32_t WIN32_API s8_GetErrorMode(void) { return s8_error_mode; }
static int WIN32_API s8_SetThreadErrorMode(uint32_t mode, uint32_t *old) {
    if (old) *old = s8_error_mode;
    s8_error_mode = mode;
    return 1;
}

static int WIN32_API s8_AreFileApisANSI(void) { return s8_file_apis_ansi; }
static void WIN32_API s8_SetFileApisToANSI(void) { s8_file_apis_ansi = true; }
static void WIN32_API s8_SetFileApisToOEM(void) { s8_file_apis_ansi = false; }

static uint32_t WIN32_API s8_GetConsoleCP(void) { return s8_console_input_cp; }
static uint32_t WIN32_API s8_GetConsoleOutputCP(void) {
    return s8_console_output_cp;
}
static int WIN32_API s8_SetConsoleCP(uint32_t cp) {
    if (!cp) return 0;
    s8_console_input_cp = cp;
    return 1;
}
static int WIN32_API s8_SetConsoleOutputCP(uint32_t cp) {
    if (!cp) return 0;
    s8_console_output_cp = cp;
    return 1;
}

static int WIN32_API s8_Beep(uint32_t frequency, uint32_t duration) {
    if (frequency < 37U || frequency > 32767U) return 0;
    return bk_sound_tone(frequency, duration);
}

static uint16_t WIN32_API s8_GetSystemDefaultLangID(void) {
    return (uint16_t)S8_LANG_ENGLISH_US;
}
static uint16_t WIN32_API s8_GetUserDefaultLangID(void) {
    return (uint16_t)S8_LANG_ENGLISH_US;
}
static uint16_t WIN32_API s8_GetSystemDefaultUILanguage(void) {
    return (uint16_t)S8_LANG_ENGLISH_US;
}
static uint16_t WIN32_API s8_GetUserDefaultUILanguage(void) {
    return (uint16_t)S8_LANG_ENGLISH_US;
}

static int WIN32_API s8_IsValidCodePage(uint32_t cp) {
    return cp == 0U || cp == 1U || cp == 437U || cp == 850U ||
           cp == 1252U || cp == 65001U;
}

static int WIN32_API s8_GetCPInfoExA(uint32_t cp, uint32_t flags UNUSED,
                                     s8_cp_info_ex_a_t *info) {
    const char *name = cp == 65001U ? "Unicode (UTF-8)" :
                       (cp == 437U ? "OEM United States" :
                        "Western European (Windows)");
    if (!info || !s8_IsValidCodePage(cp)) return 0;
    memset(info, 0, sizeof(*info));
    info->max_char_size = cp == 65001U ? 4U : 1U;
    info->default_char[0] = '?';
    info->unicode_default_char = '?';
    info->code_page = cp ? cp : 1252U;
    s8_copy_a(info->code_page_name, sizeof(info->code_page_name), name);
    return 1;
}

static int WIN32_API s8_GetCPInfoExW(uint32_t cp, uint32_t flags,
                                     s8_cp_info_ex_w_t *info) {
    s8_cp_info_ex_a_t ansi;
    if (!info || !s8_GetCPInfoExA(cp, flags, &ansi)) return 0;
    memset(info, 0, sizeof(*info));
    info->max_char_size = ansi.max_char_size;
    info->default_char[0] = ansi.default_char[0];
    info->unicode_default_char = ansi.unicode_default_char;
    info->code_page = ansi.code_page;
    s8_ansi_to_wide(ansi.code_page_name, info->code_page_name, 260U);
    return 1;
}

static int WIN32_API s8_GetSystemPowerStatus(s8_power_status_t *status) {
    if (!status) return 0;
    memset(status, 0, sizeof(*status));
    status->ac_line_status = 1U;
    status->battery_flag = 128U;
    status->battery_life_percent = 255U;
    status->battery_life_time = 0xFFFFFFFFU;
    status->battery_full_life_time = 0xFFFFFFFFU;
    return 1;
}

static void WIN32_API s8_GlobalMemoryStatus(s8_memory_status_t *status) {
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->length = sizeof(*status);
    /* Old Win9x applications treat this as a coarse compatibility signal, not
     * as telemetry.  Reporting the tiny/current kernel heap makes WinZip enter
     * its low-resource path and later abort while building WZSBAR. */
    status->memory_load = 25U;
    status->total_physical = 128U << 20;
    status->available_physical = 96U << 20;
    status->total_page_file = status->total_physical;
    status->available_page_file = status->available_physical;
    status->total_virtual = 0x70000000U;
    status->available_virtual = 0x60000000U;
}

static int WIN32_API s8_DuplicateHandle(void *source_process UNUSED,
                                        void *source,
                                        void *target_process UNUSED,
                                        void **target,
                                        uint32_t access UNUSED,
                                        int inherit UNUSED,
                                        uint32_t options UNUSED) {
    if (!target) return 0;
    *target = source;
    return source != NULL;
}

static int WIN32_API s8_GetHandleInformation(void *handle,
                                             uint32_t *flags) {
    uintptr_t value = (uintptr_t)handle;
    if (!flags || !handle) return 0;
    *flags = s8_handle_flags[value & 15U];
    return 1;
}

static int WIN32_API s8_SetHandleInformation(void *handle,
                                             uint32_t mask,
                                             uint32_t flags) {
    uintptr_t value = (uintptr_t)handle;
    if (!handle) return 0;
    s8_handle_flags[value & 15U] =
        (s8_handle_flags[value & 15U] & ~mask) | (flags & mask);
    return 1;
}

static int WIN32_API s8_SetStdHandle(int32_t which, void *handle) {
    uint32_t index;
    if (which == -10) index = 0U;
    else if (which == -11) index = 1U;
    else if (which == -12) index = 2U;
    else return 0;
    s8_std_handles[index] = handle;
    return 1;
}

static uint32_t WIN32_API s8_GetProcessVersion(uint32_t process UNUSED) {
    return 0x00040000U;
}
static uint32_t WIN32_API s8_GetPriorityClass(void *process UNUSED) {
    return 0x20U;
}
static int WIN32_API s8_SetPriorityClass(void *process UNUSED,
                                         uint32_t value UNUSED) { return 1; }
static int WIN32_API s8_GetThreadPriority(void *thread UNUSED) { return 0; }
static int WIN32_API s8_SetThreadPriority(void *thread UNUSED,
                                          int priority UNUSED) { return 1; }

/* BLES_WINE_RESOURCE_DEBUG_20260723 */
static uint16_t WIN32_API s8_GetFreeSystemResources(uint16_t resource_type) {
    uint16_t result;

    /* Wine implements USER.284 as the free percentage of the 16-bit USER/GDI
     * local heaps, and SYSTEM as the lower of both values. BlesKernOS has no
     * Win16 local heaps, so expose a conservative healthy profile. */
    switch (resource_type) {
    case S8_GFSR_SYSTEMRESOURCES:
    case S8_GFSR_GDIRESOURCES:
    case S8_GFSR_USERRESOURCES:
        result = 90U;
        break;
    default:
        result = 0U;
        break;
    }

    kprintf("[USER:RESOURCES] GetFreeSystemResources type=%u -> %u%%\n",
            resource_type, result);
    return result;
}

/* ------------------------------------------------------------------------- */
/* MSVCRT20/40: VC2/MFC30 data exports and low-risk compatibility helpers.    */
/* ------------------------------------------------------------------------- */

static int32_t s8_adjust_fdiv;
static int32_t s8_fileinfo;
static int32_t s8_osver = 0x00040000;
static int32_t s8_winver = 0x00040000;
static int32_t s8_winmajor = 4;
static int32_t s8_winminor;
static int32_t s8_osplatform = 1;
static int32_t s8_sys_nerr;
static char **s8_sys_errlist;
static double s8_huge = 1.7976931348623157e308;
static uint16_t s8_pctype_storage[257];
static uint16_t *s8_pctype = &s8_pctype_storage[1];
static uint16_t s8_pwctype_storage[257];
static uint16_t *s8_pwctype = &s8_pwctype_storage[1];
static uint8_t s8_mbctype_storage[257];
static uint8_t *s8_mbctype = &s8_mbctype_storage[1];
static uint8_t s8_mbcasemap_storage[256];
static uint8_t *s8_mbcasemap = s8_mbcasemap_storage;
static uint32_t s8_control_word = 0x0009001FU;

static int WIN32_API s8_purecall(void) { return 0; }
static int WIN32_API s8_except_handler3(void *record UNUSED,
                                        void *frame UNUSED,
                                        void *context UNUSED,
                                        void *dispatch UNUSED) { return 1; }
static int WIN32_API s8_CxxFrameHandler(void *record UNUSED,
                                        void *frame UNUSED,
                                        void *context UNUSED,
                                        void *dispatch UNUSED) { return 1; }
static void WIN32_API s8_CxxThrowException(void *object UNUSED,
                                           void *info UNUSED) { }
static void WIN32_API s8_global_unwind2(void *frame UNUSED) { }
static void WIN32_API s8_local_unwind2(void *frame UNUSED,
                                       int stop UNUSED) { }
static int WIN32_API s8_abnormal_termination(void) { return 0; }

static uint32_t WIN32_API s8_controlfp(uint32_t value, uint32_t mask) {
    uint32_t old = s8_control_word;
    s8_control_word = (s8_control_word & ~mask) | (value & mask);
    return old;
}
static uint32_t WIN32_API s8_control87(uint32_t value, uint32_t mask) {
    return s8_controlfp(value, mask);
}
static uint32_t WIN32_API s8_clearfp(void) { return 0U; }
static uint32_t WIN32_API s8_statusfp(void) { return 0U; }
static void WIN32_API s8_fpreset(void) { s8_control_word = 0x0009001FU; }

static int WIN32_API s8_memicmp(const void *left, const void *right,
                                 uint32_t length) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    for (uint32_t i = 0; i < length; i++) {
        int difference = (int)s8_upper(a[i]) - (int)s8_upper(b[i]);
        if (difference) return difference;
    }
    return 0;
}

static int WIN32_API s8_stricmp(const char *left, const char *right) {
    uint8_t a, b;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    do {
        a = s8_upper((uint8_t)*left++);
        b = s8_upper((uint8_t)*right++);
        if (a != b) return (int)a - (int)b;
    } while (a);
    return 0;
}

static int WIN32_API s8_strnicmp(const char *left, const char *right,
                                  uint32_t length) {
    if (!length) return 0;
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (length--) {
        uint8_t a = s8_upper((uint8_t)*left++);
        uint8_t b = s8_upper((uint8_t)*right++);
        if (a != b) return (int)a - (int)b;
        if (!a) return 0;
    }
    return 0;
}

static char *WIN32_API s8_strdup(const char *text) {
    uint32_t length;
    char *copy;
    if (!text) return NULL;
    length = s8_strlen(text) + 1U;
    copy = (char *)bk_sys_alloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static char *WIN32_API s8_strlwr(char *text) {
    if (text) for (uint32_t i = 0; text[i]; i++)
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 'a' - 'A';
    return text;
}
static char *WIN32_API s8_strupr(char *text) {
    if (text) for (uint32_t i = 0; text[i]; i++)
        if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 'a' - 'A';
    return text;
}
static char *WIN32_API s8_strset(char *text, int value) {
    if (text) for (uint32_t i = 0; text[i]; i++) text[i] = (char)value;
    return text;
}
static char *WIN32_API s8_strnset(char *text, int value, uint32_t length) {
    if (text) for (uint32_t i = 0; i < length && text[i]; i++)
        text[i] = (char)value;
    return text;
}

static uint32_t WIN32_API s8_mbslen(const uint8_t *text) {
    return s8_strlen((const char *)text);
}
static uint32_t WIN32_API s8_mbstrlen(const uint8_t *text) {
    return s8_mbslen(text);
}
static uint8_t *WIN32_API s8_mbsinc(const uint8_t *text) {
    return text && *text ? (uint8_t *)(text + 1) : (uint8_t *)text;
}
static uint32_t WIN32_API s8_mbsnextc(const uint8_t *text) {
    return text ? *text : 0U;
}
static int WIN32_API s8_mbclen(const uint8_t *text) {
    return text && *text ? 1 : 0;
}
static int WIN32_API s8_ismbblead(uint32_t value UNUSED) { return 0; }
static int WIN32_API s8_ismbbtrail(uint32_t value UNUSED) { return 0; }
static uint8_t *WIN32_API s8_mbscpy(uint8_t *dst, const uint8_t *src) {
    if (!dst || !src) return dst;
    s8_copy_a((char *)dst, s8_strlen((const char *)src) + 1U,
              (const char *)src);
    return dst;
}
static uint8_t *WIN32_API s8_mbsncpy(uint8_t *dst, const uint8_t *src,
                                     uint32_t count) {
    uint32_t i = 0U;
    if (!dst || !src) return dst;
    while (i < count && src[i]) { dst[i] = src[i]; i++; }
    while (i < count) dst[i++] = 0U;
    return dst;
}
static uint8_t *WIN32_API s8_mbscat(uint8_t *dst, const uint8_t *src) {
    uint32_t offset;
    if (!dst || !src) return dst;
    offset = s8_strlen((const char *)dst);
    s8_mbscpy(dst + offset, src);
    return dst;
}
static int WIN32_API s8_mbscmp(const uint8_t *left, const uint8_t *right) {
    if (!left || !right) return left == right ? 0 : (left ? 1 : -1);
    while (*left && *left == *right) { left++; right++; }
    return (int)*left - (int)*right;
}
static int WIN32_API s8_mbsicmp(const uint8_t *left, const uint8_t *right) {
    return s8_stricmp((const char *)left, (const char *)right);
}
static uint8_t *WIN32_API s8_mbschr(const uint8_t *text, uint32_t value) {
    if (!text) return NULL;
    while (*text != (uint8_t)value) { if (!*text++) return NULL; }
    return (uint8_t *)text;
}
static uint8_t *WIN32_API s8_mbsrchr(const uint8_t *text, uint32_t value) {
    const uint8_t *result = NULL;
    if (!text) return NULL;
    do { if (*text == (uint8_t)value) result = text; } while (*text++);
    return (uint8_t *)result;
}
static uint8_t *WIN32_API s8_mbsstr(const uint8_t *text,
                                    const uint8_t *needle) {
    uint32_t length;
    if (!text || !needle) return NULL;
    length = s8_strlen((const char *)needle);
    if (!length) return (uint8_t *)text;
    for (; *text; text++)
        if (s8_memicmp(text, needle, length) == 0) return (uint8_t *)text;
    return NULL;
}

static int WIN32_API s8_isnan(double value) { return value != value; }
static int WIN32_API s8_finite(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return ((bits.u >> 52) & 0x7FFU) != 0x7FFU;
}
static double WIN32_API s8_copysign(double value, double sign) {
    union { double d; uint64_t u; } a, b;
    a.d = value; b.d = sign;
    a.u = (a.u & 0x7FFFFFFFFFFFFFFFULL) |
          (b.u & 0x8000000000000000ULL);
    return a.d;
}
static double WIN32_API s8_chgsign(double value) { return -value; }

/* ------------------------------------------------------------------------- */
/* Resolver registration.                                                    */
/* ------------------------------------------------------------------------- */

uint32_t win32_wine_stage8_resolve(const char *dll, const char *name) {
    if (!dll || !name) return 0U;

    if (s8_equal_ci(dll, "USER32.DLL")) {
#define S8_USER(api) if (s8_equal_ci(name, #api)) return (uint32_t)(uintptr_t)&s8_##api
        S8_USER(MapDialogRect); S8_USER(GetLastActivePopup);
        S8_USER(GetWindow); S8_USER(GetNextWindow); S8_USER(GetWindowTask);
        S8_USER(IsIconic); S8_USER(IsZoomed); S8_USER(OpenIcon);
        S8_USER(CloseWindow); S8_USER(FlashWindow); S8_USER(LockWindowUpdate);
        S8_USER(GetWindowRgn); S8_USER(SetWindowRgn);
        S8_USER(ArrangeIconicWindows); S8_USER(CascadeWindows); S8_USER(TileWindows);
        S8_USER(AnyPopup); S8_USER(DefFrameProcA); S8_USER(DefFrameProcW);
        S8_USER(DefMDIChildProcA); S8_USER(DefMDIChildProcW);
        S8_USER(TranslateMDISysAccel);
        S8_USER(GetWindowContextHelpId); S8_USER(SetWindowContextHelpId);
        S8_USER(GetMenuContextHelpId); S8_USER(SetMenuContextHelpId);
        S8_USER(SetWindowPlacement);
        S8_USER(DdeClientTransaction); S8_USER(DdeCreateDataHandle);
        S8_USER(DdeAccessData); S8_USER(DdeUnaccessData);
        S8_USER(DdeFreeDataHandle); S8_USER(DdeGetData); S8_USER(DdeAddData);
        S8_USER(DdeAbandonTransaction); S8_USER(DdeSetUserHandle);
        S8_USER(DdeReconnect);
#undef S8_USER
    }

    if (s8_equal_ci(dll, "KERNEL32.DLL") ||
        s8_equal_ci(dll, "KERNELBASE.DLL")) {
#define S8_KERNEL(api) if (s8_equal_ci(name, #api)) return (uint32_t)(uintptr_t)&s8_##api
        S8_KERNEL(WritePrivateProfileSectionA);
        S8_KERNEL(WritePrivateProfileSectionW);
        S8_KERNEL(GetPrivateProfileSectionA);
        S8_KERNEL(GetPrivateProfileSectionW);
        S8_KERNEL(GetPrivateProfileSectionNamesA);
        S8_KERNEL(GetPrivateProfileSectionNamesW);
        S8_KERNEL(WriteProfileSectionA); S8_KERNEL(WriteProfileSectionW);
        S8_KERNEL(GetProfileSectionA); S8_KERNEL(GetProfileSectionW);
        S8_KERNEL(GetTimeZoneInformation); S8_KERNEL(SetTimeZoneInformation);
        S8_KERNEL(SetErrorMode); S8_KERNEL(GetErrorMode); S8_KERNEL(SetThreadErrorMode);
        S8_KERNEL(AreFileApisANSI); S8_KERNEL(SetFileApisToANSI); S8_KERNEL(SetFileApisToOEM);
        S8_KERNEL(GetConsoleCP); S8_KERNEL(GetConsoleOutputCP);
        S8_KERNEL(SetConsoleCP); S8_KERNEL(SetConsoleOutputCP);
        S8_KERNEL(Beep);
        S8_KERNEL(GetSystemDefaultLangID); S8_KERNEL(GetUserDefaultLangID);
        S8_KERNEL(GetSystemDefaultUILanguage); S8_KERNEL(GetUserDefaultUILanguage);
        S8_KERNEL(IsValidCodePage); S8_KERNEL(GetCPInfoExA);
        S8_KERNEL(GetCPInfoExW); S8_KERNEL(GetSystemPowerStatus);
        S8_KERNEL(GlobalMemoryStatus); S8_KERNEL(DuplicateHandle);
        S8_KERNEL(GetHandleInformation); S8_KERNEL(SetHandleInformation);
        S8_KERNEL(SetStdHandle); S8_KERNEL(GetProcessVersion);
        S8_KERNEL(GetPriorityClass); S8_KERNEL(SetPriorityClass);
        S8_KERNEL(GetThreadPriority); S8_KERNEL(SetThreadPriority);
#undef S8_KERNEL
    }

    if (s8_equal_ci(dll, "USER.EXE") || s8_equal_ci(dll, "USER")) {
        if (s8_equal_ci(name, "GetFreeSystemResources") ||
            s8_equal_ci(name, "GetFreeSystemResources16"))
            return (uint32_t)(uintptr_t)&s8_GetFreeSystemResources;
    }

    if (s8_is_crt(dll)) {
#define S8_CRT(api_name, target) \
        if (s8_equal_ci(name, api_name)) return (uint32_t)(uintptr_t)&target
        S8_CRT("_purecall", s8_purecall);
        S8_CRT("_except_handler3", s8_except_handler3);
        S8_CRT("__CxxFrameHandler", s8_CxxFrameHandler);
        S8_CRT("_CxxThrowException", s8_CxxThrowException);
        S8_CRT("_global_unwind2", s8_global_unwind2);
        S8_CRT("_local_unwind2", s8_local_unwind2);
        S8_CRT("_abnormal_termination", s8_abnormal_termination);
        S8_CRT("_controlfp", s8_controlfp);
        S8_CRT("_control87", s8_control87);
        S8_CRT("_clearfp", s8_clearfp);
        S8_CRT("_statusfp", s8_statusfp);
        S8_CRT("_fpreset", s8_fpreset);
        S8_CRT("_memicmp", s8_memicmp);
        S8_CRT("_stricmp", s8_stricmp);
        S8_CRT("_strnicmp", s8_strnicmp);
        S8_CRT("_strdup", s8_strdup);
        S8_CRT("_strlwr", s8_strlwr);
        S8_CRT("_strupr", s8_strupr);
        S8_CRT("_strset", s8_strset);
        S8_CRT("_strnset", s8_strnset);
        S8_CRT("_mbslen", s8_mbslen);
        S8_CRT("_mbstrlen", s8_mbstrlen);
        S8_CRT("_mbsinc", s8_mbsinc);
        S8_CRT("_mbsnextc", s8_mbsnextc);
        S8_CRT("_mbclen", s8_mbclen);
        S8_CRT("_ismbblead", s8_ismbblead);
        S8_CRT("_ismbbtrail", s8_ismbbtrail);
        S8_CRT("_mbscpy", s8_mbscpy);
        S8_CRT("_mbsncpy", s8_mbsncpy);
        S8_CRT("_mbscat", s8_mbscat);
        S8_CRT("_mbscmp", s8_mbscmp);
        S8_CRT("_mbsicmp", s8_mbsicmp);
        S8_CRT("_mbschr", s8_mbschr);
        S8_CRT("_mbsrchr", s8_mbsrchr);
        S8_CRT("_mbsstr", s8_mbsstr);
        S8_CRT("_isnan", s8_isnan);
        S8_CRT("_finite", s8_finite);
        S8_CRT("_copysign", s8_copysign);
        S8_CRT("_chgsign", s8_chgsign);
#undef S8_CRT
        if (s8_equal_ci(name, "_adjust_fdiv"))
            return (uint32_t)(uintptr_t)&s8_adjust_fdiv;
        if (s8_equal_ci(name, "_fileinfo"))
            return (uint32_t)(uintptr_t)&s8_fileinfo;
        if (s8_equal_ci(name, "_osver"))
            return (uint32_t)(uintptr_t)&s8_osver;
        if (s8_equal_ci(name, "_winver"))
            return (uint32_t)(uintptr_t)&s8_winver;
        if (s8_equal_ci(name, "_winmajor"))
            return (uint32_t)(uintptr_t)&s8_winmajor;
        if (s8_equal_ci(name, "_winminor"))
            return (uint32_t)(uintptr_t)&s8_winminor;
        if (s8_equal_ci(name, "_osplatform"))
            return (uint32_t)(uintptr_t)&s8_osplatform;
        if (s8_equal_ci(name, "_sys_nerr"))
            return (uint32_t)(uintptr_t)&s8_sys_nerr;
        if (s8_equal_ci(name, "_sys_errlist"))
            return (uint32_t)(uintptr_t)&s8_sys_errlist;
        if (s8_equal_ci(name, "_HUGE"))
            return (uint32_t)(uintptr_t)&s8_huge;
        if (s8_equal_ci(name, "_pctype"))
            return (uint32_t)(uintptr_t)&s8_pctype;
        if (s8_equal_ci(name, "_pwctype"))
            return (uint32_t)(uintptr_t)&s8_pwctype;
        if (s8_equal_ci(name, "_mbctype"))
            return (uint32_t)(uintptr_t)&s8_mbctype;
        if (s8_equal_ci(name, "_mbcasemap"))
            return (uint32_t)(uintptr_t)&s8_mbcasemap;
    }

    return 0U;
}

uint32_t win32_wine_stage8_resolve_ordinal(const char *dll,
                                            uint16_t ordinal) {
    if ((s8_equal_ci(dll, "USER.EXE") || s8_equal_ci(dll, "USER")) &&
        ordinal == 284U)
        return (uint32_t)(uintptr_t)&s8_GetFreeSystemResources;
    return 0U;
}

bool win32_wine_stage8_is_data_export(const char *dll, const char *name) {
    if (!s8_is_crt(dll) || !name) return false;
    return s8_equal_ci(name, "_adjust_fdiv") ||
           s8_equal_ci(name, "_fileinfo") ||
           s8_equal_ci(name, "_osver") ||
           s8_equal_ci(name, "_winver") ||
           s8_equal_ci(name, "_winmajor") ||
           s8_equal_ci(name, "_winminor") ||
           s8_equal_ci(name, "_osplatform") ||
           s8_equal_ci(name, "_sys_nerr") ||
           s8_equal_ci(name, "_sys_errlist") ||
           s8_equal_ci(name, "_HUGE") ||
           s8_equal_ci(name, "_pctype") ||
           s8_equal_ci(name, "_pwctype") ||
           s8_equal_ci(name, "_mbctype") ||
           s8_equal_ci(name, "_mbcasemap");
}

static void s8_initialize_ctype(void) {
    for (uint32_t i = 0; i < 256U; i++) {
        uint16_t flags = 0U;
        if (i >= 'A' && i <= 'Z') flags |= 0x0001U | 0x0100U;
        if (i >= 'a' && i <= 'z') flags |= 0x0002U | 0x0100U;
        if (i >= '0' && i <= '9') flags |= 0x0004U | 0x0080U;
        if (i == ' ' || i == '\t' || i == '\n' || i == '\r' || i == '\f' || i == '\v')
            flags |= 0x0008U;
        if (i < 0x20U || i == 0x7FU) flags |= 0x0020U;
        if (i >= 0x21U && i <= 0x7EU && !(flags & 0x0107U)) flags |= 0x0010U;
        s8_pctype_storage[i + 1U] = flags;
        s8_pwctype_storage[i + 1U] = flags;
        s8_mbctype_storage[i + 1U] = 0U;
        s8_mbcasemap_storage[i] = (uint8_t)i;
    }
}

bool win32_wine_stage8_init(void) {
    s8_initialize_ctype();
    return win32_register_resolver(win32_wine_stage8_resolve,
                                   win32_wine_stage8_resolve_ordinal,
                                   win32_wine_stage8_is_data_export);
}
