/*
 * BlesKernOS Win95/98 compatibility fallbacks
 * SPDX-License-Identifier: MIT
 *
 * This is an independent BlesKernOS implementation. API behavior was
 * cross-checked against Wine, but Wine implementation code is not copied here.
 *
 * Covered cumulatively through stage 4:
 *   KERNEL32: atom APIs, SetErrorMode, GetLogicalDriveStrings,
 *             FileTimeToDosDateTime, SearchPath
 *   USER32:   PostThreadMessage, MsgWaitForMultipleObjects
 *   GDI32:    ExtTextOut
 *   SHELL32:  ShellAbout, DragAcceptFiles
 *   WINMM:    basic mixer device opening/capabilities
 */

#include "win32.h"
#include "../include/types.h"
#include "../include/memory.h"
#include "../include/vfs.h"

#ifndef BLES_WIN32_VERBOSE_LEGACY_IO
#define BLES_WIN32_VERBOSE_LEGACY_IO 0
#endif
#include "../include/task.h"
#include "../include/pit.h"
#include "../include/rtc.h"
#include "../include/pe_loader.h"
#include "../stdio.h"

#define COMPAT_VFS_PATH 260U
#define COMPAT_INVALID_FILE_ATTRIBUTES 0xFFFFFFFFU
#define COMPAT_FILE_ATTRIBUTE_DIRECTORY 0x00000010U
#define COMPAT_WAIT_OBJECT_0 0U
#define COMPAT_WAIT_TIMEOUT 258U
#define COMPAT_WAIT_FAILED 0xFFFFFFFFU
#define COMPAT_INFINITE 0xFFFFFFFFU
#define COMPAT_PM_NOREMOVE 0x0000U
#define COMPAT_GWL_EXSTYLE (-20)
#define COMPAT_WS_EX_ACCEPTFILES 0x00000010U
#define COMPAT_MMSYSERR_NOERROR 0U
#define COMPAT_MMSYSERR_BADDEVICEID 2U
#define COMPAT_MMSYSERR_INVALHANDLE 5U
#define COMPAT_MMSYSERR_INVALPARAM 11U
#define COMPAT_MMSYSERR_NOTSUPPORTED 8U
#define COMPAT_MIXER_HANDLE 0x7B4D0001U

static uint8_t compat_upper(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static bool compat_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static bool compat_equal_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (compat_upper((uint8_t)*a) != compat_upper((uint8_t)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static uint32_t compat_copy(char *dst, uint32_t capacity, const char *src) {
    uint32_t length = src ? (uint32_t)kstrlen(src) : 0U;
    if (dst && capacity) {
        uint32_t count = length < capacity - 1U ? length : capacity - 1U;
        if (count) kmemcpy(dst, src, count);
        dst[count] = '\0';
    }
    return length;
}

static bool compat_append(char *dst, uint32_t capacity, const char *src) {
    uint32_t used, length;
    if (!dst || !capacity || !src) return false;
    used = (uint32_t)kstrlen(dst);
    length = (uint32_t)kstrlen(src);
    if (used + length >= capacity) return false;
    kmemcpy(dst + used, src, length + 1U);
    return true;
}

static bool compat_wide_to_ansi(const uint16_t *wide, char *ansi,
                                uint32_t capacity) {
    uint32_t i = 0U;
    if (!wide || !ansi || !capacity) return false;
    while (wide[i]) {
        if (i + 1U >= capacity) return false;
        ansi[i] = wide[i] <= 0x00FFU ? (char)wide[i] : '?';
        i++;
    }
    ansi[i] = '\0';
    return true;
}

static uint32_t compat_ansi_to_wide(const char *ansi, uint16_t *wide,
                                    uint32_t capacity) {
    uint32_t length = ansi ? (uint32_t)kstrlen(ansi) : 0U;
    uint32_t count;
    if (!wide || !capacity) return length;
    count = length < capacity - 1U ? length : capacity - 1U;
    for (uint32_t i = 0U; i < count; i++) wide[i] = (uint8_t)ansi[i];
    wide[count] = 0U;
    return count;
}

/* ------------------------------------------------------------------------- */
/* Atom table. Wine uses native NT atom tables; BlesKernOS keeps a compact
 * process-shared table with the same public return conventions. */

#define COMPAT_ATOM_BASE 0xC000U
#define COMPAT_ATOM_SLOTS 64U
#define COMPAT_ATOM_NAME 64U

typedef struct {
    bool used;
    uint16_t references;
    char name[COMPAT_ATOM_NAME];
} compat_atom_entry_t;

static compat_atom_entry_t compat_atoms[COMPAT_ATOM_SLOTS];

static uint16_t compat_integral_atom_a(const char *name) {
    uintptr_t value = (uintptr_t)name;
    uint32_t number = 0U;
    if ((value >> 16) == 0U) return (uint16_t)value;
    if (!name || name[0] != '#' || !name[1]) return 0U;
    for (uint32_t i = 1U; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') return 0U;
        number = number * 10U + (uint32_t)(name[i] - '0');
        if (number >= COMPAT_ATOM_BASE) return 0U;
    }
    return (uint16_t)number;
}

static uint16_t compat_integral_atom_w(const uint16_t *name) {
    uintptr_t value = (uintptr_t)name;
    uint32_t number = 0U;
    if ((value >> 16) == 0U) return (uint16_t)value;
    if (!name || name[0] != '#' || !name[1]) return 0U;
    for (uint32_t i = 1U; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9') return 0U;
        number = number * 10U + (uint32_t)(name[i] - '0');
        if (number >= COMPAT_ATOM_BASE) return 0U;
    }
    return (uint16_t)number;
}

static int compat_find_atom_name(const char *name) {
    if (!name || !*name) return -1;
    for (uint32_t i = 0U; i < COMPAT_ATOM_SLOTS; i++)
        if (compat_atoms[i].used &&
            compat_equal_ci(compat_atoms[i].name, name)) return (int)i;
    return -1;
}

static uint16_t compat_atom_add_a(const char *name) {
    uint16_t integral = compat_integral_atom_a(name);
    int index;
    if (integral) return integral;
    if (!name || !*name || kstrlen(name) >= COMPAT_ATOM_NAME) return 0U;
    index = compat_find_atom_name(name);
    if (index >= 0) {
        if (compat_atoms[index].references != 0xFFFFU)
            compat_atoms[index].references++;
        return (uint16_t)(COMPAT_ATOM_BASE + (uint32_t)index);
    }
    for (uint32_t i = 0U; i < COMPAT_ATOM_SLOTS; i++) {
        if (compat_atoms[i].used) continue;
        compat_atoms[i].used = true;
        compat_atoms[i].references = 1U;
        kstrncpy(compat_atoms[i].name, name, COMPAT_ATOM_NAME - 1U);
        compat_atoms[i].name[COMPAT_ATOM_NAME - 1U] = '\0';
        return (uint16_t)(COMPAT_ATOM_BASE + i);
    }
    return 0U;
}

static uint16_t compat_atom_find_a(const char *name) {
    uint16_t integral = compat_integral_atom_a(name);
    int index;
    if (integral) return integral;
    index = compat_find_atom_name(name);
    return index >= 0 ? (uint16_t)(COMPAT_ATOM_BASE + (uint32_t)index) : 0U;
}

static uint16_t WIN32_API compat_GlobalAddAtomA(const char *name) {
    return compat_atom_add_a(name);
}

static uint16_t WIN32_API compat_AddAtomA(const char *name) {
    return compat_atom_add_a(name);
}

static uint16_t WIN32_API compat_GlobalAddAtomW(const uint16_t *name) {
    char ansi[COMPAT_ATOM_NAME];
    uint16_t integral = compat_integral_atom_w(name);
    if (integral) return integral;
    return compat_wide_to_ansi(name, ansi, sizeof(ansi))
        ? compat_atom_add_a(ansi) : 0U;
}

static uint16_t WIN32_API compat_AddAtomW(const uint16_t *name) {
    return compat_GlobalAddAtomW(name);
}

static uint16_t WIN32_API compat_GlobalFindAtomA(const char *name) {
    return compat_atom_find_a(name);
}

static uint16_t WIN32_API compat_FindAtomA(const char *name) {
    return compat_atom_find_a(name);
}

static uint16_t WIN32_API compat_GlobalFindAtomW(const uint16_t *name) {
    char ansi[COMPAT_ATOM_NAME];
    uint16_t integral = compat_integral_atom_w(name);
    if (integral) return integral;
    return compat_wide_to_ansi(name, ansi, sizeof(ansi))
        ? compat_atom_find_a(ansi) : 0U;
}

static uint16_t WIN32_API compat_FindAtomW(const uint16_t *name) {
    return compat_GlobalFindAtomW(name);
}

static uint16_t compat_atom_delete(uint16_t atom) {
    uint32_t index;
    if (atom < COMPAT_ATOM_BASE) return 0U;
    index = (uint32_t)atom - COMPAT_ATOM_BASE;
    if (index >= COMPAT_ATOM_SLOTS || !compat_atoms[index].used) return atom;
    if (compat_atoms[index].references > 1U) compat_atoms[index].references--;
    else kmemset(&compat_atoms[index], 0, sizeof(compat_atoms[index]));
    return 0U;
}

static uint16_t WIN32_API compat_GlobalDeleteAtom(uint16_t atom) {
    return compat_atom_delete(atom);
}

static uint16_t WIN32_API compat_DeleteAtom(uint16_t atom) {
    return compat_atom_delete(atom);
}

static uint32_t compat_decimal(uint32_t value, char *out, uint32_t capacity) {
    char reverse[12];
    uint32_t count = 0U;
    if (!out || !capacity) return 0U;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(reverse));
    if (count + 1U > capacity) return 0U;
    for (uint32_t i = 0U; i < count; i++) out[i] = reverse[count - i - 1U];
    out[count] = '\0';
    return count;
}

static uint32_t compat_atom_name_a(uint16_t atom, char *buffer, int count) {
    const char *name;
    char integral[16];
    uint32_t length;
    if (!buffer || count <= 0) return 0U;
    if (atom < COMPAT_ATOM_BASE) {
        integral[0] = '#';
        if (!compat_decimal(atom, integral + 1, sizeof(integral) - 1U)) return 0U;
        name = integral;
    } else {
        uint32_t index = (uint32_t)atom - COMPAT_ATOM_BASE;
        if (index >= COMPAT_ATOM_SLOTS || !compat_atoms[index].used) return 0U;
        name = compat_atoms[index].name;
    }
    length = (uint32_t)kstrlen(name);
    if ((uint32_t)count <= length) {
        if (count > 1) kmemcpy(buffer, name, (uint32_t)count - 1U);
        buffer[count - 1] = '\0';
        return (uint32_t)count - 1U;
    }
    kmemcpy(buffer, name, length + 1U);
    return length;
}

static uint32_t WIN32_API compat_GlobalGetAtomNameA(uint16_t atom,
                                                    char *buffer, int count) {
    return compat_atom_name_a(atom, buffer, count);
}

static uint32_t WIN32_API compat_GetAtomNameA(uint16_t atom,
                                              char *buffer, int count) {
    return compat_atom_name_a(atom, buffer, count);
}

static uint32_t compat_atom_name_w(uint16_t atom, uint16_t *buffer, int count) {
    char ansi[COMPAT_ATOM_NAME];
    uint32_t length = compat_atom_name_a(atom, ansi, sizeof(ansi));
    if (!length || !buffer || count <= 0) return 0U;
    if ((uint32_t)count <= length) length = (uint32_t)count - 1U;
    for (uint32_t i = 0U; i < length; i++) buffer[i] = (uint8_t)ansi[i];
    buffer[length] = 0U;
    return length;
}

static uint32_t WIN32_API compat_GlobalGetAtomNameW(uint16_t atom,
                                                    uint16_t *buffer, int count) {
    return compat_atom_name_w(atom, buffer, count);
}

static uint32_t WIN32_API compat_GetAtomNameW(uint16_t atom,
                                              uint16_t *buffer, int count) {
    return compat_atom_name_w(atom, buffer, count);
}

static int WIN32_API compat_InitAtomTable(uint32_t entries) {
    (void)entries;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Process error mode. */

typedef struct {
    uint32_t process_id;
    uint32_t mode;
} compat_error_mode_t;

static compat_error_mode_t compat_error_modes[TASK_MAX];

static uint32_t WIN32_API compat_SetErrorMode(uint32_t mode) {
    uint32_t process_id = task_current_process_id();
    int free_slot = -1;
    for (uint32_t i = 0U; i < TASK_MAX; i++) {
        if (compat_error_modes[i].process_id == process_id) {
            uint32_t previous = compat_error_modes[i].mode;
            compat_error_modes[i].mode = mode;
            return previous;
        }
        if (!compat_error_modes[i].process_id && free_slot < 0) free_slot = (int)i;
    }
    if (free_slot >= 0) {
        compat_error_modes[free_slot].process_id = process_id;
        compat_error_modes[free_slot].mode = mode;
    }
    return 0U;
}

/* ------------------------------------------------------------------------- */
/* Drive and path helpers. */

static uint32_t WIN32_API compat_GetLogicalDriveStringsA(uint32_t size,
                                                         char *buffer) {
    static const char drives[5] = {'C', ':', '\\', '\0', '\0'};
    if (!buffer || size < sizeof(drives)) return sizeof(drives);
    kmemcpy(buffer, drives, sizeof(drives));
    return sizeof(drives) - 1U;
}

static uint32_t WIN32_API compat_GetLogicalDriveStringsW(uint32_t size,
                                                         uint16_t *buffer) {
    static const uint16_t drives[5] = {'C', ':', '\\', 0U, 0U};
    if (!buffer || size < (uint32_t)(sizeof(drives) / sizeof(drives[0])))
        return (uint32_t)(sizeof(drives) / sizeof(drives[0]));
    kmemcpy(buffer, drives, sizeof(drives));
    return (uint32_t)(sizeof(drives) / sizeof(drives[0])) - 1U;
}

static bool compat_windows_to_native(const char *source, char *native) {
    uint32_t s = 0U, d = 0U;
    if (!source || !*source || !native) return false;
    if (((source[0] >= 'A' && source[0] <= 'Z') ||
         (source[0] >= 'a' && source[0] <= 'z')) && source[1] == ':') s = 2U;
    while (source[s] && d + 1U < COMPAT_VFS_PATH) {
        native[d++] = source[s] == '\\' ? '/' : source[s];
        s++;
    }
    native[d] = '\0';
    return source[s] == '\0' && d != 0U;
}

static bool compat_file_exists(const char *windows_path) {
    char native[COMPAT_VFS_PATH];
    int fd;
    if (!compat_windows_to_native(windows_path, native)) return false;
    fd = vfs_open(native, VFS_O_RDONLY);
    if (fd < 0) return false;
    vfs_close(fd);
    return true;
}

static bool compat_has_path(const char *name) {
    if (!name) return false;
    for (uint32_t i = 0U; name[i]; i++)
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':') return true;
    return false;
}

static bool compat_has_extension(const char *name) {
    int32_t last_separator = -1;
    if (!name) return false;
    for (uint32_t i = 0U; name[i]; i++) {
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':')
            last_separator = (int32_t)i;
        else if (name[i] == '.' && (int32_t)i > last_separator) return true;
    }
    return false;
}

static bool compat_build_filename(const char *file, const char *extension,
                                  char *out, uint32_t capacity) {
    if (!file || !*file || !out || !capacity) return false;
    out[0] = '\0';
    if (!compat_append(out, capacity, file)) return false;
    if (extension && *extension && !compat_has_extension(file)) {
        if (*extension != '.' && !compat_append(out, capacity, ".")) return false;
        if (!compat_append(out, capacity, extension)) return false;
    }
    return true;
}

static bool compat_join_path(const char *directory, const char *file,
                             char *out, uint32_t capacity) {
    uint32_t length;
    if (!directory || !*directory) return compat_build_filename(file, NULL, out, capacity);
    out[0] = '\0';
    if (!compat_append(out, capacity, directory)) return false;
    length = (uint32_t)kstrlen(out);
    if (length && out[length - 1U] != '\\' && out[length - 1U] != '/')
        if (!compat_append(out, capacity, "\\")) return false;
    return compat_append(out, capacity, file);
}

static bool compat_search_list(const char *path_list, const char *file,
                               char *found, uint32_t capacity) {
    uint32_t position = 0U;
    char directory[COMPAT_VFS_PATH];
    char candidate[COMPAT_VFS_PATH];
    if (!path_list) return false;
    while (path_list[position]) {
        uint32_t count = 0U;
        while (path_list[position] == ';') position++;
        while (path_list[position] && path_list[position] != ';') {
            if (count + 1U >= sizeof(directory)) return false;
            directory[count++] = path_list[position++];
        }
        directory[count] = '\0';
        if (count && compat_join_path(directory, file, candidate, sizeof(candidate)) &&
            compat_file_exists(candidate)) {
            compat_copy(found, capacity, candidate);
            return true;
        }
        if (path_list[position] == ';') position++;
    }
    return false;
}

static uint32_t compat_finish_search_path(const char *found, uint32_t size,
                                          char *buffer, char **file_part) {
    uint32_t length = (uint32_t)kstrlen(found);
    if (!buffer || size <= length) return length + 1U;
    kmemcpy(buffer, found, length + 1U);
    if (file_part) {
        char *part = buffer;
        for (uint32_t i = 0U; i < length; i++)
            if (buffer[i] == '\\' || buffer[i] == '/') part = buffer + i + 1U;
        *file_part = part;
    }
    return length;
}

static uint32_t WIN32_API compat_SearchPathA(const char *path,
                                             const char *file,
                                             const char *extension,
                                             uint32_t size,
                                             char *buffer,
                                             char **file_part) {
    static const char *defaults[] = {
        "", "C:\\SYSTEM", "C:\\WINDOWS",
        "C:\\SYSTEM\\LIBS\\WINE", "C:\\SYSTEM\\LIBS\\WIN32"
    };
    char filename[COMPAT_VFS_PATH];
    char candidate[COMPAT_VFS_PATH];
    char found[COMPAT_VFS_PATH];
    found[0] = '\0';
    if (file_part) *file_part = NULL;
    if (!compat_build_filename(file, extension, filename, sizeof(filename))) return 0U;

    if (compat_has_path(filename)) {
        if (!compat_file_exists(filename)) return 0U;
        compat_copy(found, sizeof(found), filename);
    } else if (path && *path) {
        if (!compat_search_list(path, filename, found, sizeof(found))) return 0U;
    } else {
        for (uint32_t i = 0U; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
            if (!defaults[i][0]) compat_copy(candidate, sizeof(candidate), filename);
            else if (!compat_join_path(defaults[i], filename, candidate,
                                       sizeof(candidate))) continue;
            if (compat_file_exists(candidate)) {
                compat_copy(found, sizeof(found), candidate);
                break;
            }
        }
        if (!found[0]) return 0U;
    }
    return compat_finish_search_path(found, size, buffer, file_part);
}

static uint32_t WIN32_API compat_SearchPathW(const uint16_t *path,
                                             const uint16_t *file,
                                             const uint16_t *extension,
                                             uint32_t size,
                                             uint16_t *buffer,
                                             uint16_t **file_part) {
    char path_a[COMPAT_VFS_PATH], file_a[COMPAT_VFS_PATH];
    char extension_a[64], result_a[COMPAT_VFS_PATH];
    uint32_t length;
    if (file_part) *file_part = NULL;
    if (!file || !compat_wide_to_ansi(file, file_a, sizeof(file_a))) return 0U;
    if (path && !compat_wide_to_ansi(path, path_a, sizeof(path_a))) return 0U;
    if (extension &&
        !compat_wide_to_ansi(extension, extension_a, sizeof(extension_a))) return 0U;
    length = compat_SearchPathA(path ? path_a : NULL, file_a,
                                extension ? extension_a : NULL,
                                sizeof(result_a), result_a, NULL);
    if (!length || length >= sizeof(result_a)) return length;
    if (!buffer || size <= length) return length + 1U;
    compat_ansi_to_wide(result_a, buffer, size);
    if (file_part) {
        uint16_t *part = buffer;
        for (uint32_t i = 0U; i < length; i++)
            if (buffer[i] == '\\' || buffer[i] == '/') part = buffer + i + 1U;
        *file_part = part;
    }
    return length;
}

/* ------------------------------------------------------------------------- */
/* FILETIME conversion. Prefer an existing kernel32 conversion, with the
 * current local time as a compatibility fallback for legacy extractors. */

typedef struct {
    uint32_t low;
    uint32_t high;
} compat_filetime_t;

typedef struct {
    uint16_t year, month, day_of_week, day;
    uint16_t hour, minute, second, milliseconds;
} compat_systemtime_t;

static int WIN32_API compat_FileTimeToDosDateTime(const compat_filetime_t *filetime,
                                                  uint16_t *fat_date,
                                                  uint16_t *fat_time) {
    typedef int (WIN32_API *convert_t)(const compat_filetime_t *,
                                       compat_systemtime_t *);
    typedef void (WIN32_API *local_time_t)(compat_systemtime_t *);
    convert_t convert;
    local_time_t get_local;
    compat_systemtime_t time;
    if (!filetime || !fat_date || !fat_time) return 0;
    kmemset(&time, 0, sizeof(time));
    convert = (convert_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "FileTimeToSystemTime");
    if (!convert || !convert(filetime, &time)) {
        get_local = (local_time_t)(uintptr_t)
            win32_resolve_import("KERNEL32.DLL", "GetLocalTime");
        if (!get_local) return 0;
        get_local(&time);
    }
    if (time.year < 1980U || time.year > 2107U ||
        time.month < 1U || time.month > 12U ||
        time.day < 1U || time.day > 31U ||
        time.hour > 23U || time.minute > 59U || time.second > 59U) return 0;
    *fat_date = (uint16_t)(((time.year - 1980U) << 9) |
                           (time.month << 5) | time.day);
    *fat_time = (uint16_t)((time.hour << 11) |
                           (time.minute << 5) | (time.second / 2U));
    return 1;
}

/* ------------------------------------------------------------------------- */
/* USER32 message waiting. BlesKernOS currently has a process-wide queue, so
 * PostThreadMessage posts to that queue while preserving Win32 call shape. */

typedef struct {
    void *hwnd;
    uint32_t message;
    uint32_t wparam;
    int32_t lparam;
    uint32_t time;
    int32_t x;
    int32_t y;
} compat_msg_t;

static int WIN32_API compat_PostThreadMessageA(uint32_t thread_id,
                                               uint32_t message,
                                               uint32_t wparam,
                                               int32_t lparam) {
    typedef int (WIN32_API *post_t)(void *, uint32_t, uint32_t, int32_t);
    post_t post = (post_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "PostMessageA");
    (void)thread_id;
    return post ? post(NULL, message, wparam, lparam) : 0;
}

static int WIN32_API compat_PostThreadMessageW(uint32_t thread_id,
                                               uint32_t message,
                                               uint32_t wparam,
                                               int32_t lparam) {
    return compat_PostThreadMessageA(thread_id, message, wparam, lparam);
}

static uint32_t WIN32_API compat_MsgWaitForMultipleObjects(
    uint32_t count, void *const *handles, int wait_all,
    uint32_t milliseconds, uint32_t wake_mask) {
    typedef int (WIN32_API *peek_t)(compat_msg_t *, void *,
                                    uint32_t, uint32_t, uint32_t);
    typedef uint32_t (WIN32_API *wait_t)(uint32_t, void *const *,
                                         int, uint32_t);
    typedef uint32_t (WIN32_API *ticks_t)(void);
    peek_t peek = (peek_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "PeekMessageA");
    wait_t wait = (wait_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "WaitForMultipleObjects");
    ticks_t ticks = (ticks_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetTickCount");
    compat_msg_t message;
    uint32_t start = ticks ? ticks() : 0U;
    uint32_t fallback_elapsed = 0U;
    (void)wake_mask;

    if (count && (!handles || !wait)) return COMPAT_WAIT_FAILED;
    for (;;) {
        if (peek && peek(&message, NULL, 0U, 0U, COMPAT_PM_NOREMOVE))
            return COMPAT_WAIT_OBJECT_0 + count;
        if (count) {
            uint32_t result = wait(count, handles, wait_all, 0U);
            if (result != COMPAT_WAIT_TIMEOUT) return result;
        }
        if (milliseconds == 0U) return COMPAT_WAIT_TIMEOUT;
        if (milliseconds != COMPAT_INFINITE) {
            if (ticks) {
                if ((uint32_t)(ticks() - start) >= milliseconds)
                    return COMPAT_WAIT_TIMEOUT;
            } else if (fallback_elapsed++ >= milliseconds) {
                return COMPAT_WAIT_TIMEOUT;
            }
        }
        task_sleep(1U);
    }
}

static uint32_t WIN32_API compat_MsgWaitForMultipleObjectsEx(
    uint32_t count, void *const *handles, uint32_t milliseconds,
    uint32_t wake_mask, uint32_t flags) {
    return compat_MsgWaitForMultipleObjects(
        count, handles, (flags & 0x00000001U) != 0U,
        milliseconds, wake_mask);
}

/* ------------------------------------------------------------------------- */
/* GDI and shell wrappers. */

static int WIN32_API compat_ExtTextOutA(void *dc, int x, int y,
                                        uint32_t options, const int32_t *rect,
                                        const char *text, uint32_t count,
                                        const int32_t *spacing) {
    typedef int (WIN32_API *text_out_t)(void *, int, int,
                                        const char *, int);
    text_out_t text_out = (text_out_t)(uintptr_t)
        win32_resolve_import("GDI32.DLL", "TextOutA");
    (void)options;
    (void)rect;
    (void)spacing;
    if (!count) return 1;
    return text_out && text ? text_out(dc, x, y, text, (int)count) : 0;
}

static int WIN32_API compat_ExtTextOutW(void *dc, int x, int y,
                                        uint32_t options, const int32_t *rect,
                                        const uint16_t *text, uint32_t count,
                                        const int32_t *spacing) {
    typedef int (WIN32_API *text_out_t)(void *, int, int,
                                        const uint16_t *, int);
    text_out_t text_out = (text_out_t)(uintptr_t)
        win32_resolve_import("GDI32.DLL", "TextOutW");
    (void)options;
    (void)rect;
    (void)spacing;
    if (!count) return 1;
    return text_out && text ? text_out(dc, x, y, text, (int)count) : 0;
}

static void WIN32_API compat_DragAcceptFiles(void *window, int accept) {
    typedef int (WIN32_API *is_window_t)(void *);
    typedef int32_t (WIN32_API *get_long_t)(void *, int);
    typedef int32_t (WIN32_API *set_long_t)(void *, int, int32_t);
    is_window_t is_window = (is_window_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "IsWindow");
    get_long_t get_long = (get_long_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "GetWindowLongA");
    set_long_t set_long = (set_long_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "SetWindowLongA");
    int32_t style;
    if (!is_window || !get_long || !set_long || !is_window(window)) return;
    style = get_long(window, COMPAT_GWL_EXSTYLE);
    if (accept) style |= (int32_t)COMPAT_WS_EX_ACCEPTFILES;
    else style &= ~(int32_t)COMPAT_WS_EX_ACCEPTFILES;
    set_long(window, COMPAT_GWL_EXSTYLE, style);
}

static int WIN32_API compat_ShellAboutA(void *owner, const char *application,
                                        const char *other, void *icon) {
    typedef int (WIN32_API *message_box_t)(void *, const char *,
                                           const char *, uint32_t);
    message_box_t message_box = (message_box_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "MessageBoxA");
    char caption[128];
    char text[512];
    uint32_t i = 0U;
    const char *details = NULL;
    (void)icon;
    if (!message_box) return 0;
    if (!application || !*application) application = "BlesKernOS";
    while (application[i] && application[i] != '#' &&
           i + 1U < sizeof(caption)) {
        caption[i] = application[i];
        i++;
    }
    caption[i] = '\0';
    if (application[i] == '#') details = application + i + 1U;
    text[0] = '\0';
    if (details && *details) compat_append(text, sizeof(text), details);
    if (other && *other) {
        if (text[0]) compat_append(text, sizeof(text), "\n\n");
        compat_append(text, sizeof(text), other);
    }
    if (!text[0]) compat_append(text, sizeof(text), caption);
    return message_box(owner, text, caption[0] ? caption : "About", 0U) != 0;
}

static int WIN32_API compat_ShellAboutW(void *owner,
                                        const uint16_t *application,
                                        const uint16_t *other, void *icon) {
    char application_a[128], other_a[384];
    if (application &&
        !compat_wide_to_ansi(application, application_a, sizeof(application_a)))
        return 0;
    if (other && !compat_wide_to_ansi(other, other_a, sizeof(other_a))) return 0;
    return compat_ShellAboutA(owner, application ? application_a : NULL,
                              other ? other_a : NULL, icon);
}

/* ------------------------------------------------------------------------- */
/* Minimal virtual mixer. It exposes one mixer endpoint so old control panels
 * can enumerate audio even before the full mixer-control graph is present. */

typedef struct {
    uint16_t manufacturer_id;
    uint16_t product_id;
    uint32_t driver_version;
    char product_name[32];
    uint32_t support;
    uint32_t destinations;
} compat_mixer_caps_a_t;

typedef struct {
    uint16_t manufacturer_id;
    uint16_t product_id;
    uint32_t driver_version;
    uint16_t product_name[32];
    uint32_t support;
    uint32_t destinations;
} compat_mixer_caps_w_t;

static uint32_t WIN32_API compat_mixerGetNumDevs(void) {
    return 1U;
}

static uint32_t WIN32_API compat_mixerOpen(void **mixer, uint32_t device_id,
                                           uintptr_t callback,
                                           uintptr_t instance,
                                           uint32_t flags) {
    (void)callback;
    (void)instance;
    (void)flags;
    if (!mixer) return COMPAT_MMSYSERR_INVALPARAM;
    if (device_id != 0U && device_id != 0xFFFFFFFFU)
        return COMPAT_MMSYSERR_BADDEVICEID;
    *mixer = (void *)(uintptr_t)COMPAT_MIXER_HANDLE;
    return COMPAT_MMSYSERR_NOERROR;
}

static uint32_t WIN32_API compat_mixerClose(void *mixer) {
    return (uint32_t)(uintptr_t)mixer == COMPAT_MIXER_HANDLE
        ? COMPAT_MMSYSERR_NOERROR : COMPAT_MMSYSERR_INVALHANDLE;
}

static uint32_t WIN32_API compat_mixerGetID(void *mixer_object,
                                            uint32_t *device_id,
                                            uint32_t flags) {
    (void)flags;
    if (!device_id) return COMPAT_MMSYSERR_INVALPARAM;
    if (mixer_object &&
        (uint32_t)(uintptr_t)mixer_object != COMPAT_MIXER_HANDLE)
        return COMPAT_MMSYSERR_INVALHANDLE;
    *device_id = 0U;
    return COMPAT_MMSYSERR_NOERROR;
}

static uint32_t WIN32_API compat_mixerGetDevCapsA(uintptr_t device,
                                                  void *raw_caps,
                                                  uint32_t size) {
    compat_mixer_caps_a_t caps;
    uint32_t copy;
    if ((uint32_t)device != 0U && (uint32_t)device != COMPAT_MIXER_HANDLE)
        return COMPAT_MMSYSERR_BADDEVICEID;
    if (!raw_caps || !size) return COMPAT_MMSYSERR_INVALPARAM;
    kmemset(&caps, 0, sizeof(caps));
    caps.driver_version = 0x00010000U;
    compat_copy(caps.product_name, sizeof(caps.product_name),
                "BlesKernOS Audio Mixer");
    caps.destinations = 1U;
    copy = size < sizeof(caps) ? size : sizeof(caps);
    kmemcpy(raw_caps, &caps, copy);
    return COMPAT_MMSYSERR_NOERROR;
}

static uint32_t WIN32_API compat_mixerGetDevCapsW(uintptr_t device,
                                                  void *raw_caps,
                                                  uint32_t size) {
    compat_mixer_caps_w_t caps;
    uint32_t copy;
    if ((uint32_t)device != 0U && (uint32_t)device != COMPAT_MIXER_HANDLE)
        return COMPAT_MMSYSERR_BADDEVICEID;
    if (!raw_caps || !size) return COMPAT_MMSYSERR_INVALPARAM;
    kmemset(&caps, 0, sizeof(caps));
    caps.driver_version = 0x00010000U;
    compat_ansi_to_wide("BlesKernOS Audio Mixer", caps.product_name,
                        sizeof(caps.product_name) / sizeof(caps.product_name[0]));
    caps.destinations = 1U;
    copy = size < sizeof(caps) ? size : sizeof(caps);
    kmemcpy(raw_caps, &caps, copy);
    return COMPAT_MMSYSERR_NOERROR;
}

static uint32_t WIN32_API compat_mixerMessage(void *mixer, uint32_t message,
                                               uintptr_t parameter1,
                                               uintptr_t parameter2) {
    (void)mixer;
    (void)message;
    (void)parameter1;
    (void)parameter2;
    return COMPAT_MMSYSERR_NOTSUPPORTED;
}


/* ========================================================================= */
/* Stage 4: wider Windows 95/98 compatibility surface.                       */

#define COMPAT_GENERIC_READ 0x80000000U
#define COMPAT_GENERIC_WRITE 0x40000000U
#define COMPAT_OPEN_EXISTING 3U
#define COMPAT_CREATE_ALWAYS 2U
#define COMPAT_FILE_BEGIN 0U
#define COMPAT_FILE_CURRENT 1U
#define COMPAT_FILE_END 2U
#define COMPAT_INVALID_HANDLE_VALUE ((void *)(uintptr_t)0xFFFFFFFFU)
#define COMPAT_GMEM_MOVEABLE 0x0002U
#define COMPAT_MAX_ENV 32U
#define COMPAT_ENV_NAME 32U
#define COMPAT_ENV_VALUE 256U
#define COMPAT_STD_INPUT_HANDLE  ((uint32_t)-10)
#define COMPAT_STD_OUTPUT_HANDLE ((uint32_t)-11)
#define COMPAT_STD_ERROR_HANDLE  ((uint32_t)-12)
#define COMPAT_VER_PLATFORM_WIN32_WINDOWS 1U
#define COMPAT_LANG_EN_US 0x0409U
#define COMPAT_HKL_EN_US 0x04090409U
#define COMPAT_CSTR_LESS_THAN 1
#define COMPAT_CSTR_EQUAL 2
#define COMPAT_CSTR_GREATER_THAN 3
#define COMPAT_NORM_IGNORECASE 0x00000001U
#define COMPAT_LCMAP_LOWERCASE 0x00000100U
#define COMPAT_LCMAP_UPPERCASE 0x00000200U
#define COMPAT_SHFILEOP_COPY 0x0002U
#define COMPAT_SHFILEOP_MOVE 0x0001U
#define COMPAT_SHFILEOP_DELETE 0x0003U
#define COMPAT_SHFILEOP_RENAME 0x0004U
#define COMPAT_FILE_ACTION_ADDED 0x00000001U
#define COMPAT_SHELL_SUCCESS 33U
#define COMPAT_MAX_PATH 260U
#define COMPAT_MMSYSERR_ERROR 1U
#define COMPAT_WAVERR_BADFORMAT 32U

bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms);

static uint32_t compat_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void compat_set_last_error(uint32_t error) {
    pe_win32_set_last_error(error);
}

/* ------------------------------------------------------------------------- */
/* Legacy global/local heap entry points. Win32 itself keeps compact/fix as
 * compatibility no-ops; lock/wire wrappers delegate to the real BlesKernOS
 * global-memory implementation. */

static uint32_t WIN32_API compat_GlobalCompact(uint32_t minimum_free) {
    (void)minimum_free;
    return 0U;
}

static uint32_t WIN32_API compat_LocalCompact(uint32_t minimum_free) {
    (void)minimum_free;
    return 0U;
}

static uint32_t WIN32_API compat_LocalShrink(void *handle, uint32_t size) {
    (void)handle;
    (void)size;
    return 0U;
}

static void *compat_call_global_lock(void *handle) {
    typedef void * (WIN32_API *function_t)(void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GlobalLock");
    return function ? function(handle) : NULL;
}

static int compat_call_global_unlock(void *handle) {
    typedef int (WIN32_API *function_t)(void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GlobalUnlock");
    return function ? function(handle) : 0;
}

static void WIN32_API compat_GlobalFix(void *handle) {
    (void)compat_call_global_lock(handle);
}

static void WIN32_API compat_GlobalUnfix(void *handle) {
    (void)compat_call_global_unlock(handle);
}

static void *WIN32_API compat_GlobalWire(void *handle) {
    return compat_call_global_lock(handle);
}

static int WIN32_API compat_GlobalUnWire(void *handle) {
    return compat_call_global_unlock(handle);
}

static void *WIN32_API compat_GlobalDiscard(void *handle) {
    typedef void * (WIN32_API *function_t)(void *, uint32_t, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GlobalReAlloc");
    return function ? function(handle, 0U, COMPAT_GMEM_MOVEABLE) : NULL;
}

static void *WIN32_API compat_GlobalLRUNewest(void *handle) { return handle; }
static void *WIN32_API compat_GlobalLRUOldest(void *handle) { return handle; }
static int WIN32_API compat_GlobalPageLock(void *handle) { return handle != NULL; }
static int WIN32_API compat_GlobalPageUnlock(void *handle) { return handle != NULL; }

static void WIN32_API compat_GlobalMemoryStatus(void *raw_status) {
    uint32_t *status = (uint32_t *)raw_status;
    system_memory_info_t memory;
    if (!status) return;
    mm_get_system_info(&memory);
    kmemset(status, 0, 32U);
    status[0] = 32U;
    status[1] = memory.total_bytes
        ? (uint32_t)(((uint64_t)memory.used_bytes * 100U) / memory.total_bytes)
        : 0U;
    status[2] = memory.total_bytes;
    status[3] = memory.free_bytes;
    status[4] = memory.total_bytes;
    status[5] = memory.free_bytes;
    status[6] = 0x7FFE0000U;
    status[7] = memory.free_bytes;
}

typedef struct {
    uint32_t length;
    uint32_t memory_load;
    uint64_t total_physical;
    uint64_t available_physical;
    uint64_t total_page_file;
    uint64_t available_page_file;
    uint64_t total_virtual;
    uint64_t available_virtual;
    uint64_t available_extended_virtual;
} compat_memory_status_ex_t;

static int WIN32_API compat_GlobalMemoryStatusEx(compat_memory_status_ex_t *status) {
    system_memory_info_t memory;
    if (!status || status->length < sizeof(*status)) return 0;
    mm_get_system_info(&memory);
    status->memory_load = memory.total_bytes
        ? (uint32_t)(((uint64_t)memory.used_bytes * 100U) / memory.total_bytes)
        : 0U;
    status->total_physical = memory.total_bytes;
    status->available_physical = memory.free_bytes;
    status->total_page_file = memory.total_bytes;
    status->available_page_file = memory.free_bytes;
    status->total_virtual = 0x7FFE0000ULL;
    status->available_virtual = memory.free_bytes;
    status->available_extended_virtual = 0ULL;
    return 1;
}

static int WIN32_API compat_HeapLock(void *heap) { return heap != NULL; }
static int WIN32_API compat_HeapUnlock(void *heap) { return heap != NULL; }
static int WIN32_API compat_HeapValidate(void *heap, uint32_t flags, const void *memory) {
    (void)flags;
    return heap != NULL && (memory != NULL || memory == NULL);
}
static uint32_t WIN32_API compat_HeapCompact(void *heap, uint32_t flags) {
    system_memory_info_t memory;
    (void)flags;
    if (!heap) return 0U;
    mm_get_system_info(&memory);
    return memory.free_bytes;
}
static uint32_t WIN32_API compat_GetProcessHeaps(uint32_t count, void **heaps) {
    typedef void * (WIN32_API *get_heap_t)(void);
    get_heap_t get_heap = (get_heap_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetProcessHeap");
    if (heaps && count && get_heap) heaps[0] = get_heap();
    return 1U;
}

/* ------------------------------------------------------------------------- */
/* Standard handles and legacy HFILE APIs. */

static void *compat_standard_handles[3] = {
    (void *)(uintptr_t)0U,
    (void *)(uintptr_t)1U,
    (void *)(uintptr_t)2U
};

static int compat_std_index(uint32_t which) {
    if (which == COMPAT_STD_INPUT_HANDLE) return 0;
    if (which == COMPAT_STD_OUTPUT_HANDLE) return 1;
    if (which == COMPAT_STD_ERROR_HANDLE) return 2;
    return -1;
}

static void *WIN32_API compat_GetStdHandle(uint32_t which) {
    int index = compat_std_index(which);
    if (index < 0) {
        compat_set_last_error(87U);
        return COMPAT_INVALID_HANDLE_VALUE;
    }
    compat_set_last_error(0U);
    return compat_standard_handles[index];
}

static int WIN32_API compat_SetStdHandle(uint32_t which, void *handle) {
    int index = compat_std_index(which);
    if (index < 0) {
        compat_set_last_error(87U);
        return 0;
    }
    compat_standard_handles[index] = handle;
    compat_set_last_error(0U);
    return 1;
}

static uint32_t WIN32_API compat_SetHandleCount(uint32_t count) {
    return count;
}

static int WIN32_API compat_WriteFile(void *handle, const void *buffer,
                                      uint32_t length, uint32_t *written,
                                      void *overlapped) {
    uintptr_t value = (uintptr_t)handle;
    (void)overlapped;
    if (written) *written = 0U;
    if (!buffer && length) return 0;
    if (handle == compat_standard_handles[1] ||
        handle == compat_standard_handles[2] || value == 1U || value == 2U) {
        const uint8_t *bytes = (const uint8_t *)buffer;
        for (uint32_t i = 0U; i < length; i++) putchar(bytes[i]);
        if (written) *written = length;
        return 1;
    }
    return win32_file_write(handle, buffer, length, written);
}

static void *compat_create_file(const char *name, uint32_t access,
                                uint32_t creation) {
    typedef void * (WIN32_API *function_t)(const char *, uint32_t, uint32_t,
        void *, uint32_t, uint32_t, void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "CreateFileA");
    return function ? function(name, access, 3U, NULL, creation, 0U, NULL)
                    : COMPAT_INVALID_HANDLE_VALUE;
}

static int WIN32_API compat__lclose(int file) {
    typedef int (WIN32_API *function_t)(void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "CloseHandle");
    return function && function((void *)(uintptr_t)(uint32_t)file) ? 0 : -1;
}

static int WIN32_API compat__lopen(const char *name, int mode) {
    uint32_t access = COMPAT_GENERIC_READ;
    void *handle;
    if ((mode & 3) == 1) access = COMPAT_GENERIC_WRITE;
    else if ((mode & 3) == 2) access = COMPAT_GENERIC_READ | COMPAT_GENERIC_WRITE;
    handle = compat_create_file(name, access, COMPAT_OPEN_EXISTING);
    printf("[WIN32:legacy-io] _lopen %s mode=0x%x -> 0x%x\n",
           name ? name : "(null)", (uint32_t)mode,
           (uint32_t)(uintptr_t)handle);
    return handle == COMPAT_INVALID_HANDLE_VALUE ? -1 : (int)(uintptr_t)handle;
}

static int WIN32_API compat__lcreat(const char *name, int attributes) {
    void *handle;
    (void)attributes;
    /* WinZip SFX commonly reaches _lcreat through TEMP/TMP without
     * calling GetTempPathA first.  Keep this recovery specific to
     * the system temp directory. */
    if (name && ((name[0] == 'C' || name[0] == 'c') && name[1] == ':' &&
                 (name[2] == '\\' || name[2] == '/') &&
                 (name[3] == 'T' || name[3] == 't') &&
                 (name[4] == 'E' || name[4] == 'e') &&
                 (name[5] == 'M' || name[5] == 'm') &&
                 (name[6] == 'P' || name[6] == 'p') &&
                 (name[7] == '\\' || name[7] == '/')))
        (void)vfs_mkdir("/TEMP");
    handle = compat_create_file(name, COMPAT_GENERIC_READ | COMPAT_GENERIC_WRITE,
                                COMPAT_CREATE_ALWAYS);
    printf("[WIN32:legacy-io] _lcreat %s attr=0x%x -> 0x%x\n",
           name ? name : "(null)", (uint32_t)attributes,
           (uint32_t)(uintptr_t)handle);
    return handle == COMPAT_INVALID_HANDLE_VALUE ? -1 : (int)(uintptr_t)handle;
}

static uint32_t WIN32_API compat__llseek(int file, int32_t offset, int origin) {
    typedef uint32_t (WIN32_API *function_t)(void *, int32_t, int32_t *, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "SetFilePointer");
    uint32_t result = function
        ? function((void *)(uintptr_t)(uint32_t)file, offset, NULL,
                   (uint32_t)origin) : 0xFFFFFFFFU;
    if (BLES_WIN32_VERBOSE_LEGACY_IO || result == 0xFFFFFFFFU)
        printf("[WIN32:legacy-io] _llseek h=0x%x off=%d origin=%u -> 0x%x\n",
               (uint32_t)file, offset, (uint32_t)origin, result);
    return result;
}

static uint32_t WIN32_API compat__lread(int file, void *buffer, uint32_t length) {
    typedef int (WIN32_API *function_t)(void *, void *, uint32_t, uint32_t *, void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "ReadFile");
    uint32_t read = 0U;
    if (!function || !function((void *)(uintptr_t)(uint32_t)file, buffer,
                               length, &read, NULL)) {
        printf("[WIN32:legacy-io] _lread h=0x%x req=%u -> ERROR\n",
               (uint32_t)file, length);
        return 0xFFFFFFFFU;
    }
    if (BLES_WIN32_VERBOSE_LEGACY_IO && length >= 1024U) {
        const uint8_t *bytes = (const uint8_t *)buffer;
        printf("[WIN32:legacy-io] _lread h=0x%x req=%u -> %u head=%x%x%x%x\n",
               (uint32_t)file, length, read,
               read > 0U ? bytes[0] : 0U, read > 1U ? bytes[1] : 0U,
               read > 2U ? bytes[2] : 0U, read > 3U ? bytes[3] : 0U);
    }
    return read;
}

static uint32_t WIN32_API compat__hread(int file, void *buffer, int32_t length) {
    return length < 0 ? 0xFFFFFFFFU
                      : compat__lread(file, buffer, (uint32_t)length);
}

static uint32_t WIN32_API compat__lwrite(int file, const void *buffer,
                                         uint32_t length) {
    typedef int (WIN32_API *set_end_t)(void *);
    uint32_t written = 0U;
    /* Win32's legacy _lwrite/_hwrite contract gives a zero-byte write a
     * useful meaning: truncate or extend the file at the current pointer. */
    if (!length) {
        set_end_t set_end = (set_end_t)(uintptr_t)
            win32_resolve_import("KERNEL32.DLL", "SetEndOfFile");
        return set_end && set_end((void *)(uintptr_t)(uint32_t)file)
            ? 0U : 0xFFFFFFFFU;
    }
    if (!compat_WriteFile((void *)(uintptr_t)(uint32_t)file, buffer,
                          length, &written, NULL)) {
        printf("[WIN32:legacy-io] _lwrite h=0x%x req=%u -> ERROR\n",
               (uint32_t)file, length);
        return 0xFFFFFFFFU;
    }
    if (written != length || BLES_WIN32_VERBOSE_LEGACY_IO)
        printf("[WIN32:legacy-io] _lwrite h=0x%x req=%u -> %u\n",
               (uint32_t)file, length, written);
    return written;
}

static uint32_t WIN32_API compat__hwrite(int file, const void *buffer,
                                         int32_t length) {
    return length < 0 ? 0xFFFFFFFFU
                      : compat__lwrite(file, buffer, (uint32_t)length);
}

typedef struct PACKED {
    uint8_t bytes;
    uint8_t fixed_disk;
    uint16_t error;
    uint16_t reserved1;
    uint16_t reserved2;
    char path[128];
} compat_ofstruct_t;

static int WIN32_API compat_OpenFile(const char *name, compat_ofstruct_t *info,
                                     uint32_t style) {
    char path[COMPAT_MAX_PATH];
    int handle;
    if (!name || !*name) return -1;
    compat_copy(path, sizeof(path), name);
    if (info) {
        kmemset(info, 0, sizeof(*info));
        info->bytes = (uint8_t)sizeof(*info);
        info->fixed_disk = 1U;
        compat_copy(info->path, sizeof(info->path), path);
    }
    if (style & 0x0100U) return 0; /* OF_PARSE */
    if (style & 0x0200U) { /* OF_DELETE */
        char native[COMPAT_MAX_PATH];
        return compat_windows_to_native(path, native) && vfs_remove(native) ? 1 : -1;
    }
    if (style & 0x1000U) return compat__lcreat(path, 0); /* OF_CREATE */
    handle = compat__lopen(path, (int)(style & 3U));
    if (handle < 0 && info) info->error = 2U;
    if ((style & 0x4000U) && handle >= 0) { /* OF_EXIST */
        compat__lclose(handle);
        return 1;
    }
    return handle;
}

static int WIN32_API compat_DeleteFileA(const char *path) {
    char native[COMPAT_MAX_PATH];
    return compat_windows_to_native(path, native) && vfs_remove(native) ? 1 : 0;
}

static int WIN32_API compat_DeleteFileW(const uint16_t *path) {
    char ansi[COMPAT_MAX_PATH];
    return compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? compat_DeleteFileA(ansi) : 0;
}

static int WIN32_API compat_MoveFileA(const char *source, const char *target) {
    char src[COMPAT_MAX_PATH], dst[COMPAT_MAX_PATH];
    return compat_windows_to_native(source, src) &&
           compat_windows_to_native(target, dst) && vfs_rename(src, dst) ? 1 : 0;
}

static int WIN32_API compat_MoveFileW(const uint16_t *source,
                                      const uint16_t *target) {
    char src[COMPAT_MAX_PATH], dst[COMPAT_MAX_PATH];
    return compat_wide_to_ansi(source, src, sizeof(src)) &&
           compat_wide_to_ansi(target, dst, sizeof(dst))
        ? compat_MoveFileA(src, dst) : 0;
}

static int WIN32_API compat_MoveFileExA(const char *source, const char *target,
                                        uint32_t flags) {
    if ((flags & 1U) && target) (void)compat_DeleteFileA(target);
    return target ? compat_MoveFileA(source, target) : compat_DeleteFileA(source);
}

static int WIN32_API compat_MoveFileExW(const uint16_t *source,
                                        const uint16_t *target,
                                        uint32_t flags) {
    char src[COMPAT_MAX_PATH], dst[COMPAT_MAX_PATH];
    if (!compat_wide_to_ansi(source, src, sizeof(src))) return 0;
    if (!target) return compat_MoveFileExA(src, NULL, flags);
    return compat_wide_to_ansi(target, dst, sizeof(dst))
        ? compat_MoveFileExA(src, dst, flags) : 0;
}

static int WIN32_API compat_RemoveDirectoryA(const char *path) {
    return compat_DeleteFileA(path);
}
static int WIN32_API compat_RemoveDirectoryW(const uint16_t *path) {
    return compat_DeleteFileW(path);
}

static int WIN32_API compat_CreateDirectoryW(const uint16_t *path,
                                              void *security) {
    typedef int (WIN32_API *function_t)(const char *, void *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "CreateDirectoryA");
    char ansi[COMPAT_MAX_PATH];
    return function && compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? function(ansi, security) : 0;
}

static int WIN32_API compat_CopyFileW(const uint16_t *source,
                                      const uint16_t *target,
                                      int fail_if_exists) {
    typedef int (WIN32_API *function_t)(const char *, const char *, int);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "CopyFileA");
    char src[COMPAT_MAX_PATH], dst[COMPAT_MAX_PATH];
    return function && compat_wide_to_ansi(source, src, sizeof(src)) &&
           compat_wide_to_ansi(target, dst, sizeof(dst))
        ? function(src, dst, fail_if_exists) : 0;
}

static uint32_t WIN32_API compat_GetFileAttributesW(const uint16_t *path) {
    typedef uint32_t (WIN32_API *function_t)(const char *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetFileAttributesA");
    char ansi[COMPAT_MAX_PATH];
    return function && compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? function(ansi) : 0xFFFFFFFFU;
}

static int WIN32_API compat_SetFileAttributesW(const uint16_t *path,
                                                uint32_t attributes) {
    typedef int (WIN32_API *function_t)(const char *, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "SetFileAttributesA");
    char ansi[COMPAT_MAX_PATH];
    return function && compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? function(ansi, attributes) : 0;
}

static uint32_t WIN32_API compat_GetCurrentDirectoryW(uint32_t size,
                                                       uint16_t *buffer) {
    typedef uint32_t (WIN32_API *function_t)(uint32_t, char *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetCurrentDirectoryA");
    char ansi[COMPAT_MAX_PATH];
    uint32_t length;
    if (!function) return 0U;
    length = function(sizeof(ansi), ansi);
    if (!length) return 0U;
    if (!buffer || size <= length) return length + 1U;
    compat_ansi_to_wide(ansi, buffer, size);
    return length;
}

static int WIN32_API compat_SetCurrentDirectoryW(const uint16_t *path) {
    typedef int (WIN32_API *function_t)(const char *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "SetCurrentDirectoryA");
    char ansi[COMPAT_MAX_PATH];
    return function && compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? function(ansi) : 0;
}

static uint32_t WIN32_API compat_GetFullPathNameW(const uint16_t *path,
                                                  uint32_t size,
                                                  uint16_t *buffer,
                                                  uint16_t **file_part) {
    typedef uint32_t (WIN32_API *function_t)(const char *, uint32_t, char *, char **);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetFullPathNameA");
    char input[COMPAT_MAX_PATH], output[COMPAT_MAX_PATH], *part = NULL;
    uint32_t length;
    if (file_part) *file_part = NULL;
    if (!function || !compat_wide_to_ansi(path, input, sizeof(input))) return 0U;
    length = function(input, sizeof(output), output, &part);
    if (!length || !buffer || size <= length) return length ? length + 1U : 0U;
    compat_ansi_to_wide(output, buffer, size);
    if (file_part && part) *file_part = buffer + (uint32_t)(part - output);
    return length;
}

static uint32_t compat_fixed_path_w(const char *api, uint16_t *buffer,
                                    uint32_t size) {
    typedef uint32_t (WIN32_API *function_t)(char *, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", api);
    char ansi[COMPAT_MAX_PATH];
    uint32_t length = function ? function(ansi, sizeof(ansi)) : 0U;
    if (!length || !buffer || size <= length) return length ? length + 1U : 0U;
    compat_ansi_to_wide(ansi, buffer, size);
    return length;
}

static uint32_t WIN32_API compat_GetWindowsDirectoryW(uint16_t *buffer,
                                                       uint32_t size) {
    return compat_fixed_path_w("GetWindowsDirectoryA", buffer, size);
}
static uint32_t WIN32_API compat_GetSystemDirectoryW(uint16_t *buffer,
                                                      uint32_t size) {
    return compat_fixed_path_w("GetSystemDirectoryA", buffer, size);
}

static uint32_t WIN32_API compat_GetTempPathW(uint32_t size,
                                              uint16_t *buffer) {
    typedef uint32_t (WIN32_API *function_t)(uint32_t, char *);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetTempPathA");
    char ansi[COMPAT_MAX_PATH];
    uint32_t length = function ? function(sizeof(ansi), ansi) : 0U;
    if (!length || !buffer || size <= length) return length ? length + 1U : 0U;
    compat_ansi_to_wide(ansi, buffer, size);
    return length;
}

static uint32_t WIN32_API compat_GetModuleFileNameW(void *module,
                                                    uint16_t *buffer,
                                                    uint32_t size) {
    typedef uint32_t (WIN32_API *function_t)(void *, char *, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetModuleFileNameA");
    char ansi[COMPAT_MAX_PATH];
    uint32_t length = function ? function(module, ansi, sizeof(ansi)) : 0U;
    if (!length || !buffer || !size) return 0U;
    if (length >= size) length = size - 1U;
    compat_ansi_to_wide(ansi, buffer, size);
    return length;
}

static int WIN32_API compat_GetDiskFreeSpaceA(const char *root,
                                               uint32_t *sectors_per_cluster,
                                               uint32_t *bytes_per_sector,
                                               uint32_t *free_clusters,
                                               uint32_t *total_clusters) {
    uint64_t total = 0ULL, free = 0ULL;
    (void)root;
    if (!sectors_per_cluster || !bytes_per_sector || !free_clusters ||
        !total_clusters || !vfs_get_space(&total, &free)) return 0;
    *sectors_per_cluster = 8U;
    *bytes_per_sector = 512U;
    *free_clusters = (uint32_t)(free / 4096ULL);
    *total_clusters = (uint32_t)(total / 4096ULL);
    return 1;
}
static int WIN32_API compat_GetDiskFreeSpaceW(const uint16_t *root,
                                               uint32_t *spc, uint32_t *bps,
                                               uint32_t *free_clusters,
                                               uint32_t *total_clusters) {
    (void)root;
    return compat_GetDiskFreeSpaceA(NULL, spc, bps, free_clusters, total_clusters);
}

static int WIN32_API compat_GetDiskFreeSpaceExA(const char *root,
                                                 uint64_t *available,
                                                 uint64_t *total,
                                                 uint64_t *free) {
    uint64_t total_value = 0ULL, free_value = 0ULL;
    (void)root;
    if (!vfs_get_space(&total_value, &free_value)) return 0;
    if (available) *available = free_value;
    if (total) *total = total_value;
    if (free) *free = free_value;
    return 1;
}
static int WIN32_API compat_GetDiskFreeSpaceExW(const uint16_t *root,
                                                 uint64_t *available,
                                                 uint64_t *total,
                                                 uint64_t *free) {
    (void)root;
    return compat_GetDiskFreeSpaceExA(NULL, available, total, free);
}

static int WIN32_API compat_GetVolumeInformationA(const char *root,
                                                   char *volume_name,
                                                   uint32_t volume_size,
                                                   uint32_t *serial,
                                                   uint32_t *max_component,
                                                   uint32_t *flags,
                                                   char *fs_name,
                                                   uint32_t fs_size) {
    (void)root;
    if (volume_name && volume_size) compat_copy(volume_name, volume_size, "BLESKERNOS");
    if (serial) *serial = 0xB1E50008U;
    if (max_component) *max_component = 255U;
    if (flags) *flags = 0U;
    if (fs_name && fs_size) compat_copy(fs_name, fs_size, "FAT32");
    return 1;
}
static int WIN32_API compat_GetVolumeInformationW(const uint16_t *root,
                                                   uint16_t *volume_name,
                                                   uint32_t volume_size,
                                                   uint32_t *serial,
                                                   uint32_t *max_component,
                                                   uint32_t *flags,
                                                   uint16_t *fs_name,
                                                   uint32_t fs_size) {
    (void)root;
    if (volume_name && volume_size)
        compat_ansi_to_wide("BLESKERNOS", volume_name, volume_size);
    if (serial) *serial = 0xB1E50008U;
    if (max_component) *max_component = 255U;
    if (flags) *flags = 0U;
    if (fs_name && fs_size) compat_ansi_to_wide("FAT32", fs_name, fs_size);
    return 1;
}

static uint32_t WIN32_API compat_GetShortPathNameA(const char *path,
                                                    char *buffer,
                                                    uint32_t size) {
    uint32_t length = path ? (uint32_t)kstrlen(path) : 0U;
    if (!path) return 0U;
    if (!buffer || size <= length) return length + 1U;
    kmemcpy(buffer, path, length + 1U);
    return length;
}
static uint32_t WIN32_API compat_GetLongPathNameA(const char *path,
                                                   char *buffer,
                                                   uint32_t size) {
    return compat_GetShortPathNameA(path, buffer, size);
}
static uint32_t WIN32_API compat_GetShortPathNameW(const uint16_t *path,
                                                    uint16_t *buffer,
                                                    uint32_t size) {
    uint32_t length = 0U;
    if (!path) return 0U;
    while (path[length]) length++;
    if (!buffer || size <= length) return length + 1U;
    kmemcpy(buffer, path, (length + 1U) * sizeof(uint16_t));
    return length;
}
static uint32_t WIN32_API compat_GetLongPathNameW(const uint16_t *path,
                                                   uint16_t *buffer,
                                                   uint32_t size) {
    return compat_GetShortPathNameW(path, buffer, size);
}

static uint32_t WIN32_API compat_GetTempFileNameA(const char *path,
                                                   const char *prefix,
                                                   uint32_t unique,
                                                   char *buffer) {
    char number[16];
    uint32_t value = unique ? unique : (pit_get_ticks() & 0xFFFFU);
    uint32_t attempt;
    if (!path || !*path || !buffer) return 0U;
    for (attempt = 0U; attempt < 32U; attempt++, value++) {
        buffer[0] = '\0';
        if (!compat_append(buffer, COMPAT_MAX_PATH, path)) return 0U;
        if (buffer[kstrlen(buffer) - 1U] != '\\' &&
            buffer[kstrlen(buffer) - 1U] != '/')
            if (!compat_append(buffer, COMPAT_MAX_PATH, "\\")) return 0U;
        if (prefix && *prefix) {
            for (uint32_t i = 0U; prefix[i] && i < 3U; i++) {
                char one[2] = {prefix[i], '\0'};
                if (!compat_append(buffer, COMPAT_MAX_PATH, one)) return 0U;
            }
        } else compat_append(buffer, COMPAT_MAX_PATH, "TMP");
        compat_decimal(value, number, sizeof(number));
        if (!compat_append(buffer, COMPAT_MAX_PATH, number) ||
            !compat_append(buffer, COMPAT_MAX_PATH, ".TMP")) return 0U;
        if (unique || !compat_file_exists(buffer)) {
            if (!unique) {
                int file = compat__lcreat(buffer, 0);
                if (file < 0) continue;
                compat__lclose(file);
            }
            return value;
        }
    }
    return 0U;
}
static uint32_t WIN32_API compat_GetTempFileNameW(const uint16_t *path,
                                                   const uint16_t *prefix,
                                                   uint32_t unique,
                                                   uint16_t *buffer) {
    char path_a[COMPAT_MAX_PATH], prefix_a[16], result[COMPAT_MAX_PATH];
    uint32_t value;
    if (!compat_wide_to_ansi(path, path_a, sizeof(path_a))) return 0U;
    if (prefix && !compat_wide_to_ansi(prefix, prefix_a, sizeof(prefix_a))) return 0U;
    value = compat_GetTempFileNameA(path_a, prefix ? prefix_a : NULL, unique, result);
    if (value && buffer) compat_ansi_to_wide(result, buffer, COMPAT_MAX_PATH);
    return value;
}

static int WIN32_API compat_GetFileTime(void *handle, uint64_t *creation,
                                        uint64_t *access, uint64_t *write) {
    if (!handle || handle == COMPAT_INVALID_HANDLE_VALUE) return 0;
    if (creation) *creation = 0ULL;
    if (access) *access = 0ULL;
    if (write) *write = 0ULL;
    return 1;
}
static int WIN32_API compat_SetFileTime(void *handle, const uint64_t *creation,
                                        const uint64_t *access,
                                        const uint64_t *write) {
    (void)creation; (void)access; (void)write;
    return handle && handle != COMPAT_INVALID_HANDLE_VALUE;
}
static int WIN32_API compat_GetFileInformationByHandle(void *handle, void *info) {
    if (!handle || handle == COMPAT_INVALID_HANDLE_VALUE || !info) return 0;
    kmemset(info, 0, 52U);
    *(uint32_t *)info = 0x20U;
    return 1;
}

static int WIN32_API compat_GetBinaryTypeA(const char *path, uint32_t *type) {
    if (!path || !type || !compat_file_exists(path)) return 0;
    *type = 0U; /* SCS_32BIT_BINARY */
    return 1;
}
static int WIN32_API compat_GetBinaryTypeW(const uint16_t *path, uint32_t *type) {
    char ansi[COMPAT_MAX_PATH];
    return compat_wide_to_ansi(path, ansi, sizeof(ansi))
        ? compat_GetBinaryTypeA(ansi, type) : 0;
}

static int compat_file_apis_ansi = 1;
static int WIN32_API compat_AreFileApisANSI(void) { return compat_file_apis_ansi; }
static void WIN32_API compat_SetFileApisToANSI(void) { compat_file_apis_ansi = 1; }
static void WIN32_API compat_SetFileApisToOEM(void) { compat_file_apis_ansi = 0; }

/* ------------------------------------------------------------------------- */
/* Mutable Win32 environment block. */

typedef struct {
    bool used;
    char name[COMPAT_ENV_NAME];
    char value[COMPAT_ENV_VALUE];
} compat_environment_entry_t;

static compat_environment_entry_t compat_environment[COMPAT_MAX_ENV];
static bool compat_environment_ready;

static int compat_environment_find(const char *name) {
    if (!name) return -1;
    for (uint32_t i = 0U; i < COMPAT_MAX_ENV; i++)
        if (compat_environment[i].used &&
            compat_equal_ci(compat_environment[i].name, name)) return (int)i;
    return -1;
}

static void compat_environment_put_default(const char *name, const char *value) {
    for (uint32_t i = 0U; i < COMPAT_MAX_ENV; i++) {
        if (compat_environment[i].used) continue;
        compat_environment[i].used = true;
        compat_copy(compat_environment[i].name, sizeof(compat_environment[i].name), name);
        compat_copy(compat_environment[i].value, sizeof(compat_environment[i].value), value);
        return;
    }
}

static void compat_environment_init(void) {
    if (compat_environment_ready) return;
    compat_environment_ready = true;
    compat_environment_put_default("PATH", "C:\\SYSTEM\\WIN32;C:\\SYSTEM\\LIBS\\WINE");
    compat_environment_put_default("TEMP", "C:\\TEMP");
    compat_environment_put_default("TMP", "C:\\TEMP");
    compat_environment_put_default("WINDIR", "C:\\SYSTEM");
    compat_environment_put_default("COMSPEC", "C:\\SYSTEM\\PROGRAMS\\SHELL.EXE");
    compat_environment_put_default("OS", "Windows_98");
}

int win32_compat_set_environment_variable_a(const char *name,
                                             const char *value);
uint32_t win32_compat_get_environment_variable_a(const char *name,
                                                  char *buffer,
                                                  uint32_t size);

static int WIN32_API compat_SetEnvironmentVariableA(const char *name,
                                                     const char *value) {
    return win32_compat_set_environment_variable_a(name, value);
}

int win32_compat_set_environment_variable_a(const char *name,
                                             const char *value) {
    int index, free_index = -1;
    compat_environment_init();
    if (!name || !*name || kstrlen(name) >= COMPAT_ENV_NAME) return 0;
    index = compat_environment_find(name);
    if (!value) {
        if (index >= 0) kmemset(&compat_environment[index], 0,
                                sizeof(compat_environment[index]));
        return 1;
    }
    if (kstrlen(value) >= COMPAT_ENV_VALUE) return 0;
    if (index < 0) {
        for (uint32_t i = 0U; i < COMPAT_MAX_ENV; i++)
            if (!compat_environment[i].used) { free_index = (int)i; break; }
        if (free_index < 0) return 0;
        index = free_index;
        compat_environment[index].used = true;
        compat_copy(compat_environment[index].name,
                    sizeof(compat_environment[index].name), name);
    }
    compat_copy(compat_environment[index].value,
                sizeof(compat_environment[index].value), value);
    return 1;
}

static int WIN32_API compat_SetEnvironmentVariableW(const uint16_t *name,
                                                     const uint16_t *value) {
    char name_a[COMPAT_ENV_NAME], value_a[COMPAT_ENV_VALUE];
    if (!compat_wide_to_ansi(name, name_a, sizeof(name_a))) return 0;
    if (!value) return compat_SetEnvironmentVariableA(name_a, NULL);
    return compat_wide_to_ansi(value, value_a, sizeof(value_a))
        ? compat_SetEnvironmentVariableA(name_a, value_a) : 0;
}

static uint32_t WIN32_API compat_GetEnvironmentVariableA(const char *name,
                                                          char *buffer,
                                                          uint32_t size) {
    return win32_compat_get_environment_variable_a(name, buffer, size);
}

uint32_t win32_compat_get_environment_variable_a(const char *name,
                                                  char *buffer,
                                                  uint32_t size) {
    int index;
    uint32_t length;
    compat_environment_init();
    index = compat_environment_find(name);
    if (index < 0) return 0U;
    length = (uint32_t)kstrlen(compat_environment[index].value);
    if (!buffer || size <= length) return length + 1U;
    kmemcpy(buffer, compat_environment[index].value, length + 1U);
    return length;
}

static uint32_t WIN32_API compat_GetEnvironmentVariableW(const uint16_t *name,
                                                          uint16_t *buffer,
                                                          uint32_t size) {
    char name_a[COMPAT_ENV_NAME], value[COMPAT_ENV_VALUE];
    uint32_t length;
    if (!compat_wide_to_ansi(name, name_a, sizeof(name_a))) return 0U;
    length = compat_GetEnvironmentVariableA(name_a, value, sizeof(value));
    if (!length) return 0U;
    if (!buffer || size <= length) return length + 1U;
    compat_ansi_to_wide(value, buffer, size);
    return length;
}

static char *WIN32_API compat_GetEnvironmentStringsA(void) {
    extern char *win32_compat_get_environment_strings_a(void);
    return win32_compat_get_environment_strings_a();
}

char *win32_compat_get_environment_strings_a(void) {
    uint32_t total = 1U, position = 0U;
    char *block;
    compat_environment_init();
    for (uint32_t i = 0U; i < COMPAT_MAX_ENV; i++)
        if (compat_environment[i].used)
            total += (uint32_t)kstrlen(compat_environment[i].name) + 1U +
                     (uint32_t)kstrlen(compat_environment[i].value) + 1U;
    block = (char *)kmalloc(total);
    if (!block) return NULL;
    for (uint32_t i = 0U; i < COMPAT_MAX_ENV; i++) {
        uint32_t length;
        if (!compat_environment[i].used) continue;
        length = (uint32_t)kstrlen(compat_environment[i].name);
        kmemcpy(block + position, compat_environment[i].name, length);
        position += length; block[position++] = '=';
        length = (uint32_t)kstrlen(compat_environment[i].value);
        kmemcpy(block + position, compat_environment[i].value, length);
        position += length; block[position++] = '\0';
    }
    block[position] = '\0';
    return block;
}

static int WIN32_API compat_FreeEnvironmentStringsA(char *block) {
    if (!block) return 0;
    kfree(block);
    return 1;
}

static uint16_t *WIN32_API compat_GetEnvironmentStringsW(void) {
    char *ansi = compat_GetEnvironmentStringsA();
    uint32_t count = 0U;
    uint16_t *wide;
    if (!ansi) return NULL;
    while (!(ansi[count] == '\0' && ansi[count + 1U] == '\0')) count++;
    count += 2U;
    wide = (uint16_t *)kmalloc(count * sizeof(uint16_t));
    if (!wide) { kfree(ansi); return NULL; }
    for (uint32_t i = 0U; i < count; i++) wide[i] = (uint8_t)ansi[i];
    kfree(ansi);
    return wide;
}

static int WIN32_API compat_FreeEnvironmentStringsW(uint16_t *block) {
    if (!block) return 0;
    kfree(block);
    return 1;
}

static uint32_t compat_expand_environment_a(const char *source, char *target,
                                             uint32_t capacity) {
    uint32_t needed = 0U, out = 0U, position = 0U;
    if (!source) return 0U;
    while (source[position]) {
        if (source[position] == '%') {
            uint32_t end = position + 1U;
            while (source[end] && source[end] != '%') end++;
            if (source[end] == '%' && end > position + 1U) {
                char name[COMPAT_ENV_NAME], value[COMPAT_ENV_VALUE];
                uint32_t name_length = end - position - 1U;
                uint32_t value_length;
                if (name_length < sizeof(name)) {
                    kmemcpy(name, source + position + 1U, name_length);
                    name[name_length] = '\0';
                    value_length = compat_GetEnvironmentVariableA(name, value,
                                                                  sizeof(value));
                    if (value_length) {
                        needed += value_length;
                        if (target && capacity) {
                            uint32_t room = out < capacity - 1U ? capacity - 1U - out : 0U;
                            uint32_t copy = compat_min_u32(room, value_length);
                            if (copy) kmemcpy(target + out, value, copy);
                            out += copy;
                        }
                        position = end + 1U;
                        continue;
                    }
                }
            }
        }
        needed++;
        if (target && capacity && out + 1U < capacity) target[out++] = source[position];
        position++;
    }
    if (target && capacity) target[out < capacity ? out : capacity - 1U] = '\0';
    return needed + 1U;
}

static uint32_t WIN32_API compat_ExpandEnvironmentStringsA(const char *source,
                                                            char *target,
                                                            uint32_t size) {
    return compat_expand_environment_a(source, target, size);
}
static uint32_t WIN32_API compat_ExpandEnvironmentStringsW(const uint16_t *source,
                                                            uint16_t *target,
                                                            uint32_t size) {
    char source_a[1024], target_a[2048];
    uint32_t needed;
    if (!compat_wide_to_ansi(source, source_a, sizeof(source_a))) return 0U;
    needed = compat_expand_environment_a(source_a, target_a, sizeof(target_a));
    if (target && size) compat_ansi_to_wide(target_a, target, size);
    return needed;
}

/* ------------------------------------------------------------------------- */
/* Win9x version, locale, codepage and time compatibility. */

static uint32_t WIN32_API compat_GetVersion(void) {
    return 0x80000000U | (2222U << 16) | (10U << 8) | 4U;
}

static int WIN32_API compat_GetVersionExA(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    uint32_t size;
    if (!info) return 0;
    size = *(uint32_t *)info;
    if (size < 148U) return 0;
    *(uint32_t *)(info + 4U) = 4U;
    *(uint32_t *)(info + 8U) = 10U;
    *(uint32_t *)(info + 12U) = 2222U;
    *(uint32_t *)(info + 16U) = COMPAT_VER_PLATFORM_WIN32_WINDOWS;
    compat_copy((char *)(info + 20U), size - 20U,
                "BlesKernOS Windows 98 compatibility");
    return 1;
}

static int WIN32_API compat_GetVersionExW(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    uint32_t size;
    if (!info) return 0;
    size = *(uint32_t *)info;
    if (size < 276U) return 0;
    *(uint32_t *)(info + 4U) = 4U;
    *(uint32_t *)(info + 8U) = 10U;
    *(uint32_t *)(info + 12U) = 2222U;
    *(uint32_t *)(info + 16U) = COMPAT_VER_PLATFORM_WIN32_WINDOWS;
    compat_ansi_to_wide("BlesKernOS Windows 98 compatibility",
                        (uint16_t *)(info + 20U), (size - 20U) / 2U);
    return 1;
}

static uint32_t WIN32_API compat_GetTickCount(void) {
    uint32_t frequency = pit_get_frequency_hz();
    return frequency ? (uint32_t)(((uint64_t)pit_get_ticks() * 1000ULL) / frequency)
                     : 0U;
}

static void WIN32_API compat_Sleep(uint32_t milliseconds) {
    uint32_t frequency = pit_get_frequency_hz();
    uint32_t ticks;
    if (!milliseconds) { task_yield(); return; }
    ticks = frequency
        ? (uint32_t)(((uint64_t)milliseconds * frequency + 999ULL) / 1000ULL)
        : 1U;
    task_sleep(ticks ? ticks : 1U);
}

static int WIN32_API compat_Beep(uint32_t frequency, uint32_t duration) {
    return bk_sound_tone(frequency, duration) ? 1 : 0;
}

static uint16_t compat_day_of_week(uint16_t year, uint8_t month, uint8_t day) {
    static const uint8_t table[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    uint32_t y = year;
    if (month < 3U) y--;
    return (uint16_t)((y + y / 4U - y / 100U + y / 400U +
                       table[month - 1U] + day) % 7U);
}

typedef struct {
    uint16_t year, month, day_of_week, day;
    uint16_t hour, minute, second, milliseconds;
} compat_system_time_t;

static void compat_fill_system_time(compat_system_time_t *time) {
    rtc_datetime_t now;
    if (!time) return;
    kmemset(time, 0, sizeof(*time));
    if (!rtc_get_datetime(&now)) {
        time->year = 1998U; time->month = 1U; time->day = 1U;
        time->day_of_week = 4U;
        return;
    }
    time->year = now.date.year;
    time->month = now.date.month;
    time->day = now.date.day;
    time->day_of_week = compat_day_of_week(time->year, (uint8_t)time->month,
                                           (uint8_t)time->day);
    time->hour = now.time.hour;
    time->minute = now.time.minute;
    time->second = now.time.second;
}

static void WIN32_API compat_GetLocalTime(compat_system_time_t *time) {
    compat_fill_system_time(time);
}
static void WIN32_API compat_GetSystemTime(compat_system_time_t *time) {
    compat_fill_system_time(time);
}

static int64_t compat_days_from_civil(int32_t year, uint32_t month,
                                      uint32_t day) {
    int32_t y = year - (month <= 2U);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153U * (month + (month > 2U ? (uint32_t)-3 : 9U)) + 2U) /
                   5U + day - 1U;
    uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

static void compat_civil_from_days(int64_t days, int32_t *year,
                                   uint32_t *month, uint32_t *day) {
    int64_t z = days + 719468LL;
    int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    uint32_t doe = (uint32_t)(z - era * 146097LL);
    uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int32_t y = (int32_t)yoe + (int32_t)era * 400;
    uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    uint32_t mp = (5U * doy + 2U) / 153U;
    *day = doy - (153U * mp + 2U) / 5U + 1U;
    *month = mp + (mp < 10U ? 3U : (uint32_t)-9);
    *year = y + (*month <= 2U);
}

static int WIN32_API compat_SystemTimeToFileTime(const compat_system_time_t *time,
                                                  uint64_t *file_time) {
    int64_t days, seconds;
    if (!time || !file_time || time->year < 1601U || time->month < 1U ||
        time->month > 12U || time->day < 1U || time->day > 31U ||
        time->hour > 23U || time->minute > 59U || time->second > 59U)
        return 0;
    days = compat_days_from_civil((int32_t)time->year, time->month, time->day);
    seconds = days * 86400LL + (int64_t)time->hour * 3600LL +
              (int64_t)time->minute * 60LL + time->second + 11644473600LL;
    if (seconds < 0) return 0;
    *file_time = (uint64_t)seconds * 10000000ULL +
                 (uint64_t)time->milliseconds * 10000ULL;
    return 1;
}


/*
 * División unsigned de 64 bits sin libgcc.
 * Evita __divdi3, __moddi3 y __divmoddi4 en el kernel freestanding.
 */
static uint64_t compat_udivmod64(uint64_t dividend, uint64_t divisor,
                                  uint64_t *remainder) {
    uint64_t quotient = 0ULL;
    uint64_t rest = 0ULL;

    if (!divisor) {
        if (remainder) *remainder = dividend;
        return 0ULL;
    }

    for (int bit = 63; bit >= 0; bit--) {
        rest = (rest << 1) | ((dividend >> bit) & 1ULL);

        if (rest >= divisor) {
            rest -= divisor;
            quotient |= 1ULL << bit;
        }
    }

    if (remainder) *remainder = rest;
    return quotient;
}

static int WIN32_API compat_FileTimeToSystemTime(
        const uint64_t *file_time,
        compat_system_time_t *time) {
    uint64_t total_seconds;
    uint64_t subsecond_ticks;
    uint64_t day_remainder;
    uint64_t unix_seconds;
    int64_t days;
    uint32_t day_seconds;
    uint32_t minute_seconds;
    int32_t year;
    uint32_t month;
    uint32_t day;

    if (!file_time || !time) return 0;

    total_seconds = compat_udivmod64(
        *file_time,
        10000000ULL,
        &subsecond_ticks
    );

    if (total_seconds < 11644473600ULL) return 0;

    unix_seconds = total_seconds - 11644473600ULL;

    days = (int64_t)compat_udivmod64(
        unix_seconds,
        86400ULL,
        &day_remainder
    );

    day_seconds = (uint32_t)day_remainder;

    compat_civil_from_days(days, &year, &month, &day);

    kmemset(time, 0, sizeof(*time));

    time->year = (uint16_t)year;
    time->month = (uint16_t)month;
    time->day = (uint16_t)day;
    time->day_of_week = compat_day_of_week(
        time->year,
        (uint8_t)month,
        (uint8_t)day
    );

    time->hour = (uint16_t)(day_seconds / 3600U);

    minute_seconds = day_seconds % 3600U;
    time->minute = (uint16_t)(minute_seconds / 60U);
    time->second = (uint16_t)(minute_seconds % 60U);

    /* subsecond_ticks siempre es menor que 10.000.000. */
    time->milliseconds = (uint16_t)(
        (uint32_t)subsecond_ticks / 10000U
    );

    return 1;
}


static void WIN32_API compat_GetSystemTimeAsFileTime(uint64_t *file_time) {
    compat_system_time_t time;
    compat_fill_system_time(&time);
    if (file_time && !compat_SystemTimeToFileTime(&time, file_time))
        *file_time = 0ULL;
}

static int WIN32_API compat_LocalFileTimeToFileTime(const uint64_t *local,
                                                     uint64_t *utc) {
    if (!local || !utc) return 0;
    *utc = *local;
    return 1;
}
static int WIN32_API compat_FileTimeToLocalFileTime(const uint64_t *utc,
                                                     uint64_t *local) {
    return compat_LocalFileTimeToFileTime(utc, local);
}
static int32_t WIN32_API compat_CompareFileTime(const uint64_t *a,
                                                const uint64_t *b) {
    if (!a || !b) return 0;
    return *a < *b ? -1 : (*a > *b ? 1 : 0);
}
static int WIN32_API compat_DosDateTimeToFileTime(uint16_t date, uint16_t time,
                                                  uint64_t *file_time) {
    compat_system_time_t system;
    kmemset(&system, 0, sizeof(system));
    system.year = (uint16_t)(1980U + ((date >> 9) & 0x7FU));
    system.month = (uint16_t)((date >> 5) & 0x0FU);
    system.day = (uint16_t)(date & 0x1FU);
    system.hour = (uint16_t)((time >> 11) & 0x1FU);
    system.minute = (uint16_t)((time >> 5) & 0x3FU);
    system.second = (uint16_t)((time & 0x1FU) * 2U);
    return compat_SystemTimeToFileTime(&system, file_time);
}
static int WIN32_API compat_SystemTimeToTzSpecificLocalTime(const void *zone,
                                                             const compat_system_time_t *utc,
                                                             compat_system_time_t *local) {
    (void)zone;
    if (!utc || !local) return 0;
    *local = *utc;
    return 1;
}

static uint16_t WIN32_API compat_GetSystemDefaultLangID(void) { return COMPAT_LANG_EN_US; }
static uint16_t WIN32_API compat_GetUserDefaultLangID(void) { return COMPAT_LANG_EN_US; }
static uint16_t WIN32_API compat_GetUserDefaultUILanguage(void) { return COMPAT_LANG_EN_US; }
static uint32_t WIN32_API compat_GetSystemDefaultLCID(void) { return COMPAT_LANG_EN_US; }
static uint32_t WIN32_API compat_GetUserDefaultLCID(void) { return COMPAT_LANG_EN_US; }
static uint32_t compat_thread_locale = COMPAT_LANG_EN_US;
static uint32_t WIN32_API compat_GetThreadLocale(void) { return compat_thread_locale; }
static int WIN32_API compat_SetThreadLocale(uint32_t locale) {
    if (!locale) return 0;
    compat_thread_locale = locale;
    return 1;
}
static int WIN32_API compat_IsValidCodePage(uint32_t page) {
    return page == 0U || page == 1U || page == 437U || page == 1252U ||
           page == 65001U;
}
static int WIN32_API compat_GetCPInfo(uint32_t page, void *raw) {
    uint8_t *info = (uint8_t *)raw;
    if (!info || !compat_IsValidCodePage(page)) return 0;
    kmemset(info, 0, 18U);
    *(uint32_t *)(info + 0U) = page == 65001U ? 4U : 1U;
    info[4] = '?';
    return 1;
}

static uint16_t compat_character_type(uint16_t character) {
    uint16_t type = 0U;
    if (character >= 'A' && character <= 'Z') type |= 0x0001U | 0x0100U;
    if (character >= 'a' && character <= 'z') type |= 0x0002U | 0x0100U;
    if (character >= '0' && character <= '9') type |= 0x0004U;
    if (character == ' ' || character == '\t') type |= 0x0008U | 0x0040U;
    if (character < 32U || character == 127U) type |= 0x0020U;
    if ((character >= 'A' && character <= 'F') ||
        (character >= 'a' && character <= 'f') ||
        (character >= '0' && character <= '9')) type |= 0x0080U;
    if ((character >= 33U && character <= 47U) ||
        (character >= 58U && character <= 64U) ||
        (character >= 91U && character <= 96U) ||
        (character >= 123U && character <= 126U)) type |= 0x0010U;
    return type;
}

static int WIN32_API compat_GetStringTypeA(uint32_t locale, uint32_t type,
                                            const char *source, int count,
                                            uint16_t *output) {
    (void)locale; (void)type;
    if (!source || !output || count == 0) return 0;
    if (count < 0) { count = 0; do { count++; } while (source[count - 1]); }
    for (int i = 0; i < count; i++) output[i] = compat_character_type((uint8_t)source[i]);
    return 1;
}
static int WIN32_API compat_GetStringTypeW(uint32_t type,
                                            const uint16_t *source, int count,
                                            uint16_t *output) {
    int actual = count;
    (void)type;
    if (!source || !output || count == 0) return 0;
    if (actual < 0) {
        actual = 0;
        do { actual++; } while (source[actual - 1]);
    }
    for (int i = 0; i < actual; i++)
        output[i] = compat_character_type(source[i]);
    return 1;
}

static int compat_compare_ansi(const char *a, int a_count, const char *b,
                               int b_count, bool ignore_case) {
    int i = 0, limit_a = a_count, limit_b = b_count;
    if (limit_a < 0) limit_a = a ? (int)kstrlen(a) : 0;
    if (limit_b < 0) limit_b = b ? (int)kstrlen(b) : 0;
    while (i < limit_a && i < limit_b) {
        uint8_t ca = (uint8_t)a[i], cb = (uint8_t)b[i];
        if (ignore_case) { ca = compat_upper(ca); cb = compat_upper(cb); }
        if (ca != cb) return ca < cb ? -1 : 1;
        i++;
    }
    return limit_a < limit_b ? -1 : (limit_a > limit_b ? 1 : 0);
}
static int WIN32_API compat_CompareStringA(uint32_t locale, uint32_t flags,
                                            const char *a, int a_count,
                                            const char *b, int b_count) {
    int result;
    (void)locale;
    if (!a || !b) return 0;
    result = compat_compare_ansi(a, a_count, b, b_count,
                                 (flags & COMPAT_NORM_IGNORECASE) != 0U);
    return result < 0 ? COMPAT_CSTR_LESS_THAN
                      : (result > 0 ? COMPAT_CSTR_GREATER_THAN : COMPAT_CSTR_EQUAL);
}
static int WIN32_API compat_CompareStringW(uint32_t locale, uint32_t flags,
                                            const uint16_t *a, int a_count,
                                            const uint16_t *b, int b_count) {
    int limit_a = a_count, limit_b = b_count, i = 0;
    (void)locale;
    if (!a || !b) return 0;
    if (limit_a < 0) { limit_a = 0; while (a[limit_a]) limit_a++; }
    if (limit_b < 0) { limit_b = 0; while (b[limit_b]) limit_b++; }
    while (i < limit_a && i < limit_b) {
        uint16_t ca = a[i], cb = b[i];
        if (flags & COMPAT_NORM_IGNORECASE) {
            if (ca >= 'a' && ca <= 'z') ca -= 32U;
            if (cb >= 'a' && cb <= 'z') cb -= 32U;
        }
        if (ca != cb) return ca < cb ? COMPAT_CSTR_LESS_THAN : COMPAT_CSTR_GREATER_THAN;
        i++;
    }
    return limit_a < limit_b ? COMPAT_CSTR_LESS_THAN
         : (limit_a > limit_b ? COMPAT_CSTR_GREATER_THAN : COMPAT_CSTR_EQUAL);
}

static int WIN32_API compat_LCMapStringA(uint32_t locale, uint32_t flags,
                                         const char *source, int source_count,
                                         char *target, int target_count) {
    int count = source_count;
    (void)locale;
    if (!source || source_count == 0) return 0;
    if (count < 0) { count = 0; do { count++; } while (source[count - 1]); }
    if (!target || !target_count) return count;
    if (target_count < count) return 0;
    for (int i = 0; i < count; i++) {
        uint8_t c = (uint8_t)source[i];
        if ((flags & COMPAT_LCMAP_UPPERCASE) && c >= 'a' && c <= 'z') c -= 32U;
        if ((flags & COMPAT_LCMAP_LOWERCASE) && c >= 'A' && c <= 'Z') c += 32U;
        target[i] = (char)c;
    }
    return count;
}
static int WIN32_API compat_LCMapStringW(uint32_t locale, uint32_t flags,
                                         const uint16_t *source, int source_count,
                                         uint16_t *target, int target_count) {
    int count = source_count;
    (void)locale;
    if (!source || source_count == 0) return 0;
    if (count < 0) { count = 0; do { count++; } while (source[count - 1]); }
    if (!target || !target_count) return count;
    if (target_count < count) return 0;
    for (int i = 0; i < count; i++) {
        uint16_t c = source[i];
        if ((flags & COMPAT_LCMAP_UPPERCASE) && c >= 'a' && c <= 'z') c -= 32U;
        if ((flags & COMPAT_LCMAP_LOWERCASE) && c >= 'A' && c <= 'Z') c += 32U;
        target[i] = c;
    }
    return count;
}

/* ------------------------------------------------------------------------- */
/* USER32 keyboard and small input-state compatibility. */

static uintptr_t compat_keyboard_layout = COMPAT_HKL_EN_US;

static void *WIN32_API compat_GetKeyboardLayout(uint32_t thread_id) {
    (void)thread_id;
    return (void *)compat_keyboard_layout;
}
static int WIN32_API compat_GetKeyboardLayoutList(int count, void **layouts) {
    if (count > 0 && layouts) layouts[0] = (void *)compat_keyboard_layout;
    return 1;
}
static int WIN32_API compat_GetKeyboardLayoutNameA(char *name) {
    if (!name) return 0;
    compat_copy(name, 9U, "00000409");
    return 1;
}
static int WIN32_API compat_GetKeyboardLayoutNameW(uint16_t *name) {
    if (!name) return 0;
    compat_ansi_to_wide("00000409", name, 9U);
    return 1;
}
static void *WIN32_API compat_ActivateKeyboardLayout(void *layout,
                                                     uint32_t flags) {
    void *previous = (void *)compat_keyboard_layout;
    (void)flags;
    if (layout) compat_keyboard_layout = (uintptr_t)layout;
    return previous;
}
static void *WIN32_API compat_LoadKeyboardLayoutA(const char *name,
                                                   uint32_t flags) {
    (void)name; (void)flags;
    return (void *)compat_keyboard_layout;
}
static void *WIN32_API compat_LoadKeyboardLayoutW(const uint16_t *name,
                                                   uint32_t flags) {
    (void)name; (void)flags;
    return (void *)compat_keyboard_layout;
}
static int WIN32_API compat_UnloadKeyboardLayout(void *layout) {
    return layout != NULL;
}
static int WIN32_API compat_GetKeyboardType(int type) {
    if (type == 0) return 4;
    if (type == 1) return 0;
    if (type == 2) return 12;
    return 0;
}
static uint32_t WIN32_API compat_MapVirtualKeyA(uint32_t code, uint32_t map_type) {
    if (map_type == 2U) {
        if (code >= 'A' && code <= 'Z') return code;
        if (code >= '0' && code <= '9') return code;
        if (code == 0x20U) return ' ';
        if (code == 0x0DU) return '\r';
        if (code == 0x09U) return '\t';
        return 0U;
    }
    return code & 0xFFU;
}
static uint32_t WIN32_API compat_MapVirtualKeyW(uint32_t code, uint32_t type) {
    return compat_MapVirtualKeyA(code, type);
}
static int16_t WIN32_API compat_VkKeyScanA(uint8_t character) {
    if (character >= 'a' && character <= 'z') return (int16_t)(character - 32U);
    if (character >= 'A' && character <= 'Z') return (int16_t)(character | 0x0100U);
    if (character >= '0' && character <= '9') return (int16_t)character;
    if (character == ' ') return 0x20;
    return -1;
}
static int16_t WIN32_API compat_VkKeyScanW(uint16_t character) {
    return character <= 0xFFU ? compat_VkKeyScanA((uint8_t)character) : -1;
}
static int16_t WIN32_API compat_GetAsyncKeyState(int key) {
    typedef int16_t (WIN32_API *function_t)(int);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "GetKeyState");
    return function ? function(key) : 0;
}
static int compat_vk_to_character(uint32_t key, const uint8_t *state,
                                  uint16_t *character) {
    uint16_t c = 0U;
    bool shift = state && (state[0x10U] & 0x80U);
    if (key >= 'A' && key <= 'Z') c = shift ? (uint16_t)key : (uint16_t)(key + 32U);
    else if (key >= '0' && key <= '9') c = (uint16_t)key;
    else if (key == 0x20U) c = ' ';
    else if (key == 0x0DU) c = '\r';
    else if (key == 0x09U) c = '\t';
    else if (key == 0x08U) c = '\b';
    if (!c) return 0;
    if (character) *character = c;
    return 1;
}
static int WIN32_API compat_ToAscii(uint32_t key, uint32_t scan,
                                     const uint8_t *state, uint16_t *characters,
                                     uint32_t flags) {
    (void)scan; (void)flags;
    return compat_vk_to_character(key, state, characters);
}
static int WIN32_API compat_ToAsciiEx(uint32_t key, uint32_t scan,
                                       const uint8_t *state, uint16_t *characters,
                                       uint32_t flags, void *layout) {
    (void)layout;
    return compat_ToAscii(key, scan, state, characters, flags);
}
static int WIN32_API compat_ToUnicode(uint32_t key, uint32_t scan,
                                       const uint8_t *state, uint16_t *buffer,
                                       int size, uint32_t flags) {
    uint16_t character;
    (void)scan; (void)flags;
    if (size <= 0 || !buffer || !compat_vk_to_character(key, state, &character)) return 0;
    buffer[0] = character;
    return 1;
}
static int WIN32_API compat_ToUnicodeEx(uint32_t key, uint32_t scan,
                                         const uint8_t *state, uint16_t *buffer,
                                         int size, uint32_t flags, void *layout) {
    (void)layout;
    return compat_ToUnicode(key, scan, state, buffer, size, flags);
}
static int WIN32_API compat_GetKeyNameTextA(int32_t parameter, char *buffer,
                                             int size) {
    uint32_t scan = ((uint32_t)parameter >> 16) & 0xFFU;
    char name[16];
    if (!buffer || size <= 0) return 0;
    if (scan >= 0x02U && scan <= 0x0BU) {
        static const char digits[] = "1234567890";
        name[0] = digits[scan - 2U]; name[1] = '\0';
    } else {
        name[0] = 'K'; name[1] = 'e'; name[2] = 'y'; name[3] = ' ';
        compat_decimal(scan, name + 4, sizeof(name) - 4U);
    }
    return (int)compat_copy(buffer, (uint32_t)size, name);
}
static int WIN32_API compat_GetKeyNameTextW(int32_t parameter, uint16_t *buffer,
                                             int size) {
    char ansi[32];
    int length = compat_GetKeyNameTextA(parameter, ansi, sizeof(ansi));
    if (!length || !buffer || size <= 0) return 0;
    compat_ansi_to_wide(ansi, buffer, (uint32_t)size);
    return length < size ? length : size - 1;
}
static int WIN32_API compat_AttachThreadInput(uint32_t from, uint32_t to,
                                              int attach) {
    (void)from; (void)to; (void)attach;
    return 1;
}
static int WIN32_API compat_GetInputState(void) {
    typedef int (WIN32_API *peek_t)(void *, void *, uint32_t, uint32_t, uint32_t);
    uint32_t message[7];
    peek_t peek = (peek_t)(uintptr_t)
        win32_resolve_import("USER32.DLL", "PeekMessageA");
    return peek ? peek(message, NULL, 0U, 0U, 0U) : 0;
}
static uint32_t WIN32_API compat_GetQueueStatus(uint32_t flags) {
    return compat_GetInputState() ? (flags & 0xFFFFU) | ((flags & 0xFFFFU) << 16) : 0U;
}
static uint32_t WIN32_API compat_GetMessageTime(void) { return compat_GetTickCount(); }
static uint32_t WIN32_API compat_GetMessagePos(void) { return 0U; }
static int WIN32_API compat_InSendMessage(void) { return 0; }
static uint32_t WIN32_API compat_InSendMessageEx(void *reserved) {
    (void)reserved; return 0U;
}

static int compat_show_cursor_count;
static int WIN32_API compat_ShowCursor(int show) {
    compat_show_cursor_count += show ? 1 : -1;
    return compat_show_cursor_count;
}
static int WIN32_API compat_SetCursorPos(int x, int y) {
    (void)x; (void)y;
    return 1;
}
static int WIN32_API compat_ClipCursor(const int32_t *rect) {
    (void)rect;
    return 1;
}
static uint32_t compat_double_click_time = 500U;
static uint32_t WIN32_API compat_GetDoubleClickTime(void) { return compat_double_click_time; }
static int WIN32_API compat_SetDoubleClickTime(uint32_t value) {
    compat_double_click_time = value ? value : 500U;
    return 1;
}
static int32_t compat_caret_x, compat_caret_y;
static int compat_caret_visible;
static int WIN32_API compat_CreateCaret(void *window, void *bitmap, int width, int height) {
    (void)window; (void)bitmap; (void)width; (void)height;
    return 1;
}
static int WIN32_API compat_DestroyCaret(void) { compat_caret_visible = 0; return 1; }
static int WIN32_API compat_ShowCaret(void *window) { (void)window; compat_caret_visible = 1; return 1; }
static int WIN32_API compat_HideCaret(void *window) { (void)window; compat_caret_visible = 0; return 1; }
static int WIN32_API compat_SetCaretPos(int x, int y) { compat_caret_x = x; compat_caret_y = y; return 1; }
static int WIN32_API compat_GetCaretPos(int32_t *point) {
    if (!point) return 0;
    point[0] = compat_caret_x; point[1] = compat_caret_y;
    return 1;
}
static uint32_t WIN32_API compat_GetCaretBlinkTime(void) { return 530U; }
static int WIN32_API compat_SetCaretBlinkTime(uint32_t value) { return value != 0U; }

/* ------------------------------------------------------------------------- */
/* Additional GDI text/device wrappers. */

static int WIN32_API compat_TextOutW(void *dc, int x, int y,
                                     const uint16_t *text, int length) {
    typedef int (WIN32_API *function_t)(void *, int, int, const char *, int);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("GDI32.DLL", "TextOutA");
    char ansi[512];
    if (!function || !text || length < 0 || (uint32_t)length >= sizeof(ansi)) return 0;
    for (int i = 0; i < length; i++) ansi[i] = text[i] <= 0xFFU ? (char)text[i] : '?';
    ansi[length] = '\0';
    return function(dc, x, y, ansi, length);
}
static int WIN32_API compat_DrawTextW(void *dc, const uint16_t *text, int length,
                                      int32_t *rect, uint32_t format) {
    typedef int (WIN32_API *function_t)(void *, const char *, int, int32_t *, uint32_t);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("GDI32.DLL", "DrawTextA");
    char ansi[1024];
    if (!function || !text) return 0;
    if (length < 0) { length = 0; while (text[length]) length++; }
    if ((uint32_t)length >= sizeof(ansi)) length = sizeof(ansi) - 1U;
    for (int i = 0; i < length; i++) ansi[i] = text[i] <= 0xFFU ? (char)text[i] : '?';
    ansi[length] = '\0';
    return function(dc, ansi, length, rect, format);
}
static int WIN32_API compat_GetTextExtentPoint32A(void *dc, const char *text,
                                                  int length, int32_t *size) {
    (void)dc;
    if (!text || length < 0 || !size) return 0;
    size[0] = length * 8;
    size[1] = 16;
    return 1;
}
static int WIN32_API compat_GetTextExtentPointA(void *dc, const char *text,
                                                int length, int32_t *size) {
    return compat_GetTextExtentPoint32A(dc, text, length, size);
}
static int WIN32_API compat_GetTextExtentPoint32W(void *dc,
                                                  const uint16_t *text,
                                                  int length, int32_t *size) {
    return compat_GetTextExtentPoint32A(dc, (const char *)text, length, size);
}
static int WIN32_API compat_GetTextExtentPointW(void *dc,
                                                const uint16_t *text,
                                                int length, int32_t *size) {
    return compat_GetTextExtentPoint32W(dc, text, length, size);
}
static int WIN32_API compat_GetTextMetricsA(void *dc, void *raw) {
    uint8_t *metrics = (uint8_t *)raw;
    (void)dc;
    if (!metrics) return 0;
    kmemset(metrics, 0, 56U);
    *(int32_t *)(metrics + 0U) = 16;
    *(int32_t *)(metrics + 4U) = 12;
    *(int32_t *)(metrics + 8U) = 4;
    *(int32_t *)(metrics + 20U) = 8;
    *(int32_t *)(metrics + 24U) = 8;
    *(int32_t *)(metrics + 28U) = 400;
    metrics[52U] = 1U;
    metrics[53U] = 0U;
    return 1;
}
static int WIN32_API compat_GetTextMetricsW(void *dc, void *raw) {
    return compat_GetTextMetricsA(dc, raw);
}
static int WIN32_API compat_GetDeviceCaps(void *dc, int index) {
    (void)dc;
    switch (index) {
        case 4: return 270; case 6: return 203;
        case 8: return 800; case 10: return 600;
        case 12: return 16; case 14: return 1;
        case 24: return -1; case 88: case 90: return 96;
        case 104: return 800; case 106: return 600;
        default: return 0;
    }
}
static int compat_rop2 = 13, compat_map_mode = 1, compat_stretch_mode = 1;
static int WIN32_API compat_SetROP2(void *dc, int mode) { int old = compat_rop2; (void)dc; compat_rop2 = mode; return old; }
static int WIN32_API compat_GetROP2(void *dc) { (void)dc; return compat_rop2; }
static int WIN32_API compat_SetMapMode(void *dc, int mode) { int old = compat_map_mode; (void)dc; compat_map_mode = mode; return old; }
static int WIN32_API compat_GetMapMode(void *dc) { (void)dc; return compat_map_mode; }
static int WIN32_API compat_SetStretchBltMode(void *dc, int mode) { int old = compat_stretch_mode; (void)dc; compat_stretch_mode = mode; return old; }
static int WIN32_API compat_GetStretchBltMode(void *dc) { (void)dc; return compat_stretch_mode; }
static int WIN32_API compat_SetPolyFillMode(void *dc, int mode) { (void)dc; return mode; }
static int WIN32_API compat_GetPolyFillMode(void *dc) { (void)dc; return 1; }
static int WIN32_API compat_SetViewportOrgEx(void *dc, int x, int y, int32_t *old) { (void)dc; if (old) old[0] = old[1] = 0; (void)x; (void)y; return 1; }
static int WIN32_API compat_SetWindowOrgEx(void *dc, int x, int y, int32_t *old) { return compat_SetViewportOrgEx(dc, x, y, old); }
static int WIN32_API compat_SetViewportExtEx(void *dc, int x, int y, int32_t *old) { return compat_SetViewportOrgEx(dc, x, y, old); }
static int WIN32_API compat_SetWindowExtEx(void *dc, int x, int y, int32_t *old) { return compat_SetViewportOrgEx(dc, x, y, old); }
static void *WIN32_API compat_CreateFontA(int height, int width, int escapement,
                                          int orientation, int weight,
                                          uint32_t italic, uint32_t underline,
                                          uint32_t strikeout, uint32_t charset,
                                          uint32_t out_precision,
                                          uint32_t clip_precision,
                                          uint32_t quality, uint32_t pitch,
                                          const char *face) {
    typedef void * (WIN32_API *stock_t)(int);
    stock_t stock = (stock_t)(uintptr_t)win32_resolve_import("GDI32.DLL", "GetStockObject");
    (void)height; (void)width; (void)escapement; (void)orientation; (void)weight;
    (void)italic; (void)underline; (void)strikeout; (void)charset;
    (void)out_precision; (void)clip_precision; (void)quality; (void)pitch; (void)face;
    return stock ? stock(17) : NULL;
}
static void *WIN32_API compat_CreateFontW(int h, int w, int e, int o, int weight,
                                          uint32_t italic, uint32_t underline,
                                          uint32_t strikeout, uint32_t charset,
                                          uint32_t outp, uint32_t clipp,
                                          uint32_t quality, uint32_t pitch,
                                          const uint16_t *face) {
    (void)face;
    return compat_CreateFontA(h, w, e, o, weight, italic, underline, strikeout,
                              charset, outp, clipp, quality, pitch, NULL);
}
static void *WIN32_API compat_CreateFontIndirectA(const void *font) {
    (void)font;
    return compat_CreateFontA(0,0,0,0,400,0,0,0,0,0,0,0,0,NULL);
}
static void *WIN32_API compat_CreateFontIndirectW(const void *font) {
    return compat_CreateFontIndirectA(font);
}
static int WIN32_API compat_GetTextFaceA(void *dc, int size, char *face) {
    (void)dc;
    if (!face || size <= 0) return 6;
    return (int)compat_copy(face, (uint32_t)size, "System");
}
static int WIN32_API compat_GetTextFaceW(void *dc, int size, uint16_t *face) {
    (void)dc;
    if (!face || size <= 0) return 6;
    compat_ansi_to_wide("System", face, (uint32_t)size);
    return 6;
}
static uint32_t WIN32_API compat_GetTextCharset(void *dc) { (void)dc; return 0U; }
static uint32_t WIN32_API compat_GetTextCharsetInfo(void *dc, void *signature,
                                                    uint32_t flags) {
    (void)dc; (void)flags;
    if (signature) kmemset(signature, 0, 24U);
    return 0U;
}
static int WIN32_API compat_GetCharWidthA(void *dc, uint32_t first,
                                          uint32_t last, int32_t *widths) {
    (void)dc;
    if (!widths || last < first) return 0;
    for (uint32_t c = first; c <= last; c++) widths[c - first] = 8;
    return 1;
}
static int WIN32_API compat_GetCharWidthW(void *dc, uint32_t first,
                                          uint32_t last, int32_t *widths) {
    return compat_GetCharWidthA(dc, first, last, widths);
}

/* ------------------------------------------------------------------------- */
/* Shell folder/PIDL and wide-shell helpers. */

static const char *compat_special_folder(uint32_t folder) {
    switch (folder & 0xFFFFU) {
        case 0x0000U: return "C:\\DESKTOP";
        case 0x0002U: return "C:\\PROGRAMS";
        case 0x0005U: return "C:\\DOCUMENTS";
        case 0x0007U: return "C:\\SYSTEM\\STARTUP";
        case 0x0010U: return "C:\\DESKTOP";
        case 0x001AU: return "C:\\SYSTEM\\APPDATA";
        case 0x001CU: return "C:\\SYSTEM\\APPDATA\\LOCAL";
        case 0x0023U: return "C:\\SYSTEM\\APPDATA";
        case 0x0024U: return "C:\\SYSTEM";
        case 0x0025U: return "C:\\SYSTEM\\LIBS\\WINE";
        case 0x0026U: return "C:\\PROGRAMS";
        case 0x002BU: return "C:\\PROGRAMS\\COMMON";
        default: return "C:\\SYSTEM";
    }
}

static int WIN32_API compat_SHGetSpecialFolderPathA(void *owner, char *path,
                                                    int folder, int create) {
    const char *value = compat_special_folder((uint32_t)folder);
    char native[COMPAT_MAX_PATH];
    (void)owner;
    if (!path) return 0;
    compat_copy(path, COMPAT_MAX_PATH, value);
    if (create && compat_windows_to_native(value, native)) (void)vfs_mkdir(native);
    return 1;
}
static int WIN32_API compat_SHGetSpecialFolderPathW(void *owner, uint16_t *path,
                                                    int folder, int create) {
    char ansi[COMPAT_MAX_PATH];
    if (!compat_SHGetSpecialFolderPathA(owner, ansi, folder, create) || !path) return 0;
    compat_ansi_to_wide(ansi, path, COMPAT_MAX_PATH);
    return 1;
}
static int32_t WIN32_API compat_SHGetFolderPathA(void *owner, int folder,
                                                 void *token, uint32_t flags,
                                                 char *path) {
    (void)token; (void)flags;
    return compat_SHGetSpecialFolderPathA(owner, path, folder, 1) ? 0 : -1;
}
static int32_t WIN32_API compat_SHGetFolderPathW(void *owner, int folder,
                                                 void *token, uint32_t flags,
                                                 uint16_t *path) {
    (void)token; (void)flags;
    return compat_SHGetSpecialFolderPathW(owner, path, folder, 1) ? 0 : -1;
}
static int32_t WIN32_API compat_SHGetSpecialFolderLocation(void *owner,
                                                           int folder,
                                                           void **pidl) {
    const char *path = compat_special_folder((uint32_t)folder);
    uint32_t length;
    char *copy;
    (void)owner;
    if (!pidl) return -1;
    length = (uint32_t)kstrlen(path) + 1U;
    copy = (char *)kmalloc(length);
    if (!copy) return -1;
    kmemcpy(copy, path, length);
    *pidl = copy;
    return 0;
}
static int WIN32_API compat_SHGetPathFromIDListA(const void *pidl, char *path) {
    if (!pidl || !path) return 0;
    compat_copy(path, COMPAT_MAX_PATH, (const char *)pidl);
    return 1;
}
static int WIN32_API compat_SHGetPathFromIDListW(const void *pidl,
                                                 uint16_t *path) {
    if (!pidl || !path) return 0;
    compat_ansi_to_wide((const char *)pidl, path, COMPAT_MAX_PATH);
    return 1;
}
static void WIN32_API compat_ILFree(void *pidl) { if (pidl) kfree(pidl); }
static void *WIN32_API compat_ILClone(const void *pidl) {
    uint32_t length;
    char *copy;
    if (!pidl) return NULL;
    length = (uint32_t)kstrlen((const char *)pidl) + 1U;
    copy = (char *)kmalloc(length);
    if (copy) kmemcpy(copy, pidl, length);
    return copy;
}
static void *WIN32_API compat_ShellExecuteW(void *owner, const uint16_t *verb,
                                            const uint16_t *file,
                                            const uint16_t *parameters,
                                            const uint16_t *directory,
                                            int show) {
    typedef void * (WIN32_API *function_t)(void *, const char *, const char *,
        const char *, const char *, int);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("SHELL32.DLL", "ShellExecuteA");
    char verb_a[32], file_a[COMPAT_MAX_PATH], parameters_a[COMPAT_MAX_PATH];
    char directory_a[COMPAT_MAX_PATH];
    if (!function || !file || !compat_wide_to_ansi(file, file_a, sizeof(file_a)))
        return (void *)(uintptr_t)2U;
    if (verb && !compat_wide_to_ansi(verb, verb_a, sizeof(verb_a))) return (void *)2U;
    if (parameters && !compat_wide_to_ansi(parameters, parameters_a, sizeof(parameters_a))) return (void *)2U;
    if (directory && !compat_wide_to_ansi(directory, directory_a, sizeof(directory_a))) return (void *)2U;
    return function(owner, verb ? verb_a : NULL, file_a,
                    parameters ? parameters_a : NULL,
                    directory ? directory_a : NULL, show);
}

typedef struct {
    uint32_t size, mask;
    void *owner;
    const char *verb, *file, *parameters, *directory;
    int show;
    void *instance, *id_list;
    const char *class_name;
    void *class_key;
    uint32_t hot_key;
    void *icon;
    void *process;
} compat_shell_execute_info_a_t;

static int WIN32_API compat_ShellExecuteExA(compat_shell_execute_info_a_t *info) {
    typedef void * (WIN32_API *function_t)(void *, const char *, const char *,
        const char *, const char *, int);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("SHELL32.DLL", "ShellExecuteA");
    if (!info || info->size < 32U || !function) return 0;
    info->instance = function(info->owner, info->verb, info->file,
                              info->parameters, info->directory, info->show);
    info->process = NULL;
    return (uint32_t)(uintptr_t)info->instance > 32U;
}
static int WIN32_API compat_ShellExecuteExW(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    void *result;
    if (!info || *(uint32_t *)info < 32U) return 0;
    result = compat_ShellExecuteW(*(void **)(info + 8U),
        *(const uint16_t **)(info + 12U), *(const uint16_t **)(info + 16U),
        *(const uint16_t **)(info + 20U), *(const uint16_t **)(info + 24U),
        *(int32_t *)(info + 28U));
    *(void **)(info + 32U) = result;
    return (uint32_t)(uintptr_t)result > 32U;
}
static uint32_t WIN32_API compat_FindExecutableA(const char *file,
                                                  const char *directory,
                                                  char *result) {
    (void)directory;
    if (!file || !compat_file_exists(file)) return 2U;
    if (result) compat_copy(result, COMPAT_MAX_PATH, file);
    return COMPAT_SHELL_SUCCESS;
}
static uint32_t WIN32_API compat_FindExecutableW(const uint16_t *file,
                                                  const uint16_t *directory,
                                                  uint16_t *result) {
    char file_a[COMPAT_MAX_PATH], dir_a[COMPAT_MAX_PATH], result_a[COMPAT_MAX_PATH];
    uint32_t status;
    if (!compat_wide_to_ansi(file, file_a, sizeof(file_a))) return 2U;
    if (directory && !compat_wide_to_ansi(directory, dir_a, sizeof(dir_a))) return 2U;
    status = compat_FindExecutableA(file_a, directory ? dir_a : NULL, result_a);
    if (status > 32U && result) compat_ansi_to_wide(result_a, result, COMPAT_MAX_PATH);
    return status;
}

typedef struct {
    void *owner;
    uint32_t operation;
    const char *from;
    const char *to;
    uint16_t flags;
    int aborted;
    void *mappings;
    const char *title;
} compat_file_operation_a_t;

static int WIN32_API compat_SHFileOperationA(compat_file_operation_a_t *operation) {
    const char *source, *target;
    if (!operation || !operation->from || !*operation->from) return 87;
    source = operation->from;
    target = operation->to;
    operation->aborted = 0;
    switch (operation->operation) {
        case COMPAT_SHFILEOP_COPY: {
            typedef int (WIN32_API *copy_t)(const char *, const char *, int);
            copy_t copy = (copy_t)(uintptr_t)win32_resolve_import("KERNEL32.DLL", "CopyFileA");
            return copy && target && copy(source, target, 0) ? 0 : 5;
        }
        case COMPAT_SHFILEOP_MOVE:
        case COMPAT_SHFILEOP_RENAME:
            return target && compat_MoveFileA(source, target) ? 0 : 5;
        case COMPAT_SHFILEOP_DELETE:
            return compat_DeleteFileA(source) ? 0 : 5;
        default: return 87;
    }
}
static int WIN32_API compat_SHFileOperationW(void *raw) {
    uint8_t *operation = (uint8_t *)raw;
    compat_file_operation_a_t ansi;
    char from[COMPAT_MAX_PATH], to[COMPAT_MAX_PATH];
    if (!operation) return 87;
    kmemset(&ansi, 0, sizeof(ansi));
    ansi.owner = *(void **)(operation + 0U);
    ansi.operation = *(uint32_t *)(operation + 4U);
    if (!compat_wide_to_ansi(*(const uint16_t **)(operation + 8U), from, sizeof(from))) return 87;
    ansi.from = from;
    if (*(const uint16_t **)(operation + 12U)) {
        if (!compat_wide_to_ansi(*(const uint16_t **)(operation + 12U), to, sizeof(to))) return 87;
        ansi.to = to;
    }
    ansi.flags = *(uint16_t *)(operation + 16U);
    return compat_SHFileOperationA(&ansi);
}
static uint32_t WIN32_API compat_DragQueryFileW(void *drop, uint32_t index,
                                                uint16_t *path, uint32_t size) {
    typedef uint32_t (WIN32_API *query_t)(void *, uint32_t, char *, uint32_t);
    query_t query = (query_t)(uintptr_t)
        win32_resolve_import("SHELL32.DLL", "DragQueryFileA");
    char ansi[COMPAT_MAX_PATH];
    uint32_t length = query ? query(drop, index, ansi, sizeof(ansi)) : 0U;
    if (path && size && length) compat_ansi_to_wide(ansi, path, size);
    return length;
}
static int WIN32_API compat_DragQueryPoint(void *drop, int32_t *point) {
    (void)drop;
    if (point) point[0] = point[1] = 0;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Common-controls and multimedia enumeration fallbacks. */

static int WIN32_API compat_InitCommonControlsEx(const void *classes) {
    (void)classes;
    return 1;
}
static int32_t WIN32_API compat_DllGetVersion(void *raw) {
    uint32_t *version = (uint32_t *)raw;
    if (!version || version[0] < 20U) return -1;
    version[1] = 4U;
    version[2] = 72U;
    version[3] = 3110U;
    version[4] = 0U;
    return 0;
}

static uint32_t WIN32_API compat_timeGetTime(void) { return compat_GetTickCount(); }
static uint32_t WIN32_API compat_timeBeginPeriod(uint32_t period) { return period ? 0U : 97U; }
static uint32_t WIN32_API compat_timeEndPeriod(uint32_t period) { return period ? 0U : 97U; }
static uint32_t WIN32_API compat_waveOutGetNumDevs(void) { return 1U; }
static uint32_t WIN32_API compat_waveInGetNumDevs(void) { return 1U; }
static uint32_t WIN32_API compat_midiOutGetNumDevs(void) { return 1U; }
static uint32_t WIN32_API compat_midiInGetNumDevs(void) { return 1U; }
static uint32_t WIN32_API compat_auxGetNumDevs(void) { return 1U; }
static uint32_t WIN32_API compat_joyGetNumDevs(void) { return 0U; }

typedef struct {
    uint16_t manufacturer, product;
    uint32_t version;
    char name[32];
    uint32_t formats;
    uint16_t channels, reserved;
    uint32_t support;
} compat_wave_caps_a_t;

typedef struct {
    uint16_t manufacturer, product;
    uint32_t version;
    uint16_t name[32];
    uint32_t formats;
    uint16_t channels, reserved;
    uint32_t support;
} compat_wave_caps_w_t;

static uint32_t WIN32_API compat_waveOutGetDevCapsA(uintptr_t device,
                                                    void *raw, uint32_t size) {
    compat_wave_caps_a_t caps;
    uint32_t copy;
    if (device != 0U || !raw) return COMPAT_MMSYSERR_BADDEVICEID;
    kmemset(&caps, 0, sizeof(caps));
    caps.version = 0x00010000U;
    compat_copy(caps.name, sizeof(caps.name), "BlesKernOS WaveOut");
    caps.formats = 0x00000FFFU;
    caps.channels = 2U;
    copy = compat_min_u32(size, sizeof(caps));
    kmemcpy(raw, &caps, copy);
    return 0U;
}
static uint32_t WIN32_API compat_waveOutGetDevCapsW(uintptr_t device,
                                                    void *raw, uint32_t size) {
    compat_wave_caps_w_t caps;
    uint32_t copy;
    if (device != 0U || !raw) return COMPAT_MMSYSERR_BADDEVICEID;
    kmemset(&caps, 0, sizeof(caps));
    caps.version = 0x00010000U;
    compat_ansi_to_wide("BlesKernOS WaveOut", caps.name, 32U);
    caps.formats = 0x00000FFFU;
    caps.channels = 2U;
    copy = compat_min_u32(size, sizeof(caps));
    kmemcpy(raw, &caps, copy);
    return 0U;
}
static uint32_t WIN32_API compat_waveInGetDevCapsA(uintptr_t device,
                                                   void *raw, uint32_t size) {
    return compat_waveOutGetDevCapsA(device, raw, size);
}
static uint32_t WIN32_API compat_waveInGetDevCapsW(uintptr_t device,
                                                   void *raw, uint32_t size) {
    return compat_waveOutGetDevCapsW(device, raw, size);
}
static uint32_t WIN32_API compat_auxGetDevCapsA(uintptr_t device,
                                                void *raw, uint32_t size) {
    return compat_waveOutGetDevCapsA(device, raw, size);
}
static uint32_t WIN32_API compat_auxGetDevCapsW(uintptr_t device,
                                                void *raw, uint32_t size) {
    return compat_waveOutGetDevCapsW(device, raw, size);
}
static int WIN32_API compat_PlaySoundA(const char *sound, void *module,
                                       uint32_t flags) {
    (void)sound; (void)module; (void)flags;
    return bk_sound_tone(880U, 60U) ? 1 : 0;
}
static int WIN32_API compat_PlaySoundW(const uint16_t *sound, void *module,
                                       uint32_t flags) {
    (void)sound;
    return compat_PlaySoundA(NULL, module, flags);
}
static int WIN32_API compat_sndPlaySoundA(const char *sound, uint32_t flags) {
    return compat_PlaySoundA(sound, NULL, flags);
}
static int WIN32_API compat_sndPlaySoundW(const uint16_t *sound, uint32_t flags) {
    return compat_PlaySoundW(sound, NULL, flags);
}
static uint32_t WIN32_API compat_mciSendStringA(const char *command,
                                                char *result,
                                                uint32_t result_size,
                                                void *callback) {
    (void)command; (void)callback;
    if (result && result_size) result[0] = '\0';
    return 0U;
}
static uint32_t WIN32_API compat_mciSendStringW(const uint16_t *command,
                                                uint16_t *result,
                                                uint32_t result_size,
                                                void *callback) {
    (void)command; (void)callback;
    if (result && result_size) result[0] = 0U;
    return 0U;
}



/* ------------------------------------------------------------------------- */
/* Process startup, command line and small Win9x system helpers. */

static uint16_t compat_command_line_w[512];
static uint16_t *WIN32_API compat_GetCommandLineW(void) {
    typedef char * (WIN32_API *function_t)(void);
    function_t function = (function_t)(uintptr_t)
        win32_resolve_import("KERNEL32.DLL", "GetCommandLineA");
    const char *line = function ? function() : "";
    compat_ansi_to_wide(line ? line : "", compat_command_line_w,
                        sizeof(compat_command_line_w) /
                        sizeof(compat_command_line_w[0]));
    return compat_command_line_w;
}

static void WIN32_API compat_GetStartupInfoW(void *raw) {
    uint8_t *info = (uint8_t *)raw;
    if (!info) return;
    kmemset(info, 0, 68U);
    *(uint32_t *)(info + 0U) = 68U;
    *(void **)(info + 56U) = compat_standard_handles[0];
    *(void **)(info + 60U) = compat_standard_handles[1];
    *(void **)(info + 64U) = compat_standard_handles[2];
}

/* BLES_WINE_WINEXEC_FONT_FINAL_FIX_20260723
 * Resolve WinExec synchronously to an explicit absolute executable path.
 * WinZip SFX launches ".\\SETUP" after SetCurrentDirectory; passing only
 * the command line was ambiguous in the old loader. */
static uint32_t WIN32_API compat_WinExec(const char *command, uint32_t show) {
    typedef struct { uint32_t cb; char *reserved,*desktop,*title; uint32_t x,y,xs,ys,xc,yc,fill,flags; uint16_t show_window,r2s; uint8_t*r2; void*si,*so,*se; } startup_info_a_t;
    typedef struct { void *process_handle,*thread_handle; uint32_t process_id,thread_id; } process_information_t;
    typedef int (WIN32_API *create_process_t)(const char*,char*,void*,void*,int,uint32_t,void*,const char*,void*,void*);
    typedef uint32_t (WIN32_API *getcwd_t)(uint32_t,char*);
    typedef int (WIN32_API *close_t)(void*);
    create_process_t cp=(create_process_t)(uintptr_t)win32_resolve_import("KERNEL32.DLL","CreateProcessA");
    getcwd_t gc=(getcwd_t)(uintptr_t)win32_resolve_import("KERNEL32.DLL","GetCurrentDirectoryA");
    close_t closeh=(close_t)(uintptr_t)win32_resolve_import("KERNEL32.DLL","CloseHandle");
    startup_info_a_t si; process_information_t pi;
    char cmd[COMPAT_MAX_PATH*2U], cwd[COMPAT_MAX_PATH], app[COMPAT_MAX_PATH*2U];
    uint32_t n=0U; bool quoted=false, has_ext=false;
    if(!command||!*command||!cp||!gc)return 2U;
    compat_copy(cmd,sizeof(cmd),command);
    if(!gc(sizeof(cwd),cwd))return 3U;
    const char*p=command; while(*p==' '||*p=='\t')p++; if(*p=='"'){quoted=true;p++;}
    char token[COMPAT_MAX_PATH];
    while(*p && n+1U<sizeof(token) && ((quoted&&*p!='"')||(!quoted&&*p!=' '&&*p!='\t'))) token[n++]=*p++;
    token[n]='\0';
    if(!n)return 2U;
    for(uint32_t q=0U;token[q];q++)if(token[q]=='.' && token[q+1] && token[q+1]!='\\'&&token[q+1]!='/')has_ext=true;
    compat_copy(app,sizeof(app),cwd);
    if(app[0] && app[kstrlen(app)-1U]!='\\')kstrcat(app,"\\");
    const char*t=token; while(t[0]=='.'&&(t[1]=='\\'||t[1]=='/'))t+=2;
    if(kstrlen(app)+kstrlen(t)+5U>=sizeof(app))return 2U;
    kstrcat(app,t); if(!has_ext)kstrcat(app,".EXE");
    kmemset(&si,0,sizeof(si)); kmemset(&pi,0,sizeof(pi));
    si.cb=sizeof(si); si.flags=1U; si.show_window=(uint16_t)show;
    if(!cp(app,cmd,NULL,NULL,0,0U,NULL,cwd,&si,&pi))return 2U;
    if(closeh){if(pi.thread_handle)closeh(pi.thread_handle);if(pi.process_handle)closeh(pi.process_handle);}
    return 33U;
}

static int WIN32_API compat_GetComputerNameA(char *buffer, uint32_t *size) {
    static const char name[] = "BLESKERNOS";
    uint32_t length = sizeof(name) - 1U;
    if (!size) return 0;
    if (!buffer || *size <= length) { *size = length + 1U; return 0; }
    kmemcpy(buffer, name, sizeof(name));
    *size = length;
    return 1;
}
static int WIN32_API compat_GetComputerNameW(uint16_t *buffer, uint32_t *size) {
    static const char name[] = "BLESKERNOS";
    uint32_t length = sizeof(name) - 1U;
    if (!size) return 0;
    if (!buffer || *size <= length) { *size = length + 1U; return 0; }
    compat_ansi_to_wide(name, buffer, *size);
    *size = length;
    return 1;
}
static int WIN32_API compat_SetComputerNameA(const char *name) { return name && *name; }
static int WIN32_API compat_SetComputerNameW(const uint16_t *name) { return name && *name; }

static uint32_t compat_console_input_cp = 437U;
static uint32_t compat_console_output_cp = 437U;
static uint32_t WIN32_API compat_GetConsoleCP(void) { return compat_console_input_cp; }
static uint32_t WIN32_API compat_GetConsoleOutputCP(void) { return compat_console_output_cp; }
static int WIN32_API compat_SetConsoleCP(uint32_t page) {
    if (!compat_IsValidCodePage(page)) return 0;
    compat_console_input_cp = page; return 1;
}
static int WIN32_API compat_SetConsoleOutputCP(uint32_t page) {
    if (!compat_IsValidCodePage(page)) return 0;
    compat_console_output_cp = page; return 1;
}
static int WIN32_API compat_AllocConsole(void) { return 1; }
static int WIN32_API compat_FreeConsole(void) { return 1; }
static int WIN32_API compat_GetConsoleMode(void *handle, uint32_t *mode) {
    (void)handle;
    if (!mode) return 0;
    *mode = 0x0007U;
    return 1;
}
static int WIN32_API compat_SetConsoleMode(void *handle, uint32_t mode) {
    (void)handle; (void)mode;
    return 1;
}
static int WIN32_API compat_FlushConsoleInputBuffer(void *handle) {
    (void)handle; return 1;
}

/* ------------------------------------------------------------------------- */
/* Virtual wave, MIDI and mixer-control objects. These keep Win9x multimedia
 * programs moving through device enumeration and setup; buffers complete
 * immediately until asynchronous PCM streaming is wired to sound_core. */

#define COMPAT_WAVEOUT_HANDLE 0x7B570001U
#define COMPAT_WAVEIN_HANDLE  0x7B570002U
#define COMPAT_MIDIOUT_HANDLE 0x7B4D1001U
#define COMPAT_MIDIIN_HANDLE  0x7B4D1002U
#define COMPAT_WHDR_DONE 0x00000001U
#define COMPAT_WHDR_PREPARED 0x00000002U
#define COMPAT_MHDR_DONE 0x00000001U
#define COMPAT_MHDR_PREPARED 0x00000002U

static uint32_t compat_wave_volume = 0xFFFFFFFFU;
static uint32_t compat_aux_volume = 0xFFFFFFFFU;
static uint32_t compat_mixer_volume = 0x0000FFFFU;

static bool compat_wave_format_supported(const void *raw) {
    const uint8_t *format = (const uint8_t *)raw;
    uint16_t tag, channels, bits;
    uint32_t rate;
    if (!format) return false;
    tag = *(const uint16_t *)(format + 0U);
    channels = *(const uint16_t *)(format + 2U);
    rate = *(const uint32_t *)(format + 4U);
    bits = *(const uint16_t *)(format + 14U);
    return tag == 1U && channels >= 1U && channels <= 2U &&
           rate >= 8000U && rate <= 48000U &&
           (bits == 8U || bits == 16U);
}

static uint32_t WIN32_API compat_waveOutOpen(void **handle, uint32_t device,
                                             const void *format,
                                             uintptr_t callback,
                                             uintptr_t instance,
                                             uint32_t flags) {
    (void)callback; (void)instance;
    if (device != 0U && device != 0xFFFFFFFFU) return COMPAT_MMSYSERR_BADDEVICEID;
    if (!compat_wave_format_supported(format)) return COMPAT_WAVERR_BADFORMAT;
    if (!(flags & 0x0001U)) {
        if (!handle) return COMPAT_MMSYSERR_INVALPARAM;
        *handle = (void *)(uintptr_t)COMPAT_WAVEOUT_HANDLE;
    }
    return 0U;
}
static uint32_t WIN32_API compat_waveOutClose(void *handle) {
    return (uint32_t)(uintptr_t)handle == COMPAT_WAVEOUT_HANDLE
        ? 0U : COMPAT_MMSYSERR_INVALHANDLE;
}
static uint32_t WIN32_API compat_waveOutPrepareHeader(void *handle,
                                                      void *header,
                                                      uint32_t size) {
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE || !header)
        return COMPAT_MMSYSERR_INVALHANDLE;
    *(uint32_t *)((uint8_t *)header + 16U) |= COMPAT_WHDR_PREPARED;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutUnprepareHeader(void *handle,
                                                        void *header,
                                                        uint32_t size) {
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE || !header)
        return COMPAT_MMSYSERR_INVALHANDLE;
    *(uint32_t *)((uint8_t *)header + 16U) &= ~COMPAT_WHDR_PREPARED;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutWrite(void *handle, void *header,
                                              uint32_t size) {
    uint32_t *flags;
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE || !header)
        return COMPAT_MMSYSERR_INVALHANDLE;
    flags = (uint32_t *)((uint8_t *)header + 16U);
    if (!(*flags & COMPAT_WHDR_PREPARED)) return 34U; /* WAVERR_UNPREPARED */
    *flags |= COMPAT_WHDR_DONE;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutPause(void *handle) {
    return (uint32_t)(uintptr_t)handle == COMPAT_WAVEOUT_HANDLE ? 0U : 5U;
}
static uint32_t WIN32_API compat_waveOutRestart(void *handle) {
    return compat_waveOutPause(handle);
}
static uint32_t WIN32_API compat_waveOutReset(void *handle) {
    return compat_waveOutPause(handle);
}
static uint32_t WIN32_API compat_waveOutBreakLoop(void *handle) {
    return compat_waveOutPause(handle);
}
static uint32_t WIN32_API compat_waveOutGetPosition(void *handle, void *time,
                                                    uint32_t size) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE || !time || size < 8U)
        return COMPAT_MMSYSERR_INVALHANDLE;
    *(uint32_t *)time = 1U; /* TIME_MS */
    *(uint32_t *)((uint8_t *)time + 4U) = 0U;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutGetVolume(void *handle, uint32_t *volume) {
    if (!volume || ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE && handle))
        return COMPAT_MMSYSERR_INVALHANDLE;
    *volume = compat_wave_volume;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutSetVolume(void *handle, uint32_t volume) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE && handle)
        return COMPAT_MMSYSERR_INVALHANDLE;
    compat_wave_volume = volume;
    return 0U;
}
static uint32_t WIN32_API compat_waveOutGetID(void *handle, uint32_t *device) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEOUT_HANDLE || !device) return 5U;
    *device = 0U; return 0U;
}
static uint32_t WIN32_API compat_waveOutMessage(void *handle, uint32_t message,
                                                uintptr_t p1, uintptr_t p2) {
    (void)message; (void)p1; (void)p2;
    return (uint32_t)(uintptr_t)handle == COMPAT_WAVEOUT_HANDLE ? 0U : 5U;
}

static uint32_t WIN32_API compat_waveInOpen(void **handle, uint32_t device,
                                            const void *format,
                                            uintptr_t callback,
                                            uintptr_t instance,
                                            uint32_t flags) {
    uint32_t result = compat_waveOutOpen(handle, device, format, callback,
                                         instance, flags);
    if (!result && handle && !(flags & 0x0001U))
        *handle = (void *)(uintptr_t)COMPAT_WAVEIN_HANDLE;
    return result;
}
static uint32_t WIN32_API compat_waveInClose(void *handle) {
    return (uint32_t)(uintptr_t)handle == COMPAT_WAVEIN_HANDLE ? 0U : 5U;
}
static uint32_t WIN32_API compat_waveInPrepareHeader(void *handle, void *header,
                                                     uint32_t size) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEIN_HANDLE) return 5U;
    return compat_waveOutPrepareHeader((void *)(uintptr_t)COMPAT_WAVEOUT_HANDLE,
                                       header, size);
}
static uint32_t WIN32_API compat_waveInUnprepareHeader(void *handle, void *header,
                                                       uint32_t size) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEIN_HANDLE) return 5U;
    return compat_waveOutUnprepareHeader((void *)(uintptr_t)COMPAT_WAVEOUT_HANDLE,
                                         header, size);
}
static uint32_t WIN32_API compat_waveInAddBuffer(void *handle, void *header,
                                                 uint32_t size) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEIN_HANDLE) return 5U;
    return compat_waveOutWrite((void *)(uintptr_t)COMPAT_WAVEOUT_HANDLE,
                               header, size);
}
static uint32_t WIN32_API compat_waveInStart(void *handle) { return compat_waveInClose(handle) == 0U ? 0U : 5U; }
static uint32_t WIN32_API compat_waveInStop(void *handle) { return compat_waveInStart(handle); }
static uint32_t WIN32_API compat_waveInReset(void *handle) { return compat_waveInStart(handle); }
static uint32_t WIN32_API compat_waveInGetPosition(void *handle, void *time,
                                                   uint32_t size) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEIN_HANDLE) return 5U;
    return compat_waveOutGetPosition((void *)(uintptr_t)COMPAT_WAVEOUT_HANDLE,
                                     time, size);
}
static uint32_t WIN32_API compat_waveInGetID(void *handle, uint32_t *device) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_WAVEIN_HANDLE || !device) return 5U;
    *device = 0U; return 0U;
}
static uint32_t WIN32_API compat_waveInMessage(void *handle, uint32_t message,
                                               uintptr_t p1, uintptr_t p2) {
    (void)message; (void)p1; (void)p2;
    return (uint32_t)(uintptr_t)handle == COMPAT_WAVEIN_HANDLE ? 0U : 5U;
}

static uint32_t WIN32_API compat_midiOutOpen(void **handle, uint32_t device,
                                             uintptr_t callback,
                                             uintptr_t instance,
                                             uint32_t flags) {
    (void)callback; (void)instance; (void)flags;
    if (!handle) return 11U;
    if (device != 0U && device != 0xFFFFFFFFU) return 2U;
    *handle = (void *)(uintptr_t)COMPAT_MIDIOUT_HANDLE;
    return 0U;
}
static uint32_t WIN32_API compat_midiOutClose(void *handle) {
    return (uint32_t)(uintptr_t)handle == COMPAT_MIDIOUT_HANDLE ? 0U : 5U;
}
static uint32_t WIN32_API compat_midiOutShortMsg(void *handle, uint32_t message) {
    (void)message;
    return compat_midiOutClose(handle);
}
static uint32_t WIN32_API compat_midiOutReset(void *handle) { return compat_midiOutClose(handle); }
static uint32_t WIN32_API compat_midiOutPrepareHeader(void *handle, void *header,
                                                      uint32_t size) {
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_MIDIOUT_HANDLE || !header) return 5U;
    *(uint32_t *)((uint8_t *)header + 16U) |= COMPAT_MHDR_PREPARED;
    return 0U;
}
static uint32_t WIN32_API compat_midiOutUnprepareHeader(void *handle, void *header,
                                                        uint32_t size) {
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_MIDIOUT_HANDLE || !header) return 5U;
    *(uint32_t *)((uint8_t *)header + 16U) &= ~COMPAT_MHDR_PREPARED;
    return 0U;
}
static uint32_t WIN32_API compat_midiOutLongMsg(void *handle, void *header,
                                                uint32_t size) {
    uint32_t *flags;
    (void)size;
    if ((uint32_t)(uintptr_t)handle != COMPAT_MIDIOUT_HANDLE || !header) return 5U;
    flags = (uint32_t *)((uint8_t *)header + 16U);
    if (!(*flags & COMPAT_MHDR_PREPARED)) return 64U;
    *flags |= COMPAT_MHDR_DONE;
    return 0U;
}
static uint32_t WIN32_API compat_midiOutGetID(void *handle, uint32_t *device) {
    if ((uint32_t)(uintptr_t)handle != COMPAT_MIDIOUT_HANDLE || !device) return 5U;
    *device = 0U; return 0U;
}
static uint32_t WIN32_API compat_midiOutMessage(void *handle, uint32_t message,
                                                uintptr_t p1, uintptr_t p2) {
    (void)message; (void)p1; (void)p2;
    return compat_midiOutClose(handle);
}

static uint32_t WIN32_API compat_midiInOpen(void **handle, uint32_t device,
                                            uintptr_t callback,
                                            uintptr_t instance,
                                            uint32_t flags) {
    uint32_t result = compat_midiOutOpen(handle, device, callback, instance, flags);
    if (!result && handle) *handle = (void *)(uintptr_t)COMPAT_MIDIIN_HANDLE;
    return result;
}
static uint32_t WIN32_API compat_midiInClose(void *handle) {
    return (uint32_t)(uintptr_t)handle == COMPAT_MIDIIN_HANDLE ? 0U : 5U;
}
static uint32_t WIN32_API compat_midiInStart(void *handle) { return compat_midiInClose(handle); }
static uint32_t WIN32_API compat_midiInStop(void *handle) { return compat_midiInClose(handle); }
static uint32_t WIN32_API compat_midiInReset(void *handle) { return compat_midiInClose(handle); }

static uint32_t WIN32_API compat_auxGetVolume(uint32_t device, uint32_t *volume) {
    if (device != 0U || !volume) return 2U;
    *volume = compat_aux_volume; return 0U;
}
static uint32_t WIN32_API compat_auxSetVolume(uint32_t device, uint32_t volume) {
    if (device != 0U) return 2U;
    compat_aux_volume = volume; return 0U;
}
static uint32_t WIN32_API compat_auxOutMessage(uint32_t device, uint32_t message,
                                               uintptr_t p1, uintptr_t p2) {
    (void)message; (void)p1; (void)p2;
    return device == 0U ? 0U : 2U;
}

static uint32_t WIN32_API compat_mixerGetLineInfoA(void *mixer, void *raw,
                                                   uint32_t flags) {
    uint8_t *line = (uint8_t *)raw;
    (void)flags;
    if ((uint32_t)(uintptr_t)mixer != COMPAT_MIXER_HANDLE || !line) return 5U;
    if (*(uint32_t *)line < 120U) return 11U;
    *(uint32_t *)(line + 4U) = 0U;
    *(uint32_t *)(line + 8U) = 0U;
    *(uint32_t *)(line + 12U) = 1U;
    *(uint32_t *)(line + 16U) = 1U;
    *(uint32_t *)(line + 24U) = 4U;
    *(uint32_t *)(line + 28U) = 2U;
    *(uint32_t *)(line + 32U) = 0U;
    *(uint32_t *)(line + 36U) = 1U;
    compat_copy((char *)(line + 40U), 16U, "Speakers");
    compat_copy((char *)(line + 56U), 64U, "BlesKernOS Speakers");
    return 0U;
}
static uint32_t WIN32_API compat_mixerGetLineInfoW(void *mixer, void *raw,
                                                   uint32_t flags) {
    uint8_t *line = (uint8_t *)raw;
    uint32_t result = compat_mixerGetLineInfoA(mixer, raw, flags);
    if (!result) {
        compat_ansi_to_wide("Speakers", (uint16_t *)(line + 40U), 16U);
        compat_ansi_to_wide("BlesKernOS Speakers", (uint16_t *)(line + 72U), 64U);
    }
    return result;
}
static uint32_t WIN32_API compat_mixerGetLineControlsA(void *mixer, void *raw,
                                                       uint32_t flags) {
    uint8_t *request = (uint8_t *)raw;
    uint8_t *control;
    (void)flags;
    if ((uint32_t)(uintptr_t)mixer != COMPAT_MIXER_HANDLE || !request) return 5U;
    control = *(uint8_t **)(request + 20U);
    if (!control || *(uint32_t *)(request + 16U) < 100U) return 11U;
    kmemset(control, 0, *(uint32_t *)(request + 16U));
    *(uint32_t *)(control + 0U) = *(uint32_t *)(request + 16U);
    *(uint32_t *)(control + 4U) = 1U;
    *(uint32_t *)(control + 8U) = 0x50030001U; /* MIXERCONTROL_CONTROLTYPE_VOLUME */
    compat_copy((char *)(control + 20U), 16U, "Volume");
    compat_copy((char *)(control + 36U), 64U, "Master Volume");
    *(int32_t *)(control + 100U) = 0;
    *(int32_t *)(control + 104U) = 65535;
    return 0U;
}
static uint32_t WIN32_API compat_mixerGetLineControlsW(void *mixer, void *raw,
                                                       uint32_t flags) {
    return compat_mixerGetLineControlsA(mixer, raw, flags);
}
static uint32_t WIN32_API compat_mixerGetControlDetailsA(void *mixer, void *raw,
                                                         uint32_t flags) {
    uint8_t *request = (uint8_t *)raw;
    uint8_t *details;
    (void)flags;
    if ((uint32_t)(uintptr_t)mixer != COMPAT_MIXER_HANDLE || !request) return 5U;
    details = *(uint8_t **)(request + 20U);
    if (!details) return 11U;
    *(uint32_t *)details = compat_mixer_volume;
    return 0U;
}
static uint32_t WIN32_API compat_mixerGetControlDetailsW(void *mixer, void *raw,
                                                         uint32_t flags) {
    return compat_mixerGetControlDetailsA(mixer, raw, flags);
}
static uint32_t WIN32_API compat_mixerSetControlDetails(void *mixer, void *raw,
                                                        uint32_t flags) {
    uint8_t *request = (uint8_t *)raw;
    uint8_t *details;
    (void)flags;
    if ((uint32_t)(uintptr_t)mixer != COMPAT_MIXER_HANDLE || !request) return 5U;
    details = *(uint8_t **)(request + 20U);
    if (!details) return 11U;
    compat_mixer_volume = *(uint32_t *)details;
    return 0U;
}

/* ------------------------------------------------------------------------- */

uint32_t win32_win95_compat_resolve(const char *dll, const char *name) {
#define COMPAT_EXPORT(api) \
    if (compat_equal(name, #api)) return (uint32_t)(uintptr_t)&compat_##api

    if (!dll || !name) return 0U;

    if (compat_equal_ci(dll, "KERNEL32.DLL") ||
        compat_equal_ci(dll, "KERNELBASE.DLL")) {
        COMPAT_EXPORT(InitAtomTable);
        COMPAT_EXPORT(AddAtomA); COMPAT_EXPORT(AddAtomW);
        COMPAT_EXPORT(DeleteAtom);
        COMPAT_EXPORT(FindAtomA); COMPAT_EXPORT(FindAtomW);
        COMPAT_EXPORT(GetAtomNameA); COMPAT_EXPORT(GetAtomNameW);
        COMPAT_EXPORT(GlobalAddAtomA); COMPAT_EXPORT(GlobalAddAtomW);
        COMPAT_EXPORT(GlobalDeleteAtom);
        COMPAT_EXPORT(GlobalFindAtomA); COMPAT_EXPORT(GlobalFindAtomW);
        COMPAT_EXPORT(GlobalGetAtomNameA); COMPAT_EXPORT(GlobalGetAtomNameW);
        COMPAT_EXPORT(SetErrorMode);
        COMPAT_EXPORT(GetLogicalDriveStringsA);
        COMPAT_EXPORT(GetLogicalDriveStringsW);
        COMPAT_EXPORT(FileTimeToDosDateTime);
        COMPAT_EXPORT(SearchPathA); COMPAT_EXPORT(SearchPathW);
        COMPAT_EXPORT(GlobalCompact); COMPAT_EXPORT(LocalCompact);
        COMPAT_EXPORT(LocalShrink); COMPAT_EXPORT(GlobalFix); COMPAT_EXPORT(GlobalUnfix);
        COMPAT_EXPORT(GlobalWire); COMPAT_EXPORT(GlobalUnWire); COMPAT_EXPORT(GlobalDiscard);
        COMPAT_EXPORT(GlobalLRUNewest); COMPAT_EXPORT(GlobalLRUOldest);
        COMPAT_EXPORT(GlobalPageLock); COMPAT_EXPORT(GlobalPageUnlock);
        COMPAT_EXPORT(GlobalMemoryStatus); COMPAT_EXPORT(GlobalMemoryStatusEx);
        COMPAT_EXPORT(HeapLock); COMPAT_EXPORT(HeapUnlock); COMPAT_EXPORT(HeapValidate);
        COMPAT_EXPORT(HeapCompact); COMPAT_EXPORT(GetProcessHeaps);
        COMPAT_EXPORT(GetStdHandle); COMPAT_EXPORT(SetStdHandle); COMPAT_EXPORT(SetHandleCount);
        COMPAT_EXPORT(WriteFile);
        COMPAT_EXPORT(_lclose); COMPAT_EXPORT(_lopen); COMPAT_EXPORT(_lcreat);
        COMPAT_EXPORT(_llseek); COMPAT_EXPORT(_lread); COMPAT_EXPORT(_lwrite);
        COMPAT_EXPORT(_hread); COMPAT_EXPORT(_hwrite); COMPAT_EXPORT(OpenFile);
        COMPAT_EXPORT(DeleteFileA); COMPAT_EXPORT(DeleteFileW);
        COMPAT_EXPORT(MoveFileA); COMPAT_EXPORT(MoveFileW);
        COMPAT_EXPORT(MoveFileExA); COMPAT_EXPORT(MoveFileExW);
        COMPAT_EXPORT(RemoveDirectoryA); COMPAT_EXPORT(RemoveDirectoryW);
        COMPAT_EXPORT(CreateDirectoryW); COMPAT_EXPORT(CopyFileW);
        COMPAT_EXPORT(GetFileAttributesW); COMPAT_EXPORT(SetFileAttributesW);
        COMPAT_EXPORT(GetCurrentDirectoryW); COMPAT_EXPORT(SetCurrentDirectoryW);
        COMPAT_EXPORT(GetFullPathNameW); COMPAT_EXPORT(GetTempPathW);
        COMPAT_EXPORT(GetWindowsDirectoryW); COMPAT_EXPORT(GetSystemDirectoryW);
        COMPAT_EXPORT(GetModuleFileNameW);
        COMPAT_EXPORT(GetDiskFreeSpaceA); COMPAT_EXPORT(GetDiskFreeSpaceW);
        COMPAT_EXPORT(GetDiskFreeSpaceExA); COMPAT_EXPORT(GetDiskFreeSpaceExW);
        COMPAT_EXPORT(GetVolumeInformationA); COMPAT_EXPORT(GetVolumeInformationW);
        COMPAT_EXPORT(GetShortPathNameA); COMPAT_EXPORT(GetShortPathNameW);
        COMPAT_EXPORT(GetLongPathNameA); COMPAT_EXPORT(GetLongPathNameW);
        COMPAT_EXPORT(GetTempFileNameA); COMPAT_EXPORT(GetTempFileNameW);
        COMPAT_EXPORT(GetFileTime); COMPAT_EXPORT(SetFileTime);
        COMPAT_EXPORT(GetFileInformationByHandle);
        COMPAT_EXPORT(GetBinaryTypeA); COMPAT_EXPORT(GetBinaryTypeW);
        COMPAT_EXPORT(AreFileApisANSI); COMPAT_EXPORT(SetFileApisToANSI);
        COMPAT_EXPORT(SetFileApisToOEM);
        COMPAT_EXPORT(SetEnvironmentVariableA); COMPAT_EXPORT(SetEnvironmentVariableW);
        COMPAT_EXPORT(GetEnvironmentVariableA); COMPAT_EXPORT(GetEnvironmentVariableW);
        COMPAT_EXPORT(GetEnvironmentStringsA); COMPAT_EXPORT(GetEnvironmentStringsW);
        COMPAT_EXPORT(FreeEnvironmentStringsA); COMPAT_EXPORT(FreeEnvironmentStringsW);
        COMPAT_EXPORT(ExpandEnvironmentStringsA); COMPAT_EXPORT(ExpandEnvironmentStringsW);
        COMPAT_EXPORT(GetVersion); COMPAT_EXPORT(GetVersionExA); COMPAT_EXPORT(GetVersionExW);
        COMPAT_EXPORT(GetCommandLineW); COMPAT_EXPORT(GetStartupInfoW); COMPAT_EXPORT(WinExec);
        COMPAT_EXPORT(GetComputerNameA); COMPAT_EXPORT(GetComputerNameW);
        COMPAT_EXPORT(SetComputerNameA); COMPAT_EXPORT(SetComputerNameW);
        COMPAT_EXPORT(GetConsoleCP); COMPAT_EXPORT(GetConsoleOutputCP);
        COMPAT_EXPORT(SetConsoleCP); COMPAT_EXPORT(SetConsoleOutputCP);
        COMPAT_EXPORT(AllocConsole); COMPAT_EXPORT(FreeConsole);
        COMPAT_EXPORT(GetConsoleMode); COMPAT_EXPORT(SetConsoleMode);
        COMPAT_EXPORT(FlushConsoleInputBuffer);
        COMPAT_EXPORT(GetTickCount); COMPAT_EXPORT(Sleep); COMPAT_EXPORT(Beep);
        COMPAT_EXPORT(GetLocalTime); COMPAT_EXPORT(GetSystemTime);
        COMPAT_EXPORT(SystemTimeToFileTime); COMPAT_EXPORT(FileTimeToSystemTime);
        COMPAT_EXPORT(GetSystemTimeAsFileTime);
        COMPAT_EXPORT(LocalFileTimeToFileTime); COMPAT_EXPORT(FileTimeToLocalFileTime);
        COMPAT_EXPORT(CompareFileTime); COMPAT_EXPORT(DosDateTimeToFileTime);
        COMPAT_EXPORT(SystemTimeToTzSpecificLocalTime);
        COMPAT_EXPORT(GetSystemDefaultLangID); COMPAT_EXPORT(GetUserDefaultLangID);
        COMPAT_EXPORT(GetUserDefaultUILanguage);
        COMPAT_EXPORT(GetSystemDefaultLCID); COMPAT_EXPORT(GetUserDefaultLCID);
        COMPAT_EXPORT(GetThreadLocale); COMPAT_EXPORT(SetThreadLocale);
        COMPAT_EXPORT(IsValidCodePage); COMPAT_EXPORT(GetCPInfo);
        COMPAT_EXPORT(GetStringTypeA); COMPAT_EXPORT(GetStringTypeW);
        COMPAT_EXPORT(CompareStringA); COMPAT_EXPORT(CompareStringW);
        COMPAT_EXPORT(LCMapStringA); COMPAT_EXPORT(LCMapStringW);
    } else if (compat_equal_ci(dll, "USER32.DLL")) {
        COMPAT_EXPORT(PostThreadMessageA); COMPAT_EXPORT(PostThreadMessageW);
        COMPAT_EXPORT(MsgWaitForMultipleObjects);
        COMPAT_EXPORT(MsgWaitForMultipleObjectsEx);
        COMPAT_EXPORT(GetKeyboardLayout); COMPAT_EXPORT(GetKeyboardLayoutList);
        COMPAT_EXPORT(GetKeyboardLayoutNameA); COMPAT_EXPORT(GetKeyboardLayoutNameW);
        COMPAT_EXPORT(ActivateKeyboardLayout);
        COMPAT_EXPORT(LoadKeyboardLayoutA); COMPAT_EXPORT(LoadKeyboardLayoutW);
        COMPAT_EXPORT(UnloadKeyboardLayout); COMPAT_EXPORT(GetKeyboardType);
        COMPAT_EXPORT(MapVirtualKeyA); COMPAT_EXPORT(MapVirtualKeyW);
        COMPAT_EXPORT(VkKeyScanA); COMPAT_EXPORT(VkKeyScanW);
        COMPAT_EXPORT(GetAsyncKeyState);
        COMPAT_EXPORT(ToAscii); COMPAT_EXPORT(ToAsciiEx);
        COMPAT_EXPORT(ToUnicode); COMPAT_EXPORT(ToUnicodeEx);
        COMPAT_EXPORT(GetKeyNameTextA); COMPAT_EXPORT(GetKeyNameTextW);
        COMPAT_EXPORT(AttachThreadInput); COMPAT_EXPORT(GetInputState);
        COMPAT_EXPORT(GetQueueStatus); COMPAT_EXPORT(GetMessageTime);
        COMPAT_EXPORT(GetMessagePos); COMPAT_EXPORT(InSendMessage);
        COMPAT_EXPORT(InSendMessageEx);
        COMPAT_EXPORT(ShowCursor); COMPAT_EXPORT(SetCursorPos); COMPAT_EXPORT(ClipCursor);
        COMPAT_EXPORT(GetDoubleClickTime); COMPAT_EXPORT(SetDoubleClickTime);
        COMPAT_EXPORT(CreateCaret); COMPAT_EXPORT(DestroyCaret);
        COMPAT_EXPORT(ShowCaret); COMPAT_EXPORT(HideCaret);
        COMPAT_EXPORT(SetCaretPos); COMPAT_EXPORT(GetCaretPos);
        COMPAT_EXPORT(GetCaretBlinkTime); COMPAT_EXPORT(SetCaretBlinkTime);
    } else if (compat_equal_ci(dll, "GDI32.DLL")) {
        COMPAT_EXPORT(ExtTextOutA); COMPAT_EXPORT(ExtTextOutW);
        COMPAT_EXPORT(TextOutW); COMPAT_EXPORT(DrawTextW);
        COMPAT_EXPORT(GetTextExtentPoint32A); COMPAT_EXPORT(GetTextExtentPoint32W);
        COMPAT_EXPORT(GetTextExtentPointA); COMPAT_EXPORT(GetTextExtentPointW);
        COMPAT_EXPORT(GetTextMetricsA); COMPAT_EXPORT(GetTextMetricsW);
        COMPAT_EXPORT(GetDeviceCaps); COMPAT_EXPORT(SetROP2); COMPAT_EXPORT(GetROP2);
        COMPAT_EXPORT(SetMapMode); COMPAT_EXPORT(GetMapMode);
        COMPAT_EXPORT(SetStretchBltMode); COMPAT_EXPORT(GetStretchBltMode);
        COMPAT_EXPORT(SetPolyFillMode); COMPAT_EXPORT(GetPolyFillMode);
        COMPAT_EXPORT(SetViewportOrgEx); COMPAT_EXPORT(SetWindowOrgEx);
        COMPAT_EXPORT(SetViewportExtEx); COMPAT_EXPORT(SetWindowExtEx);
        COMPAT_EXPORT(CreateFontA); COMPAT_EXPORT(CreateFontW);
        COMPAT_EXPORT(CreateFontIndirectA); COMPAT_EXPORT(CreateFontIndirectW);
        COMPAT_EXPORT(GetTextFaceA); COMPAT_EXPORT(GetTextFaceW);
        COMPAT_EXPORT(GetTextCharset); COMPAT_EXPORT(GetTextCharsetInfo);
        COMPAT_EXPORT(GetCharWidthA); COMPAT_EXPORT(GetCharWidthW);
    } else if (compat_equal_ci(dll, "SHELL32.DLL")) {
        COMPAT_EXPORT(DragAcceptFiles);
        COMPAT_EXPORT(ShellAboutA); COMPAT_EXPORT(ShellAboutW);
        COMPAT_EXPORT(DragQueryFileW); COMPAT_EXPORT(DragQueryPoint);
        COMPAT_EXPORT(ShellExecuteW); COMPAT_EXPORT(ShellExecuteExA);
        COMPAT_EXPORT(ShellExecuteExW);
        COMPAT_EXPORT(FindExecutableA); COMPAT_EXPORT(FindExecutableW);
        COMPAT_EXPORT(SHGetSpecialFolderPathA); COMPAT_EXPORT(SHGetSpecialFolderPathW);
        COMPAT_EXPORT(SHGetFolderPathA); COMPAT_EXPORT(SHGetFolderPathW);
        COMPAT_EXPORT(SHGetSpecialFolderLocation);
        COMPAT_EXPORT(SHGetPathFromIDListA); COMPAT_EXPORT(SHGetPathFromIDListW);
        COMPAT_EXPORT(ILFree); COMPAT_EXPORT(ILClone);
        COMPAT_EXPORT(SHFileOperationA); COMPAT_EXPORT(SHFileOperationW);
        COMPAT_EXPORT(DllGetVersion);
    } else if (compat_equal_ci(dll, "SHFOLDER.DLL")) {
        COMPAT_EXPORT(SHGetFolderPathA); COMPAT_EXPORT(SHGetFolderPathW);
    } else if (compat_equal_ci(dll, "COMCTL32.DLL")) {
        COMPAT_EXPORT(InitCommonControlsEx); COMPAT_EXPORT(DllGetVersion);
    } else if (compat_equal_ci(dll, "WINMM.DLL")) {
        COMPAT_EXPORT(mixerGetNumDevs);
        COMPAT_EXPORT(mixerOpen); COMPAT_EXPORT(mixerClose);
        COMPAT_EXPORT(mixerGetID);
        COMPAT_EXPORT(mixerGetDevCapsA); COMPAT_EXPORT(mixerGetDevCapsW);
        COMPAT_EXPORT(mixerMessage);
        COMPAT_EXPORT(timeGetTime); COMPAT_EXPORT(timeBeginPeriod); COMPAT_EXPORT(timeEndPeriod);
        COMPAT_EXPORT(waveOutGetNumDevs); COMPAT_EXPORT(waveInGetNumDevs);
        COMPAT_EXPORT(midiOutGetNumDevs); COMPAT_EXPORT(midiInGetNumDevs);
        COMPAT_EXPORT(auxGetNumDevs); COMPAT_EXPORT(joyGetNumDevs);
        COMPAT_EXPORT(waveOutGetDevCapsA); COMPAT_EXPORT(waveOutGetDevCapsW);
        COMPAT_EXPORT(waveInGetDevCapsA); COMPAT_EXPORT(waveInGetDevCapsW);
        COMPAT_EXPORT(auxGetDevCapsA); COMPAT_EXPORT(auxGetDevCapsW);
        COMPAT_EXPORT(PlaySoundA); COMPAT_EXPORT(PlaySoundW);
        COMPAT_EXPORT(sndPlaySoundA); COMPAT_EXPORT(sndPlaySoundW);
        COMPAT_EXPORT(waveOutOpen); COMPAT_EXPORT(waveOutClose);
        COMPAT_EXPORT(waveOutPrepareHeader); COMPAT_EXPORT(waveOutUnprepareHeader);
        COMPAT_EXPORT(waveOutWrite); COMPAT_EXPORT(waveOutPause);
        COMPAT_EXPORT(waveOutRestart); COMPAT_EXPORT(waveOutReset);
        COMPAT_EXPORT(waveOutBreakLoop); COMPAT_EXPORT(waveOutGetPosition);
        COMPAT_EXPORT(waveOutGetVolume); COMPAT_EXPORT(waveOutSetVolume);
        COMPAT_EXPORT(waveOutGetID); COMPAT_EXPORT(waveOutMessage);
        COMPAT_EXPORT(waveInOpen); COMPAT_EXPORT(waveInClose);
        COMPAT_EXPORT(waveInPrepareHeader); COMPAT_EXPORT(waveInUnprepareHeader);
        COMPAT_EXPORT(waveInAddBuffer); COMPAT_EXPORT(waveInStart);
        COMPAT_EXPORT(waveInStop); COMPAT_EXPORT(waveInReset);
        COMPAT_EXPORT(waveInGetPosition); COMPAT_EXPORT(waveInGetID);
        COMPAT_EXPORT(waveInMessage);
        COMPAT_EXPORT(midiOutOpen); COMPAT_EXPORT(midiOutClose);
        COMPAT_EXPORT(midiOutShortMsg); COMPAT_EXPORT(midiOutReset);
        COMPAT_EXPORT(midiOutPrepareHeader); COMPAT_EXPORT(midiOutUnprepareHeader);
        COMPAT_EXPORT(midiOutLongMsg); COMPAT_EXPORT(midiOutGetID);
        COMPAT_EXPORT(midiOutMessage);
        COMPAT_EXPORT(midiInOpen); COMPAT_EXPORT(midiInClose);
        COMPAT_EXPORT(midiInStart); COMPAT_EXPORT(midiInStop); COMPAT_EXPORT(midiInReset);
        COMPAT_EXPORT(auxGetVolume); COMPAT_EXPORT(auxSetVolume); COMPAT_EXPORT(auxOutMessage);
        COMPAT_EXPORT(mixerGetLineInfoA); COMPAT_EXPORT(mixerGetLineInfoW);
        COMPAT_EXPORT(mixerGetLineControlsA); COMPAT_EXPORT(mixerGetLineControlsW);
        COMPAT_EXPORT(mixerGetControlDetailsA); COMPAT_EXPORT(mixerGetControlDetailsW);
        COMPAT_EXPORT(mixerSetControlDetails);
        COMPAT_EXPORT(mciSendStringA); COMPAT_EXPORT(mciSendStringW);
    }

#undef COMPAT_EXPORT
    return 0U;
}
