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

static void paint_cursor(gui_surface_t *surface, int x, int y) {
    for (int row = 0; row < GUI_CURSOR_HEIGHT; row++) {
        for (int col = 0; col < GUI_CURSOR_WIDTH; col++) {
            char px = g_arrow_cursor[row][col];
            uint32_t color;

            if (px == ' ') continue;
            if (px == 'X') color = 0x00000000;
            else if (px == 'o') color = 0x00808080;
            else color = 0x00FFFFFF;

            gui_gfx_putpixel(surface, x + col, y + row, color);
        }
    }
}

void gui_compositor_paint(gui_desktop_t *desktop) {
    if (!desktop) return;
    gui_gfx_clear(&desktop->surface, 0x005080B0);
    gui_desktop_paint_programs(desktop);
    paint_cursor(&desktop->surface, desktop->mouse_x, desktop->mouse_y);
    gui_gfx_present(&desktop->surface);
}
