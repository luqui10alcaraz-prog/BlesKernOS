#define BK_API_IMPLEMENTATION
#include "include/api.h"
#include "include/pit.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/gfx.h"
#include "include/mouse.h"
#include "include/keyboard.h"
#include "include/rtc.h"
#include "include/datetime_prefs.h"
#include "include/sound.h"
#include "include/task.h"
#include "include/pic.h"
#include "../programs/programs.h"
#include "stdio.h"

static bool g_datetime_runtime_preferences_valid = false;
static bk_datetime_preferences_t g_datetime_runtime_preferences;

uint32_t bk_sys_api_version(void) {
    return BK_API_VERSION;
}

uint32_t bk_sys_capabilities(void) {
    return BK_API_CAP_ALL;
}

void bk_sys_log(const char *message) {
    if (!message) return;
    kprintf("%s", message);
}

void bk_console_putchar(char character) {
    vga_putchar(character);
}

void bk_console_write(const char *text) {
    if (text) vga_puts(text);
}

uint32_t bk_sys_getpid(void) {
    return task_current_pid();
}

void bk_sys_yield(void) {
    task_yield();
}

void bk_sys_sleep_ticks(uint32_t ticks) {
    task_sleep(ticks);
}

void bk_sys_sleep_ms(uint32_t milliseconds) {
    uint32_t hz = pit_get_frequency_hz();
    uint64_t ticks;

    if (!milliseconds) {
        task_yield();
        return;
    }
    if (!hz) hz = 100U;
    ticks = ((uint64_t)milliseconds * hz + 999U) / 1000U;
    task_sleep((uint32_t)(ticks ? ticks : 1U));
}

uint32_t bk_sys_ticks(void) {
    return pit_get_ticks();
}

uint32_t bk_sys_tick_frequency(void) {
    return pit_get_frequency_hz();
}

uint32_t bk_sys_uptime_ms(void) {
    uint32_t hz = pit_get_frequency_hz();
    return hz ? (uint32_t)(((uint64_t)pit_get_ticks() * 1000U) / hz) : 0;
}

void bk_sys_reboot(void) {
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("cli; hlt");
}

void bk_sys_shutdown(void) {
    /* ACPI poweroff usado por QEMU/Bochs; hardware sin ACPI cae en halt. */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000),
                      "Nd"((uint16_t)0x604));
    for (;;) __asm__ volatile ("cli; hlt");
}

void *bk_sys_alloc(size_t size) {
    return kmalloc(size);
}

void *bk_sys_alloc_zero(size_t size) {
    return kzalloc(size);
}

void *bk_sys_realloc(void *ptr, size_t size) {
    return krealloc(ptr, size);
}

void bk_sys_free(void *ptr) {
    kfree(ptr);
}

bool bk_sys_memory_info(system_memory_info_t *info) {
    if (!info) return false;
    mm_get_system_info(info);
    return true;
}

int bk_file_open(const char *path, uint32_t flags) {
    return vfs_open(path, flags);
}

int bk_file_read(int fd, void *buffer, uint32_t size) {
    return vfs_read(fd, buffer, size);
}

int bk_file_write(int fd, const void *buffer, uint32_t size) {
    return vfs_write(fd, buffer, size);
}

bool bk_file_close(int fd) {
    return vfs_close(fd);
}

bool bk_file_read_all(const char *path, void **buffer, uint32_t *size) {
    return vfs_read_all(path, buffer, size);
}

bool bk_file_write_all(const char *path, const void *buffer, uint32_t size) {
    return vfs_write_all(path, buffer, size);
}

bool bk_file_list_dir(const char *path, vfs_dir_entry_t *entries,
                      uint32_t max_entries, uint32_t *count) {
    return vfs_listdir(path, entries, max_entries, count);
}

bool bk_file_chdir(const char *path) {
    return vfs_chdir(path);
}

const char *bk_file_getcwd(void) {
    return vfs_getcwd();
}

bool bk_file_mkdir(const char *path) {
    return vfs_mkdir(path);
}

bool bk_file_remove(const char *path) {
    return vfs_remove(path);
}

bool bk_file_rename(const char *old_path, const char *new_path) {
    return vfs_rename(old_path, new_path);
}

bool bk_file_space(uint64_t *total_bytes, uint64_t *free_bytes) {
    if (!total_bytes || !free_bytes) return false;
    return vfs_get_space(total_bytes, free_bytes);
}

bool bk_device_format_fat(const char *device_name, const char *volume_label) {
    return fat_format(device_name, volume_label);
}

gui_desktop_t *bk_gui_desktop(void) {
    return gui_get_desktop();
}

void bk_gui_request_paint(void) {
    gui_request_paint();
}

gui_window_t *bk_gui_create_window(gui_desktop_t *desktop, int x, int y,
                                   int w, int h, const char *title) {
    return gui_desktop_create_window(desktop, x, y, w, h, title);
}

void bk_gui_close_window(gui_window_t *window) {
    gui_window_close(window);
}

void bk_gui_focus_window(gui_desktop_t *desktop, gui_window_t *window) {
    gui_desktop_focus_window(desktop, window);
}

void bk_gui_set_window_content(gui_window_t *window,
                               gui_window_content_paint_t paint,
                               void *context) {
    gui_window_set_content(window, paint, context);
}

void bk_gui_set_window_event_handler(gui_window_t *window,
                                     gui_window_event_t handler,
                                     void *context) {
    gui_window_set_event_handler(window, handler, context);
}

void bk_gui_set_window_min_size(gui_window_t *window, int min_w, int min_h) {
    gui_window_set_min_size(window, min_w, min_h);
}

int bk_gui_add_menu(gui_window_t *window, const char *label) {
    return gui_window_add_menu(window, label);
}

bool bk_gui_add_menu_item(gui_window_t *window, int menu, uint32_t id,
                          const char *label, gui_menu_callback_t callback,
                          void *context) {
    return gui_window_add_menu_item(window, menu, id, label, callback, context);
}

void bk_gui_destroy_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!window) return;
    if (!desktop) desktop = gui_get_desktop();
    if (desktop) gui_desktop_remove_window(desktop, window);
    gui_window_destroy(window);
}

bool bk_gui_window_is_open(const gui_window_t *window) {
    return window && window->listed;
}

void bk_gui_window_set_owner(gui_window_t *window, uint32_t pid) {
    if (window) window->owner_pid = pid;
}

void bk_gui_window_invalidate(gui_window_t *window) {
    if (!window) return;
    window->dirty = true;
    gui_request_paint();
}

bool bk_gui_window_bounds(const gui_window_t *window, gui_rect_t *bounds) {
    if (!window || !bounds) return false;
    *bounds = window->bounds;
    return true;
}

bool bk_gui_window_content_rect(const gui_window_t *window, gui_rect_t *rect) {
    if (!window || !rect) return false;
    *rect = gui_window_content_rect(window);
    return true;
}

void bk_gui_surface_clear(gui_surface_t *surface, uint32_t color) {
    gui_gfx_clear(surface, color);
}

void bk_gui_surface_putpixel(gui_surface_t *surface, int x, int y,
                             uint32_t color) {
    gui_gfx_putpixel(surface, x, y, color);
}

void bk_gui_surface_fill_rect(gui_surface_t *surface, gui_rect_t rect,
                              uint32_t color) {
    gui_gfx_fill_rect(surface, rect, color);
}

void bk_gui_surface_draw_rect(gui_surface_t *surface, gui_rect_t rect,
                              uint32_t color) {
    gui_gfx_draw_rect(surface, rect, color);
}

void bk_gui_surface_draw_line(gui_surface_t *surface, int x0, int y0,
                              int x1, int y1, uint32_t color) {
    gui_gfx_draw_line(surface, x0, y0, x1, y1, color);
}

void bk_gui_surface_draw_text(gui_surface_t *surface, int x, int y,
                              const char *text, uint32_t fg, uint32_t bg,
                              bool fill_bg) {
    gui_font_draw_string(surface, x, y, text, fg, bg, fill_bg);
}

uint16_t bk_gui_text_width(const char *text) {
    return gui_font_text_width(text);
}

bool bk_gfx_info(gfx_info_t *info) {
    const gfx_info_t *current;
    if (!info) return false;
    current = gfx_get_info();
    if (!current) return false;
    *info = *current;
    return true;
}

bool bk_gfx_set_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    return gfx_set_display_mode(width, height, bpp);
}

void bk_gfx_clear(uint32_t rgb) {
    gfx_clear_rgb(rgb);
}

void bk_gfx_putpixel(int x, int y, uint32_t rgb) {
    gfx_putpixel_rgb(x, y, rgb);
}

uint32_t bk_gfx_getpixel(int x, int y) {
    return gfx_getpixel_rgb(x, y);
}

void bk_gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb) {
    gfx_fill_rect_rgb(x, y, w, h, rgb);
}

void bk_gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    uint8_t color = (uint8_t)(rgb & 0xFFU);
    if (gfx_get_info() && gfx_get_info()->mode == GFX_MODE_VESA_LFB) {
        int dx = x1 - x0;
        int dy = y1 - y0;
        int sx = dx < 0 ? -1 : 1;
        int sy = dy < 0 ? -1 : 1;
        int err;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        err = dx - dy;
        for (;;) {
            gfx_putpixel_rgb(x0, y0, rgb);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
        return;
    }
    gfx_draw_line(x0, y0, x1, y1, color);
}

void bk_gfx_draw_text(int x, int y, const char *text, uint8_t fg,
                      uint8_t bg, bool fill_bg) {
    gfx_draw_string(x, y, text, fg, bg, fill_bg);
}

bool bk_input_mouse(mouse_state_t *state) {
    if (!state) return false;
    mouse_get_state(state);
    return state->present;
}

bool bk_input_key_event(kbd_key_event_t *event) {
    return kbd_next_event(event);
}

bool bk_input_key_modifiers(kbd_modifiers_t *modifiers) {
    if (!modifiers) return false;
    kbd_get_modifiers(modifiers);
    return true;
}

void bk_input_mouse_set_position(int32_t x, int32_t y) {
    mouse_set_position(x, y);
}

void bk_input_mouse_set_sensitivity(uint8_t sensitivity) {
    mouse_set_sensitivity(sensitivity);
}

uint8_t bk_input_mouse_get_sensitivity(void) {
    return mouse_get_sensitivity();
}

bool bk_sound_has_sb16(void) {
    return sound_has_sb16();
}

bool bk_sound_pcm_available(void) {
    return sound_pcm_available();
}

bool bk_sound_pcm_busy(void) {
    return sound_pcm_is_busy();
}

const char *bk_sound_pcm_name(void) {
    return sound_pcm_name();
}

bool bk_sound_play_pcm_u8(const uint8_t *samples, uint32_t length,
                          uint16_t sample_rate_hz, uint8_t volume) {
    return sound_play_pcm_u8(samples, length, sample_rate_hz, volume);
}

bool bk_sound_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    return sound_start_tone(frequency_hz, duration_ms);
}

void bk_sound_stop(void) {
    sound_stop();
}

bool bk_time_datetime(rtc_datetime_t *datetime) {
    return rtc_get_datetime(datetime);
}

bool bk_datetime_runtime_preferences_get(bk_datetime_preferences_t *prefs) {
    if (!prefs || !g_datetime_runtime_preferences_valid) return false;
    *prefs = g_datetime_runtime_preferences;
    return true;
}

void bk_datetime_runtime_preferences_set(
    const bk_datetime_preferences_t *prefs) {
    if (!prefs) return;
    g_datetime_runtime_preferences = *prefs;
    g_datetime_runtime_preferences_valid = true;
}

uint32_t bk_proc_count(void) {
    return task_count();
}

const task_t *bk_proc_get(uint32_t index) {
    return task_get(index);
}

bool bk_proc_info(uint32_t index, bk_proc_info_t *info) {
    const task_t *task;

    if (!info) return false;
    task = task_get(index);
    if (!task) return false;
    info->pid = task->pid;
    info->process_id = task->process_id;
    kstrncpy(info->name, task->name, sizeof(info->name));
    info->name[sizeof(info->name) - 1U] = '\0';
    info->state = (bk_proc_state_t)task->state;
    info->cpu_ticks = task->cpu_ticks;
    info->memory_bytes = task->memory_bytes;
    info->system = task->system;
    info->user = task->user;
    info->exit_requested = task->exit_requested;
    return true;
}

bool bk_proc_request_exit(uint32_t pid) {
    return task_request_exit(pid);
}

bool bk_proc_exit_requested(void) {
    return task_exit_requested();
}

void bk_proc_set_memory_hint(uint32_t bytes) {
    task_set_memory_hint(bytes);
}

void bk_proc_bind_window(gui_window_t *window) {
    task_bind_window(window);
}

const char *bk_proc_launch_arg(void) {
    const char *argument = task_launch_arg();
    return argument ? argument : "";
}

int bk_proc_spawn_thread(const char *name, bk_thread_entry_t entry,
                         void *argument) {
    if (!entry) return -1;
    return task_create_user_thread(name ? name : "app-thread", entry,
                                   argument, task_current_process_id());
}

void bk_proc_exit(void) {
    task_exit();
}

bool bk_app_launch(const char *path, const char *argument) {
    gui_desktop_t *desktop = gui_get_desktop();
    if (!desktop || !path) return false;
    return program_execute_path_arg(desktop, path, argument);
}

bool bk_shell_take_exit_request(void) {
    return shell_take_exit_request();
}
