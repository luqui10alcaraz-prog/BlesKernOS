#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include "../../gui/gui.h"

typedef struct {
    const char *name;
    const char *version;
    const char *description;
    const char *copyright;
    const char *icon_path;
} bk_about_info_t;

bool bk_about_attach(gui_window_t *window, gui_desktop_t *desktop,
                     const bk_about_info_t *info);
void bk_about_show(gui_desktop_t *desktop, const bk_about_info_t *info);
void bk_about_detach(gui_window_t *window);

#endif
