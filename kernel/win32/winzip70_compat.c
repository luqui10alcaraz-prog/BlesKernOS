#include "win32.h"
#include "../include/pe_loader.h"
#include "../include/memory.h"
#include "../include/task.h"

/* BLES_WINE_WINZIP70_IMPORTS_20260723
 *
 * Safe, signature-correct compatibility surface for the imports used by
 * WinZip 7.0.  These functions were absent from the active resolvers; the old
 * stage-9 generic table intentionally cannot be used because a wrong stdcall
 * arity corrupts the Ring-3 stack.
 */

#define ERROR_INVALID_PARAMETER 87U
#define WAIT_OBJECT_0 0U
#define GW_HWNDNEXT 2U
#define GW_CHILD 5U
#define DEFAULT_PALETTE 15
#define NULLREGION 1
#define SIMPLEREGION 2
#define CLR_NONE 0xFFFFFFFFU
#define DRAGDROP_S_CANCEL 0x00040101U
#define MEMCTX_TASK 1U
#define COMPAT_HOOK_BASE 0x71600000U
#define COMPAT_HOOK_MAX 16U
#define COMPAT_CLIPBOARD_FORMAT_BASE 0xC000U
#define COMPAT_CLIPBOARD_FORMAT_MAX 32U
#define COMPAT_IMAGELIST_COLORS 16U

typedef void * (WIN32_API *find_window_a_t)(const char *, const char *);
typedef void * (WIN32_API *get_window_t)(void *, uint32_t);
typedef int (WIN32_API *get_class_name_a_t)(void *, char *, int);
typedef int (WIN32_API *get_window_text_a_t)(void *, char *, int);
typedef int (WIN32_API *append_menu_a_t)(void *, uint32_t, uint32_t,
                                         const char *);
typedef int (WIN32_API *invalidate_rect_t)(void *, const int32_t *, int);
typedef uint32_t (WIN32_API *track_popup_menu_ex_t)(void *, uint32_t, int,
                                                    int, void *, void *);
typedef int (WIN32_API *destroy_icon_t)(void *);
typedef void * (WIN32_API *get_dc_t)(void *);
typedef int (WIN32_API *draw_text_a_t)(void *, const char *, int, int32_t *,
                                       uint32_t);
typedef void * (WIN32_API *create_dc_a_t)(const char *, const char *,
                                          const char *, const void *);
typedef void * (WIN32_API *create_solid_brush_t)(uint32_t);
typedef void * (WIN32_API *get_stock_object_t)(int);
typedef uint32_t (WIN32_API *co_get_malloc_t)(uint32_t, void **);

typedef struct {
    bool used;
    uint32_t owner_process_id;
    int hook_type;
    void *procedure;
    void *module;
    uint32_t thread_id;
} compat_hook_t;

typedef struct {
    bool used;
    char name[64];
} compat_clipboard_format_t;

typedef struct {
    bool used;
    void *handle;
    uint32_t color;
} compat_imagelist_color_t;

static compat_hook_t compat_hooks[COMPAT_HOOK_MAX];
static compat_clipboard_format_t
    compat_clipboard_formats[COMPAT_CLIPBOARD_FORMAT_MAX];
static compat_imagelist_color_t
    compat_imagelist_colors[COMPAT_IMAGELIST_COLORS];
static void *compat_selected_palette;

static uint8_t compat_upper(uint8_t value) {
    return value >= 'a' && value <= 'z'
        ? (uint8_t)(value - ('a' - 'A')) : value;
}

static bool compat_equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right) {
        if (compat_upper((uint8_t)*left) != compat_upper((uint8_t)*right))
            return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool compat_window_matches(void *hwnd, const char *class_name,
                                  const char *title) {
    get_class_name_a_t get_class;
    get_window_text_a_t get_text;
    char class_buffer[64];
    char title_buffer[260];

    if (!hwnd) return false;
    get_class = (get_class_name_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "GetClassNameA");
    get_text = (get_window_text_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "GetWindowTextA");
    if (class_name) {
        if (!get_class || !get_class(hwnd, class_buffer,
                                     (int)sizeof(class_buffer)) ||
            !compat_equal_ci(class_buffer, class_name)) return false;
    }
    if (title) {
        if (!get_text) return false;
        (void)get_text(hwnd, title_buffer, (int)sizeof(title_buffer));
        if (kstrcmp(title_buffer, title) != 0) return false;
    }
    return true;
}

static uint32_t WIN32_API compat_HeapSize(void *heap,
                                           uint32_t flags UNUSED,
                                           const void *memory) {
    size_t size;

    if (!heap || !memory || !(size = mm_allocation_size(memory))) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0xFFFFFFFFU;
    }
    pe_win32_set_last_error(0U);
    return size > 0xFFFFFFFFU ? 0xFFFFFFFFU : (uint32_t)size;
}

static int WIN32_API compat_PeekNamedPipe(
    void *handle, void *buffer UNUSED, uint32_t buffer_size UNUSED,
    uint32_t *bytes_read, uint32_t *available, uint32_t *left_in_message) {
    if (bytes_read) *bytes_read = 0U;
    if (available) *available = 0U;
    if (left_in_message) *left_in_message = 0U;
    if (!handle || handle == (void *)(uintptr_t)0xFFFFFFFFU) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 1;
}

static void *WIN32_API compat_FindWindowExA(void *parent, void *after,
                                             const char *class_name,
                                             const char *title) {
    get_window_t get_window;
    find_window_a_t find_window;
    void *candidate;

    if (!parent && !after) {
        find_window = (find_window_a_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "FindWindowA");
        return find_window ? find_window(class_name, title) : NULL;
    }
    get_window = (get_window_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "GetWindow");
    if (!get_window) return NULL;
    candidate = after ? get_window(after, GW_HWNDNEXT)
                      : get_window(parent, GW_CHILD);
    while (candidate) {
        if (compat_window_matches(candidate, class_name, title))
            return candidate;
        candidate = get_window(candidate, GW_HWNDNEXT);
    }
    return NULL;
}

static uint32_t WIN32_API compat_WaitForInputIdle(
    void *process UNUSED, uint32_t milliseconds UNUSED) {
    task_yield();
    return WAIT_OBJECT_0;
}

static int WIN32_API compat_InsertMenuA(void *menu, uint32_t position UNUSED,
                                        uint32_t flags, uint32_t id,
                                        const char *text) {
    append_menu_a_t append = (append_menu_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "AppendMenuA");
    return append ? append(menu, flags, id, text) : 0;
}

static void *WIN32_API compat_SetWindowsHookExA(
    int type, void *procedure, void *module, uint32_t thread_id) {
    if (!procedure) return NULL;
    for (uint32_t i = 0; i < COMPAT_HOOK_MAX; i++) {
        if (compat_hooks[i].used) continue;
        compat_hooks[i].used = true;
        compat_hooks[i].owner_process_id = task_current_process_id();
        compat_hooks[i].hook_type = type;
        compat_hooks[i].procedure = procedure;
        compat_hooks[i].module = module;
        compat_hooks[i].thread_id = thread_id;
        return (void *)(uintptr_t)(COMPAT_HOOK_BASE + i);
    }
    return NULL;
}

static void *WIN32_API compat_SetWindowsHookA(int type, void *procedure) {
    return compat_SetWindowsHookExA(type, procedure, NULL,
                                    task_current_pid());
}

static int WIN32_API compat_UnhookWindowsHookEx(void *hook) {
    uint32_t value = (uint32_t)(uintptr_t)hook;
    uint32_t index;
    if (value < COMPAT_HOOK_BASE ||
        value >= COMPAT_HOOK_BASE + COMPAT_HOOK_MAX) return 0;
    index = value - COMPAT_HOOK_BASE;
    if (!compat_hooks[index].used) return 0;
    kmemset(&compat_hooks[index], 0, sizeof(compat_hooks[index]));
    return 1;
}

static int32_t WIN32_API compat_CallNextHookEx(
    void *hook UNUSED, int code UNUSED, uint32_t wparam UNUSED,
    int32_t lparam UNUSED) {
    return 0;
}

static int WIN32_API compat_DrawTextA(void *dc, const char *text, int length,
                                      int32_t *rect, uint32_t format) {
    draw_text_a_t draw = (draw_text_a_t)(uintptr_t)
        pe_win32_resolve_export("GDI32.DLL", "DrawTextA");
    return draw ? draw(dc, text, length, rect, format) : 0;
}

static int WIN32_API compat_SetMenuDefaultItem(
    void *menu, uint32_t item UNUSED, uint32_t by_position UNUSED) {
    return menu != NULL;
}

static int WIN32_API compat_InvalidateRgn(void *hwnd, void *region UNUSED,
                                          int erase) {
    invalidate_rect_t invalidate = (invalidate_rect_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "InvalidateRect");
    return invalidate ? invalidate(hwnd, NULL, erase) : 0;
}

static uint32_t WIN32_API compat_TrackPopupMenu(
    void *menu, uint32_t flags, int x, int y, int reserved UNUSED,
    void *owner, const int32_t *rect UNUSED) {
    track_popup_menu_ex_t track = (track_popup_menu_ex_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "TrackPopupMenuEx");
    return track ? track(menu, flags, x, y, owner, NULL) : 0U;
}

static int WIN32_API compat_DestroyCursor(void *cursor) {
    destroy_icon_t destroy = (destroy_icon_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "DestroyIcon");
    return destroy ? destroy(cursor) : 0;
}

static uint32_t WIN32_API compat_RegisterClipboardFormatA(const char *name) {
    if (!name || !name[0]) return 0U;
    for (uint32_t i = 0; i < COMPAT_CLIPBOARD_FORMAT_MAX; i++) {
        if (compat_clipboard_formats[i].used &&
            compat_equal_ci(compat_clipboard_formats[i].name, name))
            return COMPAT_CLIPBOARD_FORMAT_BASE + i;
    }
    for (uint32_t i = 0; i < COMPAT_CLIPBOARD_FORMAT_MAX; i++) {
        if (compat_clipboard_formats[i].used) continue;
        compat_clipboard_formats[i].used = true;
        kstrncpy(compat_clipboard_formats[i].name, name,
                 sizeof(compat_clipboard_formats[i].name) - 1U);
        return COMPAT_CLIPBOARD_FORMAT_BASE + i;
    }
    return 0U;
}

static int WIN32_API compat_GetClipboardFormatNameA(
    uint32_t format, char *name, int capacity) {
    uint32_t index;
    uint32_t length;
    if (format < COMPAT_CLIPBOARD_FORMAT_BASE || !name || capacity <= 0)
        return 0;
    index = format - COMPAT_CLIPBOARD_FORMAT_BASE;
    if (index >= COMPAT_CLIPBOARD_FORMAT_MAX ||
        !compat_clipboard_formats[index].used) return 0;
    length = (uint32_t)kstrlen(compat_clipboard_formats[index].name);
    if (length >= (uint32_t)capacity) length = (uint32_t)capacity - 1U;
    kmemcpy(name, compat_clipboard_formats[index].name, length);
    name[length] = '\0';
    return (int)length;
}

static void *WIN32_API compat_GetWindowDC(void *hwnd) {
    get_dc_t get_dc = (get_dc_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "GetDC");
    return get_dc ? get_dc(hwnd) : NULL;
}

static int WIN32_API compat_ExcludeUpdateRgn(void *dc UNUSED,
                                              void *hwnd UNUSED) {
    return SIMPLEREGION;
}

static void *WIN32_API compat_CreateICA(const char *driver,
                                         const char *device,
                                         const char *output,
                                         const void *initialization) {
    create_dc_a_t create = (create_dc_a_t)(uintptr_t)
        pe_win32_resolve_export("GDI32.DLL", "CreateDCA");
    return create ? create(driver, device, output, initialization) : NULL;
}

static int WIN32_API compat_Escape(void *dc UNUSED, int escape UNUSED,
                                    int input_size UNUSED,
                                    const char *input UNUSED,
                                    void *output UNUSED) {
    return 0;
}

static void *WIN32_API compat_CreatePatternBrush(void *bitmap UNUSED) {
    create_solid_brush_t create = (create_solid_brush_t)(uintptr_t)
        pe_win32_resolve_export("GDI32.DLL", "CreateSolidBrush");
    return create ? create(0x00C0C0C0U) : NULL;
}

static void *WIN32_API compat_CreatePalette(const void *palette UNUSED) {
    get_stock_object_t get_stock = (get_stock_object_t)(uintptr_t)
        pe_win32_resolve_export("GDI32.DLL", "GetStockObject");
    return get_stock ? get_stock(DEFAULT_PALETTE) : NULL;
}

static void *WIN32_API compat_SelectPalette(void *dc UNUSED, void *palette,
                                             int background UNUSED) {
    void *old = compat_selected_palette;
    compat_selected_palette = palette;
    return old;
}

static uint32_t WIN32_API compat_RealizePalette(void *dc UNUSED) {
    return 0U;
}

static int WIN32_API compat_IntersectClipRect(
    void *dc UNUSED, int left, int top, int right, int bottom) {
    return right > left && bottom > top ? SIMPLEREGION : NULLREGION;
}

static uint32_t WIN32_API compat_SHGetMalloc(void **out) {
    co_get_malloc_t get_malloc;
    if (!out) return 0x80004003U; /* E_POINTER */
    *out = NULL;
    get_malloc = (co_get_malloc_t)(uintptr_t)
        pe_win32_resolve_export("OLE32.DLL", "CoGetMalloc");
    return get_malloc ? get_malloc(MEMCTX_TASK, out) : 0x80004001U;
}

static uint32_t WIN32_API compat_DoDragDrop(
    void *data_object UNUSED, void *drop_source UNUSED,
    uint32_t allowed_effects UNUSED, uint32_t *effect) {
    if (effect) *effect = 0U;
    return DRAGDROP_S_CANCEL;
}

static uint32_t WIN32_API compat_ImageList_SetBkColor(void *handle,
                                                       uint32_t color) {
    compat_imagelist_color_t *free_slot = NULL;
    if (!handle) return CLR_NONE;
    for (uint32_t i = 0; i < COMPAT_IMAGELIST_COLORS; i++) {
        compat_imagelist_color_t *slot = &compat_imagelist_colors[i];
        if (slot->used && slot->handle == handle) {
            uint32_t old = slot->color;
            slot->color = color;
            return old;
        }
        if (!slot->used && !free_slot) free_slot = slot;
    }
    if (!free_slot) return CLR_NONE;
    free_slot->used = true;
    free_slot->handle = handle;
    free_slot->color = color;
    return CLR_NONE;
}

uint32_t win32_winzip70_compat_resolve(const char *dll, const char *name) {
#define COMPAT(dll_name, api) \
    if (compat_equal_ci(dll, dll_name) && compat_equal_ci(name, #api)) \
        return (uint32_t)(uintptr_t)&compat_##api

    COMPAT("KERNEL32.DLL", HeapSize);
    COMPAT("KERNEL32.DLL", PeekNamedPipe);

    COMPAT("USER32.DLL", FindWindowExA);
    COMPAT("USER32.DLL", WaitForInputIdle);
    COMPAT("USER32.DLL", InsertMenuA);
    COMPAT("USER32.DLL", SetWindowsHookA);
    COMPAT("USER32.DLL", DrawTextA);
    COMPAT("USER32.DLL", SetMenuDefaultItem);
    COMPAT("USER32.DLL", InvalidateRgn);
    COMPAT("USER32.DLL", TrackPopupMenu);
    COMPAT("USER32.DLL", DestroyCursor);
    COMPAT("USER32.DLL", GetClipboardFormatNameA);
    COMPAT("USER32.DLL", RegisterClipboardFormatA);
    COMPAT("USER32.DLL", UnhookWindowsHookEx);
    COMPAT("USER32.DLL", GetWindowDC);
    COMPAT("USER32.DLL", ExcludeUpdateRgn);
    COMPAT("USER32.DLL", CallNextHookEx);
    COMPAT("USER32.DLL", SetWindowsHookExA);

    COMPAT("GDI32.DLL", CreateICA);
    COMPAT("GDI32.DLL", Escape);
    COMPAT("GDI32.DLL", CreatePatternBrush);
    COMPAT("GDI32.DLL", RealizePalette);
    COMPAT("GDI32.DLL", SelectPalette);
    COMPAT("GDI32.DLL", CreatePalette);
    COMPAT("GDI32.DLL", IntersectClipRect);

    COMPAT("SHELL32.DLL", SHGetMalloc);
    COMPAT("OLE32.DLL", DoDragDrop);
    COMPAT("COMCTL32.DLL", ImageList_SetBkColor);
#undef COMPAT
    return 0U;
}
