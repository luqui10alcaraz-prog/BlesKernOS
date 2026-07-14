#include "win32.h"
#include "resources.h"
#include "../include/pe_loader.h"
#include "../include/types.h"

#define WS_CHILD   0x40000000U
#define WS_VISIBLE 0x10000000U
#define CCS_TOP    0x00000001U
#define TBSTYLE_TOOLTIPS 0x0100U
#define WM_INITDIALOG 0x0110U
#define WM_COMMAND 0x0111U
#define WM_NOTIFY 0x004EU
#define WIN32_IDOK 1U
#define WIN32_IDCANCEL 2U
#define PSH_PROPSHEETPAGE 0x00000008U
#define PSN_APPLY (-202)
#define PSN_RESET (-203)
#define PSNRET_INVALID 1

#define COMCTL32_ORD_CREATE_MAPPED_BITMAP 8U
#define COMCTL32_ORD_INIT_COMMON_CONTROLS 17U

typedef void * (WIN32_API *create_window_ex_a_t)(uint32_t,const char*,const char*,
    uint32_t,int,int,int,int,void*,void*,void*,void*);
typedef void * (WIN32_API *load_bitmap_a_t)(void*,const char*);
typedef int32_t (WIN32_API *dialog_box_param_a_t)(void*,const char*,void*,void*,int32_t);
typedef void * (WIN32_API *create_window_a_t)(uint32_t,const char*,const char*,uint32_t,
    int,int,int,int,void*,void*,void*,void*);
typedef int (WIN32_API *end_dialog_t)(void*,int);
typedef int (WIN32_API *set_window_pos_t)(void*,void*,int,int,int,int,uint32_t);
typedef int32_t (WIN32_API *page_proc_t)(void*,uint32_t,uint32_t,int32_t);

typedef struct {
    uint32_t dwSize, dwFlags;
    void *hInstance;
    const char *pszTemplate;
    void *hIcon;
    const char *pszTitle;
    page_proc_t pfnDlgProc;
    int32_t lParam;
    void *pfnCallback;
    uint32_t *pcRefParent;
} prop_sheet_page_a_t;

typedef struct {
    uint32_t dwSize, dwFlags;
    void *hwndParent, *hInstance, *hIcon;
    const char *pszCaption;
    uint32_t nPages, nStartPage;
    const void *pages;
    void *pfnCallback;
} prop_sheet_header_a_t;

typedef struct { void *hwndFrom; uint32_t idFrom; int32_t code; } notify_header_t;
typedef struct { const prop_sheet_page_a_t *page; } prop_sheet_state_t;
static prop_sheet_state_t active_sheet;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void WIN32_API comctl_InitCommonControls(void) {
    /* BlesKernOS registers its built-in controls eagerly. */
}

static void *WIN32_API comctl_CreateMappedBitmap(void *instance, uint32_t id,
                                                  uint32_t flags UNUSED,
                                                  const void *map UNUSED,
                                                  int map_count UNUSED) {
    load_bitmap_a_t load_bitmap = (load_bitmap_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "LoadBitmapA");
    if (!load_bitmap) return NULL;
    return load_bitmap(instance, (const char *)(uintptr_t)id);
}

static void *WIN32_API comctl_CreateToolbarEx(void *parent, uint32_t style,
                                               uint32_t id, int bitmap_count UNUSED,
                                               void *bitmap_instance UNUSED,
                                               uint32_t bitmap_id UNUSED,
                                               const void *buttons UNUSED,
                                               int button_count UNUSED,
                                               int button_width,
                                               int button_height,
                                               int bitmap_width UNUSED,
                                               int bitmap_height UNUSED,
                                               uint32_t struct_size UNUSED) {
    create_window_ex_a_t create_window = (create_window_ex_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "CreateWindowExA");
    int width = button_width > 0 ? button_width * (button_count > 0 ? button_count : 1) : 240;
    int height = button_height > 0 ? button_height + 6 : 26;
    void *toolbar;
    if (!create_window || !parent) return NULL;
    toolbar = create_window(0U, "ToolbarWindow32", "",
                         style | WS_CHILD | WS_VISIBLE | CCS_TOP | TBSTYLE_TOOLTIPS,
                         0, 0, width, height, parent,
                         (void *)(uintptr_t)id, bitmap_instance, NULL);
    if (toolbar)
        win32_toolbar_configure(toolbar, buttons,
            button_count > 0 ? (uint32_t)button_count : 0U,
            button_width, button_height);
    return toolbar;
}

static int32_t WIN32_API property_page_proxy(void *hwnd, uint32_t message,
                                               uint32_t wparam, int32_t lparam) {
    const prop_sheet_page_a_t *page = active_sheet.page;
    page_proc_t page_proc = page ? page->pfnDlgProc : NULL;
    if (message == WM_INITDIALOG) {
        create_window_a_t create_window = (create_window_a_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "CreateWindowExA");
        set_window_pos_t set_position = (set_window_pos_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "SetWindowPos");
        int32_t result = page_proc ? page_proc(hwnd, message, wparam,
            (int32_t)(uintptr_t)page) : 1;
        if (set_position) (void)set_position(hwnd, NULL, 0, 0, 470, 390, 0x0002U | 0x0004U);
        if (create_window) {
            (void)create_window(0U, "BUTTON", "OK", WS_CHILD | WS_VISIBLE,
                280, 326, 78, 26, hwnd, (void *)(uintptr_t)WIN32_IDOK,
                page ? page->hInstance : NULL, NULL);
            (void)create_window(0U, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                368, 326, 78, 26, hwnd, (void *)(uintptr_t)WIN32_IDCANCEL,
                page ? page->hInstance : NULL, NULL);
        }
        return result;
    }
    if (message == WM_COMMAND && ((wparam & 0xFFFFU) == WIN32_IDOK ||
                                  (wparam & 0xFFFFU) == WIN32_IDCANCEL)) {
        end_dialog_t end_dialog = (end_dialog_t)(uintptr_t)
            pe_win32_resolve_export("USER32.DLL", "EndDialog");
        uint32_t id = wparam & 0xFFFFU;
        if (page_proc) {
            notify_header_t notify = { hwnd, 0U,
                id == WIN32_IDOK ? PSN_APPLY : PSN_RESET };
            int32_t result = page_proc(hwnd, WM_NOTIFY, 0U,
                                      (int32_t)(uintptr_t)&notify);
            if (id == WIN32_IDOK && result == PSNRET_INVALID) return 1;
        }
        if (end_dialog) (void)end_dialog(hwnd, (int)id);
        return 1;
    }
    return page_proc ? page_proc(hwnd, message, wparam, lparam) : 0;
}

static int WIN32_API comctl_PropertySheetA(const void *raw_header) {
    const prop_sheet_header_a_t *header = (const prop_sheet_header_a_t *)raw_header;
    const prop_sheet_page_a_t *page;
    dialog_box_param_a_t dialog_box;
    uint32_t page_index, page_size;
    int32_t result;
    if (!header || header->dwSize < 40U || !header->nPages || !header->pages ||
        !(header->dwFlags & PSH_PROPSHEETPAGE)) return -1;
    page = (const prop_sheet_page_a_t *)header->pages;
    page_size = page->dwSize;
    if (page_size < 40U) return -1;
    page_index = header->nStartPage < header->nPages ? header->nStartPage : 0U;
    page = (const prop_sheet_page_a_t *)((const uint8_t *)header->pages +
                                        page_index * page_size);
    if (!page->pfnDlgProc || !page->pszTemplate) return -1;
    dialog_box = (dialog_box_param_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "DialogBoxParamA");
    if (!dialog_box) return -1;
    active_sheet.page = page;
    result = dialog_box(page->hInstance ? page->hInstance : header->hInstance,
                        page->pszTemplate, header->hwndParent,
                        (void *)(uintptr_t)&property_page_proxy,
                        (int32_t)(uintptr_t)page);
    active_sheet.page = NULL;
    return result == (int32_t)WIN32_IDOK ? 1 : 0;
}

uint32_t win32_comctl32_resolve(const char *name) {
#define C(api) if (equal(name, #api)) return (uint32_t)(uintptr_t)&comctl_##api
    C(InitCommonControls);
    C(CreateMappedBitmap);
    C(CreateToolbarEx);
    C(PropertySheetA);
#undef C
    return 0;
}

uint32_t win32_comctl32_resolve_ordinal(uint16_t ordinal) {
    if (ordinal == COMCTL32_ORD_CREATE_MAPPED_BITMAP)
        return (uint32_t)(uintptr_t)&comctl_CreateMappedBitmap;
    if (ordinal == COMCTL32_ORD_INIT_COMMON_CONTROLS)
        return (uint32_t)(uintptr_t)&comctl_InitCommonControls;
    return 0;
}
