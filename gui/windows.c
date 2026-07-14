#include "gui.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/about_dialog.h"
#include "../kernel/include/task.h"

static uint32_t callback_owner(const void *callback) {
    return callback && (uint32_t)(uintptr_t)callback >= HEAP_START &&
           task_current_is_user() ? task_current_pid() : 0U;
}

static bool queue_menu_callback(const gui_menu_item_t *item,
                                gui_window_t *window) {
    uint32_t arguments[3];
    if (!item || !item->callback) return false;
    if (!item->callback_pid) {
        item->callback(window, item->id, item->context);
        return true;
    }
    arguments[0] = (uint32_t)(uintptr_t)window;
    arguments[1] = item->id;
    arguments[2] = (uint32_t)(uintptr_t)item->context;
    return task_queue_user_upcall(item->callback_pid,
        (uint32_t)(uintptr_t)item->callback, arguments, 3, NULL, 0, -1);
}

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
    window->borderless = false;
    window->resizable = true;
    window->drag_height = 0;
    window->dirty = true;
    window->open_menu = -1;
    window->pressed_menu_item = -1;
    window->context_menu.pressed_item = -1;
    copy_text(window->title, sizeof(window->title), title);
    return window;
}

int gui_window_content_top(const gui_window_t *window) {
    return ((window && window->borderless) ? 0 : GUI_TITLEBAR_HEIGHT) +
           ((window && window->menu_count) ? GUI_MENU_HEIGHT : 0);
}

gui_rect_t gui_window_content_rect(const gui_window_t *window) {
    gui_rect_t rect;
    int top;

    if (!window) return (gui_rect_t){0, 0, 0, 0};
    top = gui_window_content_top(window);
    int border = window->borderless ? 0 : GUI_BORDER_SIZE;
    rect = (gui_rect_t){
        window->bounds.x + border,
        window->bounds.y + top,
        window->bounds.w - border * 2,
        window->bounds.h - top - border
    };
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

gui_rect_t gui_window_content_rect_inset(const gui_window_t *window, int inset) {
    gui_rect_t rect = gui_window_content_rect(window);
    if (inset < 0) inset = 0;
    rect.x += inset;
    rect.y += inset;
    rect.w -= inset * 2;
    rect.h -= inset * 2;
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

gui_rect_t gui_window_clamp_local_rect(const gui_window_t *window,
                                       gui_rect_t rect) {
    gui_rect_t content = gui_window_content_rect(window);
    int local_w = content.w;
    int local_h = content.h;

    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    if (rect.w > local_w) rect.w = local_w;
    if (rect.h > local_h) rect.h = local_h;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    if (rect.x + rect.w > local_w) rect.x = local_w - rect.w;
    if (rect.y + rect.h > local_h) rect.y = local_h - rect.h;
    if (rect.x < 0) rect.x = 0;
    if (rect.y < 0) rect.y = 0;
    return rect;
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
    item->enabled = true;
    item->callback = callback;
    item->context = context;
    item->callback_pid = callback_owner((const void *)(uintptr_t)callback);
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

static gui_rect_t context_popup_rect(const gui_window_t *window) {
    const gui_context_menu_t *menu = &window->context_menu;
    return (gui_rect_t){menu->x, menu->y, menu->width,
                        menu->item_count * 22 + 4};
}

static gui_rect_t generic_context_rect(const gui_context_menu_t *menu) {
    if (!menu) return (gui_rect_t){0, 0, 0, 0};
    return (gui_rect_t){menu->x, menu->y, menu->width,
                        menu->item_count * 22 + 4};
}

void gui_context_menu_clear(gui_context_menu_t *menu) {
    if (!menu) return;
    menu->item_count = 0;
    menu->open = false;
    menu->width = 100;
    menu->pressed_item = -1;
}

bool gui_context_menu_add_item(gui_context_menu_t *menu, uint32_t id,
                               const char *label, bool enabled,
                               gui_menu_callback_t callback, void *context) {
    gui_menu_item_t *item;
    int width;
    if (!menu || menu->item_count >= GUI_MAX_CONTEXT_ITEMS) return false;
    item = &menu->items[menu->item_count++];
    item->id = id;
    item->enabled = enabled;
    item->callback = callback;
    item->context = context;
    item->callback_pid = callback_owner((const void *)(uintptr_t)callback);
    copy_text(item->label, sizeof(item->label), label);
    width = (int)gui_font_text_width(item->label) + 28;
    if (width > menu->width) menu->width = width;
    return true;
}

void gui_context_menu_open(gui_context_menu_t *menu, int x, int y,
                           gui_rect_t limits) {
    int height;
    if (!menu || !menu->item_count) return;
    height = menu->item_count * 22 + 4;
    if (x + menu->width > limits.x + limits.w)
        x = limits.x + limits.w - menu->width;
    if (y + height > limits.y + limits.h)
        y = limits.y + limits.h - height;
    if (x < limits.x) x = limits.x;
    if (y < limits.y) y = limits.y;
    menu->x = x;
    menu->y = y;
    menu->pressed_item = -1;
    menu->open = true;
}

void gui_context_menu_close(gui_context_menu_t *menu) {
    if (!menu) return;
    menu->open = false;
    menu->pressed_item = -1;
}

void gui_context_menu_paint(gui_surface_t *surface,
                            const gui_context_menu_t *menu) {
    gui_rect_t popup;
    if (!surface || !menu || !menu->open) return;
    popup = generic_context_rect(menu);
    gui_gfx_fill_rect(surface, popup, 0x00404040);
    gui_gfx_fill_rect(surface, (gui_rect_t){popup.x + 1, popup.y + 1,
                                            popup.w - 2, popup.h - 2},
                      0x00D0D0C8);
    for (int i = 0; i < menu->item_count; i++) {
        gui_rect_t item_rect = {popup.x + 2, popup.y + 2 + i * 22,
                                popup.w - 4, 22};
        const gui_menu_item_t *item = &menu->items[i];
        if (menu->pressed_item == i && item->enabled)
            gui_gfx_fill_rect(surface, item_rect, 0x001060A0);
        gui_font_draw_string_clipped(surface, item_rect.x + 9,
                                     item_rect.y + 7, item->label,
                                     item->enabled ? 0x00101010 : 0x00808080,
                                     item_rect);
    }
}

bool gui_context_menu_handle_event(gui_context_menu_t *menu,
                                   gui_window_t *callback_window,
                                   const gui_event_t *event) {
    gui_rect_t popup;
    if (!menu || !event || !menu->open) return false;
    popup = generic_context_rect(menu);
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (event->button == 1 && gui_rect_contains(popup, event->x, event->y)) {
            int item = (event->y - popup.y - 2) / 22;
            if (item >= 0 && item < menu->item_count && menu->items[item].enabled)
                menu->pressed_item = item;
            return true;
        }
        gui_context_menu_close(menu);
        return false;
    }
    if (event->type == GUI_EVENT_MOUSE_UP && menu->pressed_item >= 0) {
        gui_menu_item_t item = menu->items[menu->pressed_item];
        gui_context_menu_close(menu);
        if (item.enabled && item.callback)
            (void)queue_menu_callback(&item, callback_window);
        return true;
    }
    return true;
}

void gui_window_context_clear(gui_window_t *window) {
    if (!window) return;
    gui_context_menu_clear(&window->context_menu);
}

bool gui_window_context_add_item(gui_window_t *window, uint32_t id,
                                 const char *label, bool enabled,
                                 gui_menu_callback_t callback, void *context) {
    if (!window) return false;
    return gui_context_menu_add_item(&window->context_menu, id, label,
                                     enabled, callback, context);
}

void gui_window_context_open(gui_window_t *window, int x, int y) {
    gui_context_menu_t *menu;
    int height;
    if (!window || !window->context_menu.item_count) return;
    menu = &window->context_menu;
    height = menu->item_count * 22 + 4;
    if (x + menu->width > window->bounds.x + window->bounds.w)
        x = window->bounds.x + window->bounds.w - menu->width;
    if (y + height > window->bounds.y + window->bounds.h)
        y = window->bounds.y + window->bounds.h - height;
    if (x < window->bounds.x) x = window->bounds.x;
    if (y < window->bounds.y) y = window->bounds.y;
    menu->x = x;
    menu->y = y;
    menu->pressed_item = -1;
    menu->open = true;
    window->open_menu = -1;
    window->dirty = true;
}

void gui_window_context_close(gui_window_t *window) {
    if (!window) return;
    gui_context_menu_close(&window->context_menu);
    window->dirty = true;
}

void gui_window_paint_menus(gui_surface_t *surface, gui_window_t *window) {
    gui_rect_t bar;
    if (!surface || !window || !window->visible) return;
    bar = (gui_rect_t){window->bounds.x + 2,
                      window->bounds.y + GUI_TITLEBAR_HEIGHT,
                      window->bounds.w - 4, GUI_MENU_HEIGHT};
    if (window->menu_count) {
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
    }
    if (window->open_menu >= 0) {
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
    if (window->context_menu.open) {
        gui_context_menu_t *menu = &window->context_menu;
        gui_rect_t popup = context_popup_rect(window);
        gui_gfx_fill_rect(surface, popup, 0x00404040);
        gui_gfx_fill_rect(surface, (gui_rect_t){popup.x + 1, popup.y + 1,
                                                popup.w - 2, popup.h - 2},
                          0x00D0D0C8);
        for (int i = 0; i < menu->item_count; i++) {
            gui_rect_t item_rect = {popup.x + 2, popup.y + 2 + i * 22,
                                    popup.w - 4, 22};
            gui_menu_item_t *item = &menu->items[i];
            if (menu->pressed_item == i && item->enabled)
                gui_gfx_fill_rect(surface, item_rect, 0x001060A0);
            gui_font_draw_string_clipped(surface, item_rect.x + 9,
                                         item_rect.y + 7, item->label,
                                         item->enabled ? 0x00101010 : 0x00808080,
                                         item_rect);
        }
    }
}

bool gui_window_handle_menu_event(gui_window_t *window,
                                  const gui_event_t *event) {
    if (!window || !event) return false;
    if (window->context_menu.open) {
        gui_context_menu_t *menu = &window->context_menu;
        gui_rect_t popup = context_popup_rect(window);
        if (event->type == GUI_EVENT_MOUSE_DOWN) {
            if (event->button == 1 &&
                gui_rect_contains(popup, event->x, event->y)) {
                int item = (event->y - popup.y - 2) / 22;
                if (item >= 0 && item < menu->item_count &&
                    menu->items[item].enabled) menu->pressed_item = item;
                return true;
            }
            gui_window_context_close(window);
        }
        if (event->type == GUI_EVENT_MOUSE_UP && menu->pressed_item >= 0) {
            int index = menu->pressed_item;
            gui_menu_item_t item = menu->items[index];
            gui_window_context_close(window);
            if (item.enabled && item.callback)
                (void)queue_menu_callback(&item, window);
            return true;
        }
        if (menu->open) return true;
    }
    if (!window->menu_count) return false;
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
        if (item.callback) (void)queue_menu_callback(&item, window);
        return true;
    }
    return window->open_menu >= 0;
}

void gui_window_destroy(gui_window_t *window) {
    gui_widget_t *widget;
    if (!window) return;
    gui_window_end_content_paint(window);
    bk_about_detach(window);
    widget = window->widgets;
    while (widget) {
        gui_widget_t *next = widget->next;
        gui_widget_destroy(widget);
        widget = next;
    }
    if (window->content_cache) kfree(window->content_cache);
    if (window->content_staging.pixels)
        kfree(window->content_staging.pixels);
    kfree(window);
}

bool gui_window_contains(gui_window_t *window, int x, int y) {
    return window && window->visible && gui_rect_contains(window->bounds, x, y);
}

bool gui_window_titlebar_contains(gui_window_t *window, int x, int y) {
    gui_rect_t titlebar;
    if (!gui_window_contains(window, x, y)) return false;
    titlebar = (gui_rect_t){window->bounds.x, window->bounds.y,
                            window->bounds.w,
                            window->borderless ? window->drag_height
                                               : GUI_TITLEBAR_HEIGHT};
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
    if (window->borderless) return GUI_WINDOW_BUTTON_NONE;
    if (gui_rect_contains(gui_window_close_button_rect(window), x, y))
        return GUI_WINDOW_BUTTON_CLOSE;
    if (gui_rect_contains(gui_window_minimize_button_rect(window), x, y))
        return GUI_WINDOW_BUTTON_MINIMIZE;
    return GUI_WINDOW_BUTTON_NONE;
}

void gui_window_minimize(gui_window_t *window) {
    if (window) {
        window->dirty = true;
        window->visible = false;
        window->listed = true;
        window->minimized = true;
    }
}

void gui_window_close(gui_window_t *window) {
    if (window) {
        window->dirty = true;
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
    if (!window->borderless && min_h < GUI_TITLEBAR_HEIGHT + 30)
        min_h = GUI_TITLEBAR_HEIGHT + 30;
    window->min_w = min_w;
    window->min_h = min_h;
    if (window->bounds.w < window->min_w) window->bounds.w = window->min_w;
    if (window->bounds.h < window->min_h) window->bounds.h = window->min_h;
    window->dirty = true;
}

void gui_window_set_borderless(gui_window_t *window, bool borderless,
                               uint8_t drag_height) {
    if (!window) return;
    window->borderless = borderless;
    window->drag_height = borderless ? drag_height : 0;
    window->resizable = !borderless;
    window->dirty = true;
}

void gui_window_set_content(gui_window_t *window,
                            gui_window_content_paint_t paint,
                            void *context) {
    if (!window) return;
    window->content_paint = paint;
    window->content_context = context;
    window->content_pid = callback_owner((const void *)(uintptr_t)paint);
    window->content_ready = false;
    window->dirty = true;
}

bool gui_window_capture_content(gui_window_t *window,
                                const gui_surface_t *surface) {
    gui_rect_t content;
    gui_rect_t screen;
    gui_rect_t source;
    uint32_t pixels;
    uint32_t *cache;

    if (!window || !surface || !surface->pixels) return false;
    content = gui_window_content_rect(window);
    if (window->content_staging_active &&
        (content.x != window->content_staging_rect.x ||
         content.y != window->content_staging_rect.y ||
         content.w != window->content_staging_rect.w ||
         content.h != window->content_staging_rect.h))
        return false;
    screen = (gui_rect_t){0, 0, surface->width, surface->height};
    if (!gui_rect_intersect(content, screen, &source) ||
        source.w <= 0 || source.h <= 0) return false;

    pixels = (uint32_t)source.w * (uint32_t)source.h;
    if (!window->content_cache ||
        window->content_cache_width != (uint16_t)source.w ||
        window->content_cache_height != (uint16_t)source.h) {
        cache = (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
        if (!cache) return false;
        if (window->content_cache) kfree(window->content_cache);
        window->content_cache = cache;
        window->content_cache_width = (uint16_t)source.w;
        window->content_cache_height = (uint16_t)source.h;
    }

    for (int y = 0; y < source.h; y++) {
        kmemcpy(&window->content_cache[(uint32_t)y * source.w],
                &surface->pixels[(uint32_t)(source.y + y) * surface->pitch +
                                 (uint32_t)source.x],
                (size_t)source.w * sizeof(uint32_t));
    }
    window->content_ready = true;
    return true;
}

bool gui_window_begin_content_paint(gui_window_t *window,
                                    const gui_surface_t *source,
                                    gui_surface_t **staging_out) {
    gui_rect_t content;
    uint32_t pixel_count;
    uint32_t *pixels;

    if (staging_out) *staging_out = NULL;
    if (!window || !source || !source->pixels || !staging_out ||
        window->content_staging_active) return false;

    pixel_count = (uint32_t)source->pitch * source->height;
    if (!window->content_staging.pixels ||
        window->content_staging.width != source->width ||
        window->content_staging.height != source->height ||
        window->content_staging.pitch != source->pitch) {
        pixels = (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t));
        if (!pixels) return false;
        if (window->content_staging.pixels)
            kfree(window->content_staging.pixels);
        window->content_staging.pixels = pixels;
        window->content_staging.width = source->width;
        window->content_staging.height = source->height;
        window->content_staging.pitch = source->pitch;
    }

    content = gui_window_content_rect(window);
    window->content_staging_rect = content;
    gui_gfx_set_clip(&window->content_staging, content);

    /*
     * Sólo se captura el cliente al terminar el callback, por lo que copiar
     * primero los 1,9 MB del escritorio era trabajo inútil. Cada ventana tiene
     * su staging independiente: varias aplicaciones Ring 3 pueden preparar el
     * siguiente cuadro sin serializarse en un único buffer global.
     */
    gui_gfx_fill_rect(&window->content_staging, content, window->bg_color);

    window->content_staging_active = true;
    *staging_out = &window->content_staging;
    return true;
}

void gui_window_end_content_paint(gui_window_t *window) {
    if (!window || !window->content_staging_active) return;
    window->content_staging_active = false;
    window->content_staging_rect = (gui_rect_t){0, 0, 0, 0};
    gui_gfx_reset_clip(&window->content_staging);
}

void gui_window_paint_cached_content(gui_surface_t *surface,
                                     const gui_window_t *window,
                                     gui_rect_t clip) {
    gui_rect_t content;
    gui_rect_t visible;
    gui_rect_t surface_clip;
    int copy_w;
    int copy_h;

    if (!surface || !surface->pixels || !window || !window->content_ready ||
        !window->content_cache) return;
    content = gui_window_content_rect(window);
    copy_w = content.w < window->content_cache_width
           ? content.w : window->content_cache_width;
    copy_h = content.h < window->content_cache_height
           ? content.h : window->content_cache_height;
    content.w = copy_w;
    content.h = copy_h;
    surface_clip = gui_gfx_get_clip(surface);
    if (!gui_rect_intersect(content, clip, &visible) ||
        !gui_rect_intersect(visible, surface_clip, &visible)) return;

    for (int y = visible.y; y < visible.y + visible.h; y++) {
        uint32_t source_y = (uint32_t)(y - content.y);
        uint32_t source_x = (uint32_t)(visible.x - content.x);
        kmemcpy(&surface->pixels[(uint32_t)y * surface->pitch +
                                 (uint32_t)visible.x],
                &window->content_cache[source_y *
                    window->content_cache_width + source_x],
                (size_t)visible.w * sizeof(uint32_t));
    }
}

void gui_window_set_event_handler(gui_window_t *window,
                                  gui_window_event_t handler,
                                  void *context) {
    if (!window) return;
    window->event_handler = handler;
    window->event_context = context;
    window->event_pid = callback_owner((const void *)(uintptr_t)handler);
}

bool gui_window_dispatch_event(gui_window_t *window,
                               const gui_event_t *event) {
    uint32_t arguments[3];
    if (!window || !event || !window->event_handler) return false;
    if (window->event_pid) {
        arguments[0] = (uint32_t)(uintptr_t)window;
        arguments[1] = 0U;
        arguments[2] = (uint32_t)(uintptr_t)window->event_context;
        return task_queue_user_upcall(window->event_pid,
            (uint32_t)(uintptr_t)window->event_handler, arguments, 3,
            event, sizeof(*event), 1);
    }
    return window->event_handler(window, event, window->event_context);
}

void gui_window_paint(gui_surface_t *surface, gui_window_t *window, gui_rect_t clip) {
    gui_rect_t frame;
    gui_rect_t titlebar;
    gui_rect_t body;
    gui_rect_t client;
    uint32_t border_color;
    uint32_t title_bg;
    uint32_t title_text;
    uint32_t button_face;
    uint32_t button_shadow;

    if (!surface || !window || !window->visible) return;
    if (!gui_rect_intersect(window->bounds, clip, &frame)) return;

    if (window->borderless) {
        gui_gfx_fill_rect(surface, window->bounds, window->bg_color);
        return;
    }

    frame = window->bounds;
    titlebar = (gui_rect_t){frame.x, frame.y, frame.w, GUI_TITLEBAR_HEIGHT};
    body = (gui_rect_t){frame.x + 2, frame.y + GUI_TITLEBAR_HEIGHT,
                        frame.w - 4, frame.h - GUI_TITLEBAR_HEIGHT - 2};
    client = gui_window_content_rect(window);
    border_color = window->border_color ? window->border_color : 0x00404040;
    title_bg = window->focused ? window->title_color : 0x00B8B8B8;
    title_text = (((title_bg >> 16) & 0xFF) +
                  ((title_bg >> 8) & 0xFF) +
                  (title_bg & 0xFF)) < 320 ? 0x00FFFFFF : 0x00000000;
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
    gui_gfx_draw_rect(surface, client, 0x00A0A0A0);
    gui_gfx_fill_rect(surface, (gui_rect_t){client.x + 1, client.y + 1,
        client.w - 2, client.h - 2}, window->bg_color);

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
}

void gui_window_paint_widgets(gui_surface_t *surface, gui_window_t *window,
                              gui_rect_t clip) {
    gui_rect_t client;
    gui_rect_t widget_clip;
    gui_widget_t *widget;

    if (!surface || !window || !window->visible) return;
    client = gui_window_content_rect(window);
    if (!gui_rect_intersect(clip, client, &widget_clip)) return;

    widget = window->widgets;
    while (widget) {
        if (widget->style != GUI_WIDGET_STYLE_DROPDOWN)
            gui_widget_paint(surface, window, widget, widget_clip);
        widget = widget->next;
    }

    widget = window->widgets;
    while (widget) {
        if (widget->style == GUI_WIDGET_STYLE_DROPDOWN)
            gui_widget_paint(surface, window, widget, widget_clip);
        widget = widget->next;
    }
}
