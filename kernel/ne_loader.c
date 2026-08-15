/*
 * BlesKernOS Win16/NE compatibility layer.
 *
 * The relay design follows Wine's Win16 architecture: imported KERNEL, USER
 * and GDI calls are redirected through generated 16-bit stubs, translated to
 * a native kernel dispatcher, and returned with the callee-cleanup size from
 * the Win16 export signature.  This is an adaptation for BlesKernOS, not a
 * verbatim copy of Wine source.  Wine's LGPL notice is kept in
 * third_party/wine/COPYING.LIB and the implementation notes live in
 * docs/WIN16_PORT.md.
 */
#include "include/ne_loader.h"
#include "include/gdt.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/syscall.h"
#include "include/pit.h"
#include "include/compat_mode.h"
#include "win32/win32.h"
#include "string.h"
#include "stdio.h"

#define NE_MAX_SEGMENTS       64U
#define NE_MAX_MODULES        32U
#define NE_MAX_RELAYS         512U
#define NE_RELAY_STUB_SIZE    20U
#define NE_RELAY_HEADER_SIZE  16U
#define NE_MAX_LOCAL_BLOCKS   64U
#define NE_MAX_GLOBAL_BLOCKS  48U
#define NE_MAX_ALIAS_SELECTORS 24U
#define NE_PROCESS_SLOTS      TASK_MAX
#define NE_MAX_STRING         512U

#define NE_SEG_DATA           0x0001U
#define NE_SEG_RELOC          0x0100U
#define NE_FLAG_LIBRARY       0x8000U
#define NE_RELOC_ADDITIVE     0x04U
#define NE_TARGET_INTERNAL    0U
#define NE_TARGET_IMPORT_ORD  1U
#define NE_TARGET_IMPORT_NAME 2U
#define NE_TARGET_OS_FIXUP    3U
#define NE_LMEM_ZEROINIT      0x0040U
#define NE_GMEM_ZEROINIT      0x0040U

typedef struct {
    uint16_t offset, length, flags, min_alloc;
} PACKED ne_segment_disk_t;

typedef struct {
    uint8_t source_type, flags;
    uint16_t source_offset, target1, target2;
} PACKED ne_reloc_t;

typedef struct {
    uint8_t *memory;
    uint32_t size;
    uint32_t stored_size;
    uint16_t selector, flags;
} ne_segment_t;

typedef struct {
    uint32_t header_offset;
    uint16_t flags;
    uint16_t auto_data, heap_size, stack_size;
    uint16_t segment_count, module_count, align_shift;
    uint16_t entry_segment, entry_offset, stack_segment, stack_offset;
    uint32_t segment_table, module_table, import_table;
} ne_header_view_t;

typedef enum {
    NE_API_UNSUPPORTED = 0,
    NE_API_ZERO,
    NE_API_TRUE,
    NE_API_EXIT,
    NE_API_GET_VERSION,
    NE_API_INIT_TASK,
    NE_API_LOCAL_ALLOC,
    NE_API_LOCAL_REALLOC,
    NE_API_LOCAL_FREE,
    NE_API_LOCAL_LOCK,
    NE_API_LOCAL_UNLOCK,
    NE_API_LOCAL_SIZE,
    NE_API_GLOBAL_ALLOC,
    NE_API_GLOBAL_REALLOC,
    NE_API_GLOBAL_FREE,
    NE_API_GLOBAL_LOCK,
    NE_API_GLOBAL_UNLOCK,
    NE_API_GLOBAL_SIZE,
    NE_API_YIELD,
    NE_API_GET_CURRENT_TASK,
    NE_API_GET_MODULE_HANDLE,
    NE_API_GET_MODULE_USAGE,
    NE_API_MAKE_PROC_INSTANCE,
    NE_API_GET_WIN_FLAGS,
    NE_API_LSTRCPY,
    NE_API_LSTRCAT,
    NE_API_LSTRLEN,
    NE_API_LSTRCMP,
    NE_API_LSTRCMPI,
    NE_API_OUTPUT_DEBUG_STRING,
    NE_API_SET_LAST_ERROR,
    NE_API_GET_LAST_ERROR,
    NE_API_GET_VERSION_EX,
    NE_API_GET_NUM_TASKS,
    NE_API_GET_TASK_DS,
    NE_API_GET_TICKS,
    NE_API_MESSAGE_BOX,
    NE_API_POST_QUIT,
    NE_API_GET_EXP_WIN_VER,
    NE_API_GET_SELECTOR_LIMIT,
    NE_API_ALLOC_SELECTOR,
    NE_API_ALLOC_DS_TO_CS,
    NE_API_ALLOC_CS_TO_DS,
    NE_API_FREE_SELECTOR,
    NE_API_LOAD_LIBRARY,
    NE_API_FREE_LIBRARY
} ne_api_handler_t;

typedef struct {
    const char *module;
    uint16_t ordinal;
    const char *name;
    ne_api_handler_t handler;
} ne_export_desc_t;

typedef struct {
    const char *module;
    uint16_t ordinal;
    const char *name;
    uint16_t argument_bytes;
} ne_export_signature_t;

static const ne_export_signature_t ne_wine_signatures[] = {
#include "../third_party/wine/win16_exports.inc"
};

typedef struct {
    char module[12];
    char name[40];
    uint16_t ordinal;
    uint16_t argument_bytes;
    uint16_t offset;
    ne_api_handler_t handler;
    bool warned;
} ne_relay_t;

typedef struct {
    bool used;
    uint16_t offset;
    uint16_t size;
} ne_local_block_t;

typedef struct {
    bool used;
    uint16_t selector;
    uint8_t *memory;
    uint32_t size;
    uint16_t flags;
} ne_global_block_t;

typedef struct {
    uint32_t process_id;
    char path[128];
    ne_header_view_t header;
    ne_segment_t segments[NE_MAX_SEGMENTS];
    uint16_t data_selector;
    uint16_t stack_selector;
    uint16_t entry_selector;
    uint16_t initial_sp;
    uint16_t local_heap_start;
    uint16_t local_heap_end;
    uint16_t local_heap_next;
    ne_local_block_t local_blocks[NE_MAX_LOCAL_BLOCKS];
    ne_global_block_t global_blocks[NE_MAX_GLOBAL_BLOCKS];
    uint16_t alias_selectors[NE_MAX_ALIAS_SELECTORS];
    uint8_t *relay_memory;
    uint32_t relay_capacity;
    uint16_t relay_selector;
    uint16_t relay_count;
    ne_relay_t relays[NE_MAX_RELAYS];
    uint32_t last_error;
} ne_process_t;

/*
 * Argument byte counts are derived from Wine's .spec descriptions.  Entries
 * not implemented yet still get the correct RETF n relay, so an application
 * can load and continue when it merely imports (but does not depend on) them.
 */
static const ne_export_desc_t ne_exports[] = {
    {"KERNEL",   2, "ExitKernel", NE_API_EXIT},
    {"KERNEL",   3, "GetVersion", NE_API_GET_VERSION},
    {"KERNEL",   4, "LocalInit", NE_API_TRUE},
    {"KERNEL",   5, "LocalAlloc", NE_API_LOCAL_ALLOC},
    {"KERNEL",   6, "LocalReAlloc", NE_API_LOCAL_REALLOC},
    {"KERNEL",   7, "LocalFree", NE_API_LOCAL_FREE},
    {"KERNEL",   8, "LocalLock", NE_API_LOCAL_LOCK},
    {"KERNEL",   9, "LocalUnlock", NE_API_LOCAL_UNLOCK},
    {"KERNEL",  10, "LocalSize", NE_API_LOCAL_SIZE},
    {"KERNEL",  15, "GlobalAlloc", NE_API_GLOBAL_ALLOC},
    {"KERNEL",  16, "GlobalReAlloc", NE_API_GLOBAL_REALLOC},
    {"KERNEL",  17, "GlobalFree", NE_API_GLOBAL_FREE},
    {"KERNEL",  18, "GlobalLock", NE_API_GLOBAL_LOCK},
    {"KERNEL",  19, "GlobalUnlock", NE_API_GLOBAL_UNLOCK},
    {"KERNEL",  20, "GlobalSize", NE_API_GLOBAL_SIZE},
    {"KERNEL",  23, "LockSegment", NE_API_TRUE},
    {"KERNEL",  24, "UnlockSegment", NE_API_TRUE},
    {"KERNEL",  29, "Yield", NE_API_YIELD},
    {"KERNEL",  30, "WaitEvent", NE_API_YIELD},
    {"KERNEL",  36, "GetCurrentTask", NE_API_GET_CURRENT_TASK},
    {"KERNEL",  47, "GetModuleHandle", NE_API_GET_MODULE_HANDLE},
    {"KERNEL",  48, "GetModuleUsage", NE_API_GET_MODULE_USAGE},
    {"KERNEL",  50, "GetProcAddress", NE_API_ZERO},
    {"KERNEL",  51, "MakeProcInstance", NE_API_MAKE_PROC_INSTANCE},
    {"KERNEL",  52, "FreeProcInstance", NE_API_TRUE},
    {"KERNEL",  74, "OpenFile", NE_API_ZERO},
    {"KERNEL",  81, "_lclose", NE_API_ZERO},
    {"KERNEL",  82, "_lread", NE_API_ZERO},
    {"KERNEL",  83, "_lcreat", NE_API_ZERO},
    {"KERNEL",  84, "_llseek", NE_API_ZERO},
    {"KERNEL",  85, "_lopen", NE_API_ZERO},
    {"KERNEL",  86, "_lwrite", NE_API_ZERO},
    {"KERNEL",  88, "lstrcpy", NE_API_LSTRCPY},
    {"KERNEL",  89, "lstrcat", NE_API_LSTRCAT},
    {"KERNEL",  90, "lstrlen", NE_API_LSTRLEN},
    {"KERNEL",  91, "InitTask", NE_API_INIT_TASK},
    {"KERNEL",  92, "GetTempDrive", NE_API_ZERO},
    {"KERNEL", 102, "DOS3Call", NE_API_ZERO},
    {"KERNEL",  95, "LoadLibrary", NE_API_LOAD_LIBRARY},
    {"KERNEL",  96, "FreeLibrary", NE_API_FREE_LIBRARY},
    {"KERNEL", 199, "SetHandleCount", NE_API_TRUE},
    {"KERNEL", 107, "SetErrorMode", NE_API_ZERO},
    {"KERNEL", 132, "GetWinFlags", NE_API_GET_WIN_FLAGS},
    {"KERNEL", 115, "OutputDebugString", NE_API_OUTPUT_DEBUG_STRING},
    {"KERNEL", 147, "SetLastError", NE_API_SET_LAST_ERROR},
    {"KERNEL", 148, "GetLastError", NE_API_GET_LAST_ERROR},
    {"KERNEL", 149, "GetVersionEx", NE_API_GET_VERSION_EX},
    {"KERNEL", 150, "DirectedYield", NE_API_YIELD},
    {"KERNEL", 152, "GetNumTasks", NE_API_GET_NUM_TASKS},
    {"KERNEL", 155, "GetTaskDS", NE_API_GET_TASK_DS},
    {"KERNEL", 161, "LocalCountFree", NE_API_ZERO},
    {"KERNEL", 162, "LocalHeapSize", NE_API_ZERO},
    {"KERNEL", 166, "WinExec", NE_API_ZERO},
    {"KERNEL", 167, "GetExpWinVer", NE_API_GET_EXP_WIN_VER},
    {"KERNEL", 175, "AllocSelector", NE_API_ALLOC_SELECTOR},
    {"KERNEL", 176, "FreeSelector", NE_API_FREE_SELECTOR},
    {"KERNEL", 171, "AllocDStoCSAlias", NE_API_ALLOC_DS_TO_CS},
    {"KERNEL", 170, "AllocCStoDSAlias", NE_API_ALLOC_CS_TO_DS},
    {"KERNEL", 172, "AllocAlias", NE_API_ALLOC_CS_TO_DS},
    {"KERNEL", 188, "GetSelectorLimit", NE_API_GET_SELECTOR_LIMIT},

    {"USER",     1, "MessageBox", NE_API_MESSAGE_BOX},
    {"USER",     5, "InitApp", NE_API_TRUE},
    {"USER",     6, "PostQuitMessage", NE_API_POST_QUIT},
    {"USER",    10, "SetTimer", NE_API_ZERO},
    {"USER",    12, "KillTimer", NE_API_TRUE},
    {"USER",    13, "GetTickCount", NE_API_GET_TICKS},
    {"USER",    15, "GetCurrentTime", NE_API_GET_TICKS},
    {"USER",    21, "GetDoubleClickTime", NE_API_ZERO},
    {"USER",    23, "GetFocus", NE_API_ZERO},
    {"USER",    17, "GetCursorPos", NE_API_ZERO},
    {"USER",    31, "IsIconic", NE_API_ZERO},
    {"USER",    34, "EnableWindow", NE_API_TRUE},
    {"USER",    35, "IsWindowEnabled", NE_API_TRUE},
    {"USER",    41, "CreateWindow", NE_API_ZERO},
    {"USER",    42, "ShowWindow", NE_API_TRUE},
    {"USER",    47, "IsWindow", NE_API_ZERO},
    {"USER",    49, "IsWindowVisible", NE_API_ZERO},
    {"USER",    53, "DestroyWindow", NE_API_TRUE},
    {"USER",    57, "RegisterClass", NE_API_ZERO},
    {"USER",    60, "GetActiveWindow", NE_API_ZERO},
    {"USER",    59, "SetActiveWindow", NE_API_ZERO},
    {"USER",    66, "GetDC", NE_API_ZERO},
    {"USER",    68, "ReleaseDC", NE_API_TRUE},
    {"USER",    69, "SetCursor", NE_API_ZERO},
    {"USER",    70, "SetCursorPos", NE_API_TRUE},
    {"USER",    88, "EndDialog", NE_API_TRUE},
    {"USER",    92, "SetDlgItemText", NE_API_TRUE},
    {"USER",    19, "ReleaseCapture", NE_API_TRUE},
    {"USER",    18, "SetCapture", NE_API_ZERO},
    {"USER",   104, "MessageBeep", NE_API_TRUE},
    {"USER",   107, "DefWindowProc", NE_API_ZERO},
    {"USER",   108, "GetMessage", NE_API_ZERO},
    {"USER",   113, "TranslateMessage", NE_API_TRUE},
    {"USER",   114, "DispatchMessage", NE_API_ZERO},
    {"USER",   118, "RegisterWindowMessage", NE_API_ZERO},
    {"USER",   124, "UpdateWindow", NE_API_TRUE},
    {"USER",   125, "InvalidateRect", NE_API_TRUE},
    {"USER",   126, "InvalidateRgn", NE_API_TRUE},
    {"USER",   127, "ValidateRect", NE_API_TRUE},
    {"USER",   128, "ValidateRgn", NE_API_TRUE},
    {"USER",   129, "GetClassWord", NE_API_ZERO},
    {"USER",   130, "SetClassWord", NE_API_ZERO},
    {"USER",   131, "GetClassLong", NE_API_ZERO},
    {"USER",   132, "SetClassLong", NE_API_ZERO},
    {"USER",   133, "GetWindowWord", NE_API_ZERO},
    {"USER",   134, "SetWindowWord", NE_API_ZERO},
    {"USER",   154, "CheckMenuItem", NE_API_ZERO},
    {"USER",   155, "EnableMenuItem", NE_API_ZERO},
    {"USER",   156, "GetSystemMenu", NE_API_ZERO},
    {"USER",   157, "GetMenu", NE_API_ZERO},
    {"USER",   158, "SetMenu", NE_API_TRUE},
    {"USER",   159, "GetSubMenu", NE_API_ZERO},
    {"USER",   160, "DrawMenuBar", NE_API_TRUE},
    {"USER",   169, "GetCaretBlinkTime", NE_API_ZERO},
    {"USER",   176, "LoadString", NE_API_ZERO},
    {"USER",   239, "DialogBoxParam", NE_API_ZERO},
    {"USER",   286, "GetDesktopWindow", NE_API_ZERO},
    {"USER",   430, "lstrcmp", NE_API_LSTRCMP},
    {"USER",   471, "lstrcmpi", NE_API_LSTRCMPI},

    {"GDI",     45, "SelectObject", NE_API_ZERO},
    {"GDI",     52, "CreateCompatibleDC", NE_API_ZERO},
    {"GDI",     61, "CreatePen", NE_API_ZERO},
    {"GDI",     66, "CreateSolidBrush", NE_API_ZERO},
    {"GDI",     68, "DeleteDC", NE_API_TRUE},
    {"GDI",     69, "DeleteObject", NE_API_TRUE},
    {"GDI",     87, "GetStockObject", NE_API_ZERO},
    {"GDI",      1, "SetBkColor", NE_API_ZERO},
    {"GDI",      2, "SetBkMode", NE_API_ZERO},
    {"GDI",      3, "SetMapMode", NE_API_ZERO},
    {"GDI",      4, "SetROP2", NE_API_ZERO},
    {"GDI",      9, "SetTextColor", NE_API_ZERO},
    {"GDI",    346, "SetTextAlign", NE_API_ZERO}
};

static const char *ne_error = "sin error";
static ne_process_t *ne_processes[NE_PROCESS_SLOTS];

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void write32(uint8_t *p, uint32_t value) {
    write16(p, (uint16_t)value);
    write16(p + 2U, (uint16_t)(value >> 16U));
}

static bool range_ok(uint32_t offset, uint32_t length, uint32_t size) {
    return offset <= size && length <= size - offset;
}

static char ne_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - 'a' + 'A');
    return value;
}

static int16_t ne_string_compare(const char *left, const char *right,
                                 bool ignore_case) {
    unsigned char a, b;
    if (!left) left = "";
    if (!right) right = "";
    while (*left || *right) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (ignore_case) {
            a = (unsigned char)ne_upper((char)a);
            b = (unsigned char)ne_upper((char)b);
        }
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

static bool ne_equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (ne_upper(*a++) != ne_upper(*b++)) return false;
    }
    return *a == '\0' && *b == '\0';
}

static void ne_copy_upper(char *out, uint32_t capacity, const char *source) {
    uint32_t i = 0U;
    if (!out || !capacity) return;
    while (source && source[i] && i + 1U < capacity) {
        out[i] = ne_upper(source[i]);
        i++;
    }
    out[i] = '\0';
}

static void ne_normalize_module(char *out, uint32_t capacity,
                                const char *source) {
    uint32_t i = 0U;
    const char *name = source;
    if (!out || !capacity) return;
    if (!name) name = "";
    for (const char *scan = name; *scan; scan++)
        if (*scan == '/' || *scan == '\\') name = scan + 1;
    while (name[i] && name[i] != '.' && i + 1U < capacity) {
        out[i] = ne_upper(name[i]);
        i++;
    }
    out[i] = '\0';
    if (ne_equal_ci(out, "KRNL386") || ne_equal_ci(out, "KERNEL16"))
        kstrcpy(out, "KERNEL");
    else if (ne_equal_ci(out, "USER16")) kstrcpy(out, "USER");
    else if (ne_equal_ci(out, "GDI16")) kstrcpy(out, "GDI");
}

static const char *ne_path_name(const char *path) {
    const char *name = path ? path : "win16";
    for (const char *scan = name; *scan; scan++)
        if (*scan == '/' || *scan == '\\') name = scan + 1;
    return name;
}

bool ne_is_image(const uint8_t *file, uint32_t size) {
    uint32_t offset;
    if (!file || size < 0x40U || read16(file) != 0x5A4DU) return false;
    offset = read32(file + 0x3CU);
    return range_ok(offset, 2U, size) && read16(file + offset) == 0x454EU;
}

static bool parse_header(const uint8_t *file, uint32_t size,
                         ne_header_view_t *out) {
    uint32_t ne;
    if (!out || !ne_is_image(file, size)) {
        ne_error = "firma NE invalida";
        return false;
    }
    ne = read32(file + 0x3CU);
    if (!range_ok(ne, 0x40U, size)) {
        ne_error = "cabecera NE truncada";
        return false;
    }
    kmemset(out, 0, sizeof(*out));
    out->header_offset = ne;
    out->flags = read16(file + ne + 0x0CU);
    out->auto_data = read16(file + ne + 0x0EU);
    out->heap_size = read16(file + ne + 0x10U);
    out->stack_size = read16(file + ne + 0x12U);
    out->entry_offset = read16(file + ne + 0x14U);
    out->entry_segment = read16(file + ne + 0x16U);
    out->stack_offset = read16(file + ne + 0x18U);
    out->stack_segment = read16(file + ne + 0x1AU);
    out->segment_count = read16(file + ne + 0x1CU);
    out->module_count = read16(file + ne + 0x1EU);
    out->segment_table = ne + read16(file + ne + 0x22U);
    out->module_table = ne + read16(file + ne + 0x28U);
    out->import_table = ne + read16(file + ne + 0x2AU);
    out->align_shift = read16(file + ne + 0x32U);
    if (!out->segment_count || out->segment_count > NE_MAX_SEGMENTS) {
        ne_error = "cantidad de segmentos NE invalida";
        return false;
    }
    if (out->module_count > NE_MAX_MODULES) {
        ne_error = "demasiados modulos importados por NE";
        return false;
    }
    if (out->align_shift > 15U ||
        !range_ok(out->segment_table,
                  out->segment_count * sizeof(ne_segment_disk_t), size) ||
        !range_ok(out->module_table, out->module_count * 2U, size)) {
        ne_error = "tablas NE fuera del archivo";
        return false;
    }
    if (!out->entry_segment || out->entry_segment > out->segment_count) {
        ne_error = "entrypoint NE invalido";
        return false;
    }
    if (out->auto_data > out->segment_count ||
        out->stack_segment > out->segment_count) {
        ne_error = "segmento de datos/stack NE invalido";
        return false;
    }
    return true;
}

static ne_process_t *ne_find_process(uint32_t process_id) {
    for (uint32_t i = 0U; i < NE_PROCESS_SLOTS; i++)
        if (ne_processes[i] && ne_processes[i]->process_id == process_id)
            return ne_processes[i];
    return NULL;
}

static bool ne_register_process(ne_process_t *process) {
    for (uint32_t i = 0U; i < NE_PROCESS_SLOTS; i++) {
        if (ne_processes[i]) continue;
        ne_processes[i] = process;
        return true;
    }
    return false;
}

static void ne_unregister_process(ne_process_t *process) {
    for (uint32_t i = 0U; i < NE_PROCESS_SLOTS; i++)
        if (ne_processes[i] == process) ne_processes[i] = NULL;
}

static void ne_destroy_process(ne_process_t *process) {
    if (!process) return;
    ne_unregister_process(process);
    for (uint32_t i = 0U; i < NE_MAX_GLOBAL_BLOCKS; i++) {
        if (!process->global_blocks[i].used) continue;
        gdt_free_user16_segment(process->global_blocks[i].selector);
        kfree(process->global_blocks[i].memory);
    }
    for (uint32_t i = 0U; i < NE_MAX_ALIAS_SELECTORS; i++)
        if (process->alias_selectors[i])
            gdt_free_user16_segment(process->alias_selectors[i]);
    if (process->relay_selector)
        gdt_free_user16_segment(process->relay_selector);
    if (process->relay_memory) kfree(process->relay_memory);
    for (uint16_t i = 0U; i < process->header.segment_count; i++) {
        if (process->segments[i].selector)
            gdt_free_user16_segment(process->segments[i].selector);
        if (process->segments[i].memory)
            kfree(process->segments[i].memory);
    }
    kfree(process);
}

void ne_win16_cleanup_process(uint32_t process_id) {
    ne_process_t *process = ne_find_process(process_id);
    if (process) {
        kprintf("[WIN16] proceso %u liberado (%s)\n", process_id,
                process->path);
        ne_destroy_process(process);
    }
}

static const ne_export_desc_t *ne_find_export(const char *module,
                                               uint16_t ordinal,
                                               const char *name) {
    for (uint32_t i = 0U; i < sizeof(ne_exports) / sizeof(ne_exports[0]); i++) {
        const ne_export_desc_t *entry = &ne_exports[i];
        if (!ne_equal_ci(entry->module, module)) continue;
        if (ordinal && entry->ordinal == ordinal) return entry;
        if (!ordinal && name && entry->name && ne_equal_ci(entry->name, name))
            return entry;
    }
    return NULL;
}

static const ne_export_signature_t *ne_find_signature(const char *module,
                                                       uint16_t ordinal,
                                                       const char *name) {
    for (uint32_t i = 0U;
         i < sizeof(ne_wine_signatures) / sizeof(ne_wine_signatures[0]); i++) {
        const ne_export_signature_t *entry = &ne_wine_signatures[i];
        if (!ne_equal_ci(entry->module, module)) continue;
        if (ordinal && entry->ordinal == ordinal) return entry;
        if (!ordinal && name && entry->name && ne_equal_ci(entry->name, name))
            return entry;
    }
    return NULL;
}

static bool ne_import_equate(const char *module, uint16_t ordinal,
                             const char *name, uint16_t *value) {
    if (!value || !ne_equal_ci(module, "KERNEL")) return false;
    if (ordinal == 113U || (!ordinal && name && ne_equal_ci(name, "__AHSHIFT")))
        *value = 3U;
    else if (ordinal == 114U ||
             (!ordinal && name && ne_equal_ci(name, "__AHINCR")))
        *value = 8U;
    else if (ordinal == 178U ||
             (!ordinal && name && ne_equal_ci(name, "__WINFLAGS")))
        *value = 0x0413U;
    else if (ordinal == 173U || ordinal == 174U || ordinal == 179U ||
             ordinal == 181U || ordinal == 182U || ordinal == 183U ||
             ordinal == 190U || ordinal == 193U || ordinal == 194U ||
             ordinal == 195U)
        *value = 0U;
    else
        return false;
    return true;
}

static bool ne_init_relays(ne_process_t *process) {
    uint8_t *code;
    process->relay_capacity = NE_RELAY_HEADER_SIZE +
        NE_MAX_RELAYS * NE_RELAY_STUB_SIZE;
    process->relay_memory = (uint8_t *)kzalloc(process->relay_capacity);
    if (!process->relay_memory) {
        ne_error = "sin memoria para relays Win16";
        return false;
    }
    process->relay_selector = gdt_alloc_user16_segment(
        (uint32_t)(uintptr_t)process->relay_memory,
        process->relay_capacity - 1U, true, false);
    if (!process->relay_selector) {
        ne_error = "sin selector para relays Win16";
        return false;
    }
    /* Exit trampoline: mov eax,SYS_EXIT; xor ebx,ebx; int 80h; jmp $. */
    code = process->relay_memory;
    code[0] = 0x66U; code[1] = 0xB8U;
    write32(code + 2U, SYS_EXIT);
    code[6] = 0x66U; code[7] = 0x31U; code[8] = 0xDBU;
    code[9] = 0xCDU; code[10] = 0x80U;
    code[11] = 0xEBU; code[12] = 0xFEU;
    code[13] = code[14] = code[15] = 0x90U;
    return true;
}

static bool ne_get_import_module(const uint8_t *file, uint32_t size,
                                 const ne_header_view_t *header,
                                 uint16_t module_index, char *out,
                                 uint32_t capacity) {
    uint16_t name_offset;
    uint32_t at, length;
    char raw[64];
    if (!module_index || module_index > header->module_count ||
        !out || capacity < 2U) return false;
    name_offset = read16(file + header->module_table +
                         (module_index - 1U) * 2U);
    at = header->import_table + name_offset;
    if (!range_ok(at, 1U, size)) return false;
    length = file[at];
    if (!length || !range_ok(at + 1U, length, size)) return false;
    if (length >= sizeof(raw)) length = sizeof(raw) - 1U;
    kmemcpy(raw, file + at + 1U, length);
    raw[length] = '\0';
    ne_normalize_module(out, capacity, raw);
    return true;
}

static bool ne_get_import_name(const uint8_t *file, uint32_t size,
                               const ne_header_view_t *header,
                               uint16_t name_offset, char *out,
                               uint32_t capacity) {
    uint32_t at = header->import_table + name_offset;
    uint32_t length;
    if (!out || capacity < 2U || !range_ok(at, 1U, size)) return false;
    length = file[at];
    if (!length || !range_ok(at + 1U, length, size)) return false;
    if (length >= capacity) length = capacity - 1U;
    kmemcpy(out, file + at + 1U, length);
    out[length] = '\0';
    return true;
}

static bool ne_add_relay(ne_process_t *process, const char *module,
                         uint16_t ordinal, const char *name,
                         uint16_t *selector_out, uint16_t *offset_out) {
    const ne_export_desc_t *description;
    const ne_export_signature_t *signature;
    ne_relay_t *relay;
    uint8_t *stub;
    uint16_t offset;
    for (uint16_t i = 0U; i < process->relay_count; i++) {
        relay = &process->relays[i];
        if (!ne_equal_ci(relay->module, module)) continue;
        if ((ordinal && relay->ordinal == ordinal) ||
            (!ordinal && name && ne_equal_ci(relay->name, name))) {
            *selector_out = process->relay_selector;
            *offset_out = relay->offset;
            return true;
        }
    }
    if (process->relay_count >= NE_MAX_RELAYS) {
        ne_error = "demasiados imports Win16";
        return false;
    }
    description = ne_find_export(module, ordinal, name);
    signature = ne_find_signature(module, ordinal, name);
    relay = &process->relays[process->relay_count];
    kmemset(relay, 0, sizeof(*relay));
    ne_copy_upper(relay->module, sizeof(relay->module), module);
    relay->ordinal = ordinal;
    if (name) kstrncpy(relay->name, name, sizeof(relay->name) - 1U);
    relay->argument_bytes = signature ? signature->argument_bytes : 0U;
    relay->handler = description ? description->handler : NE_API_UNSUPPORTED;
    if (!relay->ordinal) {
        if (description) relay->ordinal = description->ordinal;
        else if (signature) relay->ordinal = signature->ordinal;
    }
    if (!relay->name[0]) {
        const char *resolved_name = description ? description->name :
                                    (signature ? signature->name : NULL);
        if (resolved_name)
            kstrncpy(relay->name, resolved_name,
                     sizeof(relay->name) - 1U);
    }
    offset = (uint16_t)(NE_RELAY_HEADER_SIZE +
        process->relay_count * NE_RELAY_STUB_SIZE);
    relay->offset = offset;
    stub = process->relay_memory + offset;
    /* 66 B8 imm32 / 66 BB imm32 / int 80h / retf imm16 / nop padding. */
    stub[0] = 0x66U; stub[1] = 0xB8U;
    write32(stub + 2U, SYS_WIN16_RELAY);
    stub[6] = 0x66U; stub[7] = 0xBBU;
    write32(stub + 8U, process->relay_count);
    stub[12] = 0xCDU; stub[13] = 0x80U;
    stub[14] = 0xCAU;
    write16(stub + 15U, relay->argument_bytes);
    stub[17] = stub[18] = stub[19] = 0x90U;
    process->relay_count++;
    *selector_out = process->relay_selector;
    *offset_out = offset;
    return true;
}

static bool ne_patch_target(uint8_t *where, uint8_t type, uint16_t selector,
                            uint16_t offset, bool additive) {
    switch (type & 0x0FU) {
        case 0:
            if (additive) *where = (uint8_t)(*where + offset);
            else *where = (uint8_t)offset;
            return true;
        case 2:
            if (additive) *(uint16_t *)(void *)where += selector;
            else *(uint16_t *)(void *)where = selector;
            return true;
        case 3:
            if (additive) {
                *(uint16_t *)(void *)where += offset;
                *(uint16_t *)(void *)(where + 2U) += selector;
            } else {
                *(uint16_t *)(void *)where = offset;
                *(uint16_t *)(void *)(where + 2U) = selector;
            }
            return true;
        case 5:
            if (additive) *(uint16_t *)(void *)where += offset;
            else *(uint16_t *)(void *)where = offset;
            return true;
        default:
            return false;
    }
}

static bool ne_apply_relocation_chain(ne_segment_t *source_segment,
                                      const ne_reloc_t *rel,
                                      uint16_t selector, uint16_t offset) {
    uint32_t width = ((rel->source_type & 0x0FU) == 3U) ? 4U :
        (((rel->source_type & 0x0FU) == 0U) ? 1U : 2U);
    uint16_t source = rel->source_offset;
    bool additive = (rel->flags & NE_RELOC_ADDITIVE) != 0U;
    for (uint32_t links = 0U; links < 65536U; links++) {
        uint16_t next = 0xFFFFU;
        if (!range_ok(source, width, source_segment->size) ||
            (!additive && !range_ok(source, 2U, source_segment->size))) {
            ne_error = "destino de relocacion NE invalido";
            return false;
        }
        if (!additive) next = read16(source_segment->memory + source);
        if (!ne_patch_target(source_segment->memory + source,
                             rel->source_type, selector, offset, additive)) {
            ne_error = "tipo de relocacion NE no soportado";
            return false;
        }
        if (additive || next == 0xFFFFU) return true;
        source = next;
    }
    ne_error = "cadena de relocaciones NE ciclica";
    return false;
}

static bool ne_load_segments(ne_process_t *process, const uint8_t *file,
                             uint32_t file_size, uint32_t *external_relocs) {
    const ne_header_view_t *header = &process->header;
    const ne_segment_disk_t *disk =
        (const ne_segment_disk_t *)(const void *)(file + header->segment_table);
    *external_relocs = 0U;
    for (uint16_t i = 0U; i < header->segment_count; i++) {
        uint32_t file_offset = (uint32_t)disk[i].offset << header->align_shift;
        uint32_t stored = disk[i].offset ?
            (disk[i].length ? disk[i].length : 65536U) : 0U;
        uint32_t allocated = disk[i].min_alloc ? disk[i].min_alloc : 65536U;
        if (allocated < stored) allocated = stored;
        if (allocated > 65536U ||
            (stored && !range_ok(file_offset, stored, file_size))) {
            ne_error = "segmento NE fuera del archivo";
            return false;
        }
        process->segments[i].memory = (uint8_t *)kzalloc(allocated);
        if (!process->segments[i].memory) {
            ne_error = "sin memoria para segmento NE";
            return false;
        }
        process->segments[i].size = allocated;
        process->segments[i].stored_size = stored;
        process->segments[i].flags = disk[i].flags;
        if (stored)
            kmemcpy(process->segments[i].memory, file + file_offset, stored);
        process->segments[i].selector = gdt_alloc_user16_segment(
            (uint32_t)(uintptr_t)process->segments[i].memory,
            allocated - 1U, (disk[i].flags & NE_SEG_DATA) == 0U, true);
        if (!process->segments[i].selector) {
            ne_error = "sin descriptores GDT para NE";
            return false;
        }
    }

    for (uint16_t i = 0U; i < header->segment_count; i++) {
        uint32_t file_offset = (uint32_t)disk[i].offset << header->align_shift;
        uint32_t stored = disk[i].offset ?
            (disk[i].length ? disk[i].length : 65536U) : 0U;
        uint32_t reloc_offset = file_offset + stored;
        uint16_t count;
        if (!(disk[i].flags & NE_SEG_RELOC) || !disk[i].offset) continue;
        if (!range_ok(reloc_offset, 2U, file_size)) {
            ne_error = "relocaciones NE truncadas";
            return false;
        }
        count = read16(file + reloc_offset);
        reloc_offset += 2U;
        if (!range_ok(reloc_offset, (uint32_t)count * sizeof(ne_reloc_t),
                      file_size)) {
            ne_error = "tabla de relocaciones NE invalida";
            return false;
        }
        for (uint16_t r = 0U; r < count; r++) {
            const ne_reloc_t *rel = (const ne_reloc_t *)(const void *)
                (file + reloc_offset + r * sizeof(*rel));
            uint8_t target_type = rel->flags & 3U;
            uint16_t selector, offset;
            if (target_type == NE_TARGET_INTERNAL) {
                if (!rel->target1 || rel->target1 > header->segment_count) {
                    ne_error = "segmento destino NE invalido";
                    return false;
                }
                selector = process->segments[rel->target1 - 1U].selector;
                offset = rel->target2;
            } else if (target_type == NE_TARGET_IMPORT_ORD ||
                       target_type == NE_TARGET_IMPORT_NAME) {
                char module[16];
                char name[64];
                uint16_t ordinal = 0U;
                name[0] = '\0';
                if (!ne_get_import_module(file, file_size, header,
                                          rel->target1, module,
                                          sizeof(module))) {
                    ne_error = "modulo importado NE invalido";
                    return false;
                }
                if (target_type == NE_TARGET_IMPORT_ORD) ordinal = rel->target2;
                else if (!ne_get_import_name(file, file_size, header,
                                             rel->target2, name,
                                             sizeof(name))) {
                    ne_error = "nombre importado NE invalido";
                    return false;
                }
                if (ne_import_equate(module, ordinal,
                                     name[0] ? name : NULL, &offset)) {
                    uint8_t source_type = rel->source_type & 0x0FU;
                    if (source_type != 0U && source_type != 5U) {
                        ne_error = "tipo de fixup invalido para equate Win16";
                        return false;
                    }
                    selector = 0U;
                } else if (!ne_add_relay(process, module, ordinal,
                                         name[0] ? name : NULL,
                                         &selector, &offset)) {
                    return false;
                }
                (*external_relocs)++;
            } else if (target_type == NE_TARGET_OS_FIXUP) {
                ne_error = "fixup de sistema operativo NE no soportado";
                return false;
            } else {
                ne_error = "destino de relocacion NE desconocido";
                return false;
            }
            if (!ne_apply_relocation_chain(&process->segments[i], rel,
                                           selector, offset)) return false;
        }
    }
    return true;
}

static void ne_print_modules(const uint8_t *file, uint32_t size,
                             const ne_header_view_t *header) {
    kprintf("[NE] modulos:");
    for (uint16_t i = 1U; i <= header->module_count; i++) {
        char name[16];
        if (ne_get_import_module(file, size, header, i, name, sizeof(name)))
            kprintf(" %s", name);
    }
    kprintf("\n");
}

static void *ne_far_pointer(ne_process_t *process, uint32_t pointer,
                            uint32_t length, bool writable) {
    uint16_t offset = (uint16_t)pointer;
    uint16_t selector = (uint16_t)(pointer >> 16U);
    if (!selector) return NULL;
    for (uint16_t i = 0U; i < process->header.segment_count; i++) {
        ne_segment_t *segment = &process->segments[i];
        if (segment->selector != selector) continue;
        if (writable && !(segment->flags & NE_SEG_DATA)) return NULL;
        if (!range_ok(offset, length, segment->size)) return NULL;
        return segment->memory + offset;
    }
    for (uint32_t i = 0U; i < NE_MAX_GLOBAL_BLOCKS; i++) {
        ne_global_block_t *block = &process->global_blocks[i];
        if (!block->used || block->selector != selector) continue;
        if (!range_ok(offset, length, block->size)) return NULL;
        return block->memory + offset;
    }
    for (uint32_t i = 0U; i < NE_MAX_ALIAS_SELECTORS; i++) {
        uint32_t base, limit;
        bool executable, descriptor_writable;
        if (process->alias_selectors[i] != selector) continue;
        if (!gdt_query_user16_segment(selector, &base, &limit,
                                      &executable, &descriptor_writable) ||
            (writable && (executable || !descriptor_writable)) ||
            !range_ok(offset, length, limit + 1U)) return NULL;
        return (void *)(uintptr_t)(base + offset);
    }
    if (!writable && selector == process->relay_selector &&
        range_ok(offset, length, process->relay_capacity))
        return process->relay_memory + offset;
    return NULL;
}

static bool ne_far_string(ne_process_t *process, uint32_t pointer,
                          char *out, uint32_t capacity) {
    uint16_t offset = (uint16_t)pointer;
    uint16_t selector = (uint16_t)(pointer >> 16U);
    uint32_t base = ((uint32_t)selector << 16U) | offset;
    if (!out || !capacity) return false;
    for (uint32_t i = 0U; i + 1U < capacity && i < NE_MAX_STRING; i++) {
        const char *source = (const char *)ne_far_pointer(process,
            base + i, 1U, false);
        if (!source) {
            out[0] = '\0';
            return false;
        }
        out[i] = *source;
        if (!out[i]) return true;
    }
    out[capacity - 1U] = '\0';
    return true;
}

static bool ne_stack_word(ne_process_t *process, const registers_t *regs,
                          uint16_t argument_offset, uint16_t *value) {
    uint32_t stack_offset = (uint16_t)regs->useresp;
    uint32_t pointer;
    const uint8_t *data;
    stack_offset += 4U + argument_offset;
    if (stack_offset > 0xFFFEU) return false;
    pointer = ((uint32_t)(uint16_t)regs->ss << 16U) | stack_offset;
    data = (const uint8_t *)ne_far_pointer(process, pointer, 2U, false);
    if (!data) return false;
    *value = read16(data);
    return true;
}

static bool ne_stack_long(ne_process_t *process, const registers_t *regs,
                          uint16_t argument_offset, uint32_t *value) {
    uint16_t lo, hi;
    if (!ne_stack_word(process, regs, argument_offset, &lo) ||
        !ne_stack_word(process, regs, argument_offset + 2U, &hi)) return false;
    *value = (uint32_t)lo | ((uint32_t)hi << 16U);
    return true;
}

static bool ne_stack_far(ne_process_t *process, const registers_t *regs,
                         uint16_t argument_offset, uint32_t *value) {
    return ne_stack_long(process, regs, argument_offset, value);
}

static void ne_return_word(registers_t *regs, uint16_t value) {
    regs->eax = value;
}

static void ne_return_long(registers_t *regs, uint32_t value) {
    regs->eax = (uint16_t)value;
    regs->edx = (uint16_t)(value >> 16U);
}

static ne_local_block_t *ne_find_local(ne_process_t *process,
                                       uint16_t handle) {
    for (uint32_t i = 0U; i < NE_MAX_LOCAL_BLOCKS; i++)
        if (process->local_blocks[i].used &&
            process->local_blocks[i].offset == handle)
            return &process->local_blocks[i];
    return NULL;
}

static uint16_t ne_local_alloc(ne_process_t *process, uint16_t flags,
                               uint16_t requested) {
    uint32_t size = requested ? requested : 1U;
    uint32_t start = (process->local_heap_next + 1U) & ~1U;
    ne_local_block_t *slot = NULL;
    if (size & 1U) size++;
    for (uint32_t i = 0U; i < NE_MAX_LOCAL_BLOCKS; i++)
        if (!process->local_blocks[i].used) { slot = &process->local_blocks[i]; break; }
    if (!slot || start + size > process->local_heap_end || start > 0xFFFFU)
        return 0U;
    slot->used = true;
    slot->offset = (uint16_t)start;
    slot->size = (uint16_t)size;
    process->local_heap_next = (uint16_t)(start + size);
    if (flags & NE_LMEM_ZEROINIT) {
        uint32_t pointer = ((uint32_t)process->data_selector << 16U) |
                           slot->offset;
        void *memory = ne_far_pointer(process, pointer, slot->size, true);
        if (memory) kmemset(memory, 0, slot->size);
    }
    return slot->offset;
}

static uint16_t ne_local_realloc(ne_process_t *process, uint16_t handle,
                                 uint16_t requested, uint16_t flags) {
    ne_local_block_t *block = ne_find_local(process, handle);
    uint16_t replacement;
    if (!block) return 0U;
    if (!requested) requested = 1U;
    if (requested <= block->size) {
        block->size = requested;
        return handle;
    }
    replacement = ne_local_alloc(process, flags, requested);
    if (replacement) {
        uint32_t old_pointer = ((uint32_t)process->data_selector << 16U) |
                               block->offset;
        uint32_t new_pointer = ((uint32_t)process->data_selector << 16U) |
                               replacement;
        void *old_memory = ne_far_pointer(process, old_pointer,
                                           block->size, false);
        void *new_memory = ne_far_pointer(process, new_pointer,
                                           block->size, true);
        if (old_memory && new_memory) kmemcpy(new_memory, old_memory, block->size);
        block->used = false;
    }
    return replacement;
}

static ne_global_block_t *ne_find_global(ne_process_t *process,
                                         uint16_t handle) {
    for (uint32_t i = 0U; i < NE_MAX_GLOBAL_BLOCKS; i++)
        if (process->global_blocks[i].used &&
            process->global_blocks[i].selector == handle)
            return &process->global_blocks[i];
    return NULL;
}

static uint16_t ne_global_alloc(ne_process_t *process, uint16_t flags,
                                uint32_t requested) {
    ne_global_block_t *block = NULL;
    uint8_t *memory;
    uint16_t selector;
    uint32_t size = requested ? requested : 1U;
    if (size > 65536U) return 0U;
    for (uint32_t i = 0U; i < NE_MAX_GLOBAL_BLOCKS; i++)
        if (!process->global_blocks[i].used) { block = &process->global_blocks[i]; break; }
    if (!block) return 0U;
    memory = (uint8_t *)((flags & NE_GMEM_ZEROINIT) ? kzalloc(size) : kmalloc(size));
    if (!memory) return 0U;
    selector = gdt_alloc_user16_segment((uint32_t)(uintptr_t)memory,
                                         size - 1U, false, true);
    if (!selector) { kfree(memory); return 0U; }
    block->used = true;
    block->selector = selector;
    block->memory = memory;
    block->size = size;
    block->flags = flags;
    if (process->process_id) (void)mm_set_allocation_owner(memory,
                                                           process->process_id);
    return selector;
}

static uint16_t ne_global_realloc(ne_process_t *process, uint16_t handle,
                                  uint32_t requested, uint16_t flags) {
    ne_global_block_t *block = ne_find_global(process, handle);
    uint8_t *memory;
    uint32_t size = requested ? requested : 1U;
    if (!block || size > 65536U) return 0U;
    memory = (uint8_t *)krealloc(block->memory, size);
    if (!memory) return 0U;
    if (size > block->size && (flags & NE_GMEM_ZEROINIT))
        kmemset(memory + block->size, 0, size - block->size);
    if (!gdt_update_user16_segment(block->selector,
            (uint32_t)(uintptr_t)memory, size - 1U, false, true)) return 0U;
    block->memory = memory;
    block->size = size;
    block->flags = flags;
    if (process->process_id) (void)mm_set_allocation_owner(memory,
                                                           process->process_id);
    return handle;
}

static uint16_t ne_alloc_alias(ne_process_t *process, uint16_t source,
                               int executable_mode) {
    uint32_t base, limit;
    bool source_exec, writable, executable;
    uint16_t selector;
    if (!gdt_query_user16_segment(source, &base, &limit,
                                  &source_exec, &writable)) return 0U;
    executable = executable_mode < 0 ? source_exec : executable_mode != 0;
    selector = gdt_alloc_user16_segment(base, limit, executable, writable);
    if (!selector) return 0U;
    for (uint32_t i = 0U; i < NE_MAX_ALIAS_SELECTORS; i++) {
        if (process->alias_selectors[i]) continue;
        process->alias_selectors[i] = selector;
        return selector;
    }
    gdt_free_user16_segment(selector);
    return 0U;
}

static bool ne_free_alias(ne_process_t *process, uint16_t selector) {
    for (uint32_t i = 0U; i < NE_MAX_ALIAS_SELECTORS; i++) {
        if (process->alias_selectors[i] != selector) continue;
        process->alias_selectors[i] = 0U;
        gdt_free_user16_segment(selector);
        return true;
    }
    return false;
}

static bool ne_prepare_runtime(ne_process_t *process) {
    uint16_t data_index = process->header.auto_data;
    uint16_t stack_index = process->header.stack_segment;
    ne_segment_t *data, *stack;
    uint32_t heap_start, heap_end, stack_top;
    if (!data_index) {
        for (uint16_t i = 0U; i < process->header.segment_count; i++)
            if (process->segments[i].flags & NE_SEG_DATA) {
                data_index = (uint16_t)(i + 1U);
                break;
            }
    }
    if (!data_index) data_index = 1U;
    if (!stack_index) stack_index = data_index;
    data = &process->segments[data_index - 1U];
    stack = &process->segments[stack_index - 1U];
    process->data_selector = data->selector;
    process->stack_selector = stack->selector;
    process->entry_selector =
        process->segments[process->header.entry_segment - 1U].selector;
    if (process->header.entry_offset >=
        process->segments[process->header.entry_segment - 1U].size) {
        ne_error = "entrypoint Win16 fuera del segmento";
        return false;
    }
    stack_top = process->header.stack_offset;
    if (!stack_top || stack_top > stack->size)
        stack_top = stack->size > 0xFFFEU ? 0xFFFEU : stack->size;
    if (stack_top < 8U) {
        ne_error = "stack Win16 demasiado pequeno";
        return false;
    }
    stack_top -= 4U;
    write16(stack->memory + stack_top, 0U);
    write16(stack->memory + stack_top + 2U, process->relay_selector);
    process->initial_sp = (uint16_t)stack_top;

    heap_start = (data->stored_size + 15U) & ~15U;
    if (heap_start < 0x20U) heap_start = 0x20U;
    heap_end = data->size;
    if (data == stack && stack_top > 512U && heap_end > stack_top - 512U)
        heap_end = stack_top - 512U;
    if (heap_end > 0xFFFFU) heap_end = 0xFFFFU;
    if (heap_start > heap_end) heap_start = heap_end;
    process->local_heap_start = (uint16_t)heap_start;
    process->local_heap_end = (uint16_t)heap_end;
    process->local_heap_next = (uint16_t)heap_start;
    return true;
}

static void ne_assign_ownership(ne_process_t *process) {
    if (!process || !process->process_id) return;
    (void)mm_set_allocation_owner(process, process->process_id);
    if (process->relay_memory)
        (void)mm_set_allocation_owner(process->relay_memory,
                                      process->process_id);
    for (uint16_t i = 0U; i < process->header.segment_count; i++)
        if (process->segments[i].memory)
            (void)mm_set_allocation_owner(process->segments[i].memory,
                                          process->process_id);
}

bool ne_dump_image(const uint8_t *file, uint32_t size, const char *path) {
    ne_header_view_t header;
    if (!parse_header(file, size, &header)) return false;
    kprintf("[NE] %s: Win16 segmentos=%u CS:IP=%u:%x SS:SP=%u:%x "
            "DGROUP=%u heap=%u stack=%u\n",
        path ? path : "imagen", header.segment_count,
        header.entry_segment, header.entry_offset,
        header.stack_segment, header.stack_offset,
        header.auto_data, header.heap_size, header.stack_size);
    ne_print_modules(file, size, &header);
    ne_error = "sin error";
    return true;
}

bool ne_execute_image(const uint8_t *file, uint32_t size, const char *path) {
    ne_process_t *process;
    ne_header_view_t header;
    uint32_t external = 0U;
    int pid;
    if (!compat_mode_allow_pe()) {
        ne_error = "Win16 desactivado en modo de memoria reducida";
        return false;
    }
    if (!parse_header(file, size, &header)) return false;
    process = (ne_process_t *)kzalloc(sizeof(*process));
    if (!process) { ne_error = "sin memoria para proceso Win16"; return false; }
    process->header = header;
    if (process->header.flags & NE_FLAG_LIBRARY) {
        ne_error = "el archivo NE es una DLL, no un ejecutable";
        ne_destroy_process(process);
        return false;
    }
    kstrncpy(process->path, path ? path : "win16.exe",
             sizeof(process->path) - 1U);
    task_preempt_disable();
    if (!ne_init_relays(process) ||
        !ne_load_segments(process, file, size, &external) ||
        !ne_prepare_runtime(process)) {
        ne_destroy_process(process);
        task_preempt_enable();
        return false;
    }
    pid = task_create_user16_program(ne_path_name(path),
        process->entry_selector, process->header.entry_offset,
        process->stack_selector, process->initial_sp,
        process->data_selector, process->data_selector,
        process->header.stack_size, process->header.heap_size, path);
    if (pid < 0) {
        ne_error = "no se pudo crear la tarea Win16";
        ne_destroy_process(process);
        task_preempt_enable();
        return false;
    }
    process->process_id = (uint32_t)pid;
    if (!ne_register_process(process)) {
        (void)task_request_exit((uint32_t)pid);
        ne_error = "sin slots para procesos Win16";
        ne_destroy_process(process);
        task_preempt_enable();
        return false;
    }
    ne_assign_ownership(process);
    task_preempt_enable();
    kprintf("[WIN16] %s pid=%u: %u segmentos, %u relays, "
            "CS:IP=%x:%x SS:SP=%x:%x DS=%x\n",
        process->path, process->process_id, process->header.segment_count,
        process->relay_count, process->entry_selector,
        process->header.entry_offset, process->stack_selector,
        process->initial_sp, process->data_selector);
    ne_print_modules(file, size, &process->header);
    (void)external;
    ne_error = "sin error";
    return true;
}

static registers_t *ne_relay_fault(registers_t *regs, ne_process_t *process,
                                   ne_relay_t *relay, const char *reason) {
    kprintf("[WIN16] abortando pid=%u: %s %s.%s#%u\n",
        process ? process->process_id : 0U, reason,
        relay ? relay->module : "?",
        (relay && relay->name[0]) ? relay->name : "ordinal",
        relay ? relay->ordinal : 0U);
    task_exit_from_interrupt(0x1600);
    return task_schedule(regs);
}

static void ne_warn_stub(ne_process_t *process, ne_relay_t *relay) {
    if (relay->warned) return;
    relay->warned = true;
    kprintf("[WIN16] stub sin implementar: %s.%s#%u (retf %u) pid=%u\n",
        relay->module, relay->name[0] ? relay->name : "ordinal",
        relay->ordinal, relay->argument_bytes, process->process_id);
}

registers_t *ne_win16_syscall(registers_t *regs) {
    ne_process_t *process;
    ne_relay_t *relay;
    uint16_t a = 0U, b = 0U, c = 0U;
    uint32_t long_value = 0U, pointer = 0U;
    if (!regs || !task_current_is_win16()) {
        if (regs) regs->eax = (uint32_t)-BK_EACCES;
        return regs;
    }
    process = ne_find_process(task_current_process_id());
    if (!process || regs->ebx >= process->relay_count)
        return ne_relay_fault(regs, process, NULL, "relay invalido");
    relay = &process->relays[regs->ebx];
    if (relay->handler == NE_API_UNSUPPORTED)
        return ne_relay_fault(regs, process, relay, "import no soportado");

    switch (relay->handler) {
        case NE_API_ZERO:
            ne_warn_stub(process, relay);
            ne_return_word(regs, 0U);
            break;
        case NE_API_TRUE:
            ne_return_word(regs, 1U);
            break;
        case NE_API_EXIT:
        case NE_API_POST_QUIT:
            task_exit_from_interrupt(0);
            return task_schedule(regs);
        case NE_API_GET_VERSION:
            ne_return_long(regs, 0x00000A03U); /* Windows 3.10 */
            break;
        case NE_API_INIT_TASK:
            /*
             * Wine implements KERNEL.91 as a register entry.  We preserve
             * that contract without its TDB/PDB machinery: AX reports
             * success; CX is a conservative stack boundary; DX is
             * SW_SHOWNORMAL; SI/DI are previous/current instance handles;
             * ES:BX is an empty command line inside DGROUP.
             */
            regs->eax = 1U;
            regs->ebx = 0U;
            regs->ecx = process->initial_sp > 150U
                ? process->initial_sp - 150U : process->initial_sp;
            regs->edx = 1U;
            regs->esi = 0U;
            regs->edi = process->data_selector;
            regs->es = process->data_selector;
            break;
        case NE_API_LOCAL_ALLOC:
            if (!ne_stack_word(process, regs, 0U, &a) ||
                !ne_stack_word(process, regs, 2U, &b))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_local_alloc(process, b, a));
            break;
        case NE_API_LOCAL_REALLOC:
            if (!ne_stack_word(process, regs, 0U, &a) ||
                !ne_stack_word(process, regs, 2U, &b) ||
                !ne_stack_word(process, regs, 4U, &c))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_local_realloc(process, c, b, a));
            break;
        case NE_API_LOCAL_FREE:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                ne_local_block_t *block = ne_find_local(process, a);
                if (block) { block->used = false; ne_return_word(regs, 0U); }
                else ne_return_word(regs, a);
            }
            break;
        case NE_API_LOCAL_LOCK:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_find_local(process, a) ? a : 0U);
            break;
        case NE_API_LOCAL_UNLOCK:
            ne_return_word(regs, 0U);
            break;
        case NE_API_LOCAL_SIZE:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                ne_local_block_t *block = ne_find_local(process, a);
                ne_return_word(regs, block ? block->size : 0U);
            }
            break;
        case NE_API_GLOBAL_ALLOC:
            if (!ne_stack_long(process, regs, 0U, &long_value) ||
                !ne_stack_word(process, regs, 4U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_global_alloc(process, a, long_value));
            break;
        case NE_API_GLOBAL_REALLOC:
            if (!ne_stack_word(process, regs, 0U, &a) ||
                !ne_stack_long(process, regs, 2U, &long_value) ||
                !ne_stack_word(process, regs, 6U, &b))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs,
                ne_global_realloc(process, b, long_value, a));
            break;
        case NE_API_GLOBAL_FREE:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                ne_global_block_t *block = ne_find_global(process, a);
                if (!block) ne_return_word(regs, a);
                else {
                    gdt_free_user16_segment(block->selector);
                    kfree(block->memory);
                    kmemset(block, 0, sizeof(*block));
                    ne_return_word(regs, 0U);
                }
            }
            break;
        case NE_API_GLOBAL_LOCK:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_long(regs, ne_find_global(process, a) ?
                           ((uint32_t)a << 16U) : 0U);
            break;
        case NE_API_GLOBAL_UNLOCK:
            ne_return_word(regs, 0U);
            break;
        case NE_API_GLOBAL_SIZE:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                ne_global_block_t *block = ne_find_global(process, a);
                ne_return_long(regs, block ? block->size : 0U);
            }
            break;
        case NE_API_YIELD:
            ne_return_word(regs, 0U);
            break;
        case NE_API_GET_CURRENT_TASK:
            ne_return_word(regs, (uint16_t)task_current_pid());
            break;
        case NE_API_GET_MODULE_HANDLE:
            ne_return_word(regs, process->data_selector);
            break;
        case NE_API_GET_MODULE_USAGE:
            ne_return_word(regs, 1U);
            break;
        case NE_API_MAKE_PROC_INSTANCE:
            if (!ne_stack_far(process, regs, 2U, &pointer))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_long(regs, pointer);
            break;
        case NE_API_GET_WIN_FLAGS:
            ne_return_long(regs, 0x00000413U);
            break;
        case NE_API_LSTRLEN:
            if (!ne_stack_far(process, regs, 0U, &pointer))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                char text[NE_MAX_STRING];
                ne_return_word(regs, ne_far_string(process, pointer, text,
                    sizeof(text)) ? (uint16_t)kstrlen(text) : 0U);
            }
            break;
        case NE_API_LSTRCMP:
        case NE_API_LSTRCMPI:
            {
                uint32_t right_pointer;
                char left[NE_MAX_STRING], right[NE_MAX_STRING];
                if (!ne_stack_far(process, regs, 0U, &right_pointer) ||
                    !ne_stack_far(process, regs, 4U, &pointer) ||
                    !ne_far_string(process, pointer, left, sizeof(left)) ||
                    !ne_far_string(process, right_pointer, right,
                                   sizeof(right)))
                    return ne_relay_fault(regs, process, relay,
                                          "strings Win16 invalidos");
                ne_return_word(regs, (uint16_t)ne_string_compare(
                    left, right, relay->handler == NE_API_LSTRCMPI));
            }
            break;
        case NE_API_LSTRCPY:
        case NE_API_LSTRCAT:
            {
                uint32_t source_pointer, destination_pointer;
                char text[NE_MAX_STRING];
                char existing[NE_MAX_STRING];
                char *destination;
                uint32_t needed;
                if (!ne_stack_far(process, regs, 0U, &source_pointer) ||
                    !ne_stack_far(process, regs, 4U, &destination_pointer) ||
                    !ne_far_string(process, source_pointer, text,
                                   sizeof(text)))
                    return ne_relay_fault(regs, process, relay,
                                          "puntero de string invalido");
                existing[0] = '\0';
                if (relay->handler == NE_API_LSTRCAT &&
                    !ne_far_string(process, destination_pointer, existing,
                                   sizeof(existing)))
                    return ne_relay_fault(regs, process, relay,
                                          "destino de string invalido");
                needed = (uint32_t)kstrlen(existing) +
                         (uint32_t)kstrlen(text) + 1U;
                destination = (char *)ne_far_pointer(process,
                    destination_pointer, needed, true);
                if (!destination)
                    return ne_relay_fault(regs, process, relay,
                                          "destino de string fuera de rango");
                if (relay->handler == NE_API_LSTRCPY) kstrcpy(destination, text);
                else kstrcat(destination, text);
                ne_return_long(regs, destination_pointer);
            }
            break;
        case NE_API_OUTPUT_DEBUG_STRING:
            if (!ne_stack_far(process, regs, 0U, &pointer))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                char text[NE_MAX_STRING];
                if (ne_far_string(process, pointer, text, sizeof(text)))
                    kprintf("[WIN16:DEBUG] %s\n", text);
                ne_return_word(regs, 0U);
            }
            break;
        case NE_API_SET_LAST_ERROR:
            if (!ne_stack_long(process, regs, 0U, &long_value))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            process->last_error = long_value;
            ne_return_word(regs, 0U);
            break;
        case NE_API_GET_LAST_ERROR:
            ne_return_long(regs, process->last_error);
            break;
        case NE_API_GET_VERSION_EX:
            if (!ne_stack_far(process, regs, 0U, &pointer))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                uint8_t *info = (uint8_t *)ne_far_pointer(process, pointer,
                                                           20U, true);
                if (!info) ne_return_word(regs, 0U);
                else {
                    uint32_t structure_size = read32(info);
                    if (structure_size < 20U) ne_return_word(regs, 0U);
                    else {
                        write32(info + 4U, 3U);
                        write32(info + 8U, 10U);
                        write32(info + 12U, 0U);
                        write32(info + 16U, 1U);
                        if (structure_size > 20U) info[20] = '\0';
                        ne_return_word(regs, 1U);
                    }
                }
            }
            break;
        case NE_API_GET_NUM_TASKS:
            ne_return_word(regs, (uint16_t)task_count());
            break;
        case NE_API_GET_TASK_DS:
            ne_return_word(regs, process->data_selector);
            break;
        case NE_API_GET_TICKS:
            {
                uint32_t hz = pit_get_frequency_hz();
                ne_return_long(regs, hz ?
                    (pit_get_ticks() * 1000U) / hz : 0U);
            }
            break;
        case NE_API_MESSAGE_BOX:
            {
                uint32_t caption_pointer, text_pointer;
                char caption[128], text[NE_MAX_STRING];
                if (!ne_stack_word(process, regs, 0U, &a) ||
                    !ne_stack_far(process, regs, 2U, &caption_pointer) ||
                    !ne_stack_far(process, regs, 6U, &text_pointer) ||
                    !ne_stack_word(process, regs, 10U, &b))
                    return ne_relay_fault(regs, process, relay,
                                          "stack de MessageBox invalido");
                if (!ne_far_string(process, text_pointer, text, sizeof(text)))
                    kstrcpy(text, "");
                if (!ne_far_string(process, caption_pointer, caption,
                                   sizeof(caption)))
                    kstrcpy(caption, "Win16");
                ne_return_word(regs, (uint16_t)win32_user_message_box_a(
                    (void *)(uintptr_t)b, text, caption, a));
            }
            break;
        case NE_API_GET_EXP_WIN_VER:
            ne_return_word(regs, 0x030AU);
            break;
        case NE_API_GET_SELECTOR_LIMIT:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                uint32_t base, limit;
                bool executable, writable;
                if (!gdt_query_user16_segment(a, &base, &limit,
                                               &executable, &writable))
                    limit = 0U;
                ne_return_long(regs, limit);
            }
            break;
        case NE_API_ALLOC_SELECTOR:
        case NE_API_ALLOC_DS_TO_CS:
        case NE_API_ALLOC_CS_TO_DS:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_alloc_alias(process, a,
                relay->handler == NE_API_ALLOC_SELECTOR ? -1 :
                (relay->handler == NE_API_ALLOC_DS_TO_CS ? 1 : 0)));
            break;
        case NE_API_FREE_SELECTOR:
            if (!ne_stack_word(process, regs, 0U, &a))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            ne_return_word(regs, ne_free_alias(process, a) ? 0U : a);
            break;
        case NE_API_LOAD_LIBRARY:
            if (!ne_stack_far(process, regs, 0U, &pointer))
                return ne_relay_fault(regs, process, relay, "stack invalido");
            {
                char library[64], module[16];
                if (!ne_far_string(process, pointer, library, sizeof(library)))
                    ne_return_word(regs, 0U);
                else {
                    ne_normalize_module(module, sizeof(module), library);
                    ne_return_word(regs,
                        (ne_equal_ci(module, "KERNEL") ||
                         ne_equal_ci(module, "USER") ||
                         ne_equal_ci(module, "GDI"))
                            ? process->data_selector : 0U);
                }
            }
            break;
        case NE_API_FREE_LIBRARY:
            ne_return_word(regs, 1U);
            break;
        default:
            return ne_relay_fault(regs, process, relay,
                                  "handler Win16 invalido");
    }
    return regs;
}

const char *ne_last_error(void) { return ne_error; }
