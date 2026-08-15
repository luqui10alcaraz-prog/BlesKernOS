#ifndef BLESKERNOS_CONTROL_COMMON_H
#define BLESKERNOS_CONTROL_COMMON_H

#include "kernel/include/api.h"
#include <stdio.h>

#define CPL_FACE       0x00C0C0C0
#define CPL_LIGHT      0x00FFFFFF
#define CPL_SHADOW     0x00808080
#define CPL_DARK       0x00404040
#define CPL_TEXT       0x00101010
#define CPL_BLUE       0x00000080
#define CPL_WHITE      0x00FFFFFF

typedef struct {
    const char *label;
    int width;
} cpl_tab_spec_t;

static inline void cpl_draw_bevel(gui_surface_t *surface, gui_rect_t rect,
                           uint32_t fill, bool sunken) {
    uint32_t top = sunken ? CPL_SHADOW : CPL_LIGHT;
    uint32_t bottom = sunken ? CPL_LIGHT : CPL_DARK;
    bk_gui_gfx_fill_rect(surface, rect, fill);
    bk_gui_gfx_draw_line(surface, rect.x, rect.y, rect.x + rect.w - 1, rect.y, top);
    bk_gui_gfx_draw_line(surface, rect.x, rect.y, rect.x, rect.y + rect.h - 1, top);
    bk_gui_gfx_draw_line(surface, rect.x, rect.y + rect.h - 1,
                      rect.x + rect.w - 1, rect.y + rect.h - 1, bottom);
    bk_gui_gfx_draw_line(surface, rect.x + rect.w - 1, rect.y,
                      rect.x + rect.w - 1, rect.y + rect.h - 1, bottom);
}

static inline void cpl_draw_group(gui_surface_t *surface, gui_rect_t rect,
                           const char *label) {
    bk_gui_gfx_draw_rect(surface, rect, CPL_SHADOW);
    if (label) {
        bk_gui_gfx_fill_rect(surface,
            (gui_rect_t){rect.x + 8, rect.y - 4,
                         bk_gui_font_text_width(label) + 8, 10}, CPL_FACE);
        bk_gui_font_draw_string(surface, rect.x + 12, rect.y - 3, label,
                             CPL_TEXT, 0, false);
    }
}

static inline gui_rect_t cpl_tab_rect(gui_rect_t client,
                                      const cpl_tab_spec_t *tabs,
                                      int index,
                                      int left,
                                      int top) {
    int x = client.x + left;

    if (!tabs || index < 0) return (gui_rect_t){0, 0, 0, 0};
    for (int i = 0; i < index; i++) x += tabs[i].width;
    return (gui_rect_t){x, client.y + top, tabs[index].width, 24};
}

static inline int cpl_hit_tab(gui_rect_t client,
                              const cpl_tab_spec_t *tabs,
                              int count,
                              int left,
                              int top,
                              int x,
                              int y) {
    if (!tabs || count <= 0) return -1;
    for (int i = 0; i < count; i++) {
        if (bk_gui_rect_contains(cpl_tab_rect(client, tabs, i, left, top), x, y))
            return i;
    }
    return -1;
}

static inline void cpl_draw_tabs(gui_surface_t *surface,
                                 gui_rect_t client,
                                 const cpl_tab_spec_t *tabs,
                                 int count,
                                 int active,
                                 gui_rect_t page,
                                 int left,
                                 int top) {
    if (!surface || !tabs || count <= 0) return;

    for (int i = 0; i < count; i++) {
        gui_rect_t tab = cpl_tab_rect(client, tabs, i, left, top);
        bool selected = i == active;
        int text_x;

        bk_gui_gfx_fill_rect(surface, tab, CPL_FACE);
        bk_gui_gfx_draw_line(surface, tab.x, tab.y + tab.h - 1,
                          tab.x, tab.y, CPL_LIGHT);
        bk_gui_gfx_draw_line(surface, tab.x, tab.y,
                          tab.x + tab.w - 1, tab.y, CPL_LIGHT);
        bk_gui_gfx_draw_line(surface, tab.x + tab.w - 1, tab.y,
                          tab.x + tab.w - 1, tab.y + tab.h - 1, CPL_DARK);
        if (!selected) {
            bk_gui_gfx_draw_line(surface, tab.x, tab.y + tab.h - 1,
                              tab.x + tab.w - 1, tab.y + tab.h - 1,
                              CPL_SHADOW);
        } else if (tab.w > 4) {
            bk_gui_gfx_fill_rect(surface,
                (gui_rect_t){tab.x + 2, tab.y + tab.h - 1, tab.w - 4, 1},
                CPL_FACE);
        }

        text_x = tab.x + (tab.w - (int)bk_gui_font_text_width(tabs[i].label)) / 2;
        if (text_x < tab.x + 8) text_x = tab.x + 8;
        bk_gui_font_draw_string(surface, text_x, tab.y + 7,
                             tabs[i].label, CPL_TEXT, 0, false);
    }

    cpl_draw_bevel(surface, page, CPL_FACE, false);
    if (page.w > 4 && page.h > 4) {
        bk_gui_gfx_fill_rect(surface,
            (gui_rect_t){page.x + 2, page.y + 2, page.w - 4, page.h - 4},
            CPL_FACE);
    }
}

static inline bool cpl_write_text(const char *path, const char *text) {
    return path && text && bk_file_write_all(path, text, (uint32_t)bk_runtime_strlen(text));
}

static inline void cpl_destroy_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!window) return;
    bk_gui_desktop_remove_window(desktop, window);
    bk_gui_window_destroy_raw(window);
    bk_proc_bind_window(NULL);
}

#endif
