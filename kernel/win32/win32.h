#ifndef BLESKERNOS_WIN32_H
#define BLESKERNOS_WIN32_H
#include "../include/types.h"
#define WIN32_API __attribute__((stdcall))

typedef uint32_t (*win32_named_resolver_t)(const char *dll, const char *name);
typedef uint32_t (*win32_ordinal_resolver_t)(const char *dll, uint16_t ordinal);
typedef bool (*win32_data_resolver_t)(const char *dll, const char *name);
bool win32_register_resolver(win32_named_resolver_t named,
                             win32_ordinal_resolver_t ordinal,
                             win32_data_resolver_t data);
bool win32_import_is_data(const char *dll, const char *name);
uint32_t win32_resolve_import(const char *dll, const char *name);
uint32_t win32_resolve_ordinal(const char *dll, uint16_t ordinal);
bool win32_is_builtin_dll(const char *dll);
void win32_msvcrt_cleanup_process(uint32_t pid);
void win32_msvcrt_cleanup_thread(uint32_t tid);
void win32_winsock_cleanup_process(uint32_t pid);
void win32_winmm_cleanup_process(uint32_t pid);
void win32_ole32_cleanup_process(uint32_t pid);
void win32_lz32_cleanup_process(uint32_t pid);
void win32_wininet_cleanup_process(uint32_t pid);
void win32_dx_cleanup_process(uint32_t pid);
void win32_legacy_cleanup_process(uint32_t pid);
void win32_kernel32_cleanup_process(uint32_t pid);
int WIN32_API win32_kernel32_ReadFile(void *handle, void *buffer,
                                      uint32_t length, uint32_t *read,
                                      void *overlapped);
int WIN32_API win32_kernel32_CloseHandle(void *handle);
void win32_user32_cleanup_process(uint32_t pid);
int WIN32_API win32_user_message_box_a(void *owner, const char *text,
                                        const char *caption, uint32_t type);
/* BLES_WINE_SYNC_WINDOW_FIX_20260723
 * Fixed-layout request shared with the Ring-3 SendMessage thunk.  A same-task
 * PE WndProc is returned to user mode instead of being converted into a
 * deferred upcall. */
typedef struct {
    uint32_t invoke;
    uint32_t proc;
    uint32_t hwnd;
    uint32_t message;
    uint32_t wparam;
    int32_t lparam;
    int32_t result;
    uint32_t flags;
} win32_user_message_plan_t;

int WIN32_API win32_user_send_message_prepare_a(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    win32_user_message_plan_t *plan);
int WIN32_API win32_user_send_message_prepare_w(
    void *hwnd, uint32_t message, uint32_t wparam, int32_t lparam,
    win32_user_message_plan_t *plan);
int32_t WIN32_API win32_user_send_message_complete(
    win32_user_message_plan_t *plan, int32_t result);

/* Modal DialogBox* helpers called by the Ring-3 loop thunk. */
void *WIN32_API win32_user_dialog_begin_param_a(
    void *instance, const char *template_name, void *parent, void *proc,
    int32_t init_param);
void *WIN32_API win32_user_dialog_begin_param_w(
    void *instance, const uint16_t *template_name, void *parent, void *proc,
    int32_t init_param);
void *WIN32_API win32_user_dialog_begin_indirect_a(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param);
void *WIN32_API win32_user_dialog_begin_indirect_w(
    void *instance, const void *template_data, void *parent, void *proc,
    int32_t init_param);
typedef struct {
    int32_t result;
    uint32_t invoke;
    uint32_t proc;
    uint32_t hwnd;
    uint32_t message;
    uint32_t wparam;
    int32_t lparam;
} win32_user_dialog_plan_t;
int WIN32_API win32_user_dialog_modal_step(
    void *hwnd, win32_user_dialog_plan_t *plan);
void win32_winsock_poll(void);
void win32_winmm_poll(void);
bool win32_user_post_message(void *hwnd, uint32_t message,
                             uint32_t wparam, int32_t lparam);
bool win32_global_handle_valid(void *handle);
void win32_global_release_handle(void *handle);
void win32_global_transfer_handle(void *handle, uint32_t owner_process_id);
void *win32_global_alloc_block(uint32_t flags, uint32_t size);
void *win32_global_lock_block(void *handle);
int win32_global_unlock_block(void *handle);
uint32_t win32_global_size_block(void *handle);
bool win32_registry_query_string(uint32_t root, const char *path,
                                 const char *name, char *out,
                                 uint32_t capacity);
int win32_file_write(void *handle, const void *buffer, uint32_t length,
                     uint32_t *written);
/* BLES_WINE_DIALOG_UI_PERF_FIX_20260723 */
bool win32_gdi_text_ex(void *hwnd, int x, int y, const char *text,
                       uint32_t color, int pixel_height, bool bold,
                       bool italic, bool monospace);
bool win32_gdi_text(void *hwnd,int x,int y,const char *text,uint32_t color);
bool win32_gdi_font_query(void *font, int *pixel_height, bool *bold,
                          bool *italic, bool *monospace);
void *win32_gdi_create_font_internal(int pixel_height, int weight,
                                     bool italic, bool monospace,
                                     const char *face);
/* Convert an RT_BITMAP resource handle into a real GDI HBITMAP. */
void *win32_gdi_bitmap_from_resource(void *resource);
bool win32_gdi_bitmap_query(void *bitmap, int *width, int *height,
                            const uint32_t **pixels);
void win32_gdi_cleanup_process(uint32_t pid);
bool win32_gdi_line(void *hwnd,int x1,int y1,int x2,int y2,uint32_t color);
bool win32_gdi_rect(void *hwnd,int left,int top,int right,int bottom,uint32_t color);
bool win32_gdi_fill_rect(void *hwnd,int left,int top,int right,int bottom,uint32_t color);
void win32_gdi_begin(void *hwnd);
bool win32_gdi_blit(void *hwnd,int dx,int dy,int w,int h,const uint32_t *pixels,int pitch,int sx,int sy);
bool win32_directdraw_blit(void *hwnd,int w,int h,const uint32_t *pixels);
bool win32_toolbar_configure(void *hwnd, const void *buttons,
                               uint32_t count, int button_width,
                               int button_height);
bool win32_comctl_image_list_get_pixels(void *handle, int index,
                                        const uint32_t **pixels,
                                        int *width, int *height);
bool win32_user_path_dialog(const char *title, char *buffer,
                            uint32_t capacity, bool save_mode);
bool win32_user_file_dialog(const char *title, const char *initial_dir,
                            const char *extension, char *buffer,
                            uint32_t capacity);
void *win32_user_find_dialog(const char *title, void *owner,
                             uint32_t notify_message, void *find_replace,
                             bool replace_mode);
#endif
