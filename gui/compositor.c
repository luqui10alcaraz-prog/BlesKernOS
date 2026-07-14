#include "gui.h"

#define GUI_CURSOR_WIDTH  32
#define GUI_CURSOR_HEIGHT 32

static const char *g_arrow_cursor[GUI_CURSOR_HEIGHT] = {
    "XX                              ",
    "XoX                             ",
    "X.oX                            ",
    "X..oX                           ",
    "X...oX                          ",
    "X....oX                         ",
    "X.....oX                        ",
    "X......oX                       ",
    "X.......oX                      ",
    "X........oX                     ",
    "X.....oXXXX                     ",
    "X..oo.oX                        ",
    "X.oXX..X                        ",
    "XoX Xo.oX                       ",
    "XX   X..X                       ",
    "     XooX                       ",
    "      XX                        ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                ",
    "                                "
};

static gui_rect_t cursor_rect(int x, int y) {
    return (gui_rect_t){x, y, GUI_CURSOR_WIDTH, GUI_CURSOR_HEIGHT};
}

static uint8_t compositor_planned_cursor_rects(const gui_desktop_t *desktop,
                                               gui_rect_t *rects,
                                               uint8_t max_rects) {
    uint8_t count = 0;

    if (!desktop || !rects || !max_rects) return 0;
    if (desktop->cursor_trail_enabled) {
        for (uint8_t i = 0;
             i < desktop->cursor_trail_count && count < max_rects - 1;
             i++) {
            rects[count++] = cursor_rect(desktop->cursor_trail_x[i],
                                         desktop->cursor_trail_y[i]);
        }
    }
    rects[count++] = cursor_rect(desktop->mouse_x, desktop->mouse_y);
    return count;
}

static void dirty_union(gui_rect_t *dirty, bool *valid, gui_rect_t rect) {
    if (!dirty || !valid || rect.w <= 0 || rect.h <= 0) return;
    if (*valid) *dirty = gui_rect_union(*dirty, rect);
    else {
        *dirty = rect;
        *valid = true;
    }
}

static bool compositor_dirty_rect(gui_desktop_t *desktop, gui_rect_t *out) {
    gui_window_t *window;
    gui_rect_t screen;
    gui_rect_t dirty = {0, 0, 0, 0};
    gui_rect_t clipped;
    bool valid = false;

    if (!desktop || !out) return false;
    screen = (gui_rect_t){0, 0, desktop->surface.width,
                          desktop->surface.height};
    if (!desktop->paint_valid) {
        *out = screen;
        return true;
    }

    if (desktop->dirty_valid)
        dirty_union(&dirty, &valid, desktop->dirty_rect);

    window = desktop->first_window;
    while (window) {
        if (window->dirty) {
            if (window->paint_bounds_valid)
                dirty_union(&dirty, &valid, window->paint_bounds);
            if (window->visible)
                dirty_union(&dirty, &valid, window->bounds);
        }
        window = window->next;
    }

    if (desktop->cursor_paint_count > 0) {
        for (uint8_t i = 0; i < desktop->cursor_paint_count; i++)
            dirty_union(&dirty, &valid, desktop->cursor_paint_rects[i]);
    } else if (desktop->cursor_valid) {
        dirty_union(&dirty, &valid, desktop->cursor_rect);
    }

    {
        gui_rect_t planned[GUI_CURSOR_TRAIL_MAX + 1];
        uint8_t planned_count = compositor_planned_cursor_rects(desktop,
                                                                planned,
                                                                GUI_CURSOR_TRAIL_MAX + 1);

        for (uint8_t i = 0; i < planned_count; i++)
            dirty_union(&dirty, &valid, planned[i]);
    }

    if (!valid) return false;
    if (!gui_rect_intersect(screen, dirty, &clipped)) return false;
    *out = clipped;
    return true;
}

static void compositor_finish_windows(gui_desktop_t *desktop) {
    gui_window_t *window;

    if (!desktop) return;
    window = desktop->first_window;
    while (window) {
        if (!window->content_repaint) window->dirty = false;
        if (window->visible) {
            window->paint_bounds = window->bounds;
            window->paint_bounds_valid = true;
        } else {
            window->paint_bounds_valid = false;
        }
        window = window->next;
    }
}

static void paint_cursor_with_palette(gui_surface_t *surface, int x, int y,
                                      uint32_t dark, uint32_t mid,
                                      uint32_t light) {
    for (int row = 0; row < GUI_CURSOR_HEIGHT; row++) {
        for (int col = 0; col < GUI_CURSOR_WIDTH; col++) {
            char px = g_arrow_cursor[row][col];
            uint32_t color;

            if (px == ' ') continue;
            if (px == 'X') color = dark;
            else if (px == 'o') color = mid;
            else color = light;

            gui_gfx_putpixel(surface, x + col, y + row, color);
        }
    }
}

static void paint_cursor(gui_surface_t *surface, int x, int y) {
    paint_cursor_with_palette(surface, x, y, 0x00000000, 0x00808080, 0x00FFFFFF);
}

void gui_compositor_paint(gui_desktop_t *desktop) {
    gui_rect_t dirty;
    gui_rect_t planned[GUI_CURSOR_TRAIL_MAX + 1];
    uint8_t planned_count;
    uint32_t dirty_generation;

    if (!desktop) return;
    if (!compositor_dirty_rect(desktop, &dirty)) return;
    dirty_generation = desktop->dirty_generation;
    gui_gfx_set_clip(&desktop->surface, dirty);
    gui_gfx_clear(&desktop->surface, 0x005080B0);
    gui_desktop_paint_programs(desktop);
    planned_count = compositor_planned_cursor_rects(desktop, planned,
                                                    GUI_CURSOR_TRAIL_MAX + 1);
    if (desktop->cursor_trail_enabled && planned_count > 1) {
        for (uint8_t i = 0; i + 1 < planned_count; i++) {
            uint8_t t = (uint8_t)(48 + (i * 128) /
                (planned_count > 1 ? planned_count - 1 : 1));
            uint32_t dark = gui_color_lerp(0x00B8B8B8, 0x00000000, t);
            uint32_t mid = gui_color_lerp(0x00E0E0E0, 0x00808080, t);
            paint_cursor_with_palette(&desktop->surface,
                                      planned[i].x, planned[i].y,
                                      dark, mid, 0x00FFFFFF);
        }
    }
    paint_cursor(&desktop->surface, desktop->mouse_x, desktop->mouse_y);
    gui_gfx_reset_clip(&desktop->surface);
    gui_gfx_present_rect(&desktop->surface, dirty);
    compositor_finish_windows(desktop);
    /* No borre una invalidacion producida por una app Ring 3 mientras este
     * frame se estaba componiendo/presentando. */
    if (desktop->dirty_generation == dirty_generation)
        desktop->dirty_valid = false;
    desktop->cursor_rect = cursor_rect(desktop->mouse_x, desktop->mouse_y);
    desktop->cursor_valid = true;
    desktop->cursor_paint_count = planned_count;
    for (uint8_t i = 0; i < planned_count; i++)
        desktop->cursor_paint_rects[i] = planned[i];
    desktop->paint_valid = true;
}
