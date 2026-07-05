#include "gui.h"
#include "../kernel/include/memory.h"

static void copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

gui_window_t *gui_window_create(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title) {
    gui_window_t *window = (gui_window_t *)kzalloc(sizeof(gui_window_t));
    if (!window || !desktop) return NULL;

    window->id = desktop->next_window_id++;
    window->bounds = (gui_rect_t){x, y, w, h};
    window->min_w = 160;
    window->min_h = 90;
    window->bg_color = 0x00D3D3D3;
    window->title_color = 0x00808080;
    window->border_color = 0x00808080;
    window->visible = true;
    window->listed = true;
    window->minimized = false;
    window->dirty = true;
    window->open_menu = -1;
    window->pressed_menu_item = -1;
    copy_text(window->title, sizeof(window->title), title);
    return window;
}

int gui_window_content_top(const gui_window_t *window) {
    return GUI_TITLEBAR_HEIGHT +
           ((window && window->menu_count) ? GUI_MENU_HEIGHT : 0);
}

int gui_window_add_menu(gui_window_t *window, const char *label) {
    if (!window || window->menu_count >= GUI_MAX_MENUS) return -1;
    int index = window->menu_count++;
    kstrncpy(window->menus[index].label, label ? label : "",
             sizeof(window->menus[index].label) - 1);
    return index;
}

bool gui_window_add_menu_item(gui_window_t *window, int menu,
                              uint32_t id, const char *label,
                              gui_menu_callback_t callback, void *context) {
    if (!window || menu < 0 || menu >= window->menu_count) return false;
    gui_menu_t *target = &window->menus[menu];
    if (target->item_count >= GUI_MAX_MENU_ITEMS) return false;
    gui_menu_item_t *item = &target->items[target->item_count++];
    item->id = id;
    item->callback = callback;
    item->context = context;
    kstrncpy(item->label, label ? label : "", sizeof(item->label) - 1);
    return true;
}

static gui_rect_t menu_title_rect(gui_window_t *window, int index) {
    int x = window->bounds.x + 2;
    for (int i = 0; i < index; i++)
        x += (int)gui_font_text_width(window->menus[i].label) + 16;
    return (gui_rect_t){x, window->bounds.y + GUI_TITLEBAR_HEIGHT,
                        (int)gui_font_text_width(window->menus[index].label) + 16,
                        GUI_MENU_HEIGHT};
}

static gui_rect_t menu_popup_rect(gui_window_t *window, int index) {
    gui_rect_t title = menu_title_rect(window, index);
    int width = 80;
    gui_menu_t *menu = &window->menus[index];
    for (int i = 0; i < menu->item_count; i++) {
        int candidate = (int)gui_font_text_width(menu->items[i].label) + 20;
        if (candidate > width) width = candidate;
    }
    return (gui_rect_t){title.x, title.y + title.h, width,
                        menu->item_count * 20 + 4};
}

void gui_window_paint_menus(gui_surface_t *surface, gui_window_t *window) {
    if (!surface || !window || !window->visible || !window->menu_count) return;
    gui_rect_t bar = {window->bounds.x + 2,
                      window->bounds.y + GUI_TITLEBAR_HEIGHT,
                      window->bounds.w - 4, GUI_MENU_HEIGHT};
    gui_gfx_fill_rect(surface, bar, 0x00D0D0C8);
    gui_gfx_fill_rect(surface, (gui_rect_t){bar.x, bar.y + bar.h - 1,
                                            bar.w, 1}, 0x00707070);
    for (int i = 0; i < window->menu_count; i++) {
        gui_rect_t title = menu_title_rect(window, i);
        if (window->open_menu == i)
            gui_gfx_fill_rect(surface, title, 0x00A8A8A0);
        gui_font_draw_string_clipped(surface, title.x + 8, title.y + 5,
                                     window->menus[i].label, 0x00101010, title);
    }
    if (window->open_menu < 0) return;
    int index = window->open_menu;
    gui_menu_t *menu = &window->menus[index];
    gui_rect_t popup = menu_popup_rect(window, index);
    gui_gfx_fill_rect(surface, popup, 0x00404040);
    gui_gfx_fill_rect(surface, (gui_rect_t){popup.x + 1, popup.y + 1,
                                            popup.w - 2, popup.h - 2},
                      0x00D0D0C8);
    for (int i = 0; i < menu->item_count; i++) {
        gui_rect_t item = {popup.x + 2, popup.y + 2 + i * 20,
                           popup.w - 4, 20};
        if (window->pressed_menu_item == i)
            gui_gfx_fill_rect(surface, item, 0x00808080);
        gui_font_draw_string_clipped(surface, item.x + 8, item.y + 6,
                                     menu->items[i].label, 0x00101010, item);
    }
}

bool gui_window_handle_menu_event(gui_window_t *window,
                                  const gui_event_t *event) {
    if (!window || !event || !window->menu_count) return false;
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        for (int i = 0; i < window->menu_count; i++) {
            if (gui_rect_contains(menu_title_rect(window, i),
                                  event->x, event->y)) {
                window->open_menu = window->open_menu == i ? -1 : i;
                window->pressed_menu_item = -1;
                return true;
            }
        }
        if (window->open_menu >= 0) {
            gui_rect_t popup = menu_popup_rect(window, window->open_menu);
            if (gui_rect_contains(popup, event->x, event->y)) {
                int item = (event->y - popup.y - 2) / 20;
                if (item >= 0 &&
                    item < window->menus[window->open_menu].item_count) {
                    window->pressed_menu_item = item;
                    return true;
                }
            }
            window->open_menu = -1;
        }
    }
    if (event->type == GUI_EVENT_MOUSE_UP && window->open_menu >= 0 &&
        window->pressed_menu_item >= 0) {
        int menu_index = window->open_menu;
        int item_index = window->pressed_menu_item;
        gui_menu_item_t item = window->menus[menu_index].items[item_index];
        window->open_menu = -1;
        window->pressed_menu_item = -1;
        if (item.callback) item.callback(window, item.id, item.context);
        return true;
    }
    return window->open_menu >= 0;
}

void gui_window_destroy(gui_window_t *window) {
    gui_widget_t *widget;
    if (!window) return;
    widget = window->widgets;
    while (widget) {
        gui_widget_t *next = widget->next;
        kfree(widget);
        widget = next;
    }
    kfree(window);
}

bool gui_window_contains(gui_window_t *window, int x, int y) {
    return window && window->visible && gui_rect_contains(window->bounds, x, y);
}

bool gui_window_titlebar_contains(gui_window_t *window, int x, int y) {
    gui_rect_t titlebar;
    if (!gui_window_contains(window, x, y)) return false;
    titlebar = (gui_rect_t){window->bounds.x, window->bounds.y, window->bounds.w, GUI_TITLEBAR_HEIGHT};
    return gui_rect_contains(titlebar, x, y);
}

gui_rect_t gui_window_minimize_button_rect(gui_window_t *window) {
    if (!window) return (gui_rect_t){0, 0, 0, 0};
    return (gui_rect_t){window->bounds.x + window->bounds.w - 36,
                        window->bounds.y + 3, 14, 14};
}

gui_rect_t gui_window_close_button_rect(gui_window_t *window) {
    if (!window) return (gui_rect_t){0, 0, 0, 0};
    return (gui_rect_t){window->bounds.x + window->bounds.w - 20,
                        window->bounds.y + 3, 14, 14};
}

gui_window_button_t gui_window_titlebar_button_at(gui_window_t *window,
                                                   int x, int y) {
    if (!window || !window->visible) return GUI_WINDOW_BUTTON_NONE;
    if (gui_rect_contains(gui_window_close_button_rect(window), x, y))
        return GUI_WINDOW_BUTTON_CLOSE;
    if (gui_rect_contains(gui_window_minimize_button_rect(window), x, y))
        return GUI_WINDOW_BUTTON_MINIMIZE;
    return GUI_WINDOW_BUTTON_NONE;
}

void gui_window_minimize(gui_window_t *window) {
    if (window) {
        window->visible = false;
        window->listed = true;
        window->minimized = true;
    }
}

void gui_window_close(gui_window_t *window) {
    if (window) {
        window->visible = false;
        window->listed = false;
        window->minimized = false;
    }
}

void gui_window_restore(gui_window_t *window) {
    if (window) {
        window->visible = true;
        window->listed = true;
        window->minimized = false;
        window->dirty = true;
    }
}

void gui_window_set_min_size(gui_window_t *window, int min_w, int min_h) {
    if (!window) return;
    if (min_w < 80) min_w = 80;
    if (min_h < GUI_TITLEBAR_HEIGHT + 30) min_h = GUI_TITLEBAR_HEIGHT + 30;
    window->min_w = min_w;
    window->min_h = min_h;
    if (window->bounds.w < window->min_w) window->bounds.w = window->min_w;
    if (window->bounds.h < window->min_h) window->bounds.h = window->min_h;
    window->dirty = true;
}

void gui_window_set_content(gui_window_t *window,
                            gui_window_content_paint_t paint,
                            void *context) {
    if (!window) return;
    window->content_paint = paint;
    window->content_context = context;
    window->dirty = true;
}

void gui_window_set_event_handler(gui_window_t *window,
                                  gui_window_event_t handler,
                                  void *context) {
    if (!window) return;
    window->event_handler = handler;
    window->event_context = context;
}

bool gui_window_dispatch_event(gui_window_t *window,
                               const gui_event_t *event) {
    if (!window || !event || !window->event_handler) return false;
    return window->event_handler(window, event, window->event_context);
}

void gui_window_paint(gui_surface_t *surface, gui_window_t *window, gui_rect_t clip) {
    gui_rect_t win_clip;
    gui_rect_t frame;
    gui_rect_t titlebar;
    gui_rect_t body;
    gui_widget_t *widget;
    uint32_t border_color;
    uint32_t title_bg;
    uint32_t title_text;
    uint32_t button_face;
    uint32_t button_shadow;

    if (!surface || !window || !window->visible) return;
    if (!gui_rect_intersect(window->bounds, clip, &win_clip)) return;

    frame = window->bounds;
    titlebar = (gui_rect_t){frame.x, frame.y, frame.w, GUI_TITLEBAR_HEIGHT};
    body = (gui_rect_t){frame.x + 2, frame.y + GUI_TITLEBAR_HEIGHT, frame.w - 4, frame.h - GUI_TITLEBAR_HEIGHT - 2};
    border_color = 0x00404040;
    title_bg = window->focused ? 0x00A8A8A8 : 0x00B8B8B8;
    title_text = 0x00000000;
    button_face = 0x00E0E0E0;
    button_shadow = 0x00404040;

    gui_gfx_fill_rect(surface, frame, 0x00808080);
    gui_gfx_fill_rect(surface, (gui_rect_t){frame.x + 1, frame.y + 1, frame.w - 2, frame.h - 2}, window->bg_color);
    gui_gfx_fill_rect(surface, titlebar, title_bg);
    gui_gfx_draw_rect(surface, frame, border_color);

    gui_gfx_fill_rect(surface, (gui_rect_t){frame.x + 1, frame.y + 1, frame.w - 2, 1}, 0x00FFFFFF);
    gui_gfx_fill_rect(surface, (gui_rect_t){frame.x + 1, frame.y + frame.h - 2, frame.w - 2, 1}, button_shadow);
    gui_gfx_fill_rect(surface, (gui_rect_t){frame.x + frame.w - 2, frame.y + 1, 1, frame.h - 2}, button_shadow);

    gui_gfx_fill_rect(surface, body, window->bg_color);
    for (int y = body.y; y < body.y + body.h; y += 2) {
        gui_gfx_fill_rect(surface, (gui_rect_t){body.x, y, body.w, 1}, 0x00C8C8C8);
    }

    int btn_x = frame.x + frame.w - 36;
    int btn_y = frame.y + 3;
    for (int i = 0; i < 2; i++) {
        gui_gfx_fill_rect(surface, (gui_rect_t){btn_x, btn_y, 14, 14}, button_face);
        gui_gfx_fill_rect(surface, (gui_rect_t){btn_x, btn_y, 14, 1}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){btn_x, btn_y, 1, 14}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){btn_x, btn_y + 13, 14, 1}, button_shadow);
        gui_gfx_fill_rect(surface, (gui_rect_t){btn_x + 13, btn_y, 1, 14}, button_shadow);
        if (i == 0) {
            gui_gfx_fill_rect(surface, (gui_rect_t){btn_x + 3, btn_y + 9, 8, 2}, border_color);
        } else {
            gui_gfx_draw_line(surface, btn_x + 3, btn_y + 3, btn_x + 10, btn_y + 10, border_color);
            gui_gfx_draw_line(surface, btn_x + 10, btn_y + 3, btn_x + 3, btn_y + 10, border_color);
        }
        btn_x += 16;
    }

    gui_rect_t title_clip = {frame.x + 5, frame.y + 2,
                             frame.w - 47, GUI_TITLEBAR_HEIGHT - 4};
    int title_w = (int)gui_font_text_width(window->title);
    int title_x = frame.x + (frame.w - title_w) / 2;
    if (title_x < frame.x + 6) title_x = frame.x + 6;
    gui_font_draw_string_clipped(surface, title_x, frame.y + 6,
                                 window->title, title_text, title_clip);
    gui_font_draw_string_clipped(surface, title_x + 1, frame.y + 6,
                                 window->title, title_text, title_clip);

    widget = window->widgets;
    while (widget) {
        gui_widget_paint(surface, window, widget, win_clip);
        widget = widget->next;
    }
}
