#include "win32.h"
#include "process.h"
#include "thread.h"
#include "sync.h"
#include "resources.h"
#include "exception.h"
#include "vm.h"
#include "process_handle.h"
#include "../include/pe_loader.h"
#include "../include/elf_loader.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../include/vfs.h"
#include "../include/pit.h"
#include "../include/rtc.h"
#include "../stdio.h"
uint32_t win32_lz32_resolve(const char *name);

#define ERROR_INVALID_HANDLE 6U
#define ERROR_TOO_MANY_OPEN_FILES 4U
#define ERROR_ACCESS_DENIED 5U
#define ERROR_INVALID_PARAMETER 87U
#define ERROR_MOD_NOT_FOUND 126U
#define ERROR_PROC_NOT_FOUND 127U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define ERROR_FILE_NOT_FOUND 2U
#define ERROR_PATH_NOT_FOUND 3U
#define ERROR_READ_FAULT 30U
#define ERROR_INSUFFICIENT_BUFFER 122U
#define ERROR_NOT_OWNER 288U
#define ERROR_TOO_MANY_POSTS 298U
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFU
#define FILE_ATTRIBUTE_READONLY 0x00000001U
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010U
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020U
#define FILE_TYPE_UNKNOWN 0U
#define FILE_TYPE_DISK 1U
#define FILE_TYPE_CHAR 2U
#define MEM_COMMIT 0x1000U
#define MEM_RESERVE 0x2000U
#define MEM_RELEASE 0x8000U
#define MEM_FREE 0x10000U
#define MEM_PRIVATE 0x20000U
#define MEM_MAPPED 0x40000U
#define MEM_IMAGE 0x1000000U
#define PAGE_NOACCESS 0x01U
#define PAGE_READONLY 0x02U
#define PAGE_READWRITE 0x04U
#define PAGE_WRITECOPY 0x08U
#define PAGE_EXECUTE_READ 0x20U
#define PAGE_EXECUTE_READWRITE 0x40U
#define PAGE_EXECUTE_WRITECOPY 0x80U
#define PAGE_PROTECTION_MASK 0x000000FFU
#define HEAP_ZERO_MEMORY 0x00000008U
#define GMEM_MOVEABLE 0x0002U
#define GMEM_ZEROINIT 0x0040U
#define GMEM_MODIFY 0x0080U
#define GMEM_DISCARDABLE 0x0100U
#define GMEM_INVALID_HANDLE 0x8000U
#define ERROR_DISCARDED 157U
#define ERROR_NOT_LOCKED 158U
#define GLOBAL_HANDLE_BASE 0x72000000U
#define WIN32_MAX_GLOBAL_BLOCKS 256U
#define PROCESS_HEAP_HANDLE WIN32_PROCESS_HEAP_HANDLE
#define PRIVATE_HEAP_MAGIC 0x57484550U
#define GENERIC_READ 0x80000000U
#define GENERIC_WRITE 0x40000000U
#define FILE_SHARE_READ 0x00000001U
#define FILE_SHARE_WRITE 0x00000002U
#define FILE_SHARE_DELETE 0x00000004U
#define CREATE_NEW 1U
#define CREATE_ALWAYS 2U
#define OPEN_EXISTING 3U
#define OPEN_ALWAYS 4U
#define TRUNCATE_EXISTING 5U
#define FILE_BEGIN 0U
#define FILE_CURRENT 1U
#define FILE_END 2U
#define FILE_HANDLE_BASE 0x71000000U
#define WIN32_MAX_FILES 64U
#define FIND_HANDLE_BASE 0x71100000U
#define WIN32_MAX_FINDS 8U
#define ERROR_NO_MORE_FILES 18U
#define ERROR_CALL_NOT_IMPLEMENTED 120U
#define ERROR_ALREADY_EXISTS 183U
#define ERROR_MORE_DATA 234U
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100U
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000U
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200U
#define TLS_OUT_OF_INDEXES WIN32_TLS_OUT_OF_INDEXES
#define SNAPSHOT_HANDLE_BASE 0x71200000U
#define MAPPING_HANDLE_BASE 0x71400000U
#define WIN32_MAX_MAPPINGS 16U
#define WIN32_MAX_MAP_VIEWS 32U
#define FILE_MAP_COPY 0x00000001U
#define FILE_MAP_WRITE 0x00000002U
#define FILE_MAP_READ 0x00000004U
#define INVALID_HANDLE_VALUE ((void *)(uintptr_t)0xFFFFFFFFU)
#define WIN32_MAX_SNAPSHOTS 4U
#define TH32CS_SNAPPROCESS 0x00000002U

#define CP_ACP 0U
#define CP_OEMCP 1U
#define CP_MACCP 2U
#define CP_THREAD_ACP 3U
#define CP_SYMBOL 42U
#define CP_UTF7 65000U
#define CP_UTF8 65001U
#define WIN32_ACP 1252U
#define WIN32_OEMCP 437U

typedef struct { uint32_t magic; } private_heap_t;
typedef struct {
    bool used;
    bool movable;
    bool discarded;
    uint8_t lock_count;
    uint32_t owner_process_id;
    uint32_t flags;
    uint32_t size;
    void *data;
} win_global_block_t;
typedef struct {
    bool used;
    bool writable;
    uint32_t owner_process_id;
    uint32_t access;
    uint32_t share;
    int fd;
    char path[VFS_MAX_PATH];
} win_file_t;
static win_file_t win_files[WIN32_MAX_FILES];
typedef struct {
    bool used;
    bool handle_open;
    bool file_backed;
    bool writable;
    uint32_t owner_process_id;
    uint32_t size;
    uint32_t view_count;
    uint8_t *data;
    char path[VFS_MAX_PATH];
} win_mapping_t;
typedef struct {
    bool used;
    uint32_t owner_process_id;
    uint32_t mapping_index;
    uint32_t offset;
    uint32_t size;
    void *address;
} win_map_view_t;
static win_mapping_t win_mappings[WIN32_MAX_MAPPINGS];
static win_map_view_t win_map_views[WIN32_MAX_MAP_VIEWS];
static win_global_block_t win_global_blocks[WIN32_MAX_GLOBAL_BLOCKS];
typedef struct {
    bool used;
    uint32_t count, index;
    /* Se reserva al abrir la busqueda: no debe volver a vivir en .bss. */
    vfs_dir_entry_t *entries;
    char pattern[32];
} win_find_t;
static win_find_t win_finds[WIN32_MAX_FINDS];
typedef struct{bool used;uint8_t count,index;uint32_t pid[TASK_MAX],parent[TASK_MAX],threads[TASK_MAX];char name[TASK_MAX][TASK_NAME_LEN];}win_snapshot_t;
typedef struct{uint32_t size,usage,pid,heap,module,threads,parent;int32_t priority;uint32_t flags;char exe[260];}process_entry32_t;
typedef struct{uint32_t size,usage,pid,heap,module,threads,parent;int32_t priority;uint32_t flags;uint16_t exe[260];}process_entry32_w_t;
static win_snapshot_t win_snapshots[WIN32_MAX_SNAPSHOTS];
typedef struct {
    bool used;
    uint32_t process_id;
    char path[VFS_MAX_PATH];
} win_current_directory_t;
static win_current_directory_t win_current_directories[TASK_MAX];

/* BLES_WINE_CORE_IMPORT_FIX_20260723_KERNEL32
 * Console handler lists are process-local on Windows. The callbacks are kept
 * for future console-event delivery; registration/removal already follows the
 * observable SetConsoleCtrlHandler contract needed by old Win32 programs. */
#define WIN32_MAX_CONSOLE_CTRL_STATES TASK_MAX
#define WIN32_MAX_CONSOLE_CTRL_HANDLERS 16U
typedef struct {
    bool used;
    bool ignore_ctrl_c;
    uint8_t handler_count;
    uint32_t owner_process_id;
    void *handlers[WIN32_MAX_CONSOLE_CTRL_HANDLERS];
} win_console_ctrl_state_t;
static win_console_ctrl_state_t
    win_console_ctrl_states[WIN32_MAX_CONSOLE_CTRL_STATES];

static win_console_ctrl_state_t *console_ctrl_state(uint32_t process_id,
                                                    bool create) {
    win_console_ctrl_state_t *free_slot = NULL;
    for (uint32_t i = 0; i < WIN32_MAX_CONSOLE_CTRL_STATES; i++) {
        win_console_ctrl_state_t *state = &win_console_ctrl_states[i];
        if (state->used && state->owner_process_id == process_id)
            return state;
        if (!state->used && !free_slot) free_slot = state;
    }
    if (!create || !free_slot) return NULL;
    kmemset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->owner_process_id = process_id;
    return free_slot;
}
static bool k32_wide_to_ansi(const uint16_t *wide, char *out, uint32_t size);

typedef struct {
    const char *name;
    uint32_t handle;
    uint32_t references;
} builtin_module_t;

#define BUILTIN_CONSTANT_MODULES(X) \
    X("NTDLL.DLL",     0x70000000U) \
    X("KERNEL32.DLL",  0x70000001U) \
    X("KERNELBASE.DLL",0x70000002U) \
    X("USER32.DLL",    0x70000003U) \
    X("GDI32.DLL",     0x70000004U) \
    X("MSVCRT.DLL",    0x70000005U) \
    X("COMCTL32.DLL",  0x70000006U) \
    X("COMDLG32.DLL",  0x70000007U) \
    X("ADVAPI32.DLL",  0x70000008U) \
    X("SHELL32.DLL",   0x70000009U) \
    X("RICHED20.DLL",  0x7000000AU) \
    X("RICHED32.DLL",  0x7000000BU) \
    X("OLE32.DLL",     0x7000000CU) \
    X("OLEAUT32.DLL",  0x7000000DU) \
    X("VERSION.DLL",   0x7000000EU) \
    X("WINMM.DLL",     0x7000000FU) \
    X("MMSYSTEM.DLL",  0x70000010U) \
    X("IMM32.DLL",     0x70000011U) \
    X("SHLWAPI.DLL",   0x70000012U) \
    X("RPCRT4.DLL",    0x70000013U) \
    X("WINSPOOL.DRV",  0x70000014U) \
    X("WINSPOOL.DLL",  0x70000015U) \
    X("WSOCK32.DLL",   0x70000016U) \
    X("WS2_32.DLL",    0x70000017U) \
    X("CRTDLL.DLL",    0x70000018U) \
    X("MSVCRT20.DLL",  0x70000019U) \
    X("MSVCRT40.DLL",  0x7000001AU) \
    X("DDRAW.DLL",     0x7000001BU) \
    X("DSOUND.DLL",    0x7000001CU) \
    X("DINPUT.DLL",    0x7000001DU) \
    X("DINPUT8.DLL",   0x7000001EU) \
    X("LZ32.DLL",      0x7000001FU) \
    X("WININET.DLL",   0x70000020U) \
    X("MSACM32.DLL",   0x70000021U) \
    X("AVIFIL32.DLL",  0x70000022U) \
    X("MPR.DLL",       0x70000023U) \
    X("USER.EXE",      0x70000024U) \
    X("GDI.EXE",       0x70000025U) \
    X("WZINET32.DLL",  0x70000026U)

static builtin_module_t modules[] = {
    {"NTDLL.DLL", 0x70000000U, 1U},
    {"KERNEL32.DLL", 0x70000001U, 1U},
    {"KERNELBASE.DLL", 0x70000002U, 1U},
    {"USER32.DLL", 0x70000003U, 1U},
    {"GDI32.DLL", 0x70000004U, 1U},
    {"USER.EXE", 0x70000024U, 1U},
    {"GDI.EXE", 0x70000025U, 1U},
    {"MSVCRT.DLL", 0x70000005U, 1U},
    {"COMCTL32.DLL", 0x70000006U, 1U},
    {"COMDLG32.DLL", 0x70000007U, 1U},
    {"ADVAPI32.DLL", 0x70000008U, 1U},
    {"SHELL32.DLL", 0x70000009U, 1U},
    {"RICHED20.DLL", 0x7000000AU, 1U},
    {"RICHED32.DLL", 0x7000000BU, 1U},
    {"OLE32.DLL", 0x7000000CU, 1U},
    {"OLEAUT32.DLL", 0x7000000DU, 1U},
    {"VERSION.DLL", 0x7000000EU, 1U},
    {"WINMM.DLL", 0x7000000FU, 1U},
    {"MMSYSTEM.DLL", 0x70000010U, 1U},
    {"IMM32.DLL", 0x70000011U, 1U},
    {"SHLWAPI.DLL", 0x70000012U, 1U},
    {"RPCRT4.DLL", 0x70000013U, 1U},
    {"WINSPOOL.DRV", 0x70000014U, 1U},
    {"WINSPOOL.DLL", 0x70000015U, 1U},
    {"WSOCK32.DLL", 0x70000016U, 1U},
    {"WS2_32.DLL", 0x70000017U, 1U},
    {"CRTDLL.DLL", 0x70000018U, 1U},
    {"MSVCRT20.DLL", 0x70000019U, 1U},
    {"MSVCRT40.DLL", 0x7000001AU, 1U},
    {"DDRAW.DLL", 0x7000001BU, 1U},
    {"DSOUND.DLL", 0x7000001CU, 1U},
    {"DINPUT.DLL", 0x7000001DU, 1U},
    {"DINPUT8.DLL", 0x7000001EU, 1U},
    {"LZ32.DLL", 0x7000001FU, 1U},
    {"WININET.DLL", 0x70000020U, 1U},
    {"MSACM32.DLL", 0x70000021U, 1U},
    {"AVIFIL32.DLL", 0x70000022U, 1U},
    {"MPR.DLL", 0x70000023U, 1U},
};

static uint8_t upper(uint8_t c) { return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c; }
static bool equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) if (upper((uint8_t)*a++) != upper((uint8_t)*b++)) return false;
    return *a == *b;
}
static const char *basename(const char *name) {
    const char *base = name;
    if (!name) return NULL;
    while (*name) { if (*name == '/' || *name == '\\') base = name + 1; name++; }
    return base;
}
static bool has_extension(const char *name) {
    if (!name) return false;
    while (*name) if (*name++ == '.') return true;
    return false;
}
static builtin_module_t *by_name(const char *name) {
    const char *base = basename(name);
    uint32_t base_len = base ? (uint32_t)kstrlen(base) : 0U;
    for (uint32_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++) {
        /*
         * Priorizar la comparacion exacta libc. Algunas llamadas Ring 3 a
         * LoadLibrary llegan a este punto desde un thunk stdcall y se observo
         * que la ruta auxiliar equal_ci no reconocia incluso el nombre ya
         * canonico "SHELL32.DLL". kstrcmp tambien evita depender del helper
         * interno con argumentos en registros para el caso habitual.
         */
        if ((base && kstrcmp(base, modules[i].name) == 0) ||
            equal_ci(base, modules[i].name))
            return &modules[i];
        /* Win9x LoadLibrary also accepts a bare module name. */
        if (base_len && !has_extension(base) &&
            kstrlen(modules[i].name) == base_len + 4U) {
            bool same = true;
            for (uint32_t n = 0; n < base_len; n++)
                if (upper((uint8_t)base[n]) != upper((uint8_t)modules[i].name[n])) { same = false; break; }
            if (same && modules[i].name[base_len] == '.') return &modules[i];
        }
    }
    return NULL;
}

/*
 * WinZip 95 loads SHELL32 by its extensionless Win9x spelling.  Keep a
 * deterministic fallback for the canonical name produced by LoadLibraryA.
 * This is deliberately independent from the generic table walk: startup
 * must not fall through to the PE-on-disk loader for a module implemented
 * by the kernel.
 */
static bool builtin_constant_name_equal(const char *input,
                                        const char *canonical) {
    const char *base = basename(input);
    uint32_t i = 0U;
    if (!base || !canonical) return false;
    while (base[i] && canonical[i]) {
        if (upper((uint8_t)base[i]) != upper((uint8_t)canonical[i]))
            return false;
        i++;
    }
    if (!base[i] && !canonical[i]) return true;
    /* Win9x also accepts LoadLibrary/GetModuleHandle without ".DLL". */
    return !base[i] && canonical[i] == '.' &&
           canonical[i + 1U] == 'D' && canonical[i + 2U] == 'L' &&
           canonical[i + 3U] == 'L' && canonical[i + 4U] == '\0';
}

static uint32_t builtin_constant_handle(const char *name) {
#define MATCH_BUILTIN(module_name, module_handle) \
    if (builtin_constant_name_equal(name, module_name)) return module_handle;
    BUILTIN_CONSTANT_MODULES(MATCH_BUILTIN)
#undef MATCH_BUILTIN
    return 0U;
}

static const char *builtin_constant_name(uint32_t handle) {
#define MATCH_BUILTIN(module_name, module_handle) \
    if (handle == module_handle) return module_name;
    BUILTIN_CONSTANT_MODULES(MATCH_BUILTIN)
#undef MATCH_BUILTIN
    return NULL;
}

static builtin_module_t *by_handle(uint32_t handle) {
    for (uint32_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++)
        if (modules[i].handle == handle) return &modules[i];
    return NULL;
}
static win_file_t *file_from_handle(void *handle) {
    uint32_t value=(uint32_t)(uintptr_t)handle;
    if (value < FILE_HANDLE_BASE || value >= FILE_HANDLE_BASE+WIN32_MAX_FILES) return NULL;
    value -= FILE_HANDLE_BASE;
    return win_files[value].used &&
           win_files[value].owner_process_id == task_current_process_id()
        ? &win_files[value] : NULL;
}

static uint32_t win32_file_count(uint32_t owner_process_id,
                                 bool owner_only) {
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < WIN32_MAX_FILES; i++) {
        if (!win_files[i].used) continue;
        if (owner_only && win_files[i].owner_process_id != owner_process_id)
            continue;
        count++;
    }
    return count;
}

static void win32_dump_file_handles(uint32_t owner_process_id) {
    kprintf("[WIN32:file] handles process=%u global=%u/%u vfs=%u/%u\n",
            win32_file_count(owner_process_id, true),
            win32_file_count(0U, false), WIN32_MAX_FILES,
            vfs_open_file_count(), VFS_MAX_OPEN_FILES);
    for (uint32_t i = 0U; i < WIN32_MAX_FILES; i++) {
        win_file_t *file = &win_files[i];
        if (!file->used) continue;
        kprintf("[WIN32:file] h=%x owner=%u fd=%d access=%x share=%x path=%s\n",
                FILE_HANDLE_BASE + i, file->owner_process_id, file->fd,
                file->access, file->share, file->path);
    }
}

static bool win_path(const char *src, char *dst) {
    uint32_t s = 0, d = 0;
    if (!src || !*src || !dst) return false;
    if (((src[0] >= 'A' && src[0] <= 'Z') ||
         (src[0] >= 'a' && src[0] <= 'z')) && src[1] == ':') s = 2;
    while (src[s] && d + 1U < VFS_MAX_PATH) {
        dst[d++] = src[s] == '\\' ? '/' : src[s];
        s++;
    }
    dst[d] = '\0';
    return src[s] == '\0' && d != 0U;
}

static void path_pop_component(char *path) {
    size_t len;
    if (!path) return;
    len = kstrlen(path);
    while (len > 1U && path[len - 1U] == '/') path[--len] = '\0';
    while (len > 1U && path[len - 1U] != '/') path[--len] = '\0';
    if (len > 1U) path[len - 1U] = '\0';
}

static const char *win_current_directory(bool create);

static bool normalize_native_path(const char *path, char *out) {
    char raw[VFS_MAX_PATH];
    char component[32];
    uint32_t r = 0;
    if (!path || !out || !path[0]) return false;
    kmemset(raw, 0, sizeof(raw));
    /* CreateFileA normaliza deliberadamente sobre el mismo buffer. Primero
     * conserva la entrada completa en raw; limpiar out antes de copiarla
     * convertia cualquier ruta absoluta en el directorio actual. */
    if (path[0] == '/') kstrncpy(raw, path, sizeof(raw) - 1U);
    else {
        const char *cwd = win_current_directory(true);
        kstrncpy(raw, cwd && cwd[0] ? cwd : "/", sizeof(raw) - 1U);
        if (kstrcmp(raw, "/") != 0 && kstrlen(raw) + 1U < sizeof(raw)) kstrcat(raw, "/");
        if (kstrlen(raw) + kstrlen(path) >= sizeof(raw)) return false;
        kstrcat(raw, path);
    }
    kmemset(out, 0, VFS_MAX_PATH);
    out[0] = '/'; out[1] = '\0';
    while (raw[r]) {
        uint32_t c = 0;
        while (raw[r] == '/') r++;
        if (!raw[r]) break;
        while (raw[r] && raw[r] != '/') {
            if (c + 1U >= sizeof(component)) return false;
            component[c++] = raw[r++];
        }
        component[c] = '\0';
        if (kstrcmp(component, ".") == 0) continue;
        if (kstrcmp(component, "..") == 0) { path_pop_component(out); continue; }
        if (kstrlen(out) > 1U) {
            if (kstrlen(out) + 1U >= VFS_MAX_PATH) return false;
            kstrcat(out, "/");
        }
        if (kstrlen(out) + kstrlen(component) >= VFS_MAX_PATH) return false;
        kstrcat(out, component);
    }
    return true;
}

static const char *win_current_directory(bool create) {
    uint32_t process_id = task_current_process_id();
    win_current_directory_t *free_slot = NULL;
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (win_current_directories[i].used &&
            win_current_directories[i].process_id == process_id)
            return win_current_directories[i].path;
        if (!win_current_directories[i].used && !free_slot)
            free_slot = &win_current_directories[i];
    }
    if (!create || !free_slot) return "/";

    kmemset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = true;
    free_slot->process_id = process_id;
    kstrcpy(free_slot->path, "/");
    const char *image_path = win32_process_current_image_path();
    if (image_path && win_path(image_path, free_slot->path) &&
        normalize_native_path(free_slot->path, free_slot->path)) {
        path_pop_component(free_slot->path);
        if (!free_slot->path[0]) kstrcpy(free_slot->path, "/");
    }
    return free_slot->path;
}

static bool resolve_win_path(const char *path, char *native) {
    /* BLES_WINE_PATH_NORMALIZATION_FIX_20260723 */
    char converted[VFS_MAX_PATH];
    char normalized[VFS_MAX_PATH];
    uint32_t length;
    if (!path || !native) return false;
    kmemset(converted, 0, sizeof(converted));
    kmemset(normalized, 0, sizeof(normalized));
    if (!win_path(path, converted) ||
        !normalize_native_path(converted, normalized)) return false;
    length = (uint32_t)kstrlen(normalized);
    if (!length || length >= VFS_MAX_PATH) return false;
    if (normalized[0] == '/' &&
        (normalized[1] == 'a' || normalized[1] == 'A') &&
        (normalized[2] == 't' || normalized[2] == 'T') &&
        (normalized[3] == 'a' || normalized[3] == 'A') &&
        normalized[4] >= '0' && normalized[4] <= '9' &&
        normalized[5] != '\0' && normalized[5] != '/') {
        uint32_t i;
        if (length + 1U >= VFS_MAX_PATH) return false;
        for (i = length + 1U; i > 5U; i--)
            normalized[i] = normalized[i - 1U];
        normalized[5] = '/';
        length++;
    }
    kmemcpy(native, normalized, length + 1U);
    return true;
}

static uint32_t native_to_windows_path(const char *native, char *out, uint32_t size) {
    uint32_t needed = 2U;
    const char *p = native ? native : "/";
    while (*p) { needed++; p++; }
    if (!out || size == 0U) return needed;
    if (size <= needed) return 0U;
    out[0] = 'C'; out[1] = ':';
    p = native ? native : "/";
    for (uint32_t i = 2U; *p; i++, p++) out[i] = *p == '/' ? '\\' : *p;
    out[needed] = '\0';
    return needed;
}
static bool file_sharing_allows(const char *path, uint32_t access, uint32_t share) {
    for (uint32_t i = 0; i < WIN32_MAX_FILES; i++) {
        win_file_t *open = &win_files[i];
        if (!open->used || !equal_ci(open->path, path)) continue;
        if ((access & GENERIC_READ) && !(open->share & FILE_SHARE_READ)) return false;
        if ((access & GENERIC_WRITE) && !(open->share & FILE_SHARE_WRITE)) return false;
        if ((open->access & GENERIC_READ) && !(share & FILE_SHARE_READ)) return false;
        if ((open->access & GENERIC_WRITE) && !(share & FILE_SHARE_WRITE)) return false;
    }
    return true;
}

/* BLES_WINE_TEMP_SFX_FIX_20260723
 * C:\TEMP maps to the root FAT volume as /TEMP.  Keep the check narrow so
 * CreateFileA never invents arbitrary missing parent directories. */
static bool win32_path_is_temp_child(const char *native) {
    static const char prefix[] = "/TEMP/";
    if (!native) return false;
    for (uint32_t i = 0U; prefix[i]; i++) {
        char a = native[i];
        char b = prefix[i];
        if (!a) return false;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (a != b) return false;
    }
    return true;
}

static void *WIN32_API k32_CreateFileA(const char *name, uint32_t access,
    uint32_t share, void *security UNUSED, uint32_t creation,
    uint32_t attributes UNUSED, void *template_file UNUSED) {
    char path[VFS_MAX_PATH];
    vfs_dir_entry_t entry;
    bool exists;
    uint32_t flags;
    int fd;
    uint32_t error = 0U;

    if (!resolve_win_path(name, path)) {
        error = ERROR_INVALID_PARAMETER;
        goto fail_without_path;
    }
    if (win32_path_is_temp_child(path)) (void)vfs_mkdir("/TEMP");
    exists = vfs_stat(path, &entry) && entry.type == VFS_NODE_FILE;

    if ((creation == OPEN_EXISTING || creation == TRUNCATE_EXISTING) &&
        !exists) {
        error = ERROR_FILE_NOT_FOUND;
        goto fail;
    }
    if (creation == CREATE_NEW && exists) {
        error = ERROR_ALREADY_EXISTS;
        goto fail;
    }
    if (!file_sharing_allows(path, access, share)) {
        error = ERROR_ACCESS_DENIED;
        goto fail;
    }

    flags = (access & GENERIC_WRITE)
        ? ((access & GENERIC_READ) ? VFS_O_RDWR : VFS_O_WRONLY)
        : VFS_O_RDONLY;
    if (creation == CREATE_NEW || creation == CREATE_ALWAYS ||
        creation == OPEN_ALWAYS)
        flags |= VFS_O_CREATE;
    if (creation == CREATE_ALWAYS || creation == TRUNCATE_EXISTING)
        flags |= VFS_O_TRUNC;

    fd = vfs_open(path, flags);
    if (fd < 0) {
        const char *reason = vfs_last_error_text();
        if (reason && (kstrcmp(reason, "VFS file table exhausted") == 0 ||
                       kstrcmp(reason, "not enough memory") == 0))
            error = ERROR_TOO_MANY_OPEN_FILES;
        else if (!exists && !(flags & VFS_O_CREATE))
            error = ERROR_FILE_NOT_FOUND;
        else
            error = ERROR_ACCESS_DENIED;
        kprintf("[WIN32:file] CreateFileA VFS FAIL name=%s path=%s "
                "access=%x share=%x creation=%u flags=%x error=%u reason=%s\n",
                name ? name : "(null)", path, access, share, creation, flags,
                error, reason ? reason : "(none)");
        win32_dump_file_handles(task_current_process_id());
        vfs_dump_open_files();
        pe_win32_set_last_error(error);
        return INVALID_HANDLE_VALUE;
    }

    for (uint32_t i = 0U; i < WIN32_MAX_FILES; i++) {
        if (!win_files[i].used) {
            win_file_t *file = &win_files[i];
            kmemset(file, 0, sizeof(*file));
            file->used = true;
            file->writable = (access & GENERIC_WRITE) != 0U;
            file->owner_process_id = task_current_process_id();
            file->access = access;
            file->share = share;
            file->fd = fd;
            kstrncpy(file->path, path, sizeof(file->path) - 1U);
            pe_win32_set_last_error(
                (creation == OPEN_ALWAYS && exists)
                    ? ERROR_ALREADY_EXISTS : 0U);
            return (void *)(uintptr_t)(FILE_HANDLE_BASE + i);
        }
    }

    (void)vfs_close(fd);
    error = ERROR_TOO_MANY_OPEN_FILES;
    kprintf("[WIN32:file] CreateFileA Win32 table exhausted path=%s\n", path);
    win32_dump_file_handles(task_current_process_id());
    pe_win32_set_last_error(error);
    return INVALID_HANDLE_VALUE;

fail:
    kprintf("[WIN32:file] CreateFileA FAIL name=%s path=%s access=%x "
            "share=%x creation=%u exists=%u error=%u\n",
            name ? name : "(null)", path, access, share, creation,
            exists ? 1U : 0U, error);
    pe_win32_set_last_error(error);
    return INVALID_HANDLE_VALUE;

fail_without_path:
    kprintf("[WIN32:file] CreateFileA FAIL name=%s path=(invalid) "
            "access=%x share=%x creation=%u error=%u\n",
            name ? name : "(null)", access, share, creation, error);
    pe_win32_set_last_error(error);
    return INVALID_HANDLE_VALUE;
}
static int WIN32_API k32_ReadFile(void *handle, void *buffer, uint32_t length,
                                  uint32_t *read, void *overlapped UNUSED) {
    win_file_t *f = file_from_handle(handle);
    int result;
    if (read) *read = 0U;
    if (!f || (!buffer && length)) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0;
    }
    if (!length) { pe_win32_set_last_error(0U); return 1; }
    result = vfs_read(f->fd, buffer, length);
    if (result < 0) { pe_win32_set_last_error(ERROR_ACCESS_DENIED); return 0; }
    if (read) *read = (uint32_t)result;
    pe_win32_set_last_error(0U); return 1;
}
static int WIN32_API k32_WriteFile(void *handle, const void *buffer,
                                   uint32_t length, uint32_t *written,
                                   void *overlapped UNUSED) {
    return win32_file_write(handle, buffer, length, written);
}
int win32_file_write(void *handle, const void *buffer, uint32_t length, uint32_t *written) {
    win_file_t *f = file_from_handle(handle);
    int result;
    if (written) *written = 0U;
    if (!f || !f->writable || (!buffer && length)) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED); return 0;
    }
    if (!length) { pe_win32_set_last_error(0U); return 1; }
    result = vfs_write(f->fd, buffer, length);
    if (result < 0) { pe_win32_set_last_error(ERROR_ACCESS_DENIED); return 0; }
    if (written) *written = (uint32_t)result;
    pe_win32_set_last_error(0U); return 1;
}
static uint32_t WIN32_API k32_SetFilePointer(void *handle, int32_t low,
                                             int32_t *high, uint32_t method) {
    win_file_t *f = file_from_handle(handle);
    int32_t position;
    if (high && *high != 0 && *high != -1) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0xFFFFFFFFU;
    }
    if (!f || method > FILE_END || (position = vfs_seek(f->fd, low, method)) < 0) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0xFFFFFFFFU;
    }
    if (high) *high = 0;
    pe_win32_set_last_error(0U);
    return (uint32_t)position;
}
static uint32_t WIN32_API k32_GetFileSize(void *handle, uint32_t *high) {
    win_file_t *f = file_from_handle(handle);
    int32_t size;
    if (high) *high = 0U;
    if (!f || (size = vfs_size(f->fd)) < 0) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0xFFFFFFFFU;
    }
    pe_win32_set_last_error(0U); return (uint32_t)size;
}

/* BLES_WIN32_SFX_MAPPING_FIX
 *
 * Un mapping de archivo no es una lectura exacta del tamaño pedido.
 * Si el mapping escribible es mayor que el archivo, Win32 amplía el archivo
 * antes de devolver CreateFileMapping. El área agregada queda inicializada en
 * nuestro buffer kzalloc y se persiste al archivo mediante vfs_truncate.
 */
static bool mapping_read_prefix(const char *path, uint8_t *data,
                                uint32_t bytes) {
    int fd;
    uint32_t total = 0U;

    if (!bytes) return true;
    if (!path || !data) return false;

    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return false;

    while (total < bytes) {
        int got = vfs_read(fd, data + total, bytes - total);
        if (got < 0) {
            vfs_close(fd);
            return false;
        }
        if (got == 0) break;
        total += (uint32_t)got;
    }

    vfs_close(fd);
    return total == bytes;
}

/* Escribe el prefijo cubierto por el mapping sin truncar una posible cola del
 * archivo. vfs_write preserva el tamaño viejo cuando el write termina antes
 * del EOF, a diferencia de vfs_write_all(). */
static bool mapping_writeback(win_mapping_t *mapping) {
    int fd;
    int written;

    if (!mapping || !mapping->file_backed || !mapping->writable ||
        !mapping->path[0] || (!mapping->data && mapping->size))
        return false;

    fd = vfs_open(mapping->path, VFS_O_RDWR);
    if (fd < 0) return false;

    if (vfs_seek(fd, 0, FILE_BEGIN) < 0) {
        vfs_close(fd);
        return false;
    }

    written = mapping->size
        ? vfs_write(fd, mapping->data, mapping->size)
        : 0;
    vfs_close(fd);

    return written >= 0 && (uint32_t)written == mapping->size;
}

void win32_kernel32_cleanup_process(uint32_t pid) {
    for (uint32_t i = 0; i < TASK_MAX; i++)
        if (win_current_directories[i].used &&
            win_current_directories[i].process_id == pid)
            kmemset(&win_current_directories[i], 0,
                    sizeof(win_current_directories[i]));
    for (uint32_t i = 0; i < WIN32_MAX_FILES; i++) {
        if (!win_files[i].used || win_files[i].owner_process_id != pid) continue;
        (void)vfs_close(win_files[i].fd);
        kmemset(&win_files[i], 0, sizeof(win_files[i]));
    }
    for (uint32_t i = 0; i < WIN32_MAX_MAP_VIEWS; i++) {
        win_map_view_t *view = &win_map_views[i];
        if (!view->used || view->owner_process_id != pid) continue;
        win_mapping_t *mapping = &win_mappings[view->mapping_index];
        if (mapping->used && mapping->view_count) mapping->view_count--;
        kmemset(view, 0, sizeof(*view));
    }
    for (uint32_t i = 0; i < WIN32_MAX_MAPPINGS; i++) {
        win_mapping_t *mapping = &win_mappings[i];
        if (!mapping->used || mapping->owner_process_id != pid) continue;
        if (mapping->file_backed && mapping->writable)
            (void)mapping_writeback(mapping);
        if (mapping->data) kfree(mapping->data);
        kmemset(mapping, 0, sizeof(*mapping));
    }
    /* GlobalAlloc/LocalAlloc blocks are process-local Win32 objects.  Old
     * self-extractors allocate several movable blocks while inspecting their
     * appended archive and commonly terminate without freeing all of them.
     * Keeping those blocks in the kernel-wide 64-slot table made later SFX
     * launches fail with a spurious "header corrupt" once the table filled. */
    for (uint32_t i = 0; i < WIN32_MAX_GLOBAL_BLOCKS; i++) {
        win_global_block_t *block = &win_global_blocks[i];
        if (!block->used || block->owner_process_id != pid) continue;
        if (block->data) kfree(block->data);
        kmemset(block, 0, sizeof(*block));
    }
    for (uint32_t i = 0; i < WIN32_MAX_CONSOLE_CTRL_STATES; i++)
        if (win_console_ctrl_states[i].used &&
            win_console_ctrl_states[i].owner_process_id == pid)
            kmemset(&win_console_ctrl_states[i], 0,
                    sizeof(win_console_ctrl_states[i]));
}

static win_mapping_t *mapping_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    uint32_t slot;
    if (value < MAPPING_HANDLE_BASE || value >= MAPPING_HANDLE_BASE + WIN32_MAX_MAPPINGS)
        return NULL;
    slot = value - MAPPING_HANDLE_BASE;
    return win_mappings[slot].used && win_mappings[slot].handle_open &&
           win_mappings[slot].owner_process_id == task_current_process_id()
        ? &win_mappings[slot] : NULL;
}

static void mapping_release_if_unused(win_mapping_t *mapping) {
    if (!mapping || mapping->handle_open || mapping->view_count) return;
    if (mapping->data) kfree(mapping->data);
    kmemset(mapping, 0, sizeof(*mapping));
}

static bool mapping_close_handle(void *handle) {
    win_mapping_t *mapping = mapping_from_handle(handle);
    if (!mapping) return false;
    mapping->handle_open = false;
    mapping_release_if_unused(mapping);
    return true;
}

static void *WIN32_API k32_CreateFileMappingA(void *file_handle,
                                               void *security UNUSED,
                                               uint32_t protect,
                                               uint32_t maximum_high,
                                               uint32_t maximum_low,
                                               const char *name UNUSED) {
    win_file_t *file = NULL;
    win_mapping_t *mapping = NULL;
    uint32_t file_size = 0U;
    uint32_t read_size = 0U;
    uint32_t size;
    uint32_t page_protect = protect & PAGE_PROTECTION_MASK;
    bool anonymous = file_handle == INVALID_HANDLE_VALUE;
    bool writable = page_protect == PAGE_READWRITE ||
                    page_protect == PAGE_EXECUTE_READWRITE;
    bool copy_on_write = page_protect == PAGE_WRITECOPY ||
                         page_protect == PAGE_EXECUTE_WRITECOPY;

    if (maximum_high) {
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    if (page_protect != PAGE_READONLY &&
        page_protect != PAGE_READWRITE &&
        page_protect != PAGE_WRITECOPY &&
        page_protect != PAGE_EXECUTE_READ &&
        page_protect != PAGE_EXECUTE_READWRITE &&
        page_protect != PAGE_EXECUTE_WRITECOPY) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (!anonymous) {
        int32_t current_size;

        file = file_from_handle(file_handle);
        if (!file) {
            pe_win32_set_last_error(ERROR_INVALID_HANDLE);
            return NULL;
        }

        current_size = vfs_size(file->fd);
        if (current_size < 0) {
            pe_win32_set_last_error(ERROR_INVALID_HANDLE);
            return NULL;
        }
        file_size = (uint32_t)current_size;

        if (writable && !file->writable) {
            pe_win32_set_last_error(ERROR_ACCESS_DENIED);
            return NULL;
        }
    }

    size = maximum_low;
    if (!size && file) size = file_size;
    if (!size) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (file && size > file_size && !writable) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED);
        return NULL;
    }

    for (uint32_t i = 0; i < WIN32_MAX_MAPPINGS; i++) {
        if (!win_mappings[i].used) {
            mapping = &win_mappings[i];
            break;
        }
    }
    if (!mapping) {
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    uint8_t *data = (uint8_t *)kzalloc(size);
    if (!data) {
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    if (file) {
        read_size = file_size < size ? file_size : size;

        if (!mapping_read_prefix(file->path, data, read_size)) {
            kprintf("[WIN32:map] lectura incompleta %s "
                    "read=%u file=%u map=%u\n",
                    file->path, read_size, file_size, size);
            kfree(data);
            pe_win32_set_last_error(ERROR_READ_FAULT);
            return NULL;
        }

        if (size > file_size) {
            if (!vfs_truncate(file->fd, size)) {
                kprintf("[WIN32:map] no se pudo extender %s "
                        "%u -> %u\n", file->path, file_size, size);
                kfree(data);
                pe_win32_set_last_error(ERROR_ACCESS_DENIED);
                return NULL;
            }
            kprintf("[WIN32:map] extendido %s %u -> %u\n",
                    file->path, file_size, size);
        }
    }

    kmemset(mapping, 0, sizeof(*mapping));
    mapping->used = true;
    mapping->handle_open = true;
    mapping->file_backed = !anonymous;
    mapping->writable = writable && !copy_on_write;
    mapping->owner_process_id = task_current_process_id();
    mapping->size = size;
    mapping->data = data;
    if (file)
        kstrncpy(mapping->path, file->path, sizeof(mapping->path) - 1U);

    uint32_t slot = (uint32_t)(mapping - win_mappings);
    kprintf("[WIN32:map] CreateFileMapping %s file=%u map=%u "
            "protect=0x%x handle=0x%x\n",
            file ? file->path : "(anon)", file_size, size, protect,
            MAPPING_HANDLE_BASE + slot);

    pe_win32_set_last_error(0U);
    return (void *)(uintptr_t)(MAPPING_HANDLE_BASE + slot);
}

static void *WIN32_API k32_MapViewOfFile(void *mapping_handle,
                                          uint32_t desired_access,
                                          uint32_t offset_high,
                                          uint32_t offset_low,
                                          uint32_t bytes) {
    win_mapping_t *mapping = mapping_from_handle(mapping_handle);
    if (!mapping || offset_high || offset_low >= mapping->size) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return NULL;
    }
    if ((desired_access & FILE_MAP_WRITE) && !mapping->writable) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED); return NULL;
    }
    if (!bytes) bytes = mapping->size - offset_low;
    if (bytes > mapping->size - offset_low) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return NULL;
    }
    for (uint32_t i = 0; i < WIN32_MAX_MAP_VIEWS; i++) {
        win_map_view_t *view = &win_map_views[i];
        if (view->used) continue;
        view->used = true;
        view->owner_process_id = task_current_process_id();
        view->mapping_index = (uint32_t)(mapping - win_mappings);
        view->offset = offset_low;
        view->size = bytes;
        view->address = mapping->data + offset_low;
        mapping->view_count++;
        pe_win32_set_last_error(0U);
        return view->address;
    }
    pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return NULL;
}

static int WIN32_API k32_FlushViewOfFile(const void *address,
                                          uint32_t bytes UNUSED) {
    for (uint32_t i = 0; i < WIN32_MAX_MAP_VIEWS; i++) {
        win_map_view_t *view = &win_map_views[i];
        win_mapping_t *mapping;
        if (!view->used || view->owner_process_id != task_current_process_id() ||
            view->address != address) continue;
        mapping = &win_mappings[view->mapping_index];
        if (!mapping->used) break;
        if (mapping->file_backed && mapping->writable &&
            !mapping_writeback(mapping)) {
            pe_win32_set_last_error(ERROR_ACCESS_DENIED); return 0;
        }
        pe_win32_set_last_error(0U); return 1;
    }
    pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0;
}

static int WIN32_API k32_UnmapViewOfFile(const void *address) {
    for (uint32_t i = 0; i < WIN32_MAX_MAP_VIEWS; i++) {
        win_map_view_t *view = &win_map_views[i];
        win_mapping_t *mapping;
        if (!view->used || view->owner_process_id != task_current_process_id() ||
            view->address != address) continue;
        mapping = &win_mappings[view->mapping_index];
        if (mapping->used && mapping->file_backed && mapping->writable)
            (void)mapping_writeback(mapping);
        if (mapping->used && mapping->view_count) mapping->view_count--;
        kmemset(view, 0, sizeof(*view));
        mapping_release_if_unused(mapping);
        pe_win32_set_last_error(0U); return 1;
    }
    pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0;
}

static win_snapshot_t*snapshot_from(void*handle){uint32_t v=(uint32_t)(uintptr_t)handle;if(v<SNAPSHOT_HANDLE_BASE||v>=SNAPSHOT_HANDLE_BASE+WIN32_MAX_SNAPSHOTS)return NULL;v-=SNAPSHOT_HANDLE_BASE;return win_snapshots[v].used?&win_snapshots[v]:NULL;}
static void*WIN32_API k32_CreateToolhelp32Snapshot(uint32_t flags,uint32_t process UNUSED){
    win_snapshot_t*s=NULL;if(!(flags&TH32CS_SNAPPROCESS)){pe_win32_set_last_error(ERROR_CALL_NOT_IMPLEMENTED);return(void*)(uintptr_t)0xFFFFFFFFU;}
    for(uint32_t i=0;i<WIN32_MAX_SNAPSHOTS;i++)if(!win_snapshots[i].used){s=&win_snapshots[i];kmemset(s,0,sizeof(*s));s->used=true;break;}
    if(!s){pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);return(void*)(uintptr_t)0xFFFFFFFFU;}
    for(uint32_t i=0;i<task_count()&&s->count<TASK_MAX;i++){
        const task_t*t=task_get(i);uint32_t pid=t->process_id?t->process_id:t->pid;int found=-1;
        for(uint32_t n=0;n<s->count;n++)if(s->pid[n]==pid){found=(int)n;break;}
        if(found>=0){s->threads[found]++;continue;}
        uint32_t n=s->count++;s->pid[n]=pid;s->parent[n]=t->parent_pid;s->threads[n]=1U;kstrncpy(s->name[n],t->name,TASK_NAME_LEN-1U);
    }
    s->index=0U;pe_win32_set_last_error(0);return(void*)(uintptr_t)(SNAPSHOT_HANDLE_BASE+(uint32_t)(s-win_snapshots));
}
static int snapshot_process(win_snapshot_t*s,process_entry32_t*out,bool first){
    uint32_t n;if(!s||!out||out->size<sizeof(*out)){pe_win32_set_last_error(ERROR_INVALID_PARAMETER);return 0;}
    n=first?0U:(uint32_t)s->index+1U;if(n>=s->count){pe_win32_set_last_error(ERROR_NO_MORE_FILES);return 0;}
    uint32_t size=out->size;kmemset(out,0,sizeof(*out));out->size=size;out->pid=s->pid[n];out->threads=s->threads[n];out->parent=s->parent[n];out->priority=8;kstrncpy(out->exe,s->name[n],sizeof(out->exe)-1U);s->index=(uint8_t)n;pe_win32_set_last_error(0);return 1;
}
static int WIN32_API k32_Process32First(void*h,process_entry32_t*out){return snapshot_process(snapshot_from(h),out,true);}
static int WIN32_API k32_Process32Next(void*h,process_entry32_t*out){return snapshot_process(snapshot_from(h),out,false);}
static int snapshot_process_w(win_snapshot_t*s,process_entry32_w_t*out,bool first){process_entry32_t a;uint32_t i;if(!out||out->size<sizeof(*out)){pe_win32_set_last_error(ERROR_INVALID_PARAMETER);return 0;}a.size=sizeof(a);if(!snapshot_process(s,&a,first))return 0;uint32_t size=out->size;kmemset(out,0,sizeof(*out));out->size=size;out->usage=a.usage;out->pid=a.pid;out->heap=a.heap;out->module=a.module;out->threads=a.threads;out->parent=a.parent;out->priority=a.priority;out->flags=a.flags;for(i=0;i<259U&&a.exe[i];i++)out->exe[i]=(uint8_t)a.exe[i];out->exe[i]=0;return 1;}
static int WIN32_API k32_Process32FirstW(void*h,process_entry32_w_t*out){return snapshot_process_w(snapshot_from(h),out,true);}
static int WIN32_API k32_Process32NextW(void*h,process_entry32_w_t*out){return snapshot_process_w(snapshot_from(h),out,false);}
static int WIN32_API k32_CloseHandle(void *handle) {
    win_file_t *f;
    bool ok = true;
    win_snapshot_t*snapshot=snapshot_from(handle);
    if(snapshot){kmemset(snapshot,0,sizeof(*snapshot));pe_win32_set_last_error(0);return 1;}
    if (mapping_from_handle(handle)) {
        ok = mapping_close_handle(handle);
        pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
        return ok ? 1 : 0;
    }
    if (win32_process_handle_is_handle(handle)) {
        ok = win32_process_handle_close(handle);
        pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
        return ok ? 1 : 0;
    }
    if (win32_thread_is_handle(handle)) {
        ok = win32_thread_close_handle(handle);
        pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
        return ok ? 1 : 0;
    }
    if (win32_sync_is_handle(handle)) {
        ok = win32_sync_close_handle(handle);
        pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
        return ok ? 1 : 0;
    }
    f = file_from_handle(handle);
    if (!f) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    ok = vfs_close(f->fd);
    kmemset(f, 0, sizeof(*f));
    pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
    return ok ? 1 : 0;
}

/* Stable entry points for the loader and for the Win95 compatibility layer.
 * Keeping these outside the name table also prevents old SFX programs from
 * falling through to a placeholder when the large export table is folded by
 * the compiler. */
int WIN32_API win32_kernel32_ReadFile(void *handle, void *buffer,
                                      uint32_t length, uint32_t *read,
                                      void *overlapped) {
    return k32_ReadFile(handle, buffer, length, read, overlapped);
}

int WIN32_API win32_kernel32_CloseHandle(void *handle) {
    return k32_CloseHandle(handle);
}
static int WIN32_API k32_CopyFileA(const char *src,const char *dst,int fail_if_exists) {
    char a[VFS_MAX_PATH],b[VFS_MAX_PATH]; void *data=NULL,*existing=NULL; uint32_t size=0,es=0; bool ok;
    if(!resolve_win_path(src,a)||!resolve_win_path(dst,b)||!vfs_read_all(a,&data,&size))return 0;
    if(fail_if_exists&&vfs_read_all(b,&existing,&es)){kfree(existing);kfree(data);return 0;}
    ok=vfs_write_all(b,data,size);kfree(data);return ok?1:0;
}
static int WIN32_API k32_CreateDirectoryA(const char *path, void *security UNUSED) {
    char native[VFS_MAX_PATH];
    vfs_dir_entry_t existing;
    if (!resolve_win_path(path, native)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Windows reports FALSE/ERROR_ALREADY_EXISTS for an existing
     * directory; vfs_mkdir itself is intentionally idempotent. */
    if (vfs_stat(native, &existing)) {
        pe_win32_set_last_error(ERROR_ALREADY_EXISTS);
        return 0;
    }
    if (!vfs_mkdir(native)) {
        pe_win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 1;
}
static int WIN32_API k32_DeleteFileA(const char *path) {
    char native[VFS_MAX_PATH];
    int ok = resolve_win_path(path, native) && vfs_remove(native);
    pe_win32_set_last_error(ok ? 0U : ERROR_FILE_NOT_FOUND);
    return ok;
}
static int WIN32_API k32_RemoveDirectoryA(const char *path) {
    char native[VFS_MAX_PATH];
    int ok = resolve_win_path(path, native) && vfs_remove(native);
    pe_win32_set_last_error(ok ? 0U : ERROR_PATH_NOT_FOUND);
    return ok;
}
static int WIN32_API k32_MoveFileA(const char *old_name, const char *new_name) {
    char old_path[VFS_MAX_PATH], new_path[VFS_MAX_PATH];
    int ok = resolve_win_path(old_name, old_path) && resolve_win_path(new_name, new_path) &&
             vfs_rename(old_path, new_path);
    pe_win32_set_last_error(ok ? 0U : ERROR_FILE_NOT_FOUND);
    return ok;
}
static int WIN32_API k32_MoveFileExA(const char *old_name, const char *new_name,
                                     uint32_t flags UNUSED) {
    if (!new_name) return k32_DeleteFileA(old_name);
    return k32_MoveFileA(old_name, new_name);
}
static uint32_t WIN32_API k32_GetCurrentDirectoryA(uint32_t size, char *out) {
    const char *current = win_current_directory(true);
    uint32_t length = native_to_windows_path(current, NULL, 0U);
    if (!out || size <= length) {
        pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return length + 1U;
    }
    native_to_windows_path(current, out, size);
    pe_win32_set_last_error(0); return length;
}
static int WIN32_API k32_SetCurrentDirectoryA(const char *path) {
    char native[VFS_MAX_PATH];
    vfs_dir_entry_t entry;
    win_current_directory_t *slot = NULL;
    if (!resolve_win_path(path, native) || !vfs_stat(native, &entry) ||
        entry.type != VFS_NODE_DIR) {
        pe_win32_set_last_error(ERROR_PATH_NOT_FOUND); return 0;
    }
    (void)win_current_directory(true);
    for (uint32_t i = 0; i < TASK_MAX; i++)
        if (win_current_directories[i].used &&
            win_current_directories[i].process_id == task_current_process_id()) {
            slot = &win_current_directories[i]; break;
        }
    if (!slot) { pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    kstrncpy(slot->path, native, sizeof(slot->path) - 1U);
    pe_win32_set_last_error(0); return 1;
}
static uint32_t WIN32_API k32_GetFullPathNameA(const char *path, uint32_t size,
                                                char *out, char **file_part) {
    char full[VFS_MAX_PATH];
    uint32_t length;
    if (file_part) *file_part = NULL;
    if (!resolve_win_path(path, full)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0;
    }
    length = native_to_windows_path(full, NULL, 0U);
    if (!out || size <= length) {
        pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER); return length + 1U;
    }
    native_to_windows_path(full, out, size);
    if (file_part) {
        char *last = out;
        for (char *scan = out; *scan; scan++) if (*scan == '\\') last = scan + 1;
        *file_part = last;
    }
    pe_win32_set_last_error(0); return length;
}
static uint32_t WIN32_API k32_GetFileAttributesA(const char *path) {
    char native[VFS_MAX_PATH];
    vfs_dir_entry_t entry;
    uint32_t attributes;
    if (!resolve_win_path(path, native)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    /* vfs_listdir is an enumeration operation and emits an error when the
     * target is a regular file.  vfs_stat is the correct non-destructive type
     * query for GetFileAttributes. */
    if (!vfs_stat(native, &entry)) {
        pe_win32_set_last_error(ERROR_FILE_NOT_FOUND);
        return INVALID_FILE_ATTRIBUTES;
    }
    attributes = entry.type == VFS_NODE_DIR
        ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if (entry.attributes & FAT_ATTR_READ_ONLY)
        attributes |= FILE_ATTRIBUTE_READONLY;
    pe_win32_set_last_error(0U);
    return attributes;
}
static uint32_t copy_fixed_path(const char *path, char *out, uint32_t size) {
    uint32_t length = (uint32_t)kstrlen(path);
    if (!out || size <= length) {
        pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER); return length + 1U;
    }
    kstrcpy(out, path); pe_win32_set_last_error(0); return length;
}
static uint32_t WIN32_API k32_GetTempPathA(uint32_t size, char *out) {
    if (!vfs_mkdir("/TEMP")) {
        pe_win32_set_last_error(ERROR_PATH_NOT_FOUND);
        return 0U;
    }
    return copy_fixed_path("C:\\TEMP\\", out, size);
}
static uint32_t WIN32_API k32_GetWindowsDirectoryA(char *out, uint32_t size) {
    return copy_fixed_path("C:\\SYSTEM", out, size);
}
static uint32_t WIN32_API k32_GetSystemDirectoryA(char *out, uint32_t size) {
    return copy_fixed_path("C:\\SYSTEM\\LIBS\\WINE", out, size);
}
/* BLES_WINE_KERNEL32_FILE_LOCKS_20260723
 *
 * BlesKernOS todavía no mantiene rangos de bloqueo por archivo/proceso.
 * WinZip usa LockFile/UnlockFile para coordinación local. Como no existe
 * acceso concurrente Win32 real al mismo archivo, aceptar el rango conserva
 * la semántica observable necesaria sin bloquear I/O.
 */
static int WIN32_API k32_LockFile(void *handle,
                                  uint32_t offset_low,
                                  uint32_t offset_high,
                                  uint32_t bytes_low,
                                  uint32_t bytes_high) {
    (void)offset_low;
    (void)offset_high;
    (void)bytes_low;
    (void)bytes_high;

    if (!handle) {
        pe_win32_set_last_error(6U);
        return 0;
    }

    pe_win32_set_last_error(0U);
    return 1;
}

static int WIN32_API k32_UnlockFile(void *handle,
                                    uint32_t offset_low,
                                    uint32_t offset_high,
                                    uint32_t bytes_low,
                                    uint32_t bytes_high) {
    (void)offset_low;
    (void)offset_high;
    (void)bytes_low;
    (void)bytes_high;

    if (!handle) {
        pe_win32_set_last_error(6U);
        return 0;
    }

    pe_win32_set_last_error(0U);
    return 1;
}

static int WIN32_API k32_FlushFileBuffers(void *handle) {
    win_file_t *f = file_from_handle(handle);
    if (!f) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    /* BLES_WINE_SFX_IO_CACHE_FIX_20260723: WriteFile is cached coherently by VFS and this API is the
     * explicit durability boundary exposed to Win32 applications. */
    if (!vfs_flush(f->fd)) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 1;
}
static int WIN32_API k32_SetEndOfFile(void *handle) {
    win_file_t *f = file_from_handle(handle);
    int32_t position;
    if (!f || !f->writable || (position = vfs_tell(f->fd)) < 0 ||
        !vfs_truncate(f->fd, (uint32_t)position)) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0;
    }
    pe_win32_set_last_error(0U); return 1;
}
static uint32_t WIN32_API k32_GetFileType(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (file_from_handle(handle)) return FILE_TYPE_DISK;
    if (value <= 2U) return FILE_TYPE_CHAR;
    pe_win32_set_last_error(ERROR_INVALID_HANDLE); return FILE_TYPE_UNKNOWN;
}
static void *WIN32_API k32_GetCurrentProcess(void) { return (void *)(uintptr_t)0xFFFFFFFFU; }
static void *WIN32_API k32_GetCurrentThread(void) { return (void *)(uintptr_t)0xFFFFFFFEU; }
static uint32_t WIN32_API k32_GetCurrentProcessId(void) { return task_current_process_id(); }
static uint32_t WIN32_API k32_GetCurrentThreadId(void) { return task_current_pid(); }
/* Entry points exported explicitly as well as through the generic resolver.
 * The PE loader needs stable addresses for these two TEB-backed primitives
 * before any compatibility resolver or dynamically loaded module runs. */
uint32_t WIN32_API win32_kernel32_GetLastError(void) {
    return pe_win32_get_last_error();
}
void WIN32_API win32_kernel32_SetLastError(uint32_t error) {
    pe_win32_set_last_error(error);
}
static uint32_t WIN32_API k32_GetLastError(void) {
    return win32_kernel32_GetLastError();
}
static void WIN32_API k32_SetLastError(uint32_t error) {
    win32_kernel32_SetLastError(error);
}
static uint32_t WIN32_API k32_GetTickCount(void) {
    uint32_t hz = pit_get_frequency_hz();
    return hz ? (uint32_t)(((uint64_t)pit_get_ticks() * 1000U) / hz) : 0U;
}
static void WIN32_API k32_Sleep(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t ticks;
    if (!milliseconds || !hz) { task_yield(); return; }
    ticks = (uint32_t)(((uint64_t)milliseconds * hz + 999U) / 1000U);
    task_sleep(ticks ? ticks : 1U);
}
static uint32_t WIN32_API k32_SleepEx(uint32_t milliseconds,
                                      int alertable UNUSED) {
    k32_Sleep(milliseconds); return 0U;
}
static void WIN32_API k32_ExitProcess(uint32_t code UNUSED) {
    pe_win32_terminate_current_process();
}

typedef struct {
    uint16_t year, month, day_of_week, day, hour, minute, second, milliseconds;
} win_system_time_t;

static uint32_t win_day_of_week(uint32_t year, uint32_t month, uint32_t day) {
    static const uint8_t offsets[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3U) year--;
    return (year + year / 4U - year / 100U + year / 400U +
            offsets[month ? month - 1U : 0U] + day) % 7U;
}
static void k32_fill_system_time(win_system_time_t *out) {
    rtc_datetime_t now;
    if (!out) return;
    kmemset(out, 0, sizeof(*out));
    if (!rtc_get_datetime(&now)) return;
    out->year = now.date.year; out->month = now.date.month;
    out->day = now.date.day; out->day_of_week =
        (uint16_t)win_day_of_week(now.date.year, now.date.month, now.date.day);
    out->hour = now.time.hour; out->minute = now.time.minute;
    out->second = now.time.second;
}
static void WIN32_API k32_GetLocalTime(win_system_time_t *out) { k32_fill_system_time(out); }
static void WIN32_API k32_GetSystemTime(win_system_time_t *out) { k32_fill_system_time(out); }
static uint64_t k32_days_before_year(uint32_t year) {
    uint64_t y = year - 1U;
    return y * 365U + y / 4U - y / 100U + y / 400U;
}
static int WIN32_API k32_SystemTimeToFileTime(const win_system_time_t *st,
                                              uint64_t *filetime) {
    static const uint16_t month_days[12] =
        {31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t days, seconds;
    if (!st || !filetime || st->year < 1601U || st->month < 1U ||
        st->month > 12U || st->day < 1U) return 0;
    days = k32_days_before_year(st->year) - k32_days_before_year(1601U);
    for (uint32_t m = 1U; m < st->month; m++) {
        days += month_days[m - 1U];
        if (m == 2U && ((st->year % 4U == 0U && st->year % 100U != 0U) ||
                        st->year % 400U == 0U)) days++;
    }
    days += st->day - 1U;
    seconds = days * 86400U + st->hour * 3600U + st->minute * 60U + st->second;
    *filetime = seconds * 10000000U + (uint64_t)st->milliseconds * 10000U;
    return 1;
}
static void WIN32_API k32_GetSystemTimeAsFileTime(uint64_t *filetime) {
    win_system_time_t st; k32_fill_system_time(&st);
    if (!k32_SystemTimeToFileTime(&st, filetime) && filetime) *filetime = 0U;
}
static bool k32_leap(uint32_t year){return(year%4U==0U&&year%100U!=0U)||year%400U==0U;}
static uint64_t k32_u64_div(uint64_t value,uint32_t divisor,uint32_t*remainder){uint64_t quotient=0;uint32_t rem=0;for(int bit=63;bit>=0;bit--){rem=(rem<<1)|(uint32_t)((value>>(uint32_t)bit)&1U);if(rem>=divisor){rem-=divisor;quotient|=(uint64_t)1U<<(uint32_t)bit;}}if(remainder)*remainder=rem;return quotient;}
static int WIN32_API k32_FileTimeToSystemTime(const uint64_t*filetime,win_system_time_t*out){static const uint8_t mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};uint64_t ticks,days,total_seconds;uint32_t seconds=0,year=1601,month=1,day_count,subsecond=0;if(!filetime||!out)return 0;ticks=*filetime;total_seconds=k32_u64_div(ticks,10000000U,&subsecond);days=k32_u64_div(total_seconds,86400U,&seconds);while(year<10000U){uint32_t yd=k32_leap(year)?366U:365U;if(days<yd)break;days-=yd;year++;}if(year>=10000U)return 0;while(month<=12U){day_count=mdays[month-1U]+(month==2U&&k32_leap(year)?1U:0U);if(days<day_count)break;days-=day_count;month++;}kmemset(out,0,sizeof(*out));out->year=(uint16_t)year;out->month=(uint16_t)month;out->day=(uint16_t)(days+1U);out->day_of_week=(uint16_t)win_day_of_week(year,month,(uint32_t)days+1U);out->hour=(uint16_t)(seconds/3600U);seconds%=3600U;out->minute=(uint16_t)(seconds/60U);out->second=(uint16_t)(seconds%60U);out->milliseconds=(uint16_t)(subsecond/10000U);return 1;}
static int WIN32_API k32_FileTimeToLocalFileTime(const uint64_t*source,uint64_t*dest){if(!source||!dest)return 0;*dest=*source;return 1;}
static int WIN32_API k32_LocalFileTimeToFileTime(const uint64_t*source,uint64_t*dest){return k32_FileTimeToLocalFileTime(source,dest);}
static uint32_t WIN32_API k32_GetVersion(void) {
    return 0x80000000U | (2222U << 16) | (10U << 8) | 4U;
}
static int k32_get_version_ex(void *raw, bool wide) {
    uint8_t *info = (uint8_t *)raw;
    uint32_t size;
    if (!info) return 0;
    size = *(uint32_t *)info;
    if (size < (wide ? 148U : 100U)) { pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER); return 0; }
    *(uint32_t *)(info + 4U) = 4U;
    *(uint32_t *)(info + 8U) = 10U;
    *(uint32_t *)(info + 12U) = 2222U;
    *(uint32_t *)(info + 16U) = 1U; /* VER_PLATFORM_WIN32_WINDOWS */
    if (wide) { uint16_t *csd = (uint16_t *)(info + 20U); csd[0] = 'A'; csd[1] = 0; }
    else { info[20U] = 'A'; info[21U] = 0; }
    return 1;
}
static int WIN32_API k32_GetVersionExA(void *raw) { return k32_get_version_ex(raw, false); }
static int WIN32_API k32_GetVersionExW(void *raw) { return k32_get_version_ex(raw, true); }
static uint32_t WIN32_API k32_GetLogicalDrives(void) { return 1U << 2; }
static uint32_t WIN32_API k32_GetDriveTypeA(const char *root UNUSED) { return 3U; }
static int WIN32_API k32_GetDiskFreeSpaceA(const char *root UNUSED,
        uint32_t *sectors_per_cluster, uint32_t *bytes_per_sector,
        uint32_t *free_clusters, uint32_t *total_clusters) {
    uint64_t total = 0, free = 0; const uint32_t cluster = 4096U;
    if (!vfs_get_space(&total, &free)) return 0;
    if (sectors_per_cluster) *sectors_per_cluster = 8U;
    if (bytes_per_sector) *bytes_per_sector = 512U;
    if (free_clusters) *free_clusters = (uint32_t)(free / cluster);
    if (total_clusters) *total_clusters = (uint32_t)(total / cluster);
    return 1;
}
static void WIN32_API k32_GetStartupInfoA(void *raw) {
    /* Layout inherited by the Win32 CRTs through STARTUPINFO.reserved2:
     * descriptor count, one flag byte per descriptor, then HANDLE values. */
    static uint8_t inherited_fds[4U + 3U + 3U * sizeof(uint32_t)];
    uint8_t *info = (uint8_t *)raw;
    if (!info) return;
    kmemset(inherited_fds, 0, sizeof(inherited_fds));
    *(uint32_t *)(inherited_fds + 0U) = 3U;
    inherited_fds[4U] = 0x41U; /* FOPEN | FDEV: stdin */
    inherited_fds[5U] = 0x41U; /* FOPEN | FDEV: stdout */
    inherited_fds[6U] = 0x41U; /* FOPEN | FDEV: stderr */
    *(uint32_t *)(inherited_fds + 7U) = 0U;
    *(uint32_t *)(inherited_fds + 11U) = 1U;
    *(uint32_t *)(inherited_fds + 15U) = 2U;
    kmemset(info, 0, 68U);
    *(uint32_t *)(info + 0U) = 68U;
    *(uint32_t *)(info + 44U) = 0x00000100U; /* STARTF_USESTDHANDLES */
    *(uint16_t *)(info + 50U) = (uint16_t)sizeof(inherited_fds);
    *(void **)(info + 52U) = inherited_fds;
    *(void **)(info + 56U) = (void *)(uintptr_t)0U;
    *(void **)(info + 60U) = (void *)(uintptr_t)1U;
    *(void **)(info + 64U) = (void *)(uintptr_t)2U;
}
static void WIN32_API k32_GetSystemInfo(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    if (!info) return;
    kmemset(info, 0, 36U);
    *(uint16_t *)(info + 0U) = 0U;          /* PROCESSOR_ARCHITECTURE_INTEL */
    *(uint32_t *)(info + 4U) = 4096U;
    *(uint32_t *)(info + 8U) = 0x00010000U;
    *(uint32_t *)(info + 12U) = 0x7FFEFFFFU;
    *(uint32_t *)(info + 16U) = 1U;
    *(uint32_t *)(info + 20U) = 1U;
    *(uint32_t *)(info + 24U) = 586U;
    *(uint32_t *)(info + 28U) = 65536U;
    *(uint16_t *)(info + 32U) = 5U;
}
static char *WIN32_API k32_GetEnvironmentStringsA(void) {
    extern char *win32_compat_get_environment_strings_a(void);
    char *block = win32_compat_get_environment_strings_a();
    pe_win32_set_last_error(block ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return block;
}
static int WIN32_API k32_FreeEnvironmentStringsA(char *block) {
    if (!block) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    kfree(block); pe_win32_set_last_error(0); return 1;
}
static char command_line_fallback[256];
static char *WIN32_API k32_GetCommandLineA(void) {
    const char *line = win32_process_current_command_line();
    if (line) return (char *)line;
    line = task_launch_arg();
    kstrncpy(command_line_fallback, line ? line : "",
             sizeof(command_line_fallback) - 1U);
    command_line_fallback[sizeof(command_line_fallback) - 1U] = '\0';
    return command_line_fallback;
}
static uint32_t WIN32_API k32_GetModuleFileNameA(void *module UNUSED,char *out,uint32_t size) {
    const char *path = win32_process_current_image_path();
    uint32_t len;
    if (!path) path = task_launch_arg();
    if(!out||!size)return 0;
    if(!path)path="";
    /* Win32 applications must never see the kernel's native /mount/path
     * namespace here.  SFX installers reopen argv[0]/GetModuleFileName and
     * seek past the PE image to locate their appended archive. */
    if (path[0] == '/') {
        len = native_to_windows_path(path, NULL, 0U);
        if (size <= len) {
            uint32_t written = 0U;
            if (written + 1U < size) out[written++] = 'C';
            if (written + 1U < size) out[written++] = ':';
            for (const char *scan = path; *scan && written + 1U < size; scan++)
                out[written++] = *scan == '/' ? '\\' : *scan;
            out[written] = '\0';
            pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }
        native_to_windows_path(path, out, size);
        pe_win32_set_last_error(0U);
        return len;
    }
    len=(uint32_t)kstrlen(path);
    if(len>=size){kstrncpy(out,path,size-1U);out[size-1U]='\0';pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);return size;}
    kstrcpy(out,path);pe_win32_set_last_error(0U);return len;
}
extern int win32_compat_set_environment_variable_a(const char *name,
                                                    const char *value);
extern uint32_t win32_compat_get_environment_variable_a(const char *name,
                                                         char *out,
                                                         uint32_t size);
static int WIN32_API k32_SetEnvironmentVariableA(const char *name,
                                                  const char *value) {
    return win32_compat_set_environment_variable_a(name, value);
}
static uint32_t WIN32_API k32_GetEnvironmentVariableA(const char *name,
                                                       char *out,
                                                       uint32_t size) {
    return win32_compat_get_environment_variable_a(name, out, size);
}
static int WIN32_API k32_QueryPerformanceCounter(uint64_t *value) {
    if(!value)return 0;
    *value=(uint64_t)pit_get_ticks();
    return 1;
}
static int WIN32_API k32_QueryPerformanceFrequency(uint64_t *value) {
    if(!value)return 0;
    *value=(uint64_t)pit_get_frequency_hz();
    return 1;
}

static void *WIN32_API k32_VirtualAlloc(void *address, uint32_t size,
                                        uint32_t type, uint32_t protect) {
    uint32_t error = 0U;
    void *memory = win32_vm_alloc(address, size, type, protect, &error);
    pe_win32_set_last_error(error);
    return memory;
}
static int WIN32_API k32_VirtualFree(void *address, uint32_t size,
                                     uint32_t type) {
    uint32_t error = 0U;
    bool ok = win32_vm_free(address, size, type, &error);
    pe_win32_set_last_error(error);
    return ok ? 1 : 0;
}
static int WIN32_API k32_VirtualProtect(void *address, uint32_t size,
                                        uint32_t protect, uint32_t *old) {
    uint32_t error = 0U;
    bool ok = win32_vm_protect(address, size, protect, old, &error);
    pe_win32_set_last_error(error);
    return ok ? 1 : 0;
}

static uint32_t WIN32_API k32_VirtualQuery(const void *address, void *raw_info,
                                            uint32_t length) {
    win32_memory_basic_information_t *info;
    const uint8_t *image_base = NULL;
    uint32_t image_size = 0U;
    if (!raw_info || length < sizeof(win32_memory_basic_information_t)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0U;
    }
    info = (win32_memory_basic_information_t *)raw_info;
    if (address && pe_win32_query_image_region(address, &image_base, &image_size)) {
        kmemset(info, 0, sizeof(*info));
        info->base_address = (void *)image_base;
        info->allocation_base = (void *)image_base;
        info->allocation_protect = PAGE_EXECUTE_READWRITE;
        info->region_size = image_size;
        info->state = MEM_COMMIT;
        info->protect = PAGE_EXECUTE_READWRITE;
        info->type = MEM_IMAGE;
    } else {
        (void)win32_vm_query(address, info);
    }
    pe_win32_set_last_error(0U);
    return (uint32_t)sizeof(*info);
}

static bool k32_process_handle_is_current(void *process) {
    return process == k32_GetCurrentProcess() ||
           process == (void *)(uintptr_t)0xFFFFFFFFU;
}

static uint32_t WIN32_API k32_VirtualQueryEx(void *process, const void *address,
                                              void *raw_info, uint32_t length) {
    if (!k32_process_handle_is_current(process) &&
        !win32_process_handle_is_handle(process)) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0U;
    }
    /* La VM aun comparte tablas fisicas; la consulta cruzada se limita al
       proceso actual hasta que el pager por proceso este activo. */
    if (!k32_process_handle_is_current(process) &&
        win32_process_handle_get_id(process) != task_current_process_id()) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0U;
    }
    return k32_VirtualQuery(address, raw_info, length);
}

static int WIN32_API k32_VirtualProtectEx(void *process, void *address,
                                           uint32_t size, uint32_t protect,
                                           uint32_t *old) {
    if (!k32_process_handle_is_current(process) &&
        !win32_process_handle_is_handle(process)) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!k32_process_handle_is_current(process) &&
        win32_process_handle_get_id(process) != task_current_process_id()) {
        pe_win32_set_last_error(ERROR_ACCESS_DENIED);
        return 0;
    }
    return k32_VirtualProtect(address, size, protect, old);
}
static void *WIN32_API k32_GetProcessHeap(void) { return (void *)(uintptr_t)PROCESS_HEAP_HANDLE; }
static void *WIN32_API k32_HeapCreate(uint32_t options UNUSED,
                                      uint32_t initial UNUSED, uint32_t maximum UNUSED) {
    private_heap_t *heap = (private_heap_t *)kmalloc(sizeof(*heap));
    if (heap) heap->magic = PRIVATE_HEAP_MAGIC;
    pe_win32_set_last_error(heap ? 0U : ERROR_NOT_ENOUGH_MEMORY); return heap;
}
static bool valid_heap(void *handle) {
    return (uint32_t)(uintptr_t)handle == PROCESS_HEAP_HANDLE ||
           (handle && ((private_heap_t *)handle)->magic == PRIVATE_HEAP_MAGIC);
}
static int WIN32_API k32_HeapDestroy(void *handle) {
    if (!handle || (uint32_t)(uintptr_t)handle == PROCESS_HEAP_HANDLE || !valid_heap(handle)) return 0;
    ((private_heap_t *)handle)->magic = 0; kfree(handle); return 1;
}
static void *WIN32_API k32_HeapAlloc(void *heap, uint32_t flags, uint32_t size) {
    if (!valid_heap(heap)) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return NULL; }
    return (flags & HEAP_ZERO_MEMORY) ? kzalloc(size) : kmalloc(size);
}
/* BLES_WINE_HEAP_POINTER_VALIDATION_20260723
 * Rechace punteros ajenos al heap en vez de entregarlos a kfree/krealloc. */
static void *WIN32_API k32_HeapReAlloc(void *heap, uint32_t flags UNUSED,
                                       void *memory, uint32_t size) {
    void *result;
    if (!valid_heap(heap) || !memory || !mm_allocation_size(memory)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    result = krealloc(memory, size);
    pe_win32_set_last_error(result ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return result;
}
static int WIN32_API k32_HeapFree(void *heap, uint32_t flags UNUSED,
                                  void *memory) {
    if (!valid_heap(heap) || !memory || !mm_allocation_size(memory)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    kfree(memory);
    pe_win32_set_last_error(0U);
    return 1;
}
static win_global_block_t *global_from_handle(void *handle, uint32_t *index_out) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    uint32_t index;

    if (value >= GLOBAL_HANDLE_BASE &&
        value < GLOBAL_HANDLE_BASE + WIN32_MAX_GLOBAL_BLOCKS) {
        index = value - GLOBAL_HANDLE_BASE;
        if (win_global_blocks[index].used) {
            if (index_out) *index_out = index;
            return &win_global_blocks[index];
        }
        return NULL;
    }

    for (index = 0; index < WIN32_MAX_GLOBAL_BLOCKS; index++) {
        if (win_global_blocks[index].used &&
            win_global_blocks[index].data == handle) {
            if (index_out) *index_out = index;
            return &win_global_blocks[index];
        }
    }
    return NULL;
}

static void *global_public_handle(win_global_block_t *block, uint32_t index) {
    if (!block) return NULL;
    return block->movable
        ? (void *)(uintptr_t)(GLOBAL_HANDLE_BASE + index)
        : block->data;
}

static void *WIN32_API k32_GlobalAlloc(uint32_t flags, uint32_t size) {
    win_global_block_t *block = NULL;
    uint32_t index;

    /* Wine turns a zero-byte fixed GlobalAlloc into a one-byte allocation;
     * movable zero-byte blocks remain valid discarded handles. */
    if (size == 0U && !(flags & GMEM_MOVEABLE)) size = 1U;

    for (index = 0; index < WIN32_MAX_GLOBAL_BLOCKS; index++) {
        if (!win_global_blocks[index].used) {
            block = &win_global_blocks[index];
            break;
        }
    }
    if (!block) {
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    kmemset(block, 0, sizeof(*block));
    block->used = true;
    block->movable = (flags & GMEM_MOVEABLE) != 0U;
    block->owner_process_id = task_current_process_id();
    block->flags = flags;
    block->size = size;

    if (size != 0U) {
        block->data = (flags & GMEM_ZEROINIT) ? kzalloc(size) : kmalloc(size);
        if (!block->data) {
            kmemset(block, 0, sizeof(*block));
            pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
    } else if (!block->movable) {
        kmemset(block, 0, sizeof(*block));
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    } else {
        block->discarded = true;
    }

    pe_win32_set_last_error(0);
    return global_public_handle(block, index);
}

static void *WIN32_API k32_GlobalLock(void *handle) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    if (!block->data || block->discarded) {
        pe_win32_set_last_error(ERROR_DISCARDED);
        return NULL;
    }
    if (block->movable && block->lock_count != 0xFFU) block->lock_count++;
    pe_win32_set_last_error(0);
    return block->data;
}

static int WIN32_API k32_GlobalUnlock(void *handle) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!block->movable) {
        pe_win32_set_last_error(ERROR_NOT_LOCKED);
        return 0;
    }
    if (block->lock_count == 0U) {
        pe_win32_set_last_error(ERROR_NOT_LOCKED);
        return 0;
    }
    block->lock_count--;
    pe_win32_set_last_error(0);
    return block->lock_count != 0U;
}

static void *WIN32_API k32_GlobalFree(void *handle) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    if (!handle) return NULL;
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return handle;
    }
    if (block->data) kfree(block->data);
    kmemset(block, 0, sizeof(*block));
    pe_win32_set_last_error(0);
    return NULL;
}

bool win32_global_handle_valid(void *handle) {
    return global_from_handle(handle, NULL) != NULL;
}

void win32_global_release_handle(void *handle) {
    if (handle && global_from_handle(handle, NULL)) (void)k32_GlobalFree(handle);
}

void win32_global_transfer_handle(void *handle, uint32_t owner_process_id) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    if (block) block->owner_process_id = owner_process_id;
}

static uint32_t WIN32_API k32_GlobalSize(void *handle);

void *win32_global_alloc_block(uint32_t flags, uint32_t size) {
    return k32_GlobalAlloc(flags, size);
}

void *win32_global_lock_block(void *handle) {
    return k32_GlobalLock(handle);
}

int win32_global_unlock_block(void *handle) {
    return k32_GlobalUnlock(handle);
}

uint32_t win32_global_size_block(void *handle) {
    return k32_GlobalSize(handle);
}

static uint32_t WIN32_API k32_GlobalSize(void *handle) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0U;
    }
    pe_win32_set_last_error(0);
    return block->size;
}

static uint32_t WIN32_API k32_GlobalFlags(void *handle) {
    win_global_block_t *block = global_from_handle(handle, NULL);
    uint32_t result;
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return GMEM_INVALID_HANDLE;
    }
    result = (uint32_t)block->lock_count;
    if (block->flags & GMEM_DISCARDABLE) result |= GMEM_DISCARDABLE;
    if (block->discarded) result |= GMEM_INVALID_HANDLE;
    pe_win32_set_last_error(0);
    return result;
}

static void *WIN32_API k32_GlobalHandle(void *memory) {
    win_global_block_t *block;
    uint32_t index = 0U;
    block = global_from_handle(memory, &index);
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }
    pe_win32_set_last_error(0);
    return global_public_handle(block, index);
}

static void *WIN32_API k32_GlobalReAlloc(void *handle, uint32_t size,
                                          uint32_t flags) {
    win_global_block_t *block;
    uint32_t index = 0U;
    uint32_t old_size;
    void *new_data;

    block = global_from_handle(handle, &index);
    if (!block) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return NULL;
    }

    if (flags & GMEM_MODIFY) {
        block->flags = (block->flags & ~(GMEM_MOVEABLE | GMEM_DISCARDABLE)) |
                       (flags & (GMEM_MOVEABLE | GMEM_DISCARDABLE));
        block->movable = (block->flags & GMEM_MOVEABLE) != 0U;
        pe_win32_set_last_error(0);
        return global_public_handle(block, index);
    }

    if (size == 0U && block->movable) {
        if (block->data) kfree(block->data);
        block->data = NULL;
        block->size = 0U;
        block->discarded = true;
        block->lock_count = 0U;
        pe_win32_set_last_error(0);
        return global_public_handle(block, index);
    }
    if (size == 0U) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    old_size = block->size;
    new_data = block->data ? krealloc(block->data, size)
                           : ((flags & GMEM_ZEROINIT) ? kzalloc(size) : kmalloc(size));
    if (!new_data) {
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if ((flags & GMEM_ZEROINIT) && size > old_size)
        kmemset((uint8_t *)new_data + old_size, 0, size - old_size);

    block->data = new_data;
    block->size = size;
    block->discarded = false;
    block->flags = (block->flags & GMEM_MOVEABLE) | flags;
    pe_win32_set_last_error(0);
    return global_public_handle(block, index);
}

static void *WIN32_API k32_LocalAlloc(uint32_t flags, uint32_t size) {
    return k32_GlobalAlloc(flags, size);
}
static void *WIN32_API k32_LocalLock(void *handle) { return k32_GlobalLock(handle); }
static int WIN32_API k32_LocalUnlock(void *handle) { return k32_GlobalUnlock(handle); }
static void *WIN32_API k32_LocalFree(void *handle) { return k32_GlobalFree(handle); }
static uint32_t WIN32_API k32_LocalSize(void *handle) { return k32_GlobalSize(handle); }
static uint32_t WIN32_API k32_LocalFlags(void *handle) { return k32_GlobalFlags(handle); }
static void *WIN32_API k32_LocalHandle(void *memory) { return k32_GlobalHandle(memory); }
static void *WIN32_API k32_LocalReAlloc(void *handle, uint32_t size, uint32_t flags) {
    return k32_GlobalReAlloc(handle, size, flags);
}
static int WIN32_API k32_lstrlenA(const char *text) { return text ? (int)kstrlen(text) : 0; }
static char *WIN32_API k32_lstrcpyA(char *dst, const char *src) {
    return (dst && src) ? kstrcpy(dst, src) : NULL;
}
static char *WIN32_API k32_lstrcatA(char *dst, const char *src) {
    return (dst && src) ? kstrcat(dst, src) : NULL;
}
static int WIN32_API k32_lstrcmpA(const char *a, const char *b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    return kstrcmp(a, b);
}
static char *WIN32_API k32_lstrcpynA(char *dst, const char *src, int count) {
    if (!dst || !src || count <= 0) return NULL;
    kstrncpy(dst, src, (size_t)count - 1U); dst[count - 1] = '\0'; return dst;
}
static int WIN32_API k32_lstrcmpiA(const char *a, const char *b) {
    uint8_t ca, cb;
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    do { ca=upper((uint8_t)*a++); cb=upper((uint8_t)*b++); if (ca != cb) return (int)ca-(int)cb; } while (ca);
    return 0;
}
static int WIN32_API k32_lstrlenW(const uint16_t *text){int n=0;if(text)while(text[n])n++;return n;}
static uint16_t *WIN32_API k32_lstrcpyW(uint16_t*dst,const uint16_t*src){uint16_t*out=dst;if(!dst)return NULL;if(!src){*dst=0;return out;}while((*dst++=*src++));return out;}
static uint16_t *WIN32_API k32_lstrcpynW(uint16_t*dst,const uint16_t*src,int count){uint16_t*out=dst;if(!dst||count<=0)return dst;while(--count>0&&src&&*src)*dst++=*src++;*dst=0;return out;}
static int WIN32_API k32_lstrcmpW(const uint16_t*a,const uint16_t*b){if(!a)return b?-1:0;if(!b)return 1;while(*a&&*a==*b){a++;b++;}return(int)*a-(int)*b;}
static int WIN32_API k32_lstrcmpiW(const uint16_t*a,const uint16_t*b){uint16_t ca,cb;if(!a)return b?-1:0;if(!b)return 1;do{ca=*a++;cb=*b++;if(ca>='a'&&ca<='z')ca-=32;if(cb>='a'&&cb<='z')cb-=32;if(ca!=cb)return(int)ca-(int)cb;}while(ca);return 0;}
static int WIN32_API k32_GetComputerNameA(char*out,uint32_t*size){const char*name="BLESKERNOS";uint32_t length=10U;if(!size)return 0;if(!out||*size<=length){*size=length+1U;pe_win32_set_last_error(ERROR_MORE_DATA);return 0;}kstrcpy(out,name);*size=length;return 1;}
static uint32_t WIN32_API k32_GetUserDefaultLCID(void){return 0x0409U;}
static uint32_t WIN32_API k32_GetSystemDefaultLCID(void){return 0x0409U;}
static uint32_t WIN32_API k32_GetThreadLocale(void){return 0x0409U;}
static int WIN32_API k32_SetThreadLocale(uint32_t locale UNUSED){return 1;}
static int WIN32_API k32_CompareStringA(uint32_t locale UNUSED,uint32_t flags UNUSED,const char*a,int alen,const char*b,int blen){if(!a||!b)return 0;if(alen<0)alen=(int)kstrlen(a);if(blen<0)blen=(int)kstrlen(b);int length=alen<blen?alen:blen;for(int i=0;i<length;i++){uint8_t ca=upper((uint8_t)a[i]),cb=upper((uint8_t)b[i]);if(ca<cb)return 1;if(ca>cb)return 3;}return alen<blen?1:(alen>blen?3:2);}
static uint32_t WIN32_API k32_GetTempFileNameA(const char*path,const char*prefix,uint32_t unique,char*out){static uint32_t sequence=1U;char native[VFS_MAX_PATH];uint32_t value=unique?unique:sequence++;char hex[9];if(!path||!out)return 0;kstrncpy(out,path,VFS_MAX_PATH-1U);out[VFS_MAX_PATH-1U]=0;if(out[0]&&out[kstrlen(out)-1U]!='\\')kstrcat(out,"\\");if(prefix)for(uint32_t i=0;prefix[i]&&i<3U;i++){size_t n=kstrlen(out);out[n]=prefix[i];out[n+1U]=0;}for(int i=7;i>=0;i--){hex[i]="0123456789ABCDEF"[value&15U];value>>=4;}hex[8]=0;kstrcat(out,hex);kstrcat(out,".TMP");if(!unique&&resolve_win_path(out,native))if(!vfs_write_all(native,"",0U))return 0;return unique?unique:sequence-1U;}
/* BLES_WINE_SETVOLUMELABELA_BATCH_20260723
 *
 * BlesKernOS todavía no tiene una operación VFS para reescribir de forma
 * segura la etiqueta FAT de un volumen montado. La API se publica como
 * compatibilidad para clientes Win9x que la usan como operación opcional.
 */
static int WIN32_API k32_SetVolumeLabelA(const char *root_path,
                                         const char *volume_name) {
    (void)root_path;
    (void)volume_name;
    pe_win32_set_last_error(0U);
    return 1;
}

static int WIN32_API k32_GetVolumeInformationA(const char*root UNUSED,char*name,uint32_t name_size,uint32_t*serial,uint32_t*max_component,uint32_t*flags,char*filesystem,uint32_t filesystem_size){if(name&&name_size){kstrncpy(name,"BLESKERNOS",name_size-1U);name[name_size-1U]=0;}if(serial)*serial=0xB1E59800U;if(max_component)*max_component=255U;if(flags)*flags=0U;if(filesystem&&filesystem_size){kstrncpy(filesystem,"FAT32",filesystem_size-1U);filesystem[filesystem_size-1U]=0;}return 1;}
static int WIN32_API k32_GetDiskFreeSpaceExA(const char*root UNUSED,uint64_t*available,uint64_t*total,uint64_t*free_total){uint64_t t=0,f=0;if(!vfs_get_space(&t,&f))return 0;if(available)*available=f;if(total)*total=t;if(free_total)*free_total=f;return 1;}
static int WIN32_API k32_GetBinaryTypeA(const char*file,uint32_t*type){char path[VFS_MAX_PATH];void*data=NULL;uint32_t size=0;if(!file||!type||!resolve_win_path(file,path)||!vfs_read_all(path,&data,&size))return 0;if(size>=2U&&((uint8_t*)data)[0]=='M'&&((uint8_t*)data)[1]=='Z'){*type=0U;kfree(data);return 1;}kfree(data);return 0;}


typedef struct {
    uint32_t attributes;
    uint32_t creation_low, creation_high;
    uint32_t access_low, access_high;
    uint32_t write_low, write_high;
    uint32_t size_high, size_low;
    uint32_t reserved0, reserved1;
    char file_name[260];
    char alternate_name[14];
} win32_find_data_a_t;

typedef struct {
    uint16_t year, month, day_of_week, day;
    uint16_t hour, minute, second, milliseconds;
} win32_system_time_t;

static bool wildcard_match_ci(const char *pattern, const char *text) {
    if (!pattern || !text) return false;
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return true;
            while (*text) {
                if (wildcard_match_ci(pattern, text)) return true;
                text++;
            }
            return wildcard_match_ci(pattern, text);
        }
        if (*pattern != '?') {
            uint8_t a = upper((uint8_t)*pattern);
            uint8_t b = upper((uint8_t)*text);
            if (a != b) return false;
        } else if (!*text) return false;
        pattern++; text++;
    }
    return *text == '\0';
}

static void fill_find_data(const vfs_dir_entry_t *entry, win32_find_data_a_t *out) {
    if (!entry || !out) return;
    kmemset(out, 0, sizeof(*out));
    out->attributes = entry->type == VFS_NODE_DIR
        ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    out->size_low = entry->size;
    kstrncpy(out->file_name, entry->name, sizeof(out->file_name) - 1U);
    kstrncpy(out->alternate_name, entry->name,
             sizeof(out->alternate_name) - 1U);
}

static win_find_t *find_from_handle(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (value < FIND_HANDLE_BASE || value >= FIND_HANDLE_BASE + WIN32_MAX_FINDS)
        return NULL;
    value -= FIND_HANDLE_BASE;
    return win_finds[value].used ? &win_finds[value] : NULL;
}

static bool find_next_match(win_find_t *find, win32_find_data_a_t *out) {
    while (find && find->index < find->count) {
        vfs_dir_entry_t *entry = &find->entries[find->index++];
        if (!wildcard_match_ci(find->pattern, entry->name)) continue;
        fill_find_data(entry, out);
        return true;
    }
    return false;
}

static void *WIN32_API k32_FindFirstFileA(const char *pattern,
                                           win32_find_data_a_t *out) {
    char native[VFS_MAX_PATH], directory[VFS_MAX_PATH];
    char mask[32];
    char *last;
    if (!pattern || !out || !resolve_win_path(pattern, native)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return (void *)(uintptr_t)0xFFFFFFFFU;
    }
    kstrncpy(directory, native, sizeof(directory) - 1U);
    last = directory;
    for (char *scan = directory; *scan; scan++)
        if (*scan == '/') last = scan;
    if (*last == '/') {
        kstrncpy(mask, last + 1, sizeof(mask) - 1U);
        if (last == directory) directory[1] = '\0';
        else *last = '\0';
    } else {
        kstrncpy(mask, directory, sizeof(mask) - 1U);
        kstrcpy(directory, win_current_directory(true));
    }
    mask[sizeof(mask) - 1U] = '\0';
    if (!mask[0]) kstrcpy(mask, "*");
    for (uint32_t i = 0; i < WIN32_MAX_FINDS; i++) {
        win_find_t *find;
        uint32_t actual = 0;
        if (win_finds[i].used) continue;
        find = &win_finds[i];
        kmemset(find, 0, sizeof(*find));
        find->entries = (vfs_dir_entry_t *)kmalloc(
            sizeof(*find->entries) * VFS_MAX_DIR_ENTRIES);
        if (!find->entries) {
            pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
            return (void *)(uintptr_t)0xFFFFFFFFU;
        }
        if (!vfs_listdir(directory, find->entries, VFS_MAX_DIR_ENTRIES, &actual)) {
            kfree(find->entries);
            find->entries = NULL;
            pe_win32_set_last_error(ERROR_PATH_NOT_FOUND);
            return (void *)(uintptr_t)0xFFFFFFFFU;
        }
        find->used = true;
        find->count = actual;
        kstrncpy(find->pattern, mask, sizeof(find->pattern) - 1U);
        if (!find_next_match(find, out)) {
            kfree(find->entries);
            kmemset(find, 0, sizeof(*find));
            pe_win32_set_last_error(ERROR_FILE_NOT_FOUND);
            return (void *)(uintptr_t)0xFFFFFFFFU;
        }
        pe_win32_set_last_error(0U);
        return (void *)(uintptr_t)(FIND_HANDLE_BASE + i);
    }
    pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    return (void *)(uintptr_t)0xFFFFFFFFU;
}

static int WIN32_API k32_FindNextFileA(void *handle,
                                       win32_find_data_a_t *out) {
    win_find_t *find = find_from_handle(handle);
    if (!find || !out) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0;
    }
    if (!find_next_match(find, out)) {
        pe_win32_set_last_error(ERROR_NO_MORE_FILES); return 0;
    }
    pe_win32_set_last_error(0U); return 1;
}

static int WIN32_API k32_FindClose(void *handle) {
    win_find_t *find = find_from_handle(handle);
    if (!find) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    if (find->entries) kfree(find->entries);
    kmemset(find, 0, sizeof(*find)); pe_win32_set_last_error(0U); return 1;
}

static int WIN32_API k32_SetFileAttributesA(const char *path,
                                             uint32_t attributes UNUSED) {
    return k32_GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? 0 : 1;
}

static int WIN32_API k32_MulDiv(int number, int numerator, int denominator) {
    int64_t value;
    if (!denominator) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return -1; }
    value = (int64_t)number * numerator;
    value += value >= 0 ? denominator / 2 : -(denominator / 2);
    value /= denominator;
    if (value > 0x7FFFFFFFLL || value < -0x80000000LL) return -1;
    return (int)value;
}

typedef struct {
    void *process_handle;
    void *thread_handle;
    uint32_t process_id;
    uint32_t thread_id;
} win32_process_information_t;

/* BLES_WINE_WINEXEC_FONT_FINAL_FIX_20260723
 * CreateProcess must resolve relative executable names against lpCurrentDirectory
 * when supplied, otherwise against the caller's process-local cwd. */
static int WIN32_API k32_CreateProcessA(const char *application,
                                         char *command_line,
                                         void *process_attributes UNUSED,
                                         void *thread_attributes UNUSED,
                                         int inherit_handles UNUSED,
                                         uint32_t creation_flags UNUSED,
                                         void *environment UNUSED,
                                         const char *directory,
                                         void *startup UNUSED,
                                         void *process_info) {
    char path[VFS_MAX_PATH], native[VFS_MAX_PATH], base[VFS_MAX_PATH];
    const char *source = application;
    uint32_t pid = 0U, main_tid = 0U;
    win32_process_information_t *info =
        (win32_process_information_t *)process_info;
    if (!source || !*source) source = command_line;
    if (!source || !*source || !info) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0;
    }
    uint32_t n = 0;
    if (*source == '"') {
        source++;
        while (source[n] && source[n] != '"' && n + 1U < sizeof(path)) {
            path[n] = source[n]; n++;
        }
    } else {
        while (source[n] && source[n] != ' ' && source[n] != '\t' &&
               n + 1U < sizeof(path)) { path[n] = source[n]; n++; }
    }
    path[n] = '\0';
    /* BLES_WINE_WINEXEC_FONT_FINAL_FIX_20260723: make lpCurrentDirectory effective even though the child has not
       been created yet. This is also used by WinExec self-extractors. */
    if (directory && *directory && path[0] != '/' &&
        !(((path[0] >= 'A' && path[0] <= 'Z') ||
           (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')) {
        char dir_native[VFS_MAX_PATH];
        if (!resolve_win_path(directory, dir_native)) {
            pe_win32_set_last_error(ERROR_PATH_NOT_FOUND); return 0;
        }
        kstrncpy(base, dir_native, sizeof(base)-1U); base[sizeof(base)-1U]='\0';
        if (kstrcmp(base,"/") != 0 && kstrlen(base)+1U < sizeof(base)) kstrcat(base,"/");
        char rel[VFS_MAX_PATH];
        if (!win_path(path, rel) || kstrlen(base)+kstrlen(rel) >= sizeof(base)) {
            pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
        }
        uint32_t rel_start=0U; while(rel[rel_start]=='/')rel_start++;
        kstrcat(base,rel+rel_start);
        if (!normalize_native_path(base,native)) {
            pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
        }
    } else if (!resolve_win_path(path, native)) {
        pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
    }
    if (!pe_execute_program_command_line_ex(native,
            command_line && *command_line ? command_line : path, &pid)) {
        if (has_extension(basename(path)) ||
            kstrlen(native) + 4U >= sizeof(native)) {
            pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
        }
        kstrcat(native, ".exe");
        if (!pe_execute_program_command_line_ex(native,
                command_line && *command_line ? command_line : path, &pid)) {
            pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
        }
    }
    /* BLES_WINE_FIXED_VIEW_HANDOFF_EXACT_K32_20260723 */
    if (pid == 0U) {
        kmemset(info, 0, sizeof(*info));
        pe_win32_set_last_error(0U);
        return 1;
    }
    if (!task_query_process(pid, NULL, NULL, &main_tid)) main_tid = pid;
    kmemset(info, 0, sizeof(*info));
    info->process_handle = win32_process_handle_create(pid, false);
    info->thread_handle = win32_process_handle_create(main_tid, true);
    info->process_id = pid;
    info->thread_id = main_tid;
    if (!info->process_handle || !info->thread_handle) {
        if (info->process_handle) win32_process_handle_close(info->process_handle);
        if (info->thread_handle) win32_process_handle_close(info->thread_handle);
        task_request_exit_process(pid, (int32_t)ERROR_NOT_ENOUGH_MEMORY);
        kmemset(info, 0, sizeof(*info));
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    pe_win32_set_last_error(0U); return 1;
}

static void *WIN32_API k32_OpenProcess(uint32_t access UNUSED,
                                        int inherit UNUSED, uint32_t pid) {
    void *handle = win32_process_handle_open(pid);
    pe_win32_set_last_error(handle ? 0U : ERROR_INVALID_PARAMETER);
    return handle;
}
static int WIN32_API k32_TerminateProcess(void *handle, uint32_t exit_code) {
    bool ok;
    if (k32_process_handle_is_current(handle)) {
        pe_win32_set_last_error(0U);
        pe_win32_terminate_current_process();
        return 1;
    }
    ok = win32_process_handle_terminate(handle, exit_code);
    pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
    return ok ? 1 : 0;
}
static int WIN32_API k32_GetExitCodeProcess(void *handle, uint32_t *exit_code) {
    bool ok;
    if (!exit_code) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    if (k32_process_handle_is_current(handle)) {
        *exit_code = WIN32_STILL_ACTIVE;
        pe_win32_set_last_error(0U); return 1;
    }
    ok = win32_process_handle_get_exit_code(handle, exit_code);
    pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
    return ok ? 1 : 0;
}
static uint32_t WIN32_API k32_GetProcessId(void *handle) {
    uint32_t id = k32_process_handle_is_current(handle)
        ? task_current_process_id() : win32_process_handle_get_id(handle);
    pe_win32_set_last_error(id ? 0U : ERROR_INVALID_HANDLE);
    return id;
}

static const char *system_error_text(uint32_t error) {
    switch (error) {
        case 0: return "The operation completed successfully.";
        case ERROR_FILE_NOT_FOUND: return "The system cannot find the file specified.";
        case ERROR_PATH_NOT_FOUND: return "The system cannot find the path specified.";
        case ERROR_ACCESS_DENIED: return "Access is denied.";
        case ERROR_INVALID_HANDLE: return "The handle is invalid.";
        case ERROR_NOT_ENOUGH_MEMORY: return "Not enough memory is available.";
        case ERROR_INVALID_PARAMETER: return "The parameter is incorrect.";
        case ERROR_CALL_NOT_IMPLEMENTED: return "This function is not supported.";
        default: return "Unknown error.";
    }
}

static uint32_t WIN32_API k32_FormatMessageA(uint32_t flags,
                                              const void *source UNUSED,
                                              uint32_t message_id,
                                              uint32_t language UNUSED,
                                              char *buffer,
                                              uint32_t size,
                                              void *arguments UNUSED) {
    const char *text = (flags & FORMAT_MESSAGE_FROM_SYSTEM)
        ? system_error_text(message_id) : NULL;
    uint32_t length;
    if (!text || !buffer || !size) return 0;
    length = (uint32_t)kstrlen(text);
    if (length >= size) length = size - 1U;
    kmemcpy(buffer, text, length); buffer[length] = '\0'; return length;
}

static int copy_locale_string(const char *value, char *out, int size) {
    int length = (int)kstrlen(value) + 1;
    if (!out || size <= 0) return length;
    if (size < length) return 0;
    kstrcpy(out, value); return length;
}

static int WIN32_API k32_GetLocaleInfoA(uint32_t locale UNUSED,
                                         uint32_t type, char *out, int size) {
    switch (type & 0xFFFFU) {
        case 0x0002U: return copy_locale_string("English (United States)", out, size);
        case 0x000EU: return copy_locale_string(".", out, size);
        case 0x000FU: return copy_locale_string(",", out, size);
        case 0x001DU: return copy_locale_string("/", out, size);
        case 0x001EU: return copy_locale_string(":", out, size);
        case 0x001FU: return copy_locale_string("M/d/yyyy", out, size);
        case 0x0020U: return copy_locale_string("dddd, MMMM d, yyyy", out, size);
        case 0x1003U: return copy_locale_string("HH:mm:ss", out, size);
        default: return copy_locale_string("", out, size);
    }
}

static int format_date_value(const win32_system_time_t *time,
                             char *out, int size) {
    uint32_t month = time && time->month ? time->month : 1U;
    uint32_t day = time && time->day ? time->day : 1U;
    uint32_t year = time && time->year ? time->year : 2000U;
    return snprintf(out, (size_t)size, "%02u/%02u/%04u", month, day, year);
}
static int format_time_value(const win32_system_time_t *time,
                             char *out, int size) {
    uint32_t hour = time ? time->hour : 0U;
    uint32_t minute = time ? time->minute : 0U;
    uint32_t second = time ? time->second : 0U;
    return snprintf(out, (size_t)size, "%02u:%02u:%02u", hour, minute, second);
}
static int WIN32_API k32_GetDateFormatA(uint32_t locale UNUSED,
                                         uint32_t flags UNUSED,
                                         const win32_system_time_t *time,
                                         const char *format UNUSED,
                                         char *out, int size) {
    int written;if(!out||size<=0)return 0;written=format_date_value(time,out,size);
    return written>=0&&written<size?written+1:0;
}
static int WIN32_API k32_GetTimeFormatA(uint32_t locale UNUSED,
                                         uint32_t flags UNUSED,
                                         const win32_system_time_t *time,
                                         const char *format UNUSED,
                                         char *out, int size) {
    int written;if(!out||size<=0)return 0;written=format_time_value(time,out,size);
    return written>=0&&written<size?written+1:0;
}

static uint32_t WIN32_API k32_GetACP(void) {
    pe_win32_set_last_error(0U);
    return WIN32_ACP;
}

static uint32_t WIN32_API k32_GetOEMCP(void) {
    pe_win32_set_last_error(0U);
    return WIN32_OEMCP;
}

static bool k32_code_page_supported(uint32_t page) {
    return page == CP_ACP || page == CP_OEMCP || page == CP_MACCP ||
           page == CP_THREAD_ACP || page == CP_SYMBOL ||
           page == WIN32_ACP || page == WIN32_OEMCP ||
           page == CP_UTF7 || page == CP_UTF8;
}

static int WIN32_API k32_IsDBCSLeadByteEx(uint32_t page, uint8_t test_char UNUSED) {
    /* BlesKernOS currently exposes only single-byte ACP/OEM pages and UTF encodings. */
    if (!k32_code_page_supported(page)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 0;
}

static int WIN32_API k32_IsDBCSLeadByte(uint8_t test_char) {
    return k32_IsDBCSLeadByteEx(CP_ACP, test_char);
}

static int WIN32_API k32_MultiByteToWideChar(uint32_t page UNUSED,
                                             uint32_t flags UNUSED,
                                             const char *src, int src_len,
                                             uint16_t *dst, int dst_len) {
    int count = 0;
    if (!src || src_len == 0) return 0;
    if (src_len < 0) { do { count++; } while (src[count - 1]); }
    else count = src_len;
    if (!dst || dst_len == 0) return count;
    if (dst_len < count) return 0;
    for (int i = 0; i < count; i++) dst[i] = (uint8_t)src[i];
    return count;
}
static int WIN32_API k32_WideCharToMultiByte(uint32_t page UNUSED,
                                             uint32_t flags UNUSED,
                                             const uint16_t *src, int src_len,
                                             char *dst, int dst_len,
                                             const char *fallback UNUSED,
                                             int *used_fallback) {
    int count = 0;
    if (!src || src_len == 0) return 0;
    if (src_len < 0) { do { count++; } while (src[count - 1]); }
    else count = src_len;
    if (!dst || dst_len == 0) return count;
    if (dst_len < count) return 0;
    if (used_fallback) *used_fallback = 0;
    for (int i = 0; i < count; i++) {
        if (src[i] > 0xFFU && used_fallback) *used_fallback = 1;
        dst[i] = src[i] <= 0xFFU ? (char)src[i] : '?';
    }
    return count;
}

static uint32_t WIN32_API k32_TlsAlloc(void) {
    uint32_t index = win32_process_tls_alloc();
    if (index == TLS_OUT_OF_INDEXES)
        pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
    else
        pe_win32_set_last_error(0U);
    return index;
}

static int WIN32_API k32_TlsFree(uint32_t index) {
    if (!win32_process_tls_free(index)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 1;
}

static int WIN32_API k32_TlsSetValue(uint32_t index, void *value) {
    if (!win32_process_tls_set(index, value)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    pe_win32_set_last_error(0U);
    return 1;
}

static void *WIN32_API k32_TlsGetValue(uint32_t index) {
    bool valid = false;
    void *value = win32_process_tls_get(index, &valid);
    if (!valid) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    /* Windows limpia LastError incluso cuando el valor valido es NULL. */
    pe_win32_set_last_error(0U);
    return value;
}

static void *WIN32_API k32_CreateThread(void *security UNUSED,
                                            uint32_t stack_size,
                                            win32_thread_start_t start,
                                            void *parameter,
                                            uint32_t creation_flags,
                                            uint32_t *thread_id) {
    void *handle = win32_thread_create(stack_size, start, parameter,
                                       creation_flags, thread_id);
    pe_win32_set_last_error(handle ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return handle;
}

static void WIN32_API k32_ExitThread(uint32_t exit_code) {
    win32_thread_exit(exit_code);
}

static uint32_t win32_wait_milliseconds_now(void) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t scaled;
    if (!hz) return 0U;
    scaled = (uint64_t)pit_get_ticks() * 1000U;
    return (uint32_t)(scaled / hz);
}

static uint32_t WIN32_API k32_WaitForSingleObject(void *handle,
                                                   uint32_t milliseconds) {
    uint32_t result;
    if (win32_process_handle_is_handle(handle))
        result = win32_process_handle_wait(handle, milliseconds);
    else if (win32_thread_is_handle(handle))
        result = win32_thread_wait(handle, milliseconds);
    else
        result = win32_sync_wait(handle, milliseconds);
    pe_win32_set_last_error(result == WIN32_WAIT_FAILED
        ? ERROR_INVALID_HANDLE : 0U);
    return result;
}

static uint32_t WIN32_API k32_WaitForMultipleObjects(uint32_t count,
                                                       void **handles,
                                                       int wait_all,
                                                       uint32_t milliseconds) {
    uint32_t start = win32_wait_milliseconds_now();
    uint32_t tid = task_current_pid();
    if (!handles || count == 0U || count > WIN32_MAXIMUM_WAIT_OBJECTS) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return WIN32_WAIT_FAILED;
    }
    if (wait_all) {
        for (uint32_t i = 0; i < count; i++)
            for (uint32_t j = i + 1U; j < count; j++)
                if (handles[i] == handles[j]) {
                    pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
                    return WIN32_WAIT_FAILED;
                }
    }

    for (;;) {
        bool all_ready = true;
        uint32_t abandoned_index = WIN32_MAXIMUM_WAIT_OBJECTS;
        task_preempt_disable();
        for (uint32_t i = 0; i < count; i++) {
            uint32_t result;
            if (win32_process_handle_is_handle(handles[i]))
                result = win32_process_handle_try_wait(handles[i]);
            else if (win32_thread_is_handle(handles[i]))
                result = win32_thread_try_wait(handles[i]);
            else
                result = win32_sync_try_wait(handles[i], tid, !wait_all);
            if (result == WIN32_WAIT_FAILED) {
                task_preempt_enable();
                pe_win32_set_last_error(ERROR_INVALID_HANDLE);
                return WIN32_WAIT_FAILED;
            }
            if (!wait_all && result != WIN32_WAIT_TIMEOUT) {
                task_preempt_enable();
                pe_win32_set_last_error(0U);
                return (result == WIN32_WAIT_ABANDONED_0
                    ? WIN32_WAIT_ABANDONED_0 : WIN32_WAIT_OBJECT_0) + i;
            }
            if (result == WIN32_WAIT_TIMEOUT) all_ready = false;
            if (result == WIN32_WAIT_ABANDONED_0 &&
                abandoned_index == WIN32_MAXIMUM_WAIT_OBJECTS)
                abandoned_index = i;
        }
        if (wait_all && all_ready) {
            for (uint32_t i = 0; i < count; i++) {
                if (win32_thread_is_handle(handles[i]) ||
                    win32_process_handle_is_handle(handles[i])) continue;
                (void)win32_sync_try_wait(handles[i], tid, true);
            }
            task_preempt_enable();
            pe_win32_set_last_error(0U);
            return abandoned_index != WIN32_MAXIMUM_WAIT_OBJECTS
                ? WIN32_WAIT_ABANDONED_0 + abandoned_index
                : WIN32_WAIT_OBJECT_0;
        }
        task_preempt_enable();

        if (milliseconds == 0U) {
            pe_win32_set_last_error(0U);
            return WIN32_WAIT_TIMEOUT;
        }
        if (milliseconds != WIN32_INFINITE &&
            (uint32_t)(win32_wait_milliseconds_now() - start) >= milliseconds) {
            pe_win32_set_last_error(0U);
            return WIN32_WAIT_TIMEOUT;
        }
        task_sleep(1U);
    }
}

static void *WIN32_API k32_CreateEventA(void *security UNUSED,
                                         int manual_reset,
                                         int initial_state,
                                         const char *name UNUSED) {
    void *handle = win32_sync_create_event(manual_reset != 0,
                                            initial_state != 0);
    pe_win32_set_last_error(handle ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return handle;
}

static void *WIN32_API k32_CreateEventW(void *security UNUSED,
                                         int manual_reset,
                                         int initial_state,
                                         const uint16_t *name UNUSED) {
    return k32_CreateEventA(NULL, manual_reset, initial_state, NULL);
}

static int WIN32_API k32_SetEvent(void *handle) {
    int result = win32_sync_set_event(handle) ? 1 : 0;
    pe_win32_set_last_error(result ? 0U : ERROR_INVALID_HANDLE);
    return result;
}

static int WIN32_API k32_ResetEvent(void *handle) {
    int result = win32_sync_reset_event(handle) ? 1 : 0;
    pe_win32_set_last_error(result ? 0U : ERROR_INVALID_HANDLE);
    return result;
}

static void *WIN32_API k32_CreateMutexA(void *security UNUSED,
                                         int initial_owner,
                                         const char *name UNUSED) {
    void *handle = win32_sync_create_mutex(initial_owner != 0);
    pe_win32_set_last_error(handle ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return handle;
}

static void *WIN32_API k32_CreateMutexW(void *security UNUSED,
                                         int initial_owner,
                                         const uint16_t *name UNUSED) {
    return k32_CreateMutexA(NULL, initial_owner, NULL);
}

static int WIN32_API k32_ReleaseMutex(void *handle) {
    int result = win32_sync_release_mutex(handle) ? 1 : 0;
    pe_win32_set_last_error(result ? 0U : ERROR_NOT_OWNER);
    return result;
}

static void *WIN32_API k32_CreateSemaphoreA(void *security UNUSED,
                                             int32_t initial_count,
                                             int32_t maximum_count,
                                             const char *name UNUSED) {
    void *handle = win32_sync_create_semaphore(initial_count, maximum_count);
    pe_win32_set_last_error(handle ? 0U : ERROR_INVALID_PARAMETER);
    return handle;
}

static void *WIN32_API k32_CreateSemaphoreW(void *security UNUSED,
                                             int32_t initial_count,
                                             int32_t maximum_count,
                                             const uint16_t *name UNUSED) {
    return k32_CreateSemaphoreA(NULL, initial_count, maximum_count, NULL);
}

static int WIN32_API k32_ReleaseSemaphore(void *handle,
                                           int32_t release_count,
                                           int32_t *previous_count) {
    int result = win32_sync_release_semaphore(handle, release_count,
                                               previous_count) ? 1 : 0;
    pe_win32_set_last_error(result ? 0U : ERROR_TOO_MANY_POSTS);
    return result;
}

static void WIN32_API k32_InitializeCriticalSection(void *critical_section) {
    win32_critical_section_initialize(critical_section, 0U);
}

static int WIN32_API k32_InitializeCriticalSectionAndSpinCount(
        void *critical_section, uint32_t spin_count) {
    if (!critical_section) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    win32_critical_section_initialize(critical_section, spin_count);
    pe_win32_set_last_error(0U);
    return 1;
}

static void WIN32_API k32_DeleteCriticalSection(void *critical_section) {
    win32_critical_section_delete(critical_section);
}

static void WIN32_API k32_EnterCriticalSection(void *critical_section) {
    win32_critical_section_enter(critical_section);
}

static int WIN32_API k32_TryEnterCriticalSection(void *critical_section) {
    return win32_critical_section_try_enter(critical_section) ? 1 : 0;
}

static void WIN32_API k32_LeaveCriticalSection(void *critical_section) {
    (void)win32_critical_section_leave(critical_section);
}

static uint32_t WIN32_API k32_SetCriticalSectionSpinCount(
        void *critical_section, uint32_t spin_count) {
    return win32_critical_section_set_spin(critical_section, spin_count);
}

static int32_t WIN32_API k32_InterlockedIncrement(volatile int32_t *value) {
    return win32_interlocked_increment(value);
}

static int32_t WIN32_API k32_InterlockedDecrement(volatile int32_t *value) {
    return win32_interlocked_decrement(value);
}

static int32_t WIN32_API k32_InterlockedExchange(volatile int32_t *target,
                                                  int32_t value) {
    return win32_interlocked_exchange(target, value);
}

static int32_t WIN32_API k32_InterlockedExchangeAdd(volatile int32_t *target,
                                                     int32_t value) {
    return win32_interlocked_exchange_add(target, value);
}

static int32_t WIN32_API k32_InterlockedCompareExchange(
        volatile int32_t *target, int32_t exchange, int32_t compare) {
    return win32_interlocked_compare_exchange(target, exchange, compare);
}

static void WIN32_API k32_RaiseException(uint32_t code, uint32_t flags,
                                         uint32_t count,
                                         const uint32_t *arguments) {
    uint32_t return_eip = (uint32_t)(uintptr_t)__builtin_return_address(0);
    win32_exception_raise(code, flags, count, arguments, return_eip);
}

static void *WIN32_API k32_SetUnhandledExceptionFilter(void *filter) {
    return win32_exception_set_unhandled_filter(filter);
}

static int32_t WIN32_API k32_UnhandledExceptionFilter(
        win32_exception_pointers32_t *pointers) {
    return win32_exception_unhandled_filter(pointers);
}

static void *WIN32_API k32_AddVectoredExceptionHandler(uint32_t first,
                                                        void *handler) {
    return win32_exception_add_vectored(first != 0U, handler, false);
}

static uint32_t WIN32_API k32_RemoveVectoredExceptionHandler(void *cookie) {
    return win32_exception_remove_vectored(cookie, false) ? 1U : 0U;
}

static void *WIN32_API k32_AddVectoredContinueHandler(uint32_t first,
                                                       void *handler) {
    return win32_exception_add_vectored(first != 0U, handler, true);
}

static uint32_t WIN32_API k32_RemoveVectoredContinueHandler(void *cookie) {
    return win32_exception_remove_vectored(cookie, true) ? 1U : 0U;
}

static int WIN32_API k32_GetExitCodeThread(void *handle,
                                            uint32_t *exit_code) {
    int result = win32_thread_get_exit_code(handle, exit_code) ? 1 : 0;
    pe_win32_set_last_error(result ? 0U : ERROR_INVALID_HANDLE);
    return result;
}

static uint32_t WIN32_API k32_GetThreadId(void *handle) {
    uint32_t tid = win32_thread_get_id(handle);
    pe_win32_set_last_error(tid ? 0U : ERROR_INVALID_HANDLE);
    return tid;
}


static bool k32_wide_to_ansi(const uint16_t *wide, char *out, uint32_t size) {
    uint32_t i = 0;
    if (!wide || !out || !size) return false;
    while (wide[i]) {
        if (i + 1U >= size) return false;
        out[i] = wide[i] <= 0xFFU ? (char)wide[i] : '?';
        i++;
    }
    out[i] = '\0';
    return true;
}
static uint32_t k32_ansi_to_wide(const char*ansi,uint16_t*out,uint32_t count){uint32_t length=ansi?(uint32_t)kstrlen(ansi):0U;if(!out||count<=length)return length+1U;for(uint32_t i=0;i<=length;i++)out[i]=(uint8_t)ansi[i];return length;}
static void*WIN32_API k32_CreateFileW(const uint16_t*name,uint32_t access,uint32_t share,void*security,uint32_t creation,uint32_t attributes,void*template_file){char ansi[VFS_MAX_PATH];if(!k32_wide_to_ansi(name,ansi,sizeof(ansi)))return(void*)(uintptr_t)0xFFFFFFFFU;return k32_CreateFileA(ansi,access,share,security,creation,attributes,template_file);}
static void*WIN32_API k32_CreateFileMappingW(void*file,void*security,uint32_t protect,uint32_t high,uint32_t low,const uint16_t*name){(void)name;return k32_CreateFileMappingA(file,security,protect,high,low,NULL);}
static int WIN32_API k32_DeleteFileW(const uint16_t*name){char ansi[VFS_MAX_PATH];return k32_wide_to_ansi(name,ansi,sizeof(ansi))&&k32_DeleteFileA(ansi);}
static int WIN32_API k32_RemoveDirectoryW(const uint16_t*name){char ansi[VFS_MAX_PATH];return k32_wide_to_ansi(name,ansi,sizeof(ansi))&&k32_RemoveDirectoryA(ansi);}
static int WIN32_API k32_CreateDirectoryW(const uint16_t*name,void*security){char ansi[VFS_MAX_PATH];return k32_wide_to_ansi(name,ansi,sizeof(ansi))&&k32_CreateDirectoryA(ansi,security);}
static int WIN32_API k32_MoveFileW(const uint16_t*old_name,const uint16_t*new_name){char old_a[VFS_MAX_PATH],new_a[VFS_MAX_PATH];return k32_wide_to_ansi(old_name,old_a,sizeof(old_a))&&k32_wide_to_ansi(new_name,new_a,sizeof(new_a))&&k32_MoveFileA(old_a,new_a);}
static uint32_t WIN32_API k32_GetFileAttributesW(const uint16_t*name){char ansi[VFS_MAX_PATH];return k32_wide_to_ansi(name,ansi,sizeof(ansi))?k32_GetFileAttributesA(ansi):INVALID_FILE_ATTRIBUTES;}
static uint32_t WIN32_API k32_GetModuleFileNameW(void*module,uint16_t*out,uint32_t size){char ansi[VFS_MAX_PATH];uint32_t length=k32_GetModuleFileNameA(module,ansi,sizeof(ansi));if(!length)return 0;return k32_ansi_to_wide(ansi,out,size);}
static uint32_t WIN32_API k32_GetCurrentDirectoryW(uint32_t size,uint16_t*out){char ansi[VFS_MAX_PATH];uint32_t length=k32_GetCurrentDirectoryA(sizeof(ansi),ansi);if(!length)return 0;return k32_ansi_to_wide(ansi,out,size);}

static void *WIN32_API k32_FindResourceA(void *module, const char *name,
                                         const char *type) {
    return win32_resource_find(module, type, name, 0U, false);
}

static void *WIN32_API k32_FindResourceW(void *module, const uint16_t *name,
                                         const uint16_t *type) {
    return win32_resource_find_w(module, type, name, 0U, false);
}

static void *WIN32_API k32_FindResourceExA(void *module, const char *type,
                                           const char *name,
                                           uint16_t language) {
    return win32_resource_find(module, type, name, language, true);
}

static void *WIN32_API k32_FindResourceExW(void *module, const uint16_t *type,
                                           const uint16_t *name,
                                           uint16_t language) {
    return win32_resource_find_w(module, type, name, language, true);
}

static void *WIN32_API k32_LoadResource(void *module, void *resource) {
    return win32_resource_load(module, resource);
}

static const void *WIN32_API k32_LockResource(void *resource) {
    return win32_resource_lock(resource);
}

static uint32_t WIN32_API k32_SizeofResource(void *module, void *resource) {
    return win32_resource_size(module, resource);
}

static int WIN32_API k32_FreeResource(void *resource) {
    return win32_resource_free(resource) ? 1 : 0;
}

static void *WIN32_API k32_LoadLibraryA(const char *name) {
    builtin_module_t *module;
    uint32_t constant_handle;
    char normalized[VFS_MAX_PATH];
    const char *load_name = name;
    if (!name || !*name) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return NULL; }

    /*
     * LoadLibrary follows the Win32 filename rule: when the last path
     * component has no extension, ".DLL" is appended.  WinZip exercises
     * this directly with LoadLibraryA("SHELL32").  Resolve the normalized
     * spelling first instead of relying on the looser module-name lookup;
     * the same rule also applies to external DLLs.
     */
    {
        const char *base = basename(name);
        uint32_t length = (uint32_t)kstrlen(name);
        if (base && !has_extension(base) &&
            length + 4U < sizeof(normalized)) {
            kstrcpy(normalized, name);
            kstrcat(normalized, ".DLL");
            load_name = normalized;
        }
    }

    /*
     * Dynamic Win9x probes must not depend on the mutable reference table.
     * WinZip loads SHELL32 and COMCTL32 this way during startup.
     */
    constant_handle = builtin_constant_handle(load_name);
    if (constant_handle) {
        kprintf("[WIN32:loader] LoadLibraryA %s -> builtin directo %x\n",
                load_name, constant_handle);
        pe_win32_set_last_error(0);
        return (void *)(uintptr_t)constant_handle;
    }

    module = by_name(load_name);
    if (module && !module->handle) {
        kprintf("[WIN32:loader] builtin invalido %s: handle nulo\n",
                load_name);
        module = NULL;
    }
    if (!module) {
        void *loaded = pe_win32_load_library(load_name);
        if (!loaded) {
            kprintf("[WIN32:loader] LoadLibraryA %s -> no encontrado\n",
                    load_name);
            pe_win32_set_last_error(ERROR_MOD_NOT_FOUND);
        } else {
            kprintf("[WIN32:loader] LoadLibraryA %s -> %x\n", load_name,
                    (uint32_t)(uintptr_t)loaded);
            pe_win32_set_last_error(0);
        }
        return loaded;
    }
    task_preempt_disable(); module->references++; task_preempt_enable();
    kprintf("[WIN32:loader] LoadLibraryA %s -> builtin %x\n", load_name,
            module->handle);
    pe_win32_set_last_error(0); return (void *)(uintptr_t)module->handle;
}

static void *WIN32_API k32_LoadLibraryW(const uint16_t *name) {
    char ansi[VFS_MAX_PATH];
    if (!k32_wide_to_ansi(name, ansi, sizeof(ansi))) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return k32_LoadLibraryA(ansi);
}

static int WIN32_API k32_FreeLibrary(void *handle) {
    if (builtin_constant_name((uint32_t)(uintptr_t)handle)) {
        pe_win32_set_last_error(0);
        return 1;
    }
    builtin_module_t *module = by_handle((uint32_t)(uintptr_t)handle);
    if (!module) {
        if (!pe_win32_free_library(handle)) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0; }
        pe_win32_set_last_error(0); return 1;
    }
    task_preempt_disable(); if (module->references > 1U) module->references--; task_preempt_enable();
    pe_win32_set_last_error(0); return 1;
}
static void *WIN32_API k32_GetModuleHandleA(const char *name) {
    builtin_module_t *module;
    uint32_t image;
    if (!name) {
        image = pe_win32_current_image_base();
        if (!image) pe_win32_set_last_error(ERROR_MOD_NOT_FOUND);
        else pe_win32_set_last_error(0);
        return (void *)(uintptr_t)image;
    }
    image = builtin_constant_handle(name);
    if (image) {
        pe_win32_set_last_error(0);
        return (void *)(uintptr_t)image;
    }
    module = by_name(name);
    if (!module) {
        void *loaded = pe_win32_get_module_handle(name);
        if (!loaded) pe_win32_set_last_error(ERROR_MOD_NOT_FOUND);
        else pe_win32_set_last_error(0);
        return loaded;
    }
    pe_win32_set_last_error(0); return (void *)(uintptr_t)module->handle;
}

static void *WIN32_API k32_GetModuleHandleW(const uint16_t *name) {
    char ansi[VFS_MAX_PATH];
    if (!name) return k32_GetModuleHandleA(NULL);
    if (!k32_wide_to_ansi(name, ansi, sizeof(ansi))) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return k32_GetModuleHandleA(ansi);
}

static void *WIN32_API k32_GetProcAddress(void *handle, const char *name) {
    builtin_module_t *module = by_handle((uint32_t)(uintptr_t)handle);
    const char *constant_module =
        builtin_constant_name((uint32_t)(uintptr_t)handle);
    uint32_t address;
    if (constant_module) {
        if (!name) {
            pe_win32_set_last_error(ERROR_PROC_NOT_FOUND);
            return NULL;
        }
        address = (uint32_t)(uintptr_t)name <= 0xFFFFU
            ? win32_resolve_ordinal(constant_module,
                                    (uint16_t)(uintptr_t)name)
            : pe_win32_resolve_export(constant_module, name);
        if (!address) {
            if ((uint32_t)(uintptr_t)name > 0xFFFFU)
                kprintf("[WIN32:loader] GetProcAddress %s!%s -> no encontrado\n",
                        constant_module, name);
            pe_win32_set_last_error(ERROR_PROC_NOT_FOUND);
            return NULL;
        }
        if ((uint32_t)(uintptr_t)name > 0xFFFFU)
            kprintf("[WIN32:loader] GetProcAddress %s!%s -> %x\n",
                    constant_module, name, address);
        address = elf_user_api_thunk(
            (uint32_t)(uintptr_t)name <= 0xFFFFU ? "ordinal" : name,
            address);
        if ((uint32_t)(uintptr_t)name > 0xFFFFU)
            kprintf("[WIN32:loader] GetProcAddress thunk %s!%s -> %x\n",
                    constant_module, name, address);
        if (!address) {
            pe_win32_set_last_error(ERROR_PROC_NOT_FOUND);
            return NULL;
        }
        pe_win32_set_last_error(0);
        return (void *)(uintptr_t)address;
    }
    if (!module) {
        void *address = ((uint32_t)(uintptr_t)name <= 0xFFFFU)
            ? pe_win32_get_proc_ordinal(handle, (uint16_t)(uintptr_t)name)
            : pe_win32_get_proc_address(handle, name);
        if (!address) pe_win32_set_last_error(ERROR_PROC_NOT_FOUND);
        else pe_win32_set_last_error(0);
        return address;
    }
    if (!name) {
        pe_win32_set_last_error(ERROR_PROC_NOT_FOUND); return NULL;
    }
    if ((uint32_t)(uintptr_t)name <= 0xFFFFU)
        address = win32_resolve_ordinal(module->name,
                    (uint16_t)(uintptr_t)name);
    else
        address = pe_win32_resolve_export(module->name, name);
    if (!address) {
        if ((uint32_t)(uintptr_t)name > 0xFFFFU)
            printf("[WIN32] GetProcAddress no resuelto: %s!%s\n",
                   module->name, name);
        pe_win32_set_last_error(ERROR_PROC_NOT_FOUND); return NULL;
    }
    /* Preserve the resolved export name. elf_user_api_thunk uses it for
     * ABI-specific Ring-3 shims such as the modal DialogBox* loop. */
    address = elf_user_api_thunk(
        (uint32_t)(uintptr_t)name <= 0xFFFFU ? "ordinal" : name, address);
    pe_win32_set_last_error(0); return (void *)(uintptr_t)address;
}

static int WIN32_API k32_SetConsoleCtrlHandler(void *handler,
                                                   int add) {
    uint32_t process_id = task_current_process_id();
    win_console_ctrl_state_t *state;
    uint32_t found = WIN32_MAX_CONSOLE_CTRL_HANDLERS;

    task_preempt_disable();
    state = console_ctrl_state(process_id, add != 0);

    /* A NULL handler toggles the process-level CTRL+C ignore attribute. */
    if (!handler) {
        if (state) {
            state->ignore_ctrl_c = add != 0;
            if (!state->ignore_ctrl_c && state->handler_count == 0U)
                kmemset(state, 0, sizeof(*state));
        }
        task_preempt_enable();
        pe_win32_set_last_error(0U);
        return 1;
    }

    if (add) {
        if (!state || state->handler_count >= WIN32_MAX_CONSOLE_CTRL_HANDLERS) {
            task_preempt_enable();
            pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        /* Windows/Wine call the most recently registered handler first and
         * permit the same callback to be registered more than once. */
        for (uint32_t i = state->handler_count; i > 0U; i--)
            state->handlers[i] = state->handlers[i - 1U];
        state->handlers[0] = handler;
        state->handler_count++;
        task_preempt_enable();
        pe_win32_set_last_error(0U);
        return 1;
    }

    if (state) {
        for (uint32_t i = 0; i < state->handler_count; i++)
            if (state->handlers[i] == handler) {
                found = i;
                break;
            }
    }
    if (!state || found == WIN32_MAX_CONSOLE_CTRL_HANDLERS) {
        task_preempt_enable();
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    for (uint32_t i = found; i + 1U < state->handler_count; i++)
        state->handlers[i] = state->handlers[i + 1U];
    state->handler_count--;
    state->handlers[state->handler_count] = NULL;
    if (state->handler_count == 0U && !state->ignore_ctrl_c)
        kmemset(state, 0, sizeof(*state));
    task_preempt_enable();
    pe_win32_set_last_error(0U);
    return 1;
}


/* BLES_WINE_ISPROCESSORFEATUREPRESENT_20260723 */
static int WIN32_API k32_IsProcessorFeaturePresent(uint32_t feature UNUSED) {
    pe_win32_set_last_error(0U);
    return 0;
}

extern uint32_t win32_profile_resolve(const char *name);

uint32_t win32_kernel32_resolve(const char *name) {
#define EXPORT(api) if (equal_ci(name, #api)) return (uint32_t)(uintptr_t)&k32_##api
    EXPORT(IsProcessorFeaturePresent);
    EXPORT(LoadLibraryA); EXPORT(LoadLibraryW); EXPORT(FreeLibrary);
    EXPORT(GetModuleHandleA); EXPORT(GetModuleHandleW); EXPORT(GetProcAddress);
    EXPORT(FindResourceA); EXPORT(FindResourceW); EXPORT(FindResourceExA); EXPORT(FindResourceExW);
    EXPORT(LoadResource); EXPORT(LockResource); EXPORT(SizeofResource); EXPORT(FreeResource);
    EXPORT(VirtualAlloc); EXPORT(VirtualFree); EXPORT(VirtualProtect); EXPORT(VirtualProtectEx);
    EXPORT(VirtualQuery); EXPORT(VirtualQueryEx);
    EXPORT(GetProcessHeap); EXPORT(HeapCreate); EXPORT(HeapDestroy); EXPORT(HeapAlloc);
    EXPORT(HeapReAlloc); EXPORT(HeapFree);
    EXPORT(GlobalAlloc); EXPORT(GlobalLock); EXPORT(GlobalUnlock); EXPORT(GlobalFree);
    EXPORT(GlobalReAlloc); EXPORT(GlobalSize); EXPORT(GlobalFlags); EXPORT(GlobalHandle);
    EXPORT(LocalAlloc); EXPORT(LocalLock); EXPORT(LocalUnlock); EXPORT(LocalFree);
    EXPORT(LocalReAlloc); EXPORT(LocalSize); EXPORT(LocalFlags); EXPORT(LocalHandle);
    EXPORT(lstrlenA); EXPORT(lstrcpyA); EXPORT(lstrcatA);
    EXPORT(lstrcmpA); EXPORT(lstrcmpiA); EXPORT(lstrcpynA);
    EXPORT(lstrlenW); EXPORT(lstrcpyW); EXPORT(lstrcpynW); EXPORT(lstrcmpW); EXPORT(lstrcmpiW);
    EXPORT(GetACP); EXPORT(GetOEMCP);
    EXPORT(GetComputerNameA); EXPORT(GetUserDefaultLCID); EXPORT(GetSystemDefaultLCID);
    EXPORT(GetThreadLocale); EXPORT(SetThreadLocale); EXPORT(CompareStringA);
    EXPORT(IsDBCSLeadByte); EXPORT(IsDBCSLeadByteEx);
    EXPORT(MultiByteToWideChar); EXPORT(WideCharToMultiByte);
    EXPORT(CreateFileA); EXPORT(CreateFileW); EXPORT(ReadFile); EXPORT(WriteFile); EXPORT(SetFilePointer); EXPORT(GetFileSize);
    EXPORT(CreateFileMappingA); EXPORT(CreateFileMappingW); EXPORT(MapViewOfFile);
    EXPORT(UnmapViewOfFile); EXPORT(FlushViewOfFile);
    EXPORT(CloseHandle); EXPORT(CopyFileA); EXPORT(CreateDirectoryA); EXPORT(FlushFileBuffers); EXPORT(LockFile); EXPORT(UnlockFile);
    EXPORT(DeleteFileA); EXPORT(RemoveDirectoryA); EXPORT(MoveFileA); EXPORT(MoveFileExA);
    EXPORT(DeleteFileW); EXPORT(RemoveDirectoryW); EXPORT(CreateDirectoryW); EXPORT(MoveFileW);
    EXPORT(FindFirstFileA); EXPORT(FindNextFileA); EXPORT(FindClose);
    EXPORT(SetFileAttributesA); EXPORT(CreateProcessA); EXPORT(OpenProcess);
    EXPORT(TerminateProcess); EXPORT(GetExitCodeProcess); EXPORT(GetProcessId); EXPORT(MulDiv);
    EXPORT(GetDateFormatA); EXPORT(GetTimeFormatA); EXPORT(GetLocaleInfoA);
    EXPORT(FormatMessageA);
    EXPORT(SetEndOfFile); EXPORT(GetFileType); EXPORT(GetFileAttributesA); EXPORT(GetFileAttributesW);
    EXPORT(GetCurrentDirectoryA); EXPORT(GetCurrentDirectoryW); EXPORT(SetCurrentDirectoryA); EXPORT(GetFullPathNameA);
    EXPORT(GetTempPathA); EXPORT(GetWindowsDirectoryA); EXPORT(GetSystemDirectoryA);
    EXPORT(GetCommandLineA); EXPORT(GetModuleFileNameA); EXPORT(GetModuleFileNameW);
    EXPORT(GetEnvironmentVariableA); EXPORT(SetEnvironmentVariableA);
    EXPORT(GetEnvironmentStringsA); EXPORT(FreeEnvironmentStringsA);
    EXPORT(GetCurrentProcess); EXPORT(GetCurrentThread); EXPORT(GetCurrentProcessId); EXPORT(GetCurrentThreadId);
    EXPORT(GetLastError); EXPORT(SetLastError); EXPORT(GetTickCount); EXPORT(Sleep); EXPORT(SleepEx); EXPORT(ExitProcess);
    EXPORT(GetLocalTime); EXPORT(GetSystemTime); EXPORT(GetSystemTimeAsFileTime); EXPORT(SystemTimeToFileTime);
    EXPORT(FileTimeToSystemTime); EXPORT(FileTimeToLocalFileTime); EXPORT(LocalFileTimeToFileTime);
    EXPORT(GetVersion); EXPORT(GetVersionExA); EXPORT(GetVersionExW);
    EXPORT(GetLogicalDrives); EXPORT(GetDriveTypeA); EXPORT(GetDiskFreeSpaceA);
    EXPORT(GetDiskFreeSpaceExA); EXPORT(GetVolumeInformationA); EXPORT(SetVolumeLabelA); EXPORT(GetTempFileNameA); EXPORT(GetBinaryTypeA);
    EXPORT(GetStartupInfoA); EXPORT(GetSystemInfo);
    EXPORT(CreateToolhelp32Snapshot); EXPORT(Process32First); EXPORT(Process32Next);
    EXPORT(Process32FirstW); EXPORT(Process32NextW);
    EXPORT(QueryPerformanceCounter); EXPORT(QueryPerformanceFrequency);
    EXPORT(CreateThread); EXPORT(ExitThread); EXPORT(WaitForSingleObject);
    EXPORT(WaitForMultipleObjects); EXPORT(GetExitCodeThread); EXPORT(GetThreadId);
    EXPORT(CreateEventA); EXPORT(CreateEventW); EXPORT(SetEvent); EXPORT(ResetEvent);
    EXPORT(CreateMutexA); EXPORT(CreateMutexW); EXPORT(ReleaseMutex);
    EXPORT(CreateSemaphoreA); EXPORT(CreateSemaphoreW); EXPORT(ReleaseSemaphore);
    EXPORT(InitializeCriticalSection); EXPORT(InitializeCriticalSectionAndSpinCount);
    EXPORT(DeleteCriticalSection); EXPORT(EnterCriticalSection);
    EXPORT(TryEnterCriticalSection); EXPORT(LeaveCriticalSection);
    EXPORT(SetCriticalSectionSpinCount);
    EXPORT(InterlockedIncrement); EXPORT(InterlockedDecrement);
    EXPORT(InterlockedExchange); EXPORT(InterlockedExchangeAdd);
    EXPORT(InterlockedCompareExchange);
    EXPORT(SetConsoleCtrlHandler);
    EXPORT(RaiseException); EXPORT(SetUnhandledExceptionFilter);
    EXPORT(UnhandledExceptionFilter);
    EXPORT(AddVectoredExceptionHandler); EXPORT(RemoveVectoredExceptionHandler);
    EXPORT(AddVectoredContinueHandler); EXPORT(RemoveVectoredContinueHandler);
    EXPORT(TlsAlloc); EXPORT(TlsFree); EXPORT(TlsSetValue); EXPORT(TlsGetValue);
#undef EXPORT
    {
        uint32_t resolved = win32_profile_resolve(name);
        if (resolved) return resolved;
        if (equal_ci(name, "CopyLZFile")) return win32_lz32_resolve("LZCopy");
        return win32_lz32_resolve(name);
    }
}
