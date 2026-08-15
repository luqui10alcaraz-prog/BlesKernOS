#include "gui.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/about_dialog.h"
#include "../kernel/include/task.h"
#include "../kernel/include/klock.h"


#define GUI_CONTENT_STAGING_POOL_SIZE 4

/* Paleta inspirada en CDE: marcos gruesos, tonos piedra y acento turquesa.
 * Se mantiene 100 % en raster CPU y no agrega transparencias ni gradientes,
 * para conservar el aspecto de workstation de los 90 y no encarecer el paint. */
#define GUI_CDE_FACE            0x00C4B3ABU
#define GUI_CDE_FACE_GRAIN      0x00B6A49DU
#define GUI_CDE_ACTIVE_TITLE    0x004B93A8U
#define GUI_CDE_ACTIVE_GRAIN    0x0059A0B2U
#define GUI_CDE_INACTIVE_TITLE  0x00A9A0A0U
#define GUI_CDE_INACTIVE_GRAIN  0x00B4AAA7U
#define GUI_CDE_MENU            0x004B93A8U
#define GUI_CDE_MENU_PRESSED    0x003C7484U
#define GUI_CDE_HIGHLIGHT       0x00F1E5DFU
#define GUI_CDE_MIDLIGHT        0x00D8C7BFU
#define GUI_CDE_SHADOW          0x007F746EU
#define GUI_CDE_DARK            0x004D4C4FU
#define GUI_CDE_TEXT            0x001A1716U

/* Existing applications chose their outer dimensions for the former 2 px
 * frame and 20 px title bar. Inflate new decorated windows just enough to
 * preserve the client area instead of squeezing fixed-layout applets. */
#define GUI_LEGACY_BORDER_SIZE      2
#define GUI_LEGACY_TITLEBAR_HEIGHT 20
#define GUI_CHROME_COMPAT_W     ((GUI_BORDER_SIZE - GUI_LEGACY_BORDER_SIZE) * 2)
#define GUI_CHROME_COMPAT_H \
    ((GUI_TITLEBAR_HEIGHT - GUI_LEGACY_TITLEBAR_HEIGHT) + \
     (GUI_BORDER_SIZE - GUI_LEGACY_BORDER_SIZE))

typedef struct {
    gui_surface_t surface;
    bool in_use;
} gui_content_staging_slot_t;

static gui_content_staging_slot_t
    g_content_staging_pool[GUI_CONTENT_STAGING_POOL_SIZE];
static kspinlock_t g_content_staging_lock = KSPINLOCK_INITIALIZER;

/* Motif/CDE construia profundidad con dos pares de luces y sombras, no con
 * bordes planos.  Estas primitivas se usan sólo sobre marcos y controles
 * pequeños: agregan detalle sin volver a pintar el cliente ni encarecer el
 * movimiento de ventanas. */
static void cde_draw_bevel(gui_surface_t *surface, gui_rect_t bounds,
                           bool sunken, uint32_t fill, int depth) {
    uint32_t light_outer = sunken ? GUI_CDE_DARK : GUI_CDE_HIGHLIGHT;
    uint32_t light_inner = sunken ? GUI_CDE_SHADOW : GUI_CDE_MIDLIGHT;
    uint32_t dark_outer = sunken ? GUI_CDE_HIGHLIGHT : GUI_CDE_DARK;
    uint32_t dark_inner = sunken ? GUI_CDE_MIDLIGHT : GUI_CDE_SHADOW;

    if (!surface || bounds.w <= 0 || bounds.h <= 0) return;
    gui_gfx_fill_rect(surface, bounds, fill);
    if (depth < 1 || bounds.w < 3 || bounds.h < 3) return;

    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x, bounds.y, bounds.w, 1}, light_outer);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x, bounds.y, 1, bounds.h}, light_outer);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x, bounds.y + bounds.h - 1, bounds.w, 1}, dark_outer);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){bounds.x + bounds.w - 1, bounds.y, 1, bounds.h}, dark_outer);

    if (depth > 1 && bounds.w >= 5 && bounds.h >= 5) {
        gui_gfx_fill_rect(surface,
            (gui_rect_t){bounds.x + 1, bounds.y + 1, bounds.w - 2, 1},
            light_inner);
        gui_gfx_fill_rect(surface,
            (gui_rect_t){bounds.x + 1, bounds.y + 1, 1, bounds.h - 2},
            light_inner);
        gui_gfx_fill_rect(surface,
            (gui_rect_t){bounds.x + 1, bounds.y + bounds.h - 2,
                         bounds.w - 2, 1}, dark_inner);
        gui_gfx_fill_rect(surface,
            (gui_rect_t){bounds.x + bounds.w - 2, bounds.y + 1,
                         1, bounds.h - 2}, dark_inner);
    }
}

static void cde_stipple_rect(gui_surface_t *surface, gui_rect_t bounds,
                             uint32_t color) {
    int y;
    if (!surface || bounds.w < 3 || bounds.h < 3) return;
    for (y = bounds.y + 1; y < bounds.y + bounds.h - 1; y += 3) {
        int x = bounds.x + 1 + ((y / 3) & 1);
        for (; x < bounds.x + bounds.w - 1; x += 4)
            gui_gfx_putpixel(surface, x, y, color);
    }
}

static void content_staging_release_slot(int slot) {
    uint32_t flags;
    if (slot < 0 || slot >= GUI_CONTENT_STAGING_POOL_SIZE) return;
    flags = kspin_lock_irqsave(&g_content_staging_lock);
    g_content_staging_pool[slot].in_use = false;
    kspin_unlock_irqrestore(&g_content_staging_lock, flags);
}

static int content_staging_acquire(const gui_surface_t *source,
                                   gui_surface_t *view_out) {
    gui_content_staging_slot_t *pool_slot;
    uint32_t pixel_count;
    uint32_t *pixels;
    uint32_t flags;
    int slot = -1;

    if (!source || !source->pixels || !view_out) return -1;
    flags = kspin_lock_irqsave(&g_content_staging_lock);
    for (int i = 0; i < GUI_CONTENT_STAGING_POOL_SIZE; i++) {
        if (g_content_staging_pool[i].in_use) continue;
        g_content_staging_pool[i].in_use = true;
        slot = i;
        break;
    }
    kspin_unlock_irqrestore(&g_content_staging_lock, flags);
    if (slot < 0) return -1;

    pool_slot = &g_content_staging_pool[slot];
    pixel_count = (uint32_t)source->pitch * source->height;
    if (!pool_slot->surface.pixels ||
        pool_slot->surface.width != source->width ||
        pool_slot->surface.height != source->height ||
        pool_slot->surface.pitch != source->pitch) {
        pixels = (uint32_t *)kmalloc(pixel_count * sizeof(uint32_t));
        if (!pixels || !mm_set_allocation_owner(pixels, 0U)) {
            if (pixels) kfree(pixels);
            content_staging_release_slot(slot);
            return -1;
        }
        if (pool_slot->surface.pixels) kfree(pool_slot->surface.pixels);
        pool_slot->surface.pixels = pixels;
        pool_slot->surface.width = source->width;
        pool_slot->surface.height = source->height;
        pool_slot->surface.pitch = source->pitch;
    }
    pool_slot->surface.clip = (gui_rect_t){0, 0,
        source->width, source->height};
    *view_out = pool_slot->surface;
    return slot;
}

static uint32_t callback_owner(const void *callback) {
    uint32_t pid;

    if (!callback || (uint32_t)(uintptr_t)callback < HEAP_START ||
        !task_current_is_user()) return 0U;
    pid = task_current_pid();
    /*
     * Transitional SMP rule: native GUI callbacks still share mutable window,
     * widget and staging objects with the CPU0 compositor.  Keep only the
     * callback-owning thread on CPU0; worker threads from the same process
     * remain free to use APs for rendering, physics, networking, etc.
     */
    (void)task_set_affinity_mask(pid, 1U);
    return pid;
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
    return task_queue_window_upcall(window, item->callback_pid,
        (uint32_t)(uintptr_t)item->callback, arguments, 3, NULL, 0, -1);
}

static void copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

gui_window_t *gui_window_create(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title) {
    gui_window_t *window;
    int available_w;
    int available_h;
    if (!desktop) return NULL;
    available_w = (int)desktop->surface.width - (x > 0 ? x : 0);
    available_h = (int)desktop->surface.height - 24 - (y > 0 ? y : 0);
    if (w > 0 && w + GUI_CHROME_COMPAT_W <= available_w)
        w += GUI_CHROME_COMPAT_W;
    if (h > 0 && h + GUI_CHROME_COMPAT_H <= available_h)
        h += GUI_CHROME_COMPAT_H;
    window = (gui_window_t *)kzalloc(sizeof(gui_window_t));
    if (!window) return NULL;
    if (!mm_set_allocation_owner(window, 0U)) {
        kfree(window);
        return NULL;
    }

    window->id = desktop->next_window_id++;
    window->bounds = (gui_rect_t){x, y, w, h};
    window->min_w = 160;
    window->min_h = 90;
    window->bg_color = GUI_CDE_FACE;
    window->title_color = GUI_CDE_ACTIVE_TITLE;
    window->border_color = GUI_CDE_DARK;
    window->visible = true;
    window->listed = true;
    window->minimized = false;
    window->input_enabled = true;
    window->borderless = false;
    window->resizable = true;
    window->drag_height = 0;
    window->dirty = true;
    window->open_menu = -1;
    window->pressed_menu_item = -1;
    window->context_menu.pressed_item = -1;
    window->content_staging_slot = -1;
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
    int x = window->bounds.x + GUI_BORDER_SIZE;
    for (int i = 0; i < index; i++)
        x += (int)gui_font_text_width(window->menus[i].label) + 12;
    return (gui_rect_t){x, window->bounds.y + GUI_TITLEBAR_HEIGHT,
                        (int)gui_font_text_width(window->menus[index].label) + 12,
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
                        menu->item_count * 18 + 4};
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
    int i;
    if (!surface || !menu || !menu->open) return;
    popup = generic_context_rect(menu);
    cde_draw_bevel(surface, popup, false, GUI_CDE_FACE, 2);
    for (i = 0; i < menu->item_count; i++) {
        gui_rect_t item_rect = {popup.x + 3, popup.y + 3 + i * 22,
                                popup.w - 6, 21};
        const gui_menu_item_t *item = &menu->items[i];
        if (menu->pressed_item == i && item->enabled)
            cde_draw_bevel(surface, item_rect, true,
                           GUI_CDE_MENU_PRESSED, 1);
        gui_font_draw_string_clipped(surface, item_rect.x + 8,
                                     item_rect.y + 6, item->label,
                                     item->enabled
                                         ? (menu->pressed_item == i
                                                ? 0x00FFFFFFU
                                                : GUI_CDE_TEXT)
                                         : 0x00776F6BU,
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

void gui_window_set_text_context(gui_window_t *window, gui_rect_t bounds,
                                 bool has_selection, bool editable,
                                 gui_menu_callback_t callback, void *context) {
    if (!window) return;
    window->text_context.bounds = gui_window_clamp_local_rect(window, bounds);
    window->text_context.has_selection = has_selection;
    window->text_context.editable = editable;
    window->text_context.callback = callback;
    window->text_context.context = context;
    window->text_context.callback_pid =
        callback_owner((const void *)(uintptr_t)callback);
}

void gui_window_clear_text_context(gui_window_t *window) {
    if (!window) return;
    window->text_context.bounds = (gui_rect_t){0, 0, 0, 0};
    window->text_context.has_selection = false;
    window->text_context.editable = false;
    window->text_context.callback = NULL;
    window->text_context.context = NULL;
    window->text_context.callback_pid = 0U;
}

static bool gui_window_open_custom_text_context(gui_window_t *window,
                                                int x, int y) {
    gui_text_context_t *text;
    gui_rect_t content;
    gui_rect_t screen_bounds;
    gui_context_menu_t *menu;

    if (!window) return false;
    text = &window->text_context;
    if (!text->has_selection || !text->callback ||
        text->bounds.w <= 0 || text->bounds.h <= 0) return false;
    content = gui_window_content_rect(window);
    screen_bounds = (gui_rect_t){content.x + text->bounds.x,
                                 content.y + text->bounds.y,
                                 text->bounds.w, text->bounds.h};
    if (!gui_rect_contains(screen_bounds, x, y)) return false;

    gui_window_context_clear(window);
    menu = &window->context_menu;
    if (gui_context_menu_add_item(menu, GUI_TEXT_ACTION_COPY, "Copiar", true,
                                  text->callback, text->context))
        menu->items[menu->item_count - 1U].callback_pid = text->callback_pid;
    if (text->editable &&
        gui_context_menu_add_item(menu, GUI_TEXT_ACTION_PASTE, "Pegar", true,
                                  text->callback, text->context))
        menu->items[menu->item_count - 1U].callback_pid = text->callback_pid;
    if (!menu->item_count) return false;
    gui_window_context_open(window, x, y);
    return true;
}

bool gui_window_open_text_context_at(gui_window_t *window, int x, int y) {
    if (!window) return false;
    if (gui_window_open_custom_text_context(window, x, y)) return true;
    return gui_widget_open_text_context_at(window, x, y);
}

void gui_window_paint_menus(gui_surface_t *surface, gui_window_t *window) {
    gui_rect_t bar;
    uint32_t bar_bg;
    uint32_t bar_grain;
    uint32_t bar_text;
    int i;
    if (!surface || !window || !window->visible) return;
    bar = (gui_rect_t){window->bounds.x + GUI_BORDER_SIZE,
                      window->bounds.y + GUI_TITLEBAR_HEIGHT,
                      window->bounds.w - GUI_BORDER_SIZE * 2,
                      GUI_MENU_HEIGHT};
    bar_bg = window->focused ? GUI_CDE_MENU : GUI_CDE_INACTIVE_TITLE;
    bar_grain = window->focused ? GUI_CDE_ACTIVE_GRAIN
                                : GUI_CDE_INACTIVE_GRAIN;
    bar_text = (((bar_bg >> 16) & 0xFFU) +
                ((bar_bg >> 8) & 0xFFU) +
                (bar_bg & 0xFFU)) < 400U ? 0x00FFFFFFU : GUI_CDE_TEXT;
    if (window->menu_count) {
        cde_draw_bevel(surface, bar, false, bar_bg, 1);
        cde_stipple_rect(surface, bar, bar_grain);
        for (i = 0; i < window->menu_count; i++) {
            gui_rect_t title = menu_title_rect(window, i);
            if (window->open_menu == i)
                cde_draw_bevel(surface, title, true,
                               GUI_CDE_MENU_PRESSED, 1);
            gui_font_draw_string_clipped(surface, title.x + 6, title.y + 6,
                                         window->menus[i].label,
                                         window->open_menu == i
                                             ? 0x00FFFFFFU : bar_text,
                                         title);
        }
    }
    if (window->open_menu >= 0) {
        int index = window->open_menu;
        gui_menu_t *menu = &window->menus[index];
        gui_rect_t popup = menu_popup_rect(window, index);
        cde_draw_bevel(surface, popup, false, GUI_CDE_FACE, 2);
        for (i = 0; i < menu->item_count; i++) {
            gui_rect_t item = {popup.x + 3, popup.y + 3 + i * 18,
                               popup.w - 6, 17};
            if (window->pressed_menu_item == i)
                cde_draw_bevel(surface, item, true,
                               GUI_CDE_MENU_PRESSED, 1);
            gui_font_draw_string_clipped(surface, item.x + 6, item.y + 4,
                                         menu->items[i].label,
                                         window->pressed_menu_item == i
                                             ? 0x00FFFFFFU : GUI_CDE_TEXT,
                                         item);
        }
    }
    if (window->context_menu.open) {
        gui_context_menu_t *menu = &window->context_menu;
        gui_rect_t popup = context_popup_rect(window);
        cde_draw_bevel(surface, popup, false, GUI_CDE_FACE, 2);
        for (i = 0; i < menu->item_count; i++) {
            gui_rect_t item_rect = {popup.x + 3, popup.y + 3 + i * 22,
                                    popup.w - 6, 21};
            gui_menu_item_t *item = &menu->items[i];
            if (menu->pressed_item == i && item->enabled)
                cde_draw_bevel(surface, item_rect, true,
                               GUI_CDE_MENU_PRESSED, 1);
            gui_font_draw_string_clipped(surface, item_rect.x + 8,
                                         item_rect.y + 6, item->label,
                                         item->enabled
                                             ? (menu->pressed_item == i
                                                    ? 0x00FFFFFFU
                                                    : GUI_CDE_TEXT)
                                             : 0x00776F6BU,
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
                int item = (event->y - popup.y - 2) / 18;
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
    uint32_t state;
    if (!window) return;

    /* Several callbacks for the same Ring-3 window may finish on different
       CPUs. The old boolean allowed both CPUs to observe "ready to free" and
       destroy the same widget chain twice. State 1 is a tombstone; exactly one
       CPU may atomically promote it to state 2 and own finalization. */
    state = __sync_val_compare_and_swap(&window->destroy_state, 0U, 1U);
    if (state >= 2U) return;
    window->visible = false;
    window->listed = false;
    window->input_enabled = false;

    /* Remove queued callbacks first. An active callback keeps state 1 and
       invokes this function again after its return gate has restored the user
       context. New callbacks are rejected while state is non-zero. */
    if (!task_cancel_window_upcalls(window)) return;
    if (!__sync_bool_compare_and_swap(&window->destroy_state, 1U, 2U)) return;

    gui_window_end_content_paint(window);
    bk_about_detach(window);
    widget = window->widgets;
    window->widgets = NULL;
    window->focused_widget = NULL;
    while (widget) {
        gui_widget_t *next = widget->next;
        gui_widget_destroy(widget);
        widget = next;
    }
    if (window->content_cache) {
        kfree(window->content_cache);
        window->content_cache = NULL;
    }
    /* content_staging borrows storage from the global pool and is released
       by gui_window_end_content_paint(). */
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
    return (gui_rect_t){window->bounds.x + window->bounds.w -
                            GUI_BORDER_SIZE - 34,
                        window->bounds.y + 4, 16, 16};
}

gui_rect_t gui_window_close_button_rect(gui_window_t *window) {
    if (!window) return (gui_rect_t){0, 0, 0, 0};
    return (gui_rect_t){window->bounds.x + window->bounds.w -
                            GUI_BORDER_SIZE - 16,
                        window->bounds.y + 4, 16, 16};
}

gui_window_button_t gui_window_titlebar_button_at(gui_window_t *window,
                                                   int x, int y) {
    if (!window || !window->visible) return GUI_WINDOW_BUTTON_NONE;
    if (window->borderless || gui_setup_mode()) return GUI_WINDOW_BUTTON_NONE;
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
    if (!window->borderless) {
        min_w += GUI_CHROME_COMPAT_W;
        min_h += GUI_CHROME_COMPAT_H;
    }
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
    if (borderless != window->borderless) {
        /* Undo/apply the compatibility inflation when a program deliberately
           opts out of normal CDE decorations after creating the window. */
        if (borderless) {
            if (window->bounds.w > GUI_CHROME_COMPAT_W)
                window->bounds.w -= GUI_CHROME_COMPAT_W;
            if (window->bounds.h > GUI_CHROME_COMPAT_H)
                window->bounds.h -= GUI_CHROME_COMPAT_H;
        } else {
            window->bounds.w += GUI_CHROME_COMPAT_W;
            window->bounds.h += GUI_CHROME_COMPAT_H;
        }
    }
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
        if (!mm_set_allocation_owner(cache, 0U)) {
            kfree(cache);
            return false;
        }
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
    int slot;

    if (staging_out) *staging_out = NULL;
    if (!window || !source || !source->pixels || !staging_out ||
        window->content_staging_active) return false;

    /* A full-screen staging buffer per window consumed roughly 3 MiB at
     * 1024x768 and remained allocated for the entire lifetime of the window.
     * A burst of native applications therefore exhausted the heap and left
     * only kernel-painted frames visible.  Borrow one of a small number of
     * reusable surfaces instead; windows that miss a slot remain dirty and
     * retry on the next compositor pass. */
    slot = content_staging_acquire(source, &window->content_staging);
    if (slot < 0) return false;
    window->content_staging_slot = (int8_t)slot;

    content = gui_window_content_rect(window);
    window->content_staging_rect = content;
    gui_gfx_set_clip(&window->content_staging, content);
    gui_gfx_fill_rect(&window->content_staging, content, window->bg_color);

    window->content_staging_active = true;
    *staging_out = &window->content_staging;
    return true;
}

void gui_window_end_content_paint(gui_window_t *window) {
    int slot;
    if (!window || !window->content_staging_active) return;
    slot = window->content_staging_slot;
    window->content_staging_active = false;
    window->content_staging_slot = -1;
    window->content_staging_rect = (gui_rect_t){0, 0, 0, 0};
    kmemset(&window->content_staging, 0, sizeof(window->content_staging));
    content_staging_release_slot(slot);
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
        return task_queue_window_upcall(window, window->event_pid,
            (uint32_t)(uintptr_t)window->event_handler, arguments, 3,
            event, sizeof(*event), 1);
    }
    return window->event_handler(window, event, window->event_context);
}

void gui_window_paint(gui_surface_t *surface, gui_window_t *window, gui_rect_t clip) {
    gui_rect_t frame;
    gui_rect_t titlebar;
    gui_rect_t body;
    gui_rect_t left_button;
    gui_rect_t min_button;
    gui_rect_t close_button;
    uint32_t title_bg;
    uint32_t title_grain;
    uint32_t title_text;
    int title_left;
    int title_right;
    int title_w;
    int title_x;

    if (!surface || !window || !window->visible) return;
    if (!gui_rect_intersect(window->bounds, clip, &frame)) return;

    if (window->borderless) {
        gui_gfx_fill_rect(surface, window->bounds, window->bg_color);
        return;
    }

    frame = window->bounds;
    titlebar = (gui_rect_t){frame.x + GUI_BORDER_SIZE - 1,
                            frame.y + 4,
                            frame.w - (GUI_BORDER_SIZE - 1) * 2,
                            GUI_TITLEBAR_HEIGHT - 7};
    body = (gui_rect_t){frame.x + GUI_BORDER_SIZE,
                        frame.y + GUI_TITLEBAR_HEIGHT,
                        frame.w - GUI_BORDER_SIZE * 2,
                        frame.h - GUI_TITLEBAR_HEIGHT - GUI_BORDER_SIZE};
    title_bg = window->focused ? window->title_color : GUI_CDE_INACTIVE_TITLE;
    title_grain = window->focused ? GUI_CDE_ACTIVE_GRAIN
                                  : GUI_CDE_INACTIVE_GRAIN;
    title_text = (((title_bg >> 16) & 0xFFU) +
                  ((title_bg >> 8) & 0xFFU) +
                  (title_bg & 0xFFU)) < 400U ? 0x00FFFFFFU : GUI_CDE_TEXT;

    /* Marco ancho de workstation: dos relieves exteriores, canal interior y
     * grano discreto.  El centro no se rasteriza píxel a píxel. */
    cde_draw_bevel(surface, frame, false, GUI_CDE_FACE, 2);
    if (frame.w > 8 && frame.h > 8) {
        gui_gfx_draw_rect(surface,
            (gui_rect_t){frame.x + 3, frame.y + 3,
                         frame.w - 6, frame.h - 6}, GUI_CDE_SHADOW);
        gui_gfx_draw_rect(surface,
            (gui_rect_t){frame.x + 4, frame.y + 4,
                         frame.w - 8, frame.h - 8}, GUI_CDE_HIGHLIGHT);
    }
    cde_stipple_rect(surface,
        (gui_rect_t){frame.x + 3, frame.y + 3, frame.w - 6, 3},
        GUI_CDE_FACE_GRAIN);
    cde_stipple_rect(surface,
        (gui_rect_t){frame.x + 3, frame.y + frame.h - 6, frame.w - 6, 3},
        GUI_CDE_FACE_GRAIN);
    cde_stipple_rect(surface,
        (gui_rect_t){frame.x + 3, frame.y + 6, 3, frame.h - 12},
        GUI_CDE_FACE_GRAIN);
    cde_stipple_rect(surface,
        (gui_rect_t){frame.x + frame.w - 6, frame.y + 6,
                     3, frame.h - 12}, GUI_CDE_FACE_GRAIN);

    cde_draw_bevel(surface, titlebar, true, title_bg, 1);
    cde_stipple_rect(surface, titlebar, title_grain);
    gui_gfx_fill_rect(surface, body, window->bg_color);

    left_button = (gui_rect_t){frame.x + GUI_BORDER_SIZE,
                               frame.y + 4, 16, 16};
    cde_draw_bevel(surface, left_button, false, GUI_CDE_FACE, 2);

    min_button = gui_window_minimize_button_rect(window);
    close_button = gui_window_close_button_rect(window);
    if (!gui_setup_mode()) {
        cde_draw_bevel(surface, min_button, false, GUI_CDE_FACE, 2);
        cde_draw_bevel(surface, close_button, false, GUI_CDE_FACE, 2);
        gui_gfx_fill_rect(surface,
            (gui_rect_t){min_button.x + 4, min_button.y + 10, 8, 2},
            GUI_CDE_DARK);
        gui_gfx_draw_line(surface, close_button.x + 4, close_button.y + 4,
                          close_button.x + 11, close_button.y + 11,
                          GUI_CDE_DARK);
        gui_gfx_draw_line(surface, close_button.x + 11, close_button.y + 4,
                          close_button.x + 4, close_button.y + 11,
                          GUI_CDE_DARK);
    }

    /* El botón izquierdo de Motif aloja el icono de la aplicación y mantiene
     * la silueta CDE incluso cuando el programa no suministra uno. */
    if (window->icon_pixels && window->icon_width && window->icon_height) {
        int iy;
        for (iy = 0; iy < 12; iy++) {
            int ix;
            for (ix = 0; ix < 12; ix++) {
                uint32_t pixel = window->icon_pixels[
                    (iy * window->icon_height / 12) * window->icon_width +
                    (ix * window->icon_width / 12)];
                if (pixel >> 24)
                    gui_gfx_putpixel(surface, left_button.x + 2 + ix,
                                     left_button.y + 2 + iy,
                                     pixel & 0x00FFFFFFU);
            }
        }
    } else {
        gui_gfx_fill_rect(surface,
            (gui_rect_t){left_button.x + 4, left_button.y + 5, 8, 2},
            GUI_CDE_DARK);
        gui_gfx_fill_rect(surface,
            (gui_rect_t){left_button.x + 4, left_button.y + 9, 8, 2},
            GUI_CDE_SHADOW);
    }

    title_left = left_button.x + left_button.w + 5;
    title_right = gui_setup_mode() ? frame.x + frame.w - GUI_BORDER_SIZE - 4
                                   : min_button.x - 5;
    if (title_right < title_left) title_right = title_left;
    title_w = (int)gui_font_text_width(window->title);
    title_x = frame.x + (frame.w - title_w) / 2;
    if (title_x < title_left) title_x = title_left;
    if (title_x + title_w > title_right) title_x = title_right - title_w;
    if (title_x < title_left) title_x = title_left;
    gui_font_draw_string_clipped(surface, title_x, frame.y + 8,
                                 window->title, title_text,
                                 (gui_rect_t){title_left, frame.y + 5,
                                              title_right - title_left,
                                              GUI_TITLEBAR_HEIGHT - 8});
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
