#ifndef BLESKERNOS_WIN32_H
#define BLESKERNOS_WIN32_H
#include "../include/types.h"
#define WIN32_API __attribute__((stdcall))
uint32_t win32_resolve_import(const char *dll, const char *name);
uint32_t win32_resolve_ordinal(const char *dll, uint16_t ordinal);
void win32_msvcrt_cleanup_process(uint32_t pid);
void win32_msvcrt_cleanup_thread(uint32_t tid);
bool win32_global_handle_valid(void *handle);
void win32_global_release_handle(void *handle);
void *win32_global_alloc_block(uint32_t flags, uint32_t size);
void *win32_global_lock_block(void *handle);
int win32_global_unlock_block(void *handle);
uint32_t win32_global_size_block(void *handle);
int win32_file_write(void *handle, const void *buffer, uint32_t length,
                     uint32_t *written);
bool win32_gdi_text(void *hwnd,int x,int y,const char *text,uint32_t color);
bool win32_gdi_line(void *hwnd,int x1,int y1,int x2,int y2,uint32_t color);
bool win32_gdi_rect(void *hwnd,int left,int top,int right,int bottom,uint32_t color);
bool win32_gdi_fill_rect(void *hwnd,int left,int top,int right,int bottom,uint32_t color);
void win32_gdi_begin(void *hwnd);
bool win32_gdi_blit(void *hwnd,int dx,int dy,int w,int h,const uint32_t *pixels,int pitch,int sx,int sy);
bool win32_toolbar_configure(void *hwnd, const void *buttons,
                               uint32_t count, int button_width,
                               int button_height);
bool win32_user_path_dialog(const char *title, char *buffer,
                            uint32_t capacity, bool save_mode);
void *win32_user_find_dialog(const char *title, void *owner,
                             uint32_t notify_message, void *find_replace,
                             bool replace_mode);
#endif
