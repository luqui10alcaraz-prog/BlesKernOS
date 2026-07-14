#ifndef BK_API_H
#define BK_API_H

#include "types.h"
#include "memory.h"
#include "vfs.h"
#include "gfx.h"
#include "mouse.h"
#include "keyboard.h"
#include "rtc.h"
#include "task.h"
#include "../../gui/gui.h"
#include "file_dialog.h"
#include "block.h"
#include "ata.h"
#include "pci.h"
#include "driver.h"
#include "iso9660.h"
#include "elf_loader.h"
#include "pe_loader.h"
#include "about_dialog.h"
#include "startup_sound.h"
#include "sound.h"
#include "user_config.h"
#include "datetime_prefs.h"
#include "shell.h"
#include "vga.h"
#include "../stdio.h"
#include "../../gui/image.h"
#include "../../programs/programs.h"

#define BK_API_VERSION 8

/*
 * API publica y estable para aplicaciones. Los controladores del kernel no
 * forman parte de esta ABI: una aplicacion debe incluir este archivo y usar
 * exclusivamente simbolos bk_*.
 */
#define BK_API_CAP_SYSTEM   0x00000001U
#define BK_API_CAP_FILES    0x00000002U
#define BK_API_CAP_GUI      0x00000004U
#define BK_API_CAP_GRAPHICS 0x00000008U
#define BK_API_CAP_INPUT    0x00000010U
#define BK_API_CAP_SOUND    0x00000020U
#define BK_API_CAP_TIME     0x00000040U
#define BK_API_CAP_PROCESS  0x00000080U
#define BK_API_CAP_APP      0x00000100U
#define BK_API_CAP_ALL      0x000001FFU

#define BK_FILE_READ_ONLY   VFS_O_RDONLY
#define BK_FILE_WRITE_ONLY  VFS_O_WRONLY
#define BK_FILE_READ_WRITE  VFS_O_RDWR
#define BK_FILE_MAX_PATH    VFS_MAX_PATH
#define BK_FILE_MAX_NAME    VFS_MAX_NAME

typedef vfs_dir_entry_t bk_file_entry_t;
typedef gfx_info_t bk_gfx_info_t;
typedef gfx_display_mode_t bk_gfx_mode_t;
typedef mouse_state_t bk_mouse_state_t;
typedef kbd_key_event_t bk_key_event_t;
typedef kbd_modifiers_t bk_key_modifiers_t;
typedef rtc_datetime_t bk_datetime_t;

typedef enum {
    BK_PROC_UNUSED = 0,
    BK_PROC_READY,
    BK_PROC_RUNNING,
    BK_PROC_SLEEPING,
    BK_PROC_ZOMBIE
} bk_proc_state_t;

typedef struct {
    uint32_t pid;
    uint32_t process_id;
    char name[24];
    bk_proc_state_t state;
    uint32_t cpu_ticks;
    uint32_t memory_bytes;
    bool system;
    bool user;
    bool exit_requested;
} bk_proc_info_t;

typedef void (*bk_thread_entry_t)(void *argument);

/* 10 system APIs */
uint32_t bk_sys_api_version(void);
uint32_t bk_sys_capabilities(void);
void bk_sys_log(const char *message);
void bk_console_putchar(char character);
void bk_console_write(const char *text);
uint32_t bk_sys_getpid(void);
void bk_sys_yield(void);
void bk_sys_sleep_ticks(uint32_t ticks);
void bk_sys_sleep_ms(uint32_t milliseconds);
uint32_t bk_sys_ticks(void);
uint32_t bk_sys_tick_frequency(void);
uint32_t bk_sys_uptime_ms(void);
void bk_sys_reboot(void);
void bk_sys_shutdown(void);
void *bk_sys_alloc(size_t size);
void *bk_sys_alloc_zero(size_t size);
void *bk_sys_realloc(void *ptr, size_t size);
void bk_sys_free(void *ptr);
bool bk_sys_memory_info(system_memory_info_t *info);

/* 10 file APIs */
int bk_file_open(const char *path, uint32_t flags);
int bk_file_read(int fd, void *buffer, uint32_t size);
int bk_file_write(int fd, const void *buffer, uint32_t size);
bool bk_file_close(int fd);
bool bk_file_read_all(const char *path, void **buffer, uint32_t *size);
bool bk_file_write_all(const char *path, const void *buffer, uint32_t size);
bool bk_file_list_dir(const char *path, vfs_dir_entry_t *entries,
                      uint32_t max_entries, uint32_t *count);
bool bk_file_chdir(const char *path);
const char *bk_file_getcwd(void);
bool bk_file_mkdir(const char *path);
bool bk_file_remove(const char *path);
bool bk_file_rename(const char *old_path, const char *new_path);
bool bk_file_space(uint64_t *total_bytes, uint64_t *free_bytes);
bool bk_device_format_fat(const char *device_name, const char *volume_label);

/* 10 GUI APIs */
gui_desktop_t *bk_gui_desktop(void);
void bk_gui_request_paint(void);
gui_window_t *bk_gui_create_window(gui_desktop_t *desktop, int x, int y,
                                   int w, int h, const char *title);
void bk_gui_close_window(gui_window_t *window);
void bk_gui_focus_window(gui_desktop_t *desktop, gui_window_t *window);
void bk_gui_set_window_content(gui_window_t *window,
                               gui_window_content_paint_t paint,
                               void *context);
void bk_gui_set_window_event_handler(gui_window_t *window,
                                     gui_window_event_t handler,
                                     void *context);
void bk_gui_set_window_min_size(gui_window_t *window, int min_w, int min_h);
int bk_gui_add_menu(gui_window_t *window, const char *label);
bool bk_gui_add_menu_item(gui_window_t *window, int menu, uint32_t id,
                          const char *label, gui_menu_callback_t callback,
                          void *context);
void bk_gui_destroy_window(gui_desktop_t *desktop, gui_window_t *window);
bool bk_gui_window_is_open(const gui_window_t *window);
void bk_gui_window_set_owner(gui_window_t *window, uint32_t pid);
void bk_gui_window_invalidate(gui_window_t *window);
bool bk_gui_window_bounds(const gui_window_t *window, gui_rect_t *bounds);
bool bk_gui_window_content_rect(const gui_window_t *window, gui_rect_t *rect);
void bk_gui_surface_clear(gui_surface_t *surface, uint32_t color);
void bk_gui_surface_putpixel(gui_surface_t *surface, int x, int y,
                             uint32_t color);
void bk_gui_surface_fill_rect(gui_surface_t *surface, gui_rect_t rect,
                              uint32_t color);
void bk_gui_surface_draw_rect(gui_surface_t *surface, gui_rect_t rect,
                              uint32_t color);
void bk_gui_surface_draw_line(gui_surface_t *surface, int x0, int y0,
                              int x1, int y1, uint32_t color);
void bk_gui_surface_draw_text(gui_surface_t *surface, int x, int y,
                              const char *text, uint32_t fg, uint32_t bg,
                              bool fill_bg);
uint16_t bk_gui_text_width(const char *text);

/* 10 graphics/input APIs */
bool bk_gfx_info(gfx_info_t *info);
bool bk_gfx_set_mode(uint16_t width, uint16_t height, uint8_t bpp);
void bk_gfx_clear(uint32_t rgb);
void bk_gfx_putpixel(int x, int y, uint32_t rgb);
uint32_t bk_gfx_getpixel(int x, int y);
void bk_gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void bk_gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb);
void bk_gfx_draw_text(int x, int y, const char *text, uint8_t fg,
                      uint8_t bg, bool fill_bg);
bool bk_input_mouse(mouse_state_t *state);
bool bk_input_key_event(kbd_key_event_t *event);
bool bk_input_key_modifiers(kbd_modifiers_t *modifiers);
void bk_input_mouse_set_position(int32_t x, int32_t y);
void bk_input_mouse_set_sensitivity(uint8_t sensitivity);
uint8_t bk_input_mouse_get_sensitivity(void);

/* 10 sound/time/process APIs */
bool bk_sound_has_sb16(void);
bool bk_sound_pcm_available(void);
bool bk_sound_pcm_busy(void);
const char *bk_sound_pcm_name(void);
bool bk_sound_play_pcm_u8(const uint8_t *samples, uint32_t length,
                          uint16_t sample_rate_hz, uint8_t volume);
bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms);
void bk_sound_stop(void);
bool bk_time_datetime(rtc_datetime_t *datetime);
uint32_t bk_proc_count(void);
const task_t *bk_proc_get(uint32_t index);
bool bk_proc_info(uint32_t index, bk_proc_info_t *info);
bool bk_proc_request_exit(uint32_t pid);
bool bk_proc_exit_requested(void);
void bk_proc_set_memory_hint(uint32_t bytes);
void bk_proc_bind_window(gui_window_t *window);
const char *bk_proc_launch_arg(void);
int bk_proc_spawn_thread(const char *name, bk_thread_entry_t entry,
                         void *argument);
void bk_proc_exit(void) NORETURN;

bool bk_app_launch(const char *path, const char *argument);
bool bk_shell_take_exit_request(void);

#ifndef BK_API_IMPLEMENTATION
#include "api_compat.h"
#endif

#endif
