#include "gui.h"
#include "../kernel/include/task.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/keyboard.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/clipboard.h"

#define GUI_DROPDOWN_MAX_ITEMS 96
#define GUI_DROPDOWN_ITEM_HEIGHT 18
#define GUI_TEXTBOX_STORAGE 512

#define WIDGET_CDE_FACE       0x00C4B3ABU
#define WIDGET_CDE_FACE_HOVER 0x00D0C0B9U
#define WIDGET_CDE_FACE_DOWN  0x00A99A94U
#define WIDGET_CDE_SELECT     0x004B93A8U
#define WIDGET_CDE_SELECT_DN  0x003C7484U
#define WIDGET_CDE_LIGHT      0x00F1E5DFU
#define WIDGET_CDE_MIDLIGHT   0x00D8C7BFU
#define WIDGET_CDE_SHADOW     0x007F746EU
#define WIDGET_CDE_DARK       0x004D4C4FU

typedef struct {
    uint8_t item_count;
    uint8_t scroll_index;
    int8_t selected_index;
    int8_t hover_index;
    bool expanded;
    char labels[GUI_DROPDOWN_MAX_ITEMS][48];
    char values[GUI_DROPDOWN_MAX_ITEMS][64];
} gui_dropdown_state_t;

typedef struct {
    char text[GUI_TEXTBOX_STORAGE];
    uint16_t length;
    uint16_t cursor;
    uint16_t scroll;
    uint16_t max_length;
    uint16_t anchor;
    bool focused;
    bool mouse_selecting;
} gui_textbox_state_t;

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
    uint32_t outer_tl = sunken ? WIDGET_CDE_DARK : WIDGET_CDE_LIGHT;
    uint32_t inner_tl = sunken ? WIDGET_CDE_SHADOW : WIDGET_CDE_MIDLIGHT;
    uint32_t outer_br = sunken ? WIDGET_CDE_LIGHT : WIDGET_CDE_DARK;
    uint32_t inner_br = sunken ? WIDGET_CDE_MIDLIGHT : WIDGET_CDE_SHADOW;

    gui_gfx_fill_rect(surface, bounds, fill);
    if (bounds.w < 4 || bounds.h < 4) return;
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x, bounds.y, bounds.w, 1}, outer_tl);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x, bounds.y, 1, bounds.h}, outer_tl);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x + 1, bounds.y + 1, bounds.w - 2, 1}, inner_tl);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x + 1, bounds.y + 1, 1, bounds.h - 2}, inner_tl);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x, bounds.y + bounds.h - 1, bounds.w, 1}, outer_br);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x + bounds.w - 1, bounds.y, 1, bounds.h}, outer_br);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x + 1, bounds.y + bounds.h - 2, bounds.w - 2, 1}, inner_br);
    gui_gfx_fill_rect(surface, (gui_rect_t){bounds.x + bounds.w - 2, bounds.y + 1, 1, bounds.h - 2}, inner_br);
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

static gui_textbox_state_t *widget_textbox(gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_TEXTBOX) return NULL;
    return (gui_textbox_state_t *)widget->payload;
}

static const gui_textbox_state_t *widget_textbox_const(
    const gui_widget_t *widget) {
    if (!widget || widget->type != GUI_WIDGET_TEXTBOX) return NULL;
    return (const gui_textbox_state_t *)widget->payload;
}

static uint16_t textbox_char_width(char character) {
    char text[2] = {character, '\0'};
    uint16_t width = gui_font_text_width(text);
    return width ? width : 8U;
}

static uint16_t textbox_range_width(const gui_textbox_state_t *textbox,
                                    uint16_t start, uint16_t end) {
    uint32_t width = 0;
    if (!textbox) return 0;
    if (start > textbox->length) start = textbox->length;
    if (end > textbox->length) end = textbox->length;
    if (end < start) end = start;
    while (start < end) {
        width += textbox_char_width(textbox->text[start++]);
        if (width > 0xFFFFU) return 0xFFFFU;
    }
    return (uint16_t)width;
}

static void textbox_ensure_cursor_visible(gui_widget_t *widget) {
    gui_textbox_state_t *textbox = widget_textbox(widget);
    uint16_t available;

    if (!textbox || widget->bounds.w <= 8) return;
    available = (uint16_t)(widget->bounds.w - 8);
    if (textbox->cursor < textbox->scroll)
        textbox->scroll = textbox->cursor;
    while (textbox->scroll < textbox->cursor &&
           textbox_range_width(textbox, textbox->scroll,
                               textbox->cursor) >= available)
        textbox->scroll++;
    while (textbox->scroll > 0 &&
           textbox_range_width(textbox, (uint16_t)(textbox->scroll - 1U),
                               textbox->cursor) < available)
        textbox->scroll--;
}

static uint16_t textbox_hit_test(const gui_textbox_state_t *textbox,
                                 int local_x) {
    uint16_t index;
    int x = 0;

    if (!textbox || local_x <= 0) return textbox ? textbox->scroll : 0U;
    index = textbox->scroll;
    while (index < textbox->length) {
        int width = textbox_char_width(textbox->text[index]);
        if (local_x < x + width / 2) return index;
        x += width;
        if (local_x < x) return (uint16_t)(index + 1U);
        index++;
    }
    return textbox->length;
}

static void textbox_insert(gui_textbox_state_t *textbox, char character);

static void textbox_delete_at(gui_textbox_state_t *textbox, uint16_t index) {
    uint16_t i;
    if (!textbox || index >= textbox->length) return;
    for (i = index; i < textbox->length; i++)
        textbox->text[i] = textbox->text[i + 1U];
    textbox->length--;
    if (textbox->cursor > textbox->length) textbox->cursor = textbox->length;
}

static bool textbox_has_selection(const gui_textbox_state_t *textbox) {
    return textbox && textbox->cursor != textbox->anchor;
}

static void textbox_selection_bounds(const gui_textbox_state_t *textbox,
                                      uint16_t *start, uint16_t *end) {
    if (!textbox) return;
    if (textbox->cursor < textbox->anchor) {
        if (start) *start = textbox->cursor;
        if (end) *end = textbox->anchor;
    } else {
        if (start) *start = textbox->anchor;
        if (end) *end = textbox->cursor;
    }
}

static bool textbox_delete_selection(gui_textbox_state_t *textbox) {
    uint16_t start, end, i;
    if (!textbox_has_selection(textbox)) return false;
    textbox_selection_bounds(textbox, &start, &end);
    for (i = end; i <= textbox->length; i++)
        textbox->text[start + i - end] = textbox->text[i];
    textbox->length = (uint16_t)(textbox->length - (end - start));
    textbox->cursor = start;
    textbox->anchor = start;
    return true;
}

static void textbox_copy_selection(gui_textbox_state_t *textbox) {
    char buffer[GUI_TEXTBOX_STORAGE];
    uint16_t start, end, length, i;
    if (!textbox_has_selection(textbox)) return;
    textbox_selection_bounds(textbox, &start, &end);
    length = (uint16_t)(end - start);
    if (length >= GUI_TEXTBOX_STORAGE) length = GUI_TEXTBOX_STORAGE - 1U;
    for (i = 0; i < length; i++) buffer[i] = textbox->text[start + i];
    buffer[length] = '\0';
    (void)bk_clipboard_set_text(buffer);
}

static void textbox_paste_clipboard(gui_textbox_state_t *textbox) {
    char buffer[GUI_TEXTBOX_STORAGE];
    uint32_t length, i;
    if (!textbox) return;
    length = bk_clipboard_get_text(buffer, sizeof(buffer));
    if (!length) return;
    (void)textbox_delete_selection(textbox);
    for (i = 0; i < length && textbox->length < textbox->max_length &&
         textbox->length + 1U < GUI_TEXTBOX_STORAGE; i++) {
        unsigned char character = (unsigned char)buffer[i];
        if (character >= 32U && character < 127U)
            textbox_insert(textbox, (char)character);
    }
    textbox->anchor = textbox->cursor;
}

static void textbox_context_action(gui_window_t *window, uint32_t item_id,
                                    void *context) {
    gui_widget_t *widget = (gui_widget_t *)context;
    gui_textbox_state_t *textbox = widget_textbox(widget);
    if (!window || !widget || !textbox) return;
    if (item_id == GUI_TEXT_ACTION_COPY)
        textbox_copy_selection(textbox);
    else if (item_id == GUI_TEXT_ACTION_PASTE)
        textbox_paste_clipboard(textbox);
    textbox_ensure_cursor_visible(widget);
    window->dirty = true;
    gui_request_paint();
}

static void textbox_insert(gui_textbox_state_t *textbox, char character) {
    uint16_t i;
    if (!textbox || textbox->length >= textbox->max_length ||
        textbox->length + 1U >= GUI_TEXTBOX_STORAGE) return;
    for (i = textbox->length + 1U; i > textbox->cursor; i--)
        textbox->text[i] = textbox->text[i - 1U];
    textbox->text[textbox->cursor++] = character;
    textbox->length++;
    textbox->text[textbox->length] = '\0';
}

static void widget_invoke_callback(gui_window_t *window,
                                   gui_widget_t *widget) {
    uint32_t arguments[2];
    if (!window || !widget || !widget->callback) return;
    arguments[0] = (uint32_t)(uintptr_t)window;
    arguments[1] = widget->id;
    if (widget->callback_pid)
        (void)task_queue_window_upcall(
            window, widget->callback_pid,
            (uint32_t)(uintptr_t)widget->callback, arguments, 2,
            NULL, 0, -1);
    else
        widget->callback(window, widget->id);
}

gui_widget_t *gui_widget_create(gui_desktop_t *desktop, gui_window_t *window,
                                gui_widget_type_t type, gui_rect_t bounds,
                                const char *text,
                                gui_widget_callback_t callback) {
    gui_widget_t *widget;

    if (!desktop || !window) return NULL;

    widget = (gui_widget_t *)kzalloc(sizeof(gui_widget_t));
    if (!widget) return NULL;
    if (!mm_set_allocation_owner(widget, 0U)) {
        kfree(widget);
        return NULL;
    }

    widget->id = desktop->next_widget_id++;
    widget->type = type;
    widget->style = GUI_WIDGET_STYLE_BUTTON;
    widget->bounds = gui_window_clamp_local_rect(window, bounds);
    widget->callback = callback;
    if (callback && (uint32_t)(uintptr_t)callback >= HEAP_START &&
        task_current_is_user()) {
        widget->callback_pid = task_current_pid();
        /* See gui/windows.c: callbacks stay on CPU0 while worker threads keep
         * the normal SMP affinity mask. */
        (void)task_set_affinity_mask(widget->callback_pid, 1U);
    }
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
    if (!mm_set_allocation_owner(dropdown, 0U)) {
        kfree(dropdown);
        return widget;
    }

    dropdown->selected_index = -1;
    dropdown->hover_index = -1;
    widget->style = GUI_WIDGET_STYLE_DROPDOWN;
    widget->payload = dropdown;
    return widget;
}

gui_widget_t *gui_widget_create_textbox(gui_desktop_t *desktop,
                                        gui_window_t *window,
                                        gui_rect_t bounds,
                                        const char *text,
                                        uint16_t max_length,
                                        gui_widget_callback_t callback) {
    gui_widget_t *widget;
    gui_textbox_state_t *textbox;

    widget = gui_widget_create(desktop, window, GUI_WIDGET_TEXTBOX,
                               bounds, "", callback);
    if (!widget) return NULL;
    textbox = (gui_textbox_state_t *)kzalloc(sizeof(*textbox));
    if (textbox && !mm_set_allocation_owner(textbox, 0U)) {
        kfree(textbox);
        textbox = NULL;
    }
    if (!textbox) {
        if (widget->prev) widget->prev->next = NULL;
        else window->widgets = NULL;
        gui_widget_destroy(widget);
        return NULL;
    }
    if (!max_length || max_length >= GUI_TEXTBOX_STORAGE)
        max_length = GUI_TEXTBOX_STORAGE - 1U;
    textbox->max_length = max_length;
    widget->payload = textbox;
    gui_widget_set_text(widget, text ? text : "");
    return widget;
}

void gui_widget_destroy(gui_widget_t *widget) {
    if (!widget) return;
    if (widget->icon_pixels) kfree(widget->icon_pixels);
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

void gui_widget_set_visible(gui_window_t *window, gui_widget_t *widget,
                            bool visible) {
    if (!window || !widget || widget->visible == visible) return;
    if (!visible && window->focused_widget == widget)
        gui_widget_set_focus(window, widget, false);
    widget->visible = visible;
    window->dirty = true;
}

void gui_widget_set_bounds(gui_window_t *window, gui_widget_t *widget,
                           gui_rect_t bounds) {
    gui_rect_t clamped;
    if (!window || !widget) return;
    clamped = gui_window_clamp_local_rect(window, bounds);
    if (widget->bounds.x == clamped.x && widget->bounds.y == clamped.y &&
        widget->bounds.w == clamped.w && widget->bounds.h == clamped.h)
        return;
    widget->bounds = clamped;
    textbox_ensure_cursor_visible(widget);
    window->dirty = true;
}

void gui_widget_set_text(gui_widget_t *widget, const char *text) {
    gui_textbox_state_t *textbox;
    uint16_t length = 0;

    if (!widget) return;
    textbox = widget_textbox(widget);
    if (!textbox) {
        kstrncpy(widget->text, text ? text : "", sizeof(widget->text) - 1U);
        widget->text[sizeof(widget->text) - 1U] = '\0';
        return;
    }
    while (text && text[length] && length < textbox->max_length &&
           length + 1U < GUI_TEXTBOX_STORAGE) {
        textbox->text[length] = text[length];
        length++;
    }
    textbox->text[length] = '\0';
    textbox->length = length;
    textbox->cursor = length;
    textbox->anchor = length;
    textbox->scroll = 0;
    textbox->mouse_selecting = false;
    textbox_ensure_cursor_visible(widget);
}

void gui_widget_take_icon(gui_widget_t *widget, uint32_t *pixels,
                          uint16_t width, uint16_t height) {
    if (!widget) {
        if (pixels) kfree(pixels);
        return;
    }
    if (pixels && !mm_set_allocation_owner(pixels, 0U)) {
        kfree(pixels);
        pixels = NULL;
        width = 0U;
        height = 0U;
    }
    if (widget->icon_pixels) kfree(widget->icon_pixels);
    widget->icon_pixels = pixels;
    widget->icon_width = pixels ? width : 0U;
    widget->icon_height = pixels ? height : 0U;
    gui_request_paint();
}

static void widget_draw_icon(gui_surface_t *surface, gui_rect_t destination,
                             gui_rect_t clip, const gui_widget_t *widget,
                             bool disabled) {
    gui_rect_t visible;
    if (!surface || !surface->pixels || !widget || !widget->icon_pixels ||
        !widget->icon_width || !widget->icon_height ||
        destination.w <= 0 || destination.h <= 0 ||
        !gui_rect_intersect(destination, clip, &visible) ||
        !gui_rect_intersect(visible, surface->clip, &visible))
        return;
    for (int y = visible.y; y < visible.y + visible.h; y++) {
        uint32_t source_y = (uint32_t)(y - destination.y) *
                            widget->icon_height / (uint32_t)destination.h;
        for (int x = visible.x; x < visible.x + visible.w; x++) {
            uint32_t source_x = (uint32_t)(x - destination.x) *
                                widget->icon_width /
                                (uint32_t)destination.w;
            uint32_t source = widget->icon_pixels[
                source_y * widget->icon_width + source_x];
            uint8_t alpha = (uint8_t)(source >> 24);
            uint32_t *target;
            if (!alpha) continue;
            target = &surface->pixels[
                (y - surface->origin_y) * surface->pitch +
                (x - surface->origin_x)];
            if (disabled) {
                uint32_t gray = (((source >> 16) & 0xFFU) +
                                 ((source >> 8) & 0xFFU) +
                                 (source & 0xFFU)) / 3U;
                source = (gray << 16) | (gray << 8) | gray;
                alpha = (uint8_t)(alpha / 2U);
            }
            *target = alpha == 255U
                ? source & 0x00FFFFFFU
                : gui_color_blend(*target, source, alpha);
        }
    }
}

bool gui_widget_get_text(const gui_widget_t *widget, char *buffer,
                         uint32_t capacity) {
    const gui_textbox_state_t *textbox;
    const char *source;
    uint32_t index = 0;

    if (!widget || !buffer || !capacity) return false;
    textbox = widget_textbox_const(widget);
    source = textbox ? textbox->text : widget->text;
    while (source[index] && index + 1U < capacity) {
        buffer[index] = source[index];
        index++;
    }
    buffer[index] = '\0';
    return true;
}

void gui_widget_set_focus(gui_window_t *window, gui_widget_t *widget,
                          bool focused) {
    gui_textbox_state_t *textbox;

    if (!window) return;

    if (!focused || !widget) {
        if (!widget) {
            gui_textbox_state_t *previous =
                widget_textbox(window->focused_widget);
            if (previous) { previous->focused = false; previous->mouse_selecting = false; }
            window->focused_widget = NULL;
        } else {
            textbox = widget_textbox(widget);
            if (textbox) { textbox->focused = false; textbox->mouse_selecting = false; }
            if (window->focused_widget == widget)
                window->focused_widget = NULL;
        }
        window->dirty = true;
        return;
    }

    textbox = widget_textbox(widget);
    if (!textbox) return;
    if (window->focused_widget && window->focused_widget != widget) {
        gui_textbox_state_t *previous =
            widget_textbox(window->focused_widget);
        if (previous) { previous->focused = false; previous->mouse_selecting = false; }
    }
    window->focused_widget = widget;
    textbox->focused = true;
    textbox_ensure_cursor_visible(widget);
    window->dirty = true;
}

bool gui_widget_is_focused(const gui_window_t *window,
                           const gui_widget_t *widget) {
    const gui_textbox_state_t *textbox = widget_textbox_const(widget);
    return window && textbox && window->focused_widget == widget &&
           textbox->focused;
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

    if (widget->type == GUI_WIDGET_TEXTBOX) {
        gui_textbox_state_t *textbox = widget_textbox(widget);
        gui_rect_t inner;
        int caret_x;
        uint32_t fill = widget->enabled ? 0x00FFFFFFU : 0x00D8D8D8U;
        if (!textbox) return;
        textbox_ensure_cursor_visible(widget);
        widget_draw_button_bevel(surface, bounds, true, fill);
        inner = (gui_rect_t){bounds.x + 4, bounds.y + 3,
                             bounds.w - 8, bounds.h - 6};
        if (inner.w <= 0 || inner.h <= 0) return;
        {
            uint16_t index = textbox->scroll;
            uint16_t select_start = 0, select_end = 0;
            int text_x = inner.x;
            int text_y = bounds.y + (bounds.h - 8) / 2;
            bool selected = textbox_has_selection(textbox);
            if (selected)
                textbox_selection_bounds(textbox, &select_start, &select_end);
            while (index < textbox->length && text_x < inner.x + inner.w) {
                char character = textbox->text[index];
                int width = textbox_char_width(character);
                bool cell_selected = selected &&
                    index >= select_start && index < select_end;
                if (cell_selected)
                    gui_gfx_fill_rect(surface,
                        (gui_rect_t){text_x, inner.y, width, inner.h},
                        0x00000080U);
                gui_font_draw_char(surface, text_x, text_y, character,
                    cell_selected ? 0x00FFFFFFU :
                    (widget->enabled ? 0x00101010U : 0x00707070U),
                    0, false);
                text_x += width;
                index++;
            }
        }
        if (textbox->focused && window->focused && widget->enabled) {
            caret_x = inner.x + textbox_range_width(textbox,
                textbox->scroll, textbox->cursor);
            if (caret_x >= inner.x + inner.w) caret_x = inner.x + inner.w - 1;
            gui_gfx_fill_rect(surface,
                (gui_rect_t){caret_x, bounds.y + 4, 1, bounds.h - 8},
                0x00000000U);
        }
        return;
    }

    if (widget->style == GUI_WIDGET_STYLE_DROPDOWN) {
        gui_dropdown_state_t *dropdown = widget_dropdown(widget);
        gui_rect_t popup;
        gui_rect_t arrow_box;
        uint8_t start_index;
        uint8_t visible_count;

        if (!dropdown) return;
        widget_dropdown_prepare_popup(window, widget, &popup,
                                      &start_index, &visible_count);
        bg = !widget->enabled ? 0x00AFA6A2U :
             (dropdown->expanded ? WIDGET_CDE_FACE_DOWN :
              (widget->hovered ? WIDGET_CDE_FACE_HOVER : WIDGET_CDE_FACE));
        widget_draw_button_bevel(surface, bounds, dropdown->expanded, bg);
        arrow_box = (gui_rect_t){bounds.x + bounds.w - 18, bounds.y + 2,
                                 16, bounds.h - 4};
        widget_draw_button_bevel(surface, arrow_box, false, WIDGET_CDE_FACE);
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
                gui_gfx_fill_rect(surface, item_bounds, WIDGET_CDE_SELECT);
                item_fg = 0x00FFFFFF;
            } else if (item_index == dropdown->selected_index) {
                gui_gfx_fill_rect(surface, item_bounds, 0x00D8CBC6U);
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

    if (!widget->enabled) bg = 0x00AFA6A2U;
    else if (widget->style == GUI_WIDGET_STYLE_SELECTABLE && widget->selected)
        bg = widget->pressed ? WIDGET_CDE_SELECT_DN : WIDGET_CDE_SELECT;
    else if (widget->hovered)
        bg = widget->pressed ? WIDGET_CDE_FACE_DOWN : WIDGET_CDE_FACE_HOVER;
    else
        bg = widget->pressed ? WIDGET_CDE_FACE_DOWN : WIDGET_CDE_FACE;
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
            WIDGET_CDE_SHADOW);
    }

    {
        int icon_size = 0;
        int gap = 0;
        int text_width = (int)gui_font_text_width(widget->text);
        int group_width;
        int icon_x;
        int icon_y;
        if (widget->icon_pixels) {
            icon_size = bounds.h - 8;
            if (icon_size > 20) icon_size = 20;
            if (icon_size < 8) icon_size = 8;
            gap = widget->text[0] ? 4 : 0;
        }
        group_width = icon_size + gap + text_width;
        icon_x = bounds.x + (bounds.w - group_width) / 2;
        icon_y = bounds.y + (bounds.h - icon_size) / 2;
        if (sunken) { icon_x++; icon_y++; }
        if (icon_size)
            widget_draw_icon(surface,
                (gui_rect_t){icon_x, icon_y, icon_size, icon_size},
                text_clip, widget, !widget->enabled);
        text_x = icon_x + icon_size + gap;
        if (!icon_size)
            text_x = bounds.x + (bounds.w - text_width) / 2;
    }
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

bool gui_widget_open_text_context_at(gui_window_t *window, int x, int y) {
    gui_widget_t *widget;
    if (!window) return false;
    for (widget = window->widgets; widget; widget = widget->next) {
        gui_textbox_state_t *textbox;
        gui_rect_t bounds;
        if (widget->type != GUI_WIDGET_TEXTBOX || !widget->visible ||
            !widget->enabled) continue;
        bounds = gui_widget_screen_bounds(window, widget);
        if (!gui_rect_contains(bounds, x, y)) continue;
        textbox = widget_textbox(widget);
        if (!textbox_has_selection(textbox)) return false;
        gui_window_context_clear(window);
        (void)gui_window_context_add_item(window, GUI_TEXT_ACTION_COPY,
            "Copiar", true, textbox_context_action, widget);
        (void)gui_window_context_add_item(window, GUI_TEXT_ACTION_PASTE,
            "Pegar", true, textbox_context_action, widget);
        gui_window_context_open(window, x, y);
        return true;
    }
    return false;
}

bool gui_widget_handle_event(gui_window_t *window, gui_widget_t *widget,
                             const gui_event_t *event) {
    gui_rect_t bounds;
    bool inside;

    if (!window || !widget || !event || !widget->visible || !widget->enabled)
        return false;

    bounds = gui_widget_screen_bounds(window, widget);

    if (widget->type == GUI_WIDGET_TEXTBOX) {
        gui_textbox_state_t *textbox = widget_textbox(widget);
        bool textbox_inside = gui_rect_contains(bounds, event->x, event->y);
        uint8_t key;
        if (!textbox) return false;

        if (event->type == GUI_EVENT_MOUSE_MOVE) {
            bool changed = widget->hovered != textbox_inside;
            widget->hovered = textbox_inside;
            if (textbox->mouse_selecting &&
                (event->buttons & MOUSE_LEFT_BUTTON)) {
                int local_x = event->x - bounds.x - 4;
                if (local_x < 0) local_x = 0;
                textbox->cursor = textbox_hit_test(textbox, local_x);
                textbox_ensure_cursor_visible(widget);
                window->dirty = true;
                gui_request_paint();
                return true;
            }
            if (changed) window->dirty = true;
            return changed || textbox_inside;
        }
        if (event->type == GUI_EVENT_MOUSE_DOWN) {
            if (event->button != MOUSE_LEFT_BUTTON) return false;
            if (textbox_inside) {
                uint16_t cursor;
                gui_widget_set_focus(window, widget, true);
                cursor = textbox_hit_test(textbox, event->x - bounds.x - 4);
                textbox->cursor = cursor;
                if (!event->shift) textbox->anchor = cursor;
                textbox->mouse_selecting = true;
                textbox_ensure_cursor_visible(widget);
                window->dirty = true;
                gui_request_paint();
                return true;
            }
            if (gui_widget_is_focused(window, widget)) {
                textbox->mouse_selecting = false;
                gui_widget_set_focus(window, widget, false);
                gui_request_paint();
                return true;
            }
            return false;
        }
        if (event->type == GUI_EVENT_MOUSE_UP) {
            if (event->button != MOUSE_LEFT_BUTTON) return false;
            if (textbox->mouse_selecting) {
                textbox->mouse_selecting = false;
                window->dirty = true;
                gui_request_paint();
                return true;
            }
            return textbox_inside && gui_widget_is_focused(window, widget);
        }
        if (event->type != GUI_EVENT_KEY ||
            !gui_widget_is_focused(window, widget)) return false;

        key = (uint8_t)event->key;
        if (event->ctrl) {
            if (key == 'a' || key == 'A') {
                textbox->anchor = 0;
                textbox->cursor = textbox->length;
            } else if (key == 'c' || key == 'C') {
                textbox_copy_selection(textbox);
            } else if (key == 'x' || key == 'X') {
                textbox_copy_selection(textbox);
                (void)textbox_delete_selection(textbox);
            } else if (key == 'v' || key == 'V') {
                textbox_paste_clipboard(textbox);
            } else {
                return false;
            }
        } else if (key == KEY_LEFT) {
            if (textbox->cursor) textbox->cursor--;
            if (!event->shift) textbox->anchor = textbox->cursor;
        } else if (key == KEY_RIGHT) {
            if (textbox->cursor < textbox->length) textbox->cursor++;
            if (!event->shift) textbox->anchor = textbox->cursor;
        } else if (key == KEY_HOME) {
            textbox->cursor = 0;
            if (!event->shift) textbox->anchor = textbox->cursor;
        } else if (key == KEY_END) {
            textbox->cursor = textbox->length;
            if (!event->shift) textbox->anchor = textbox->cursor;
        } else if (key == KEY_BACKSPACE) {
            if (!textbox_delete_selection(textbox) && textbox->cursor) {
                textbox->cursor--;
                textbox_delete_at(textbox, textbox->cursor);
                textbox->anchor = textbox->cursor;
            }
        } else if (key == KEY_DELETE) {
            if (!textbox_delete_selection(textbox))
                textbox_delete_at(textbox, textbox->cursor);
            textbox->anchor = textbox->cursor;
        } else if (key == KEY_ENTER) {
            widget_invoke_callback(window, widget);
        } else if (key == KEY_ESCAPE || key == KEY_TAB) {
            gui_widget_set_focus(window, widget, false);
        } else if (!event->alt && key >= 32U && key < 127U) {
            (void)textbox_delete_selection(textbox);
            textbox_insert(textbox, (char)key);
            textbox->anchor = textbox->cursor;
        } else {
            return false;
        }
        textbox_ensure_cursor_visible(widget);
        window->dirty = true;
        gui_request_paint();
        return true;
    }

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
                        (void)task_queue_window_upcall(
                            window, widget->callback_pid,
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
                (void)task_queue_window_upcall(
                    window, widget->callback_pid,
                    (uint32_t)(uintptr_t)widget->callback, arguments, 2,
                    NULL, 0, -1);
            else widget->callback(window, widget->id);
            return true;
        }
        if (was_pressed) return true;
    }

    return false;
}
