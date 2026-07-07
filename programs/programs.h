#ifndef PROGRAMS_H
#define PROGRAMS_H

#include "../gui/gui.h"

typedef struct {
    gui_window_t *window;
    uint32_t memory_bytes;
} program_runtime_info_t;

void deskmanager_install(gui_desktop_t *desktop);
void deskbar_install(gui_desktop_t *desktop);

void filebrowser_install(gui_desktop_t *desktop);
void filebrowser_open_from_desktop(gui_desktop_t *desktop);
void filebrowser_open_path(gui_desktop_t *desktop, const char *path);
bool filebrowser_global_copy(void);
bool filebrowser_global_paste(void);

void shelllauncher_install(gui_desktop_t *desktop);
void shelllauncher_open_from_desktop(gui_desktop_t *desktop);

void texteditor_install(gui_desktop_t *desktop);
void texteditor_open(gui_desktop_t *desktop, const char *path);
void texteditor_open_from_desktop(gui_desktop_t *desktop);

void calculator_install(gui_desktop_t *desktop);
void calculator_open_from_desktop(gui_desktop_t *desktop);
void calendar_install(gui_desktop_t *desktop);
void calendar_open_from_desktop(gui_desktop_t *desktop);
void processmanager_install(gui_desktop_t *desktop);
void processmanager_open_from_desktop(gui_desktop_t *desktop);
void midamp_install(gui_desktop_t *desktop);
void midamp_open_from_desktop(gui_desktop_t *desktop);
void imageviewer_install(gui_desktop_t *desktop);
void imageviewer_open(gui_desktop_t *desktop, const char *path);
void imageviewer_open_from_desktop(gui_desktop_t *desktop);
void games_install(gui_desktop_t *desktop);
void games_open_from_desktop(gui_desktop_t *desktop);
void gears_install(gui_desktop_t *desktop);
void gears_open_from_desktop(gui_desktop_t *desktop);
void paint_install(gui_desktop_t *desktop);
void paint_open_from_desktop(gui_desktop_t *desktop);
void settings_install(gui_desktop_t *desktop);
void settings_open_from_desktop(gui_desktop_t *desktop);

void screensaverd_install(gui_desktop_t *desktop);
bool screensaver_is_enabled(void);
uint32_t screensaver_get_timeout_seconds(void);
void screensaver_set_enabled(bool enabled);
void screensaver_set_timeout_seconds(uint32_t seconds);
const char *screensaver_get_path(void);
void screensaver_set_path(const char *path);
bool screensaver_preview(gui_desktop_t *desktop);

void deskmanager_set_background(uint32_t color);
uint32_t deskmanager_get_background(void);
bool deskmanager_set_wallpaper(const char *path);
void deskmanager_refresh_layout(void);

bool program_is_object(const char *path);
bool program_execute_path(gui_desktop_t *desktop, const char *path);
uint32_t *program_load_bmp_icon(const char *path);
uint32_t *program_load_bmp_icon_scaled(const char *path,
                                       uint16_t output_width,
                                       uint16_t output_height);
uint32_t *program_load_bmp_wallpaper_scaled(const char *path,
                                            uint16_t output_width,
                                            uint16_t output_height);
void program_draw_icon_pixels(gui_surface_t *surface, int x, int y,
                              const uint32_t *pixels,
                              uint16_t width, uint16_t height);
void about_install(gui_desktop_t *desktop);
void about_open(gui_desktop_t *desktop);

bool about_get_runtime_info(program_runtime_info_t *info);
bool filebrowser_get_runtime_info(program_runtime_info_t *info);
bool shelllauncher_get_runtime_info(program_runtime_info_t *info);
bool texteditor_get_runtime_info(program_runtime_info_t *info);
bool calculator_get_runtime_info(program_runtime_info_t *info);

void runbox_open_from_desktop(gui_desktop_t *desktop);
bool runbox_get_runtime_info(program_runtime_info_t *info);

bool calendar_get_runtime_info(program_runtime_info_t *info);
bool processmanager_get_runtime_info(program_runtime_info_t *info);
bool midamp_get_runtime_info(program_runtime_info_t *info);
bool gears_get_runtime_info(program_runtime_info_t *info);
bool paint_get_runtime_info(program_runtime_info_t *info);

#endif
