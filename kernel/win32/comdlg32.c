#include "win32.h"
#include "../include/types.h"
#include "../include/pe_loader.h"
#include "../include/memory.h"
#include "../string.h"

#define CDERR_NONE 0U
#define CDERR_STRUCTSIZE 1U
#define CDERR_INITIALIZATION 2U
#define CDERR_MEMALLOCFAILURE 9U
#define FNERR_INVALIDFILENAME 0x3002U
#define PDERR_NODEFAULTPRN 0x1008U
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFU
#define FILE_ATTRIBUTE_DIRECTORY 0x10U
#define OFN_PATHMUSTEXIST 0x00000800U
#define OFN_FILEMUSTEXIST 0x00001000U
#define OFN_NOVALIDATE 0x00000100U
#define FR_DOWN 0x00000001U
#define FR_WHOLEWORD 0x00000002U
#define FR_MATCHCASE 0x00000004U

/* OPENFILENAMEA layout for PE32. */
typedef struct {
    uint32_t lStructSize;
    void *hwndOwner;
    void *hInstance;
    const char *lpstrFilter;
    char *lpstrCustomFilter;
    uint32_t nMaxCustFilter;
    uint32_t nFilterIndex;
    char *lpstrFile;
    uint32_t nMaxFile;
    char *lpstrFileTitle;
    uint32_t nMaxFileTitle;
    const char *lpstrInitialDir;
    const char *lpstrTitle;
    uint32_t Flags;
    uint16_t nFileOffset;
    uint16_t nFileExtension;
    const char *lpstrDefExt;
    int32_t lCustData;
    void *lpfnHook;
    const char *lpTemplateName;
    void *pvReserved;
    uint32_t dwReserved;
    uint32_t FlagsEx;
} open_file_name_a_t;

typedef struct {
    uint32_t lStructSize;
    void *hwndOwner;
    void *hInstance;
    uint32_t Flags;
    char *lpstrFindWhat;
    char *lpstrReplaceWith;
    uint16_t wFindWhatLen;
    uint16_t wReplaceWithLen;
    int32_t lCustData;
    void *lpfnHook;
    const char *lpTemplateName;
} find_replace_a_t;

typedef struct {
    uint32_t lStructSize;
    void *hwndOwner;
    void *hDC;
    void *lpLogFont;
    int32_t iPointSize;
    uint32_t Flags;
    uint32_t rgbColors;
    int32_t lCustData;
    void *lpfnHook;
    const char *lpTemplateName;
    void *hInstance;
    char *lpszStyle;
    uint16_t nFontType;
    uint16_t alignment;
    int32_t nSizeMin;
    int32_t nSizeMax;
} choose_font_a_t;

typedef struct {
    uint32_t lStructSize;
    void *hwndOwner;
    void *hInstance;
    uint32_t rgbResult;
    uint32_t *lpCustColors;
    uint32_t Flags;
    int32_t lCustData;
    void *lpfnHook;
    const char *lpTemplateName;
} choose_color_a_t;

typedef uint32_t (WIN32_API *get_current_directory_a_t)(uint32_t, char *);
typedef uint32_t (WIN32_API *get_file_attributes_a_t)(const char *);
typedef uint32_t (WIN32_API *register_window_message_a_t)(const char *);

static uint32_t common_dialog_error;
static bool equal(const char *a,const char *b){if(!a||!b)return false;while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static bool has_extension(const char *path) {
    const char *base = path, *dot = NULL;
    if (!path) return false;
    while (*path) { if (*path == '/' || *path == '\\') { base = path + 1; dot = NULL; } else if (*path == '.') dot = path; path++; }
    return dot && dot > base && dot[1] != '\0';
}
static void append_default_extension(char *path, uint32_t capacity, const char *ext) {
    uint32_t length;
    if (!path || !capacity || !ext || !*ext || has_extension(path)) return;
    length = (uint32_t)kstrlen(path);
    if (length + 1U + (uint32_t)kstrlen(ext) >= capacity) return;
    path[length++] = '.';
    kstrcpy(path + length, ext[0] == '.' ? ext + 1 : ext);
}
static void fill_offsets(open_file_name_a_t *data) {
    uint32_t length, base = 0U, dot = 0U;
    if (!data || !data->lpstrFile) return;
    length = (uint32_t)kstrlen(data->lpstrFile);
    for (uint32_t i = 0; i < length; i++) {
        if (data->lpstrFile[i] == '/' || data->lpstrFile[i] == '\\') { base = i + 1U; dot = 0U; }
        else if (data->lpstrFile[i] == '.') dot = i + 1U;
    }
    data->nFileOffset = (uint16_t)base;
    data->nFileExtension = (uint16_t)dot;
    if (data->lpstrFileTitle && data->nMaxFileTitle) {
        kstrncpy(data->lpstrFileTitle, data->lpstrFile + base, data->nMaxFileTitle - 1U);
        data->lpstrFileTitle[data->nMaxFileTitle - 1U] = '\0';
    }
}
static int file_dialog(open_file_name_a_t *data, bool save) {
    char initial[512];
    get_current_directory_a_t get_current;
    get_file_attributes_a_t get_attributes;
    uint32_t attributes;
    if (!data || data->lStructSize < 76U || !data->lpstrFile || data->nMaxFile < 2U) {
        common_dialog_error = CDERR_STRUCTSIZE; return 0;
    }
    initial[0] = '\0';
    if (data->lpstrFile[0]) kstrncpy(initial, data->lpstrFile, sizeof(initial) - 1U);
    else if (data->lpstrInitialDir && *data->lpstrInitialDir) {
        kstrncpy(initial, data->lpstrInitialDir, sizeof(initial) - 2U);
        uint32_t n = (uint32_t)kstrlen(initial);
        if (n && initial[n - 1U] != '/' && initial[n - 1U] != '\\') { initial[n++] = '/'; initial[n] = '\0'; }
    } else {
        get_current = (get_current_directory_a_t)(uintptr_t)
            pe_win32_resolve_export("KERNEL32.DLL", "GetCurrentDirectoryA");
        if (get_current) (void)get_current(sizeof(initial), initial);
    }
    if (!initial[0]) kstrcpy(initial, "/CDROM/");
    kstrncpy(data->lpstrFile, initial, data->nMaxFile - 1U);
    data->lpstrFile[data->nMaxFile - 1U] = '\0';
    if (!win32_user_path_dialog(data->lpstrTitle ? data->lpstrTitle :
        (save ? "Save As" : "Open"), data->lpstrFile, data->nMaxFile, save)) {
        common_dialog_error = CDERR_NONE; return 0;
    }
    if (save) append_default_extension(data->lpstrFile, data->nMaxFile, data->lpstrDefExt);
    get_attributes = (get_file_attributes_a_t)(uintptr_t)
        pe_win32_resolve_export("KERNEL32.DLL", "GetFileAttributesA");
    attributes = get_attributes ? get_attributes(data->lpstrFile) : INVALID_FILE_ATTRIBUTES;
    if (!save && !(data->Flags & OFN_NOVALIDATE) &&
        (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))) {
        common_dialog_error = FNERR_INVALIDFILENAME; return 0;
    }
    fill_offsets(data);
    common_dialog_error = CDERR_NONE;
    return 1;
}

static uint32_t WIN32_API comdlg_CommDlgExtendedError(void) { return common_dialog_error; }
static int WIN32_API comdlg_GetOpenFileNameA(void *data) { return file_dialog((open_file_name_a_t *)data, false); }
static int WIN32_API comdlg_GetSaveFileNameA(void *data) { return file_dialog((open_file_name_a_t *)data, true); }

static int WIN32_API comdlg_ChooseFontA(void *raw) {
    choose_font_a_t *data = (choose_font_a_t *)raw;
    uint8_t *logfont;
    if (!data || data->lStructSize < 60U || !data->lpLogFont) {
        common_dialog_error = CDERR_STRUCTSIZE; return 0;
    }
    logfont = (uint8_t *)data->lpLogFont;
    if (*(int32_t *)(logfont + 0U) == 0) *(int32_t *)(logfont + 0U) = -14;
    if (!logfont[28]) kstrcpy((char *)(logfont + 28U), "Tahoma");
    data->iPointSize = 90;
    data->nFontType = 0x0004U;
    common_dialog_error = CDERR_NONE;
    return 1;
}
static int WIN32_API comdlg_ChooseColorA(void *raw) {
    choose_color_a_t *data = (choose_color_a_t *)raw;
    if (!data || data->lStructSize < 36U) { common_dialog_error = CDERR_STRUCTSIZE; return 0; }
    if (!data->rgbResult && data->lpCustColors) data->rgbResult = data->lpCustColors[0];
    common_dialog_error = CDERR_NONE; return 1;
}
static int WIN32_API comdlg_PrintDlgA(void *data UNUSED) { common_dialog_error = PDERR_NODEFAULTPRN; return 0; }
static int WIN32_API comdlg_PageSetupDlgA(void *data UNUSED) { common_dialog_error = PDERR_NODEFAULTPRN; return 0; }

static void *find_dialog(void *raw, bool replace) {
    find_replace_a_t *data = (find_replace_a_t *)raw;
    register_window_message_a_t register_message;
    uint32_t message;
    if (!data || data->lStructSize < 40U || !data->hwndOwner ||
        !data->lpstrFindWhat || !data->wFindWhatLen) {
        common_dialog_error = CDERR_STRUCTSIZE; return NULL;
    }
    register_message = (register_window_message_a_t)(uintptr_t)
        pe_win32_resolve_export("USER32.DLL", "RegisterWindowMessageA");
    message = register_message ? register_message("commdlg_FindReplace") : 0U;
    if (!message) { common_dialog_error = CDERR_INITIALIZATION; return NULL; }
    common_dialog_error = CDERR_NONE;
    return win32_user_find_dialog(replace ? "Replace" : "Find", data->hwndOwner,
                                  message, data, replace);
}
static void *WIN32_API comdlg_FindTextA(void *data) { return find_dialog(data, false); }
static void *WIN32_API comdlg_ReplaceTextA(void *data) { return find_dialog(data, true); }

uint32_t win32_comdlg32_resolve(const char *name) {
#define C(api) if(equal(name,#api))return(uint32_t)(uintptr_t)&comdlg_##api
    C(GetOpenFileNameA); C(GetSaveFileNameA); C(ChooseFontA); C(ChooseColorA);
    C(PrintDlgA); C(PageSetupDlgA); C(FindTextA); C(ReplaceTextA);
    C(CommDlgExtendedError);
#undef C
    return 0;
}
