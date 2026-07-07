#include "gui.h"
#include "../kernel/include/memory.h"

static gui_rect_t widget_screen_bounds(gui_window_t *window, gui_widget_t *widget) {
    return (gui_rect_t){
        window->bounds.x + GUI_BORDER_SIZE + widget->bounds.x,
        window->bounds.y + GUI_TITLEBAR_HEIGHT + widget->bounds.y,
        widget->bounds.w,
        widget->bounds.h
    };
}

gui_widget_t *gui_widget_create(gui_desktop_t *desktop, gui_window_t *window, gui_widget_type_t type, gui_rect_t bounds, const char *text, gui_widget_callback_t callback) {
    gui_widget_t *widget;
    if (!desktop || !window) return NULL;

    widget = (gui_widget_t *)kzalloc(sizeof(gui_widget_t));
    if (!widget) return NULL;

    widget->id = desktop->next_widget_id++;
    widget->type = type;
    widget->bounds = bounds;
    widget->callback = callback;
    widget->visible = true;
    kstrncpy(widget->text, text ? text : "", sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';

    if (!window->widgets) {
        window->widgets = widget;
    } else {
        gui_widget_t *last = window->widgets;
        while (last->next) last = last->next;
        last->next = widget;
        widget->prev = last;
    }
    window->dirty = true;
    return widget;
}

void gui_widget_paint(gui_surface_t *surface, gui_window_t *window, gui_widget_t *widget, gui_rect_t clip) {
    gui_rect_t bounds;
    gui_rect_t inner;
    gui_rect_t text_clip;
    uint32_t bg;
    uint32_t border;

    if (!surface || !window || !widget || !widget->visible) return;
    bounds = widget_screen_bounds(window, widget);
    if (!gui_rect_intersect(bounds, clip, &text_clip)) return;

    if (widget->type == GUI_WIDGET_LABEL) {
        gui_font_draw_string_clipped(surface, bounds.x, bounds.y + 4,
                                     widget->text, 0x00283C4A, text_clip);
        return;
    }

    bg = widget->pressed ? 0x00B8B8B0 : 0x00D0D0C8;
    border = 0x00404040;
    inner = (gui_rect_t){bounds.x + 1, bounds.y + 1, bounds.w - 2, bounds.h - 2};

    gui_gfx_fill_rect(surface, bounds, border);
    gui_gfx_fill_rect(surface, inner, bg);
    if (widget->pressed) {
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, inner.w, 1}, 0x00606060);
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, 1, inner.h}, 0x00606060);
    } else {
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, inner.w, 1}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, 1, inner.h}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y + inner.h - 1, inner.w, 1}, 0x00707070);
        gui_gfx_fill_rect(surface, (gui_rect_t){inner.x + inner.w - 1, inner.y, 1, inner.h}, 0x00707070);
    }
    int text_x = bounds.x + (bounds.w - (int)gui_font_text_width(widget->text)) / 2;
    if (text_x < bounds.x + 3) text_x = bounds.x + 3;
    text_clip = (gui_rect_t){bounds.x + 3, bounds.y + 2,
                             bounds.w - 6, bounds.h - 4};
    gui_font_draw_string_clipped(surface, text_x, bounds.y + 5,
                                 widget->text, 0x00243B4E, text_clip);
}

bool gui_widget_handle_event(gui_window_t *window, gui_widget_t *widget, const gui_event_t *event) {
    gui_rect_t bounds;
    bool inside;

    if (!window || !widget || !event || !widget->visible) return false;
    bounds = widget_screen_bounds(window, widget);
    inside = gui_rect_contains(bounds, event->x, event->y);

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        widget->hovered = inside;
        return inside;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN && inside) {
        widget->pressed = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        bool was_pressed = widget->pressed;
        widget->pressed = false;
        if (was_pressed && inside && widget->callback) {
            widget->callback(window, widget->id);
            return true;
        }
    }

    return false;
}
