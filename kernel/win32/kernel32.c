#include "win32.h"
#include "process.h"
#include "thread.h"
#include "sync.h"
#include "resources.h"
#include "exception.h"
#include "../include/pe_loader.h"
#include "../include/elf_loader.h"
#include "../include/task.h"
#include "../include/memory.h"
#include "../include/vfs.h"
#include "../include/pit.h"
#include "../stdio.h"

#define ERROR_INVALID_HANDLE 6U
#define ERROR_ACCESS_DENIED 5U
#define ERROR_INVALID_PARAMETER 87U
#define ERROR_MOD_NOT_FOUND 126U
#define ERROR_PROC_NOT_FOUND 127U
#define ERROR_NOT_ENOUGH_MEMORY 8U
#define ERROR_FILE_NOT_FOUND 2U
#define ERROR_PATH_NOT_FOUND 3U
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
#define PAGE_EXECUTE_READ 0x20U
#define PAGE_EXECUTE_READWRITE 0x40U
#define HEAP_ZERO_MEMORY 0x00000008U
#define GMEM_MOVEABLE 0x0002U
#define GMEM_ZEROINIT 0x0040U
#define GMEM_MODIFY 0x0080U
#define GMEM_DISCARDABLE 0x0100U
#define GMEM_INVALID_HANDLE 0x8000U
#define ERROR_DISCARDED 157U
#define ERROR_NOT_LOCKED 158U
#define GLOBAL_HANDLE_BASE 0x72000000U
#define WIN32_MAX_GLOBAL_BLOCKS 64U
#define PROCESS_HEAP_HANDLE WIN32_PROCESS_HEAP_HANDLE
#define PRIVATE_HEAP_MAGIC 0x57484550U
#define GENERIC_WRITE 0x40000000U
#define CREATE_NEW 1U
#define CREATE_ALWAYS 2U
#define OPEN_EXISTING 3U
#define OPEN_ALWAYS 4U
#define TRUNCATE_EXISTING 5U
#define FILE_BEGIN 0U
#define FILE_CURRENT 1U
#define FILE_END 2U
#define FILE_HANDLE_BASE 0x71000000U
#define WIN32_MAX_FILES 16U
#define FIND_HANDLE_BASE 0x71100000U
#define WIN32_MAX_FINDS 8U
#define ERROR_NO_MORE_FILES 18U
#define ERROR_CALL_NOT_IMPLEMENTED 120U
#define ERROR_MORE_DATA 234U
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100U
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000U
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200U
#define TLS_OUT_OF_INDEXES WIN32_TLS_OUT_OF_INDEXES

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
    uint32_t flags;
    uint32_t size;
    void *data;
} win_global_block_t;
typedef struct {
    bool used, writable, dirty;
    char path[VFS_MAX_PATH];
    uint8_t *data;
    uint32_t size, capacity, position;
} win_file_t;
static win_file_t win_files[WIN32_MAX_FILES];
static win_global_block_t win_global_blocks[WIN32_MAX_GLOBAL_BLOCKS];
typedef struct {
    bool used;
    uint32_t count, index;
    /* Se reserva al abrir la busqueda: no debe volver a vivir en .bss. */
    vfs_dir_entry_t *entries;
    char pattern[32];
} win_find_t;
static win_find_t win_finds[WIN32_MAX_FINDS];

typedef struct {
    const char *name;
    uint32_t handle;
    uint32_t references;
} builtin_module_t;

static builtin_module_t modules[] = {
    {"NTDLL.DLL", 0x70000000U, 1U},
    {"KERNEL32.DLL", 0x70000001U, 1U},
    {"KERNELBASE.DLL", 0x70000002U, 1U},
    {"USER32.DLL", 0x70000003U, 1U},
    {"GDI32.DLL", 0x70000004U, 1U},
    {"MSVCRT.DLL", 0x70000005U, 1U},
    {"COMCTL32.DLL", 0x70000006U, 1U},
    {"COMDLG32.DLL", 0x70000007U, 1U},
    {"ADVAPI32.DLL", 0x70000008U, 1U},
    {"SHELL32.DLL", 0x70000009U, 1U},
    {"RICHED20.DLL", 0x7000000AU, 1U},
    {"RICHED32.DLL", 0x7000000BU, 1U},
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
static builtin_module_t *by_name(const char *name) {
    const char *base = basename(name);
    for (uint32_t i = 0; i < sizeof(modules) / sizeof(modules[0]); i++)
        if (equal_ci(base, modules[i].name)) return &modules[i];
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
    value -= FILE_HANDLE_BASE; return win_files[value].used ? &win_files[value] : NULL;
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

static bool normalize_native_path(const char *path, char *out) {
    char raw[VFS_MAX_PATH];
    char component[32];
    uint32_t r = 0;
    if (!path || !out || !path[0]) return false;
    kmemset(raw, 0, sizeof(raw));
    kmemset(out, 0, VFS_MAX_PATH);
    if (path[0] == '/') kstrncpy(raw, path, sizeof(raw) - 1U);
    else {
        const char *cwd = vfs_getcwd();
        kstrncpy(raw, cwd && cwd[0] ? cwd : "/", sizeof(raw) - 1U);
        if (kstrcmp(raw, "/") != 0 && kstrlen(raw) + 1U < sizeof(raw)) kstrcat(raw, "/");
        if (kstrlen(raw) + kstrlen(path) >= sizeof(raw)) return false;
        kstrcat(raw, path);
    }
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
static void *WIN32_API k32_CreateFileA(const char *name, uint32_t access,
    uint32_t share UNUSED, void *security UNUSED, uint32_t creation,
    uint32_t attributes UNUSED, void *template_file UNUSED) {
    char path[VFS_MAX_PATH]; void *data=NULL; uint32_t size=0; bool exists;
    if (!win_path(name,path)) return (void *)(uintptr_t)0xFFFFFFFFU;
    exists=vfs_read_all(path,&data,&size);
    if ((creation==OPEN_EXISTING || creation==TRUNCATE_EXISTING) && !exists) return (void *)(uintptr_t)0xFFFFFFFFU;
    if (creation==CREATE_NEW && exists) { kfree(data); return (void *)(uintptr_t)0xFFFFFFFFU; }
    for (uint32_t i=0;i<WIN32_MAX_FILES;i++) if (!win_files[i].used) {
        win_file_t *f=&win_files[i]; kmemset(f,0,sizeof(*f)); f->used=true;
        f->writable=(access&GENERIC_WRITE)!=0; kstrncpy(f->path,path,sizeof(f->path)-1U);
        if (exists && creation!=CREATE_ALWAYS && creation!=TRUNCATE_EXISTING) { f->data=data; f->size=size; f->capacity=size+1U; }
        else { if (data) kfree(data); f->capacity=256U; f->data=(uint8_t *)kzalloc(f->capacity); f->dirty=true; }
        if (!f->data) { kmemset(f,0,sizeof(*f)); return (void *)(uintptr_t)0xFFFFFFFFU; }
        return (void *)(uintptr_t)(FILE_HANDLE_BASE+i);
    }
    if (data) kfree(data);
    return (void *)(uintptr_t)0xFFFFFFFFU;
}
static int WIN32_API k32_ReadFile(void *handle, void *buffer, uint32_t length,
                                  uint32_t *read, void *overlapped UNUSED) {
    win_file_t *f=file_from_handle(handle); uint32_t available;
    if (read) *read=0;
    if (!f || (!buffer&&length)) return 0;
    available=f->position<f->size ? f->size-f->position : 0;
    if (length>available) length=available;
    if (length) kmemcpy(buffer,f->data+f->position,length);
    f->position+=length; if (read) *read=length; return 1;
}
int win32_file_write(void *handle, const void *buffer, uint32_t length, uint32_t *written) {
    win_file_t *f=file_from_handle(handle); uint32_t needed,capacity; uint8_t *grown;
    if (written) *written=0;
    if (!f || !f->writable || (!buffer&&length)) return 0;
    needed=f->position+length; if (needed<f->position) return 0;
    if (needed>f->capacity) { capacity=f->capacity?f->capacity:256U; while(capacity<needed) capacity*=2U;
        grown=(uint8_t *)krealloc(f->data,capacity); if(!grown)return 0; f->data=grown; f->capacity=capacity; }
    if (length) kmemcpy(f->data+f->position,buffer,length);
    f->position=needed;
    if (needed>f->size) f->size=needed;
    f->dirty=true;
    if(written)*written=length;
    return 1;
}
static uint32_t WIN32_API k32_SetFilePointer(void *handle, int32_t low,
                                             int32_t *high UNUSED, uint32_t method) {
    win_file_t *f=file_from_handle(handle); int64_t position;
    if(!f)return 0xFFFFFFFFU;
    position=method==FILE_BEGIN?low:(method==FILE_CURRENT?(int64_t)f->position+low:(int64_t)f->size+low);
    if(position<0 || position>0xFFFFFFFFLL)return 0xFFFFFFFFU;
    f->position=(uint32_t)position;
    return f->position;
}
static uint32_t WIN32_API k32_GetFileSize(void *handle, uint32_t *high) {
    win_file_t *f=file_from_handle(handle); if(high)*high=0; return f?f->size:0xFFFFFFFFU;
}
static int WIN32_API k32_CloseHandle(void *handle) {
    win_file_t *f;
    bool ok = true;
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
    if (f->dirty && f->writable) ok = vfs_write_all(f->path, f->data, f->size);
    kfree(f->data);
    kmemset(f, 0, sizeof(*f));
    pe_win32_set_last_error(ok ? 0U : ERROR_INVALID_HANDLE);
    return ok ? 1 : 0;
}
static int WIN32_API k32_CopyFileA(const char *src,const char *dst,int fail_if_exists) {
    char a[VFS_MAX_PATH],b[VFS_MAX_PATH]; void *data=NULL,*existing=NULL; uint32_t size=0,es=0; bool ok;
    if(!win_path(src,a)||!win_path(dst,b)||!vfs_read_all(a,&data,&size))return 0;
    if(fail_if_exists&&vfs_read_all(b,&existing,&es)){kfree(existing);kfree(data);return 0;}
    ok=vfs_write_all(b,data,size);kfree(data);return ok?1:0;
}
static int WIN32_API k32_CreateDirectoryA(const char *path, void *security UNUSED) {
    char native[VFS_MAX_PATH]; return win_path(path,native)&&vfs_mkdir(native);
}
static uint32_t WIN32_API k32_GetCurrentDirectoryA(uint32_t size, char *out) {
    uint32_t length = native_to_windows_path(vfs_getcwd(), NULL, 0U);
    if (!out || size <= length) {
        pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER);
        return length + 1U;
    }
    native_to_windows_path(vfs_getcwd(), out, size);
    pe_win32_set_last_error(0); return length;
}
static int WIN32_API k32_SetCurrentDirectoryA(const char *path) {
    char native[VFS_MAX_PATH];
    if (!win_path(path, native) || !vfs_chdir(native)) {
        pe_win32_set_last_error(ERROR_PATH_NOT_FOUND); return 0;
    }
    pe_win32_set_last_error(0); return 1;
}
static uint32_t WIN32_API k32_GetFullPathNameA(const char *path, uint32_t size,
                                                char *out, char **file_part) {
    char native[VFS_MAX_PATH], full[VFS_MAX_PATH];
    uint32_t length;
    if (file_part) *file_part = NULL;
    if (!win_path(path, native) || !normalize_native_path(native, full)) {
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
    uint32_t count = 0;
    int fd;
    if (!win_path(path, native)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return INVALID_FILE_ATTRIBUTES;
    }
    if (vfs_listdir(native, &entry, 1U, &count)) {
        pe_win32_set_last_error(0); return FILE_ATTRIBUTE_DIRECTORY;
    }
    fd = vfs_open(native, VFS_O_RDONLY);
    if (fd >= 0) {
        vfs_close(fd); pe_win32_set_last_error(0); return FILE_ATTRIBUTE_ARCHIVE;
    }
    pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES;
}
static uint32_t copy_fixed_path(const char *path, char *out, uint32_t size) {
    uint32_t length = (uint32_t)kstrlen(path);
    if (!out || size <= length) {
        pe_win32_set_last_error(ERROR_INSUFFICIENT_BUFFER); return length + 1U;
    }
    kstrcpy(out, path); pe_win32_set_last_error(0); return length;
}
static uint32_t WIN32_API k32_GetTempPathA(uint32_t size, char *out) {
    (void)vfs_mkdir("/TEMP");
    return copy_fixed_path("C:\\TEMP\\", out, size);
}
static uint32_t WIN32_API k32_GetWindowsDirectoryA(char *out, uint32_t size) {
    return copy_fixed_path("C:\\SYSTEM", out, size);
}
static uint32_t WIN32_API k32_GetSystemDirectoryA(char *out, uint32_t size) {
    return copy_fixed_path("C:\\SYSTEM\\LIBS\\WINE", out, size);
}
static int WIN32_API k32_FlushFileBuffers(void *handle) {
    win_file_t *f = file_from_handle(handle);
    if (!f || !f->writable) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    if (f->dirty && !vfs_write_all(f->path, f->data, f->size)) return 0;
    f->dirty = false; pe_win32_set_last_error(0); return 1;
}
static int WIN32_API k32_SetEndOfFile(void *handle) {
    win_file_t *f = file_from_handle(handle);
    if (!f || !f->writable) { pe_win32_set_last_error(ERROR_INVALID_HANDLE); return 0; }
    f->size = f->position; f->dirty = true; pe_win32_set_last_error(0); return 1;
}
static uint32_t WIN32_API k32_GetFileType(void *handle) {
    uint32_t value = (uint32_t)(uintptr_t)handle;
    if (file_from_handle(handle)) return FILE_TYPE_DISK;
    if (value <= 2U) return FILE_TYPE_CHAR;
    pe_win32_set_last_error(ERROR_INVALID_HANDLE); return FILE_TYPE_UNKNOWN;
}
static void *WIN32_API k32_GetCurrentProcess(void) { return (void *)(uintptr_t)0xFFFFFFFFU; }
static void *WIN32_API k32_GetCurrentThread(void) { return (void *)(uintptr_t)0xFFFFFFFEU; }
static void WIN32_API k32_GetStartupInfoA(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    if (!info) return;
    kmemset(info, 0, 68U);
    *(uint32_t *)(info + 0U) = 68U;
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
    static const char block[] =
        "PATH=C:\\SYSTEM\\WIN32;C:\\SYSTEM\\LIBS\\WINE\0"
        "TEMP=C:\\TEMP\0TMP=C:\\TEMP\0OS=BlesKernOS\0\0";
    char *copy = (char *)kmalloc(sizeof(block));
    if (!copy) { pe_win32_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    kmemcpy(copy, block, sizeof(block)); pe_win32_set_last_error(0); return copy;
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
    len=(uint32_t)kstrlen(path);
    if(len>=size){kstrncpy(out,path,size);return size;} kstrcpy(out,path);return len;
}
static uint32_t WIN32_API k32_GetEnvironmentVariableA(const char *name,char *out,uint32_t size) {
    const char *value=NULL; uint32_t len;
    if(equal_ci(name,"PATH"))value="/SYSTEM/WIN32;/SYSTEM/LIBS/WINE";
    else if(equal_ci(name,"TEMP")||equal_ci(name,"TMP"))value="/TEMP";
    else if(equal_ci(name,"OS"))value="BlesKernOS";
    if(!value)return 0;
    len=(uint32_t)kstrlen(value);
    if(!out||size<=len)return len+1U;
    kstrcpy(out,value);
    return len;
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

static void *WIN32_API k32_VirtualAlloc(void *address UNUSED, uint32_t size,
                                        uint32_t type, uint32_t protect UNUSED) {
    void *memory;
    if (!size || !(type & (MEM_COMMIT | MEM_RESERVE))) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return NULL;
    }
    memory = kzalloc(size);
    pe_win32_set_last_error(memory ? 0U : ERROR_NOT_ENOUGH_MEMORY);
    return memory;
}
static int WIN32_API k32_VirtualFree(void *address, uint32_t size,
                                     uint32_t type) {
    if (!address || type != MEM_RELEASE || size != 0U) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0;
    }
    kfree(address); pe_win32_set_last_error(0); return 1;
}
static int WIN32_API k32_VirtualProtect(void *address, uint32_t size,
                                        uint32_t protect, uint32_t *old) {
    if (!address || !size || !old) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return 0; }
    *old = 0x04U; /* PAGE_READWRITE: no hay paginacion por proceso aun. */
    (void)protect; pe_win32_set_last_error(0); return 1;
}

typedef struct {
    void *base_address;
    void *allocation_base;
    uint32_t allocation_protect;
    uint32_t region_size;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
} win32_memory_basic_information_t;

static uint32_t WIN32_API k32_VirtualQuery(const void *address, void *raw_info,
                                            uint32_t length) {
    win32_memory_basic_information_t *info;
    const uint8_t *image_base = NULL;
    uint32_t image_size = 0U;
    uintptr_t value = (uintptr_t)address;
    uintptr_t page;

    if (!raw_info || length < sizeof(win32_memory_basic_information_t)) {
        pe_win32_set_last_error(ERROR_INVALID_PARAMETER);
        return 0U;
    }

    info = (win32_memory_basic_information_t *)raw_info;
    kmemset(info, 0, sizeof(*info));

    if (address && pe_win32_query_image_region(address, &image_base, &image_size)) {
        info->base_address = (void *)image_base;
        info->allocation_base = (void *)image_base;
        info->allocation_protect = PAGE_EXECUTE_READWRITE;
        info->region_size = image_size;
        info->state = MEM_COMMIT;
        info->protect = PAGE_EXECUTE_READWRITE;
        info->type = MEM_IMAGE;
        pe_win32_set_last_error(0);
        return (uint32_t)sizeof(*info);
    }

    /*
     * Sin paginacion por proceso todavia, el resto del espacio direccionable
     * se describe por paginas de 4 KiB. Esto permite que runtimes Win32
     * consulten codigo incorporado y bloques del heap sin inventar una base
     * de modulo incorrecta.
     */
    page = value & ~(uintptr_t)0xFFFU;
    info->base_address = (void *)page;
    info->region_size = 4096U;
    if (address && value >= 0x00010000U && value < 0x80000000U) {
        info->allocation_base = (void *)page;
        info->allocation_protect = PAGE_EXECUTE_READWRITE;
        info->state = MEM_COMMIT;
        info->protect = PAGE_EXECUTE_READWRITE;
        info->type = MEM_PRIVATE;
    } else {
        info->allocation_base = NULL;
        info->allocation_protect = 0U;
        info->state = MEM_FREE;
        info->protect = PAGE_NOACCESS;
        info->type = 0U;
    }
    pe_win32_set_last_error(0);
    return (uint32_t)sizeof(*info);
}

static uint32_t WIN32_API k32_VirtualQueryEx(void *process, const void *address,
                                              void *raw_info, uint32_t length) {
    if (process != k32_GetCurrentProcess() &&
        process != (void *)(uintptr_t)0xFFFFFFFFU) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
        return 0U;
    }
    return k32_VirtualQuery(address, raw_info, length);
}

static int WIN32_API k32_VirtualProtectEx(void *process, void *address,
                                           uint32_t size, uint32_t protect,
                                           uint32_t *old) {
    if (process != k32_GetCurrentProcess() &&
        process != (void *)(uintptr_t)0xFFFFFFFFU) {
        pe_win32_set_last_error(ERROR_INVALID_HANDLE);
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
static void *WIN32_API k32_HeapReAlloc(void *heap, uint32_t flags UNUSED,
                                       void *memory, uint32_t size) {
    if (!valid_heap(heap) || !memory) return NULL;
    return krealloc(memory, size);
}
static int WIN32_API k32_HeapFree(void *heap, uint32_t flags UNUSED, void *memory) {
    if (!valid_heap(heap) || !memory) return 0;
    kfree(memory);
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
        pe_win32_set_last_error(0);
        return 1;
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
    if (!pattern || !out || !win_path(pattern, native)) {
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
        kstrcpy(directory, vfs_getcwd());
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
    char path[VFS_MAX_PATH], native[VFS_MAX_PATH];
    const char *source = application;
    uint8_t *info = (uint8_t *)process_info;
    if (!source || !*source) source = command_line;
    if (!source || !*source) {
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
    if (directory && *directory) k32_SetCurrentDirectoryA(directory);
    if (!win_path(path, native) ||
        !pe_execute_program_command_line(native,
            command_line && *command_line ? command_line : path)) {
        pe_win32_set_last_error(ERROR_FILE_NOT_FOUND); return 0;
    }
    if (info) kmemset(info, 0, 16U);
    pe_win32_set_last_error(0U); return 1;
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
    if (win32_thread_is_handle(handle))
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
            if (win32_thread_is_handle(handles[i]))
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
                if (win32_thread_is_handle(handles[i])) continue;
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
    if (!name || !*name) { pe_win32_set_last_error(ERROR_INVALID_PARAMETER); return NULL; }
    module = by_name(name);
    if (!module) {
        void *loaded = pe_win32_load_library(name);
        if (!loaded) pe_win32_set_last_error(ERROR_MOD_NOT_FOUND);
        else pe_win32_set_last_error(0);
        return loaded;
    }
    task_preempt_disable(); module->references++; task_preempt_enable();
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
    uint32_t address;
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
    if (!address) { pe_win32_set_last_error(ERROR_PROC_NOT_FOUND); return NULL; }
    address = elf_user_api_thunk("GetProcAddress", address);
    pe_win32_set_last_error(0); return (void *)(uintptr_t)address;
}

extern uint32_t win32_profile_resolve(const char *name);

uint32_t win32_kernel32_resolve(const char *name) {
#define EXPORT(api) if (equal_ci(name, #api)) return (uint32_t)(uintptr_t)&k32_##api
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
    EXPORT(GetACP); EXPORT(GetOEMCP);
    EXPORT(IsDBCSLeadByte); EXPORT(IsDBCSLeadByteEx);
    EXPORT(MultiByteToWideChar); EXPORT(WideCharToMultiByte);
    EXPORT(CreateFileA); EXPORT(ReadFile); EXPORT(SetFilePointer); EXPORT(GetFileSize);
    EXPORT(CloseHandle); EXPORT(CopyFileA); EXPORT(CreateDirectoryA); EXPORT(FlushFileBuffers);
    EXPORT(FindFirstFileA); EXPORT(FindNextFileA); EXPORT(FindClose);
    EXPORT(SetFileAttributesA); EXPORT(CreateProcessA); EXPORT(MulDiv);
    EXPORT(GetDateFormatA); EXPORT(GetTimeFormatA); EXPORT(GetLocaleInfoA);
    EXPORT(FormatMessageA);
    EXPORT(SetEndOfFile); EXPORT(GetFileType); EXPORT(GetFileAttributesA);
    EXPORT(GetCurrentDirectoryA); EXPORT(SetCurrentDirectoryA); EXPORT(GetFullPathNameA);
    EXPORT(GetTempPathA); EXPORT(GetWindowsDirectoryA); EXPORT(GetSystemDirectoryA);
    EXPORT(GetCommandLineA); EXPORT(GetModuleFileNameA); EXPORT(GetEnvironmentVariableA);
    EXPORT(GetEnvironmentStringsA); EXPORT(FreeEnvironmentStringsA);
    EXPORT(GetCurrentProcess); EXPORT(GetCurrentThread); EXPORT(GetStartupInfoA); EXPORT(GetSystemInfo);
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
    EXPORT(RaiseException); EXPORT(SetUnhandledExceptionFilter);
    EXPORT(UnhandledExceptionFilter);
    EXPORT(AddVectoredExceptionHandler); EXPORT(RemoveVectoredExceptionHandler);
    EXPORT(AddVectoredContinueHandler); EXPORT(RemoveVectoredContinueHandler);
    EXPORT(TlsAlloc); EXPORT(TlsFree); EXPORT(TlsSetValue); EXPORT(TlsGetValue);
#undef EXPORT
    return win32_profile_resolve(name);
}
