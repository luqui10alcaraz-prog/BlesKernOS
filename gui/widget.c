#include "gui.h"
#include "../kernel/include/task.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/memory.h"

#define GUI_DROPDOWN_MAX_ITEMS 96
#define GUI_DROPDOWN_ITEM_HEIGHT 18

typedef struct {
    uint8_t item_count;
    uint8_t scroll_index;
    int8_t selected_index;
    int8_t hover_index;
    bool expanded;
    char labels[GUI_DROPDOWN_MAX_ITEMS][48];
    char values[GUI_DROPDOWN_MAX_ITEMS][64];
} gui_dropdown_state_t;

gui_rect_t gui_widget_screen_bounds(const gui_window_t *window,
                                    const gui_widget_t *widget) {
    gui_rect_t client;

    if (!window || !widget) return (gui_rect_t){0, 0, 0, 0};
    client = gui_window_content_rect(window);
    return (gui_rect_t){
        client.x + widget->bounds.x,
        client.y + widget->bounds.y,
        widget->bounds.w,
        widget->bounds.h
    };
}

static uint32_t widget_text_color(const gui_widget_t *widget, uint32_t bg) {
    uint32_t sum;

    if (!widget || !widget->enabled) return 0x00707070;
    if (widget->style == GUI_WIDGET_STYLE_SELECTABLE && widget->selected)
        return 0x00FFFFFF;
    sum = ((bg >> 16) & 0xFF) + ((bg >> 8) & 0xFF) + (bg & 0xFF);
    return sum < 320 ? 0x00FFFFFF : 0x00101010;
}

static void widget_draw_button_bevel(gui_surface_t *surface, gui_rect_t bounds,
                                     bool sunken, uint32_t fill) {
    uint32_t top = sunken ? 0x00707070 : 0x00FFFFFF;
    uint32_t left = top;
    uint32_t bottom = sunken ? 0x00FFFFFF : 0x00707070;
    uint32_t right = bottom;

    gui_gfx_fill_rect(surface, bounds, 0x00404040);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + 1, bounds.y + 1, bounds.w - 2, bounds.h - 2},
        fill);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + 1, bounds.y + 1, bounds.w - 2, 1}, top);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + 1, bounds.y + 1, 1, bounds.h - 2}, left);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + 1, bounds.y + bounds.h - 2, bounds.w - 2, 1},
        bottom);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + bounds.w - 2, bounds.y + 1, 1, bounds.h - 2},
        right);
}

static gui_dropdown_state_t *widget_dropdown(gui_widget_t *widget) {
    if (!widget || widget->style != GUI_WIDGET_STYLE_DROPDOWN)
        return NULL;
    return (gui_dropdown_state_t *)widget->payload;
}

static const gui_dropdown_state_t *widget_dropdown_const(
    const gui_widget_t *widget) {
    if (!widget || widget->style != GUI_WIDGET_STYLE_DROPDOWN)
        return NULL;
    return (const gui_dropdown_state_t *)widget->payload;
}

static void widget_dropdown_sync_text(gui_widget_t *widget) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);

    if (!widget) return;
    widget->text[0] = '\0';
    if (!dropdown) return;
    if (dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count) return;
    kstrncpy(widget->text,
             dropdown->labels[(uint8_t)dropdown->selected_index],
             sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';
}

static int widget_dropdown_visible_count(const gui_window_t *window,
                                         const gui_widget_t *widget,
                                         const gui_dropdown_state_t *dropdown,
                                         bool *open_upward) {
    gui_rect_t header = gui_widget_screen_bounds(window, widget);
    gui_rect_t client = gui_window_content_rect(window);
    int below = client.y + client.h - (header.y + header.h + 1);
    int above = header.y - client.y - 1;
    int max_below = below / GUI_DROPDOWN_ITEM_HEIGHT;
    int max_above = above / GUI_DROPDOWN_ITEM_HEIGHT;
    int preferred = dropdown ? dropdown->item_count : 0;

    if (preferred < 1) preferred = 1;
    if (max_below < 1) max_below = 1;
    if (max_above < 1) max_above = 1;

    if (dropdown &&
        dropdown->item_count * GUI_DROPDOWN_ITEM_HEIGHT + 2 <= below) {
        if (open_upward) *open_upward = false;
        return dropdown->item_count;
    }
    if (above > below) {
        if (open_upward) *open_upward = true;
        return preferred < max_above ? preferred : max_above;
    }
    if (open_upward) *open_upward = false;
    return preferred < max_below ? preferred : max_below;
}

static void widget_dropdown_prepare_popup(gui_window_t *window,
                                          gui_widget_t *widget,
                                          gui_rect_t *popup,
                                          uint8_t *start_index,
                                          uint8_t *visible_count) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);
    gui_rect_t header;
    bool open_upward = false;
    int visible;
    int y;

    if (!popup || !start_index || !visible_count) return;
    *popup = (gui_rect_t){0, 0, 0, 0};
    *start_index = 0;
    *visible_count = 0;
    if (!window || !widget || !dropdown || !dropdown->item_count) return;

    header = gui_widget_screen_bounds(window, widget);
    visible = widget_dropdown_visible_count(window, widget, dropdown,
                                            &open_upward);
    if (visible < 1) visible = 1;
    if ((int)dropdown->scroll_index + visible > dropdown->item_count) {
        if (dropdown->item_count > visible)
            dropdown->scroll_index = (uint8_t)(dropdown->item_count - visible);
        else
            dropdown->scroll_index = 0;
    }

    y = open_upward
        ? header.y - (visible * GUI_DROPDOWN_ITEM_HEIGHT + 2)
        : header.y + header.h - 1;
    *popup = (gui_rect_t){header.x, y, header.w,
                          visible * GUI_DROPDOWN_ITEM_HEIGHT + 2};
    *start_index = dropdown->scroll_index;
    *visible_count = (uint8_t)visible;
}

static int widget_dropdown_item_at(gui_window_t *window, gui_widget_t *widget,
                                   int x, int y) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);
    gui_rect_t popup;
    uint8_t start_index;
    uint8_t visible_count;
    int row;

    if (!window || !widget || !dropdown || !dropdown->expanded)
        return -1;

    widget_dropdown_prepare_popup(window, widget, &popup,
                                  &start_index, &visible_count);
    if (!gui_rect_contains(popup, x, y)) return -1;
    row = (y - popup.y - 1) / GUI_DROPDOWN_ITEM_HEIGHT;
    if (row < 0 || row >= visible_count) return -1;
    if ((int)start_index + row >= dropdown->item_count) return -1;
    return start_index + row;
}

gui_widget_t *gui_widget_create(gui_desktop_t *desktop, gui_window_t *window,
                                gui_widget_type_t type, gui_rect_t bounds,
                                const char *text,
                                gui_widget_callback_t callback) {
    gui_widget_t *widget;

    if (!desktop || !window) return NULL;

    widget = (gui_widget_t *)kzalloc(sizeof(gui_widget_t));
    if (!widget) return NULL;

    widget->id = desktop->next_widget_id++;
    widget->type = type;
    widget->style = GUI_WIDGET_STYLE_BUTTON;
    widget->bounds = gui_window_clamp_local_rect(window, bounds);
    widget->callback = callback;
    if (callback && (uint32_t)(uintptr_t)callback >= HEAP_START &&
        task_current_is_user()) widget->callback_pid = task_current_pid();
    widget->enabled = true;
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

gui_widget_t *gui_widget_create_button(gui_desktop_t *desktop,
                                       gui_window_t *window,
                                       gui_rect_t bounds,
                                       const char *text,
                                       gui_widget_callback_t callback) {
    gui_widget_t *widget = gui_widget_create(desktop, window, GUI_WIDGET_BUTTON,
                                             bounds, text, callback);
    if (widget) widget->style = GUI_WIDGET_STYLE_BUTTON;
    return widget;
}

gui_widget_t *gui_widget_create_selectable_button(gui_desktop_t *desktop,
                                                  gui_window_t *window,
                                                  gui_rect_t bounds,
                                                  const char *text,
                                                  gui_widget_callback_t callback) {
    gui_widget_t *widget = gui_widget_create(desktop, window, GUI_WIDGET_BUTTON,
                                             bounds, text, callback);
    if (widget) widget->style = GUI_WIDGET_STYLE_SELECTABLE;
    return widget;
}

gui_widget_t *gui_widget_create_listbox(gui_desktop_t *desktop,
                                        gui_window_t *window,
                                        gui_rect_t bounds,
                                        const char *text) {
    gui_widget_t *widget = gui_widget_create(desktop, window, GUI_WIDGET_LABEL,
                                             bounds, text, NULL);
    if (widget) widget->style = GUI_WIDGET_STYLE_LISTBOX;
    return widget;
}

gui_widget_t *gui_widget_create_dropdown(gui_desktop_t *desktop,
                                         gui_window_t *window,
                                         gui_rect_t bounds,
                                         gui_widget_callback_t callback) {
    gui_widget_t *widget;
    gui_dropdown_state_t *dropdown;

    widget = gui_widget_create(desktop, window, GUI_WIDGET_BUTTON,
                               bounds, "", callback);
    if (!widget) return NULL;
    dropdown = (gui_dropdown_state_t *)kzalloc(sizeof(*dropdown));
    if (!dropdown) return widget;

    dropdown->selected_index = -1;
    dropdown->hover_index = -1;
    widget->style = GUI_WIDGET_STYLE_DROPDOWN;
    widget->payload = dropdown;
    return widget;
}

void gui_widget_destroy(gui_widget_t *widget) {
    if (!widget) return;
    if (widget->payload) kfree(widget->payload);
    kfree(widget);
}

void gui_widget_set_style(gui_widget_t *widget, gui_widget_style_t style) {
    if (!widget) return;
    widget->style = style;
}

void gui_widget_set_selected(gui_widget_t *widget, bool selected) {
    if (!widget) return;
    widget->selected = selected;
}

void gui_widget_set_enabled(gui_widget_t *widget, bool enabled) {
    if (!widget) return;
    widget->enabled = enabled;
}

void gui_widget_dropdown_clear(gui_widget_t *widget) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);

    if (!dropdown) return;
    dropdown->item_count = 0;
    dropdown->scroll_index = 0;
    dropdown->selected_index = -1;
    dropdown->hover_index = -1;
    dropdown->expanded = false;
    widget->text[0] = '\0';
}

bool gui_widget_dropdown_add_item(gui_widget_t *widget,
                                  const char *label,
                                  const char *value) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);
    uint8_t index;

    if (!dropdown || dropdown->item_count >= GUI_DROPDOWN_MAX_ITEMS)
        return false;
    index = dropdown->item_count++;
    kstrncpy(dropdown->labels[index], label ? label : "",
             sizeof(dropdown->labels[index]) - 1);
    dropdown->labels[index][sizeof(dropdown->labels[index]) - 1] = '\0';
    kstrncpy(dropdown->values[index], value ? value :
             dropdown->labels[index], sizeof(dropdown->values[index]) - 1);
    dropdown->values[index][sizeof(dropdown->values[index]) - 1] = '\0';
    if (dropdown->selected_index < 0) {
        dropdown->selected_index = 0;
        widget_dropdown_sync_text(widget);
    }
    return true;
}

int gui_widget_dropdown_get_selected(const gui_widget_t *widget) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    if (!dropdown) return -1;
    return dropdown->selected_index;
}

void gui_widget_dropdown_set_selected(gui_widget_t *widget, int index) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);

    if (!dropdown) return;
    if (index < 0 || index >= dropdown->item_count) {
        dropdown->selected_index = -1;
        widget->text[0] = '\0';
        return;
    }
    dropdown->selected_index = (int8_t)index;
    if (dropdown->item_count > 5 &&
        dropdown->selected_index >= 4)
        dropdown->scroll_index = (uint8_t)(dropdown->selected_index - 3);
    else
        dropdown->scroll_index = 0;
    widget_dropdown_sync_text(widget);
}

bool gui_widget_dropdown_set_selected_by_value(gui_widget_t *widget,
                                               const char *value) {
    gui_dropdown_state_t *dropdown = widget_dropdown(widget);

    if (!dropdown || !value) return false;
    for (uint8_t i = 0; i < dropdown->item_count; i++) {
        if (kstrcmp(dropdown->values[i], value) == 0) {
            gui_widget_dropdown_set_selected(widget, i);
            return true;
        }
    }
    return false;
}

const char *gui_widget_dropdown_get_selected_label(const gui_widget_t *widget) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    if (!dropdown || dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count) return "";
    return dropdown->labels[(uint8_t)dropdown->selected_index];
}

const char *gui_widget_dropdown_get_selected_value(const gui_widget_t *widget) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    if (!dropdown || dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count) return "";
    return dropdown->values[(uint8_t)dropdown->selected_index];
}

const char *gui_widget_dropdown_get_item_label(const gui_widget_t *widget,
                                               int index) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    if (!dropdown || index < 0 || index >= dropdown->item_count) return "";
    return dropdown->labels[index];
}

const char *gui_widget_dropdown_get_item_value(const gui_widget_t *widget,
                                               int index) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    if (!dropdown || index < 0 || index >= dropdown->item_count) return "";
    return dropdown->values[index];
}

bool gui_widget_is_dropdown_expanded(const gui_widget_t *widget) {
    const gui_dropdown_state_t *dropdown = widget_dropdown_const(widget);

    return dropdown && dropdown->expanded;
}

void gui_widget_paint(gui_surface_t *surface, gui_window_t *window,
                      gui_widget_t *widget, gui_rect_t clip) {
    gui_rect_t bounds;
    gui_rect_t text_clip;
    uint32_t bg;
    uint32_t fg;
    bool sunken;
    int text_x;

    if (!surface || !window || !widget || !widget->visible) return;
    bounds = gui_widget_screen_bounds(window, widget);

    if (widget->style == GUI_WIDGET_STYLE_DROPDOWN) {
        gui_dropdown_state_t *dropdown = widget_dropdown(widget);
        gui_rect_t popup;
        gui_rect_t arrow_box;
        uint8_t start_index;
        uint8_t visible_count;

        if (!dropdown) return;
        widget_dropdown_prepare_popup(window, widget, &popup,
                                      &start_index, &visible_count);
        bg = !widget->enabled ? 0x00C8C8C8 :
             (dropdown->expanded ? 0x00D8D8D0 :
              (widget->hovered ? 0x00E4E4DC : 0x00D0D0C8));
        widget_draw_button_bevel(surface, bounds, dropdown->expanded, bg);
        arrow_box = (gui_rect_t){bounds.x + bounds.w - 18, bounds.y + 2,
                                 16, bounds.h - 4};
        widget_draw_button_bevel(surface, arrow_box, false, 0x00D6D6D6);
        gui_gfx_draw_line(surface, arrow_box.x + 5, arrow_box.y + 6,
                          arrow_box.x + 8, arrow_box.y + 9, 0x00101010);
        gui_gfx_draw_line(surface, arrow_box.x + 8, arrow_box.y + 9,
                          arrow_box.x + 11, arrow_box.y + 6, 0x00101010);
        fg = widget_text_color(widget, bg);
        gui_font_draw_string_clipped(surface, bounds.x + 6, bounds.y + 6,
                                     widget->text[0] ? widget->text : "(vacio)",
                                     fg, (gui_rect_t){bounds.x + 4, bounds.y + 3,
                                     bounds.w - 26, bounds.h - 6});

        if (!dropdown->expanded || popup.w <= 0 || popup.h <= 0) return;
        if (!gui_rect_intersect(popup, clip, &arrow_box)) return;

        widget_draw_button_bevel(surface, popup, true, 0x00FFFFFF);
        for (uint8_t row = 0; row < visible_count; row++) {
            int item_index = start_index + row;
            gui_rect_t item_bounds = {
                popup.x + 2, popup.y + 1 + row * GUI_DROPDOWN_ITEM_HEIGHT,
                popup.w - 4, GUI_DROPDOWN_ITEM_HEIGHT
            };
            uint32_t item_fg = 0x00101010;

            if (item_index >= dropdown->item_count) break;
            if (item_index == dropdown->hover_index) {
                gui_gfx_fill_rect(surface, item_bounds, 0x000080A0);
                item_fg = 0x00FFFFFF;
            } else if (item_index == dropdown->selected_index) {
                gui_gfx_fill_rect(surface, item_bounds, 0x00DDE6F4);
            }
            gui_font_draw_string_clipped(surface, item_bounds.x + 4,
                item_bounds.y + 5, dropdown->labels[item_index], item_fg,
                (gui_rect_t){item_bounds.x + 4, item_bounds.y + 2,
                item_bounds.w - 8, item_bounds.h - 4});
        }
        return;
    }

    if (!gui_rect_intersect(bounds, clip, &text_clip)) return;

    if (widget->style == GUI_WIDGET_STYLE_LISTBOX) {
        widget_draw_button_bevel(surface, bounds, true, 0x00FFFFFF);
        if (widget->text[0]) {
            gui_rect_t label_clip = {bounds.x + 4, bounds.y + 3,
                                     bounds.w - 8, bounds.h - 6};
            gui_font_draw_string_clipped(surface, bounds.x + 6, bounds.y + 6,
                                         widget->text, 0x00101010, label_clip);
        }
        return;
    }

    if (widget->type == GUI_WIDGET_LABEL) {
        gui_font_draw_string_clipped(surface, bounds.x, bounds.y + 4,
                                     widget->text, 0x00283C4A, text_clip);
        return;
    }

    if (!widget->enabled) bg = 0x00C8C8C8;
    else if (widget->style == GUI_WIDGET_STYLE_SELECTABLE && widget->selected)
        bg = widget->pressed ? 0x000080A0 : 0x0000A0C0;
    else if (widget->hovered)
        bg = widget->pressed ? 0x00C8C8C0 : 0x00E4E4DC;
    else
        bg = widget->pressed ? 0x00B8B8B0 : 0x00D0D0C8;
    sunken = widget->pressed ||
             (widget->style == GUI_WIDGET_STYLE_SELECTABLE && widget->selected);
    widget_draw_button_bevel(surface, bounds, sunken, bg);

    if (widget->style == GUI_WIDGET_STYLE_SELECTABLE &&
        widget->selected && !widget->pressed) {
        gui_gfx_draw_rect(surface,
            (gui_rect_t){bounds.x + 3, bounds.y + 3, bounds.w - 6, bounds.h - 6},
            0x00FFFFFF);
    } else if (widget->hovered && widget->enabled && !widget->pressed) {
        gui_gfx_draw_rect(surface,
            (gui_rect_t){bounds.x + 3, bounds.y + 3, bounds.w - 6, bounds.h - 6},
            0x00A0A0A0);
    }

    text_x = bounds.x + (bounds.w - (int)gui_font_text_width(widget->text)) / 2;
    if (text_x < bounds.x + 4) text_x = bounds.x + 4;
    fg = widget_text_color(widget, bg);
    if (!widget->enabled) {
        gui_font_draw_string_clipped(surface, text_x + 1, bounds.y + 6,
                                     widget->text, 0x00FFFFFF,
                                     (gui_rect_t){bounds.x + 4, bounds.y + 3,
                                     bounds.w - 8, bounds.h - 6});
    } else if (widget->pressed) {
        text_x += 1;
    }
    text_clip = (gui_rect_t){bounds.x + 3, bounds.y + 2,
                             bounds.w - 6, bounds.h - 4};
    gui_font_draw_string_clipped(surface, text_x, bounds.y + 5,
                                 widget->text, fg, text_clip);
}

bool gui_widget_handle_event(gui_window_t *window, gui_widget_t *widget,
                             const gui_event_t *event) {
    gui_rect_t bounds;
    bool inside;

    if (!window || !widget || !event || !widget->visible || !widget->enabled)
        return false;

    bounds = gui_widget_screen_bounds(window, widget);

    if (widget->style == GUI_WIDGET_STYLE_DROPDOWN) {
        gui_dropdown_state_t *dropdown = widget_dropdown(widget);
        gui_rect_t popup;
        uint8_t start_index;
        uint8_t visible_count;
        bool inside_header = gui_rect_contains(bounds, event->x, event->y);
        int hover_index = -1;
        bool was_expanded;

        if (!dropdown) return false;
        widget_dropdown_prepare_popup(window, widget, &popup,
                                      &start_index, &visible_count);
        hover_index = widget_dropdown_item_at(window, widget, event->x, event->y);
        was_expanded = dropdown->expanded;

        if (event->type == GUI_EVENT_MOUSE_MOVE) {
            bool changed = widget->hovered != inside_header ||
                           dropdown->hover_index != hover_index;
            widget->hovered = inside_header;
            dropdown->hover_index = hover_index;
            if (changed) window->dirty = true;
            return changed || inside_header || hover_index >= 0 || was_expanded;
        }

        if (event->type == GUI_EVENT_MOUSE_DOWN) {
            if (inside_header || hover_index >= 0) {
                widget->pressed = true;
                window->dirty = true;
                return true;
            }
            if (dropdown->expanded) {
                dropdown->expanded = false;
                dropdown->hover_index = -1;
                window->dirty = true;
                gui_request_paint();
                return true;
            }
            return false;
        }

        if (event->type == GUI_EVENT_MOUSE_UP) {
            bool was_pressed = widget->pressed;
            widget->pressed = false;
            if (was_pressed || was_expanded) window->dirty = true;

            if (was_pressed && inside_header) {
                dropdown->expanded = !dropdown->expanded;
                if (dropdown->expanded) {
                    if (dropdown->selected_index >= 4)
                        dropdown->scroll_index =
                            (uint8_t)(dropdown->selected_index - 3);
                    else
                        dropdown->scroll_index = 0;
                } else {
                    dropdown->hover_index = -1;
                }
                gui_request_paint();
                return true;
            }

            if ((was_pressed || was_expanded) && hover_index >= 0) {
                dropdown->selected_index = (int8_t)hover_index;
                dropdown->expanded = false;
                dropdown->hover_index = -1;
                widget_dropdown_sync_text(widget);
                if (widget->callback) {
                    uint32_t arguments[2] = {
                        (uint32_t)(uintptr_t)window, widget->id};
                    if (widget->callback_pid)
                        (void)task_queue_user_upcall(widget->callback_pid,
                            (uint32_t)(uintptr_t)widget->callback, arguments,
                            2, NULL, 0, -1);
                    else widget->callback(window, widget->id);
                }
                gui_request_paint();
                return true;
            }

            if (was_expanded) {
                dropdown->expanded = false;
                dropdown->hover_index = -1;
                gui_request_paint();
                return true;
            }
            return was_pressed;
        }

        return false;
    }

    if (widget->style == GUI_WIDGET_STYLE_LISTBOX) return false;
    inside = gui_rect_contains(bounds, event->x, event->y);

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        bool changed = widget->hovered != inside;
        widget->hovered = inside;
        if (changed) window->dirty = true;
        return changed || inside;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN && inside) {
        if (!widget->pressed) {
            widget->pressed = true;
            window->dirty = true;
        }
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        bool was_pressed = widget->pressed;
        widget->pressed = false;
        if (was_pressed) window->dirty = true;
        if (was_pressed && inside && widget->callback) {
            uint32_t arguments[2] = {
                (uint32_t)(uintptr_t)window, widget->id};
            if (widget->callback_pid)
                (void)task_queue_user_upcall(widget->callback_pid,
                    (uint32_t)(uintptr_t)widget->callback, arguments, 2,
                    NULL, 0, -1);
            else widget->callback(window, widget->id);
            return true;
        }
        if (was_pressed) return true;
    }

    return false;
}
