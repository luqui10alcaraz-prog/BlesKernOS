#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/rtc.h"
#include "../kernel/include/task.h"
#include "../kernel/include/pic.h"

#define DESKBAR_HEIGHT         24
#define DESKBAR_BUTTON_W       68
#define DESKBAR_BUTTON_H       18
#define DESKBAR_MENU_W         240
#define DESKBAR_SUBMENU_W      200
#define DESKBAR_ITEM_H         32
#define DESKBAR_MAIN_ITEMS     5
#define DESKBAR_MAX_SUB_ITEMS  12
#define DESKBAR_CALENDAR_W      405
#define DESKBAR_CALENDAR_H      285

extern void calendar_open_at(gui_desktop_t *desktop, int x, int y);

typedef struct {
    char label[24];
    char path[VFS_MAX_PATH];
    bool enabled;
} deskbar_sub_item_t;

typedef struct {
    bool menu_open;
    bool button_hovered;
    bool button_pressed;
    bool clock_hovered;
    bool clock_pressed;
    uint32_t hovered_window_id;
    uint32_t pressed_window_id;
    int hovered_main_item;
    int pressed_main_item;
    int open_section;
    int hovered_sub_item;
    int pressed_sub_item;
    int cached_section;
    int cached_count;
    deskbar_sub_item_t cached_items[DESKBAR_MAX_SUB_ITEMS];
} deskbar_state_t;

static const char *g_main_items[DESKBAR_MAIN_ITEMS] = {
    "Sobre BlesKernOS",
    "Programs",
    "Documents",
    "Misc",
    "Apagar"
};

static void deskbar_copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void deskbar_reset_menu_state(deskbar_state_t *state) {
    if (!state) return;
    state->hovered_main_item = -1;
    state->pressed_main_item = -1;
    state->open_section = 0;
    state->hovered_sub_item = -1;
    state->pressed_sub_item = -1;
    state->cached_section = -1;
    state->cached_count = 0;
}

static void deskbar_reset_state(deskbar_state_t *state) {
    if (!state) return;
    state->menu_open = false;
    state->button_hovered = false;
    state->button_pressed = false;
    state->clock_hovered = false;
    state->clock_pressed = false;
    state->hovered_window_id = 0;
    state->pressed_window_id = 0;
    deskbar_reset_menu_state(state);
}

static gui_rect_t deskbar_bar_rect(const gui_desktop_t *desktop) {
    return (gui_rect_t){0, desktop->surface.height - DESKBAR_HEIGHT, desktop->surface.width, DESKBAR_HEIGHT};
}

static gui_rect_t deskbar_button_rect(const gui_desktop_t *desktop) {
    return (gui_rect_t){5, desktop->surface.height - 21, DESKBAR_BUTTON_W, DESKBAR_BUTTON_H};
}

static gui_rect_t deskbar_status_rect(const gui_desktop_t *desktop) {
    return (gui_rect_t){desktop->surface.width - 94, desktop->surface.height - 21, 88, 18};
}

static void deskbar_open_clock_calendar(gui_desktop_t *desktop) {
    int x;
    int y;

    if (!desktop) return;

    x = (int)desktop->surface.width - DESKBAR_CALENDAR_W - 6;
    y = (int)desktop->surface.height - DESKBAR_HEIGHT - DESKBAR_CALENDAR_H - 4;
    if (x < 4) x = 4;
    if (y < 4) y = 4;

    calendar_open_at(desktop, x, y);
}

static gui_rect_t deskbar_window_area_rect(const gui_desktop_t *desktop) {
    gui_rect_t button = deskbar_button_rect(desktop);
    gui_rect_t status = deskbar_status_rect(desktop);
    int width = status.x - (button.x + button.w + 8) - 6;
    if (width < 0) width = 0;
    return (gui_rect_t){button.x + button.w + 8, desktop->surface.height - 21, width, 18};
}

static gui_rect_t deskbar_menu_rect(const gui_desktop_t *desktop) {
    gui_rect_t bar = deskbar_bar_rect(desktop);
    int menu_h = DESKBAR_MAIN_ITEMS * DESKBAR_ITEM_H + 6;
    return (gui_rect_t){0, bar.y - menu_h + 1, DESKBAR_MENU_W, menu_h};
}

static gui_rect_t deskbar_main_item_rect(const gui_desktop_t *desktop, int index) {
    gui_rect_t menu = deskbar_menu_rect(desktop);
    return (gui_rect_t){menu.x + 3, menu.y + 3 + (index * DESKBAR_ITEM_H), menu.w - 6, DESKBAR_ITEM_H};
}

static gui_rect_t deskbar_submenu_rect(const gui_desktop_t *desktop, int section, int item_count) {
    gui_rect_t item = deskbar_main_item_rect(desktop, section);
    int height = item_count * DESKBAR_ITEM_H + 6;
    int y = item.y;
    if (y + height > desktop->surface.height - DESKBAR_HEIGHT + 1) {
        y = (desktop->surface.height - DESKBAR_HEIGHT + 1) - height;
    }
    if (y < 0) y = 0;
    return (gui_rect_t){item.x + item.w - 1, y, DESKBAR_SUBMENU_W, height};
}

static gui_rect_t deskbar_sub_item_rect(const gui_desktop_t *desktop, int section, int item_index, int item_count) {
    gui_rect_t menu = deskbar_submenu_rect(desktop, section, item_count);
    return (gui_rect_t){menu.x + 3, menu.y + 3 + (item_index * DESKBAR_ITEM_H), menu.w - 6, DESKBAR_ITEM_H};
}

static void deskbar_draw_panel(gui_surface_t *surface, gui_rect_t rect, uint32_t face, bool sunken) {
    if (!surface || rect.w <= 0 || rect.h <= 0) return;

    gui_gfx_fill_rect(surface, rect, face);
    if (rect.w < 2 || rect.h < 2) return;

    if (sunken) {
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, rect.w, 1}, 0x00484840);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, 1, rect.h}, 0x00484840);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1}, 0x00807C74);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2}, 0x00807C74);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 1, rect.y, 1, rect.h}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y + rect.h - 1, rect.w, 1}, 0x00FFFFFF);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 2, rect.y + 1, 1, rect.h - 2}, 0x00D8D8D0);
        gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + rect.h - 2, rect.w - 2, 1}, 0x00D8D8D0);
        return;
    }

    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, rect.w, 1}, 0x00FFFFFF);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, 1, rect.h}, 0x00FFFFFF);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1}, 0x00E4E4DC);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2}, 0x00E4E4DC);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 1, rect.y, 1, rect.h}, 0x00484840);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y + rect.h - 1, rect.w, 1}, 0x00484840);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 2, rect.y + 1, 1, rect.h - 2}, 0x00807C74);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + rect.h - 2, rect.w - 2, 1}, 0x00807C74);
}

static void deskbar_draw_arrow(gui_surface_t *surface, int x, int y, uint32_t color) {
    gui_gfx_fill_rect(surface, (gui_rect_t){x, y, 1, 7}, color);
    gui_gfx_fill_rect(surface, (gui_rect_t){x + 1, y + 1, 1, 5}, color);
    gui_gfx_fill_rect(surface, (gui_rect_t){x + 2, y + 2, 1, 3}, color);
}

static void deskbar_draw_logo(gui_surface_t *surface, int x, int y) {
    gui_gfx_fill_rect(surface, (gui_rect_t){x, y + 4, 5, 5}, 0x0075BF4E);
    gui_gfx_fill_rect(surface, (gui_rect_t){x + 5, y + 1, 5, 5}, 0x00E39C4F);
    gui_gfx_fill_rect(surface, (gui_rect_t){x + 10, y + 5, 4, 4}, 0x00E7D06E);
    gui_gfx_putpixel(surface, x + 6, y + 8, 0x004C5B48);
}

static void deskbar_focus_window(gui_desktop_t *desktop, gui_window_t *window) {
    gui_window_restore(window);
    gui_desktop_raise_window(desktop, window);
    gui_desktop_focus_window(desktop, window);
}

static inline void deskbar_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void deskbar_power_off(void) {
    cli();
    deskbar_outw(0x604, 0x2000);
    deskbar_outw(0xB004, 0x2000);
    deskbar_outw(0x4004, 0x3400);
    for (;;) __asm__ volatile ("hlt");
}

static bool deskbar_main_item_has_submenu(int index) {
    return index > 0 && index < DESKBAR_MAIN_ITEMS - 1;
}

static void deskbar_make_pretty_name(const char *src, char *dst, size_t dst_len) {
    size_t pos = 0;
    bool upper_next = true;

    if (!dst || !dst_len) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src && *src != '.' && pos + 1 < dst_len) {
        char c = *src++;
        if (c == '_') c = ' ';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (upper_next && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
            upper_next = false;
        } else if (c == ' ') {
            upper_next = true;
        }
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static void deskbar_join_path(char *dst, size_t dst_len, const char *base, const char *name) {
    size_t pos = 0;

    if (!dst || !dst_len) return;
    dst[0] = '\0';
    if (!base) base = "/";
    if (!name) name = "";

    while (*base && pos + 1 < dst_len) dst[pos++] = *base++;
    if (pos > 0 && dst[pos - 1] != '/' && pos + 1 < dst_len) dst[pos++] = '/';
    while (*name && pos + 1 < dst_len) dst[pos++] = *name++;
    dst[pos] = '\0';
}

static bool deskbar_should_list_entry(const vfs_dir_entry_t *entry) {
    if (!entry) return false;
    if (entry->type != VFS_NODE_FILE) return false;
    if (!entry->name[0]) return false;
    if (entry->name[0] == '.') return false;
    return true;
}

static bool deskbar_is_executable(const char *filename) {
    if (!filename) return false;
    const char *ext = filename;
    while (*ext && *ext != '.') ext++;
    if (!*ext) return false;
    // Comparación insensible a mayúsculas
    if (kstrcmp(ext, ".o") == 0 || kstrcmp(ext, ".O") == 0 ||
        kstrcmp(ext, ".exe") == 0 || kstrcmp(ext, ".EXE") == 0)
        return true;
    return false;
}

static int deskbar_build_submenu(int section, deskbar_sub_item_t *items, int max_items) {
    const char *dir = NULL;
    const char *empty_label = "Coming soon";
    vfs_dir_entry_t entries[DESKBAR_MAX_SUB_ITEMS];
    uint32_t count = 0;
    int actual = 0;

    if (!items || max_items <= 0) return 0;
    if (section == 1) dir = "/PROGRAMS";
    else if (section == 2) dir = "/DOCS";
    else if (section == 3) dir = "/MISC";
    else return 0;
    if (section == 1) empty_label = "Shell missing";

    if (!vfs_listdir(dir, entries, DESKBAR_MAX_SUB_ITEMS, &count) || count == 0) {
        deskbar_copy_text(items[0].label, sizeof(items[0].label), empty_label);
        items[0].path[0] = '\0';
        items[0].enabled = false;
        return 1;
    }

    for (uint32_t i = 0; i < count && actual < max_items; i++) {
        if (!deskbar_should_list_entry(&entries[i])) continue;
        deskbar_make_pretty_name(entries[i].name, items[actual].label, sizeof(items[actual].label));
        deskbar_join_path(items[actual].path, sizeof(items[actual].path), dir, entries[i].name);
        items[actual].enabled = true;
        actual++;
    }

    if (actual == 0) {
        deskbar_copy_text(items[0].label, sizeof(items[0].label), empty_label);
        items[0].path[0] = '\0';
        items[0].enabled = false;
        return 1;
    }

    return actual;
}

static void deskbar_cache_submenu(deskbar_state_t *state, int section) {
    if (!state || section <= 0) return;
    if (state->cached_section == section) return;
    state->cached_count = deskbar_build_submenu(
        section, state->cached_items, DESKBAR_MAX_SUB_ITEMS);
    state->cached_section = section;
}

static int deskbar_window_item_width(gui_window_t *window) {
    int width = (int)gui_font_text_width(window->title) + 16;
    if (width < 48) width = 48;
    if (width > 92) width = 92;
    return width;
}

static gui_window_t *deskbar_window_at(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window;
    gui_rect_t area;
    int cursor;

    if (!desktop) return NULL;
    area = deskbar_window_area_rect(desktop);
    cursor = area.x;
    task_preempt_disable();
    window = desktop->first_window;

    while (window) {
        if (!window->listed) {
            window = window->next;
            continue;
        }
        int width = deskbar_window_item_width(window);
        gui_rect_t item = (gui_rect_t){cursor, area.y, width, area.h};

        if (cursor + width > area.x + area.w) break;
        if (gui_rect_contains(item, x, y)) {
            task_preempt_enable();
            return window;
        }

        cursor += width + 4;
        window = window->next;
    }

    task_preempt_enable();
    return NULL;
}

static int deskbar_hit_main_item(const gui_desktop_t *desktop, int x, int y) {
    for (int i = 0; i < DESKBAR_MAIN_ITEMS; i++) {
        if (gui_rect_contains(deskbar_main_item_rect(desktop, i), x, y)) return i;
    }
    return -1;
}

static int deskbar_hit_sub_item(const gui_desktop_t *desktop, int section, const deskbar_sub_item_t *items, int item_count, int x, int y) {
    (void)items;
    for (int i = 0; i < item_count; i++) {
        if (gui_rect_contains(deskbar_sub_item_rect(desktop, section, i, item_count), x, y)) return i;
    }
    return -1;
}

static void deskbar_draw_menu_item(gui_surface_t *surface, gui_rect_t rect, const char *label, bool active, bool sunken, bool enabled, bool has_arrow) {
    if (active) {
        deskbar_draw_panel(surface, rect, sunken ? 0x0090A8BE : 0x00A8BED0, sunken);
    } else {
        gui_gfx_fill_rect(surface, rect, 0x00C8C8C0);
    }

    gui_rect_t text_clip = {rect.x + 8, rect.y + 2,
                            rect.w - (has_arrow ? 30 : 16), rect.h - 4};
    gui_font_draw_string_clipped(surface, rect.x + 12, rect.y + 12,
                                 label, enabled ? 0x002A3542 : 0x00747A7E,
                                 text_clip);
    if (has_arrow) deskbar_draw_arrow(surface, rect.x + rect.w - 16, rect.y + 11, enabled ? 0x0036434E : 0x00747A7E);
}

static void deskbar_draw_windows(gui_desktop_t *desktop, gui_surface_t *surface, const deskbar_state_t *state) {
    gui_window_t *window;
    gui_rect_t area;
    int cursor;

    if (!desktop || !surface || !state) return;

    area = deskbar_window_area_rect(desktop);
    cursor = area.x;
    task_preempt_disable();
    window = desktop->first_window;

    while (window) {
        if (!window->listed) {
            window = window->next;
            continue;
        }
        int width = deskbar_window_item_width(window);
        gui_rect_t item = (gui_rect_t){cursor, area.y, width, area.h};
        uint32_t face = window->focused ? 0x00D8D0A6 : 0x00D4D8D4;
        bool sunken = false;

        if (cursor + width > area.x + area.w) break;
        if (state->pressed_window_id == window->id) {
            face = 0x00B6C2CC;
            sunken = true;
        } else if (state->hovered_window_id == window->id) {
            face = 0x00E4E8E6;
        }

        deskbar_draw_panel(surface, item, face, sunken);
        gui_font_draw_string_clipped(surface, item.x + 6, item.y + 5,
                                     window->title, 0x002E3B48,
                                     (gui_rect_t){item.x + 4, item.y + 2,
                                                  item.w - 8, item.h - 4});

        cursor += width + 4;
        window = window->next;
    }
    task_preempt_enable();
}

static void deskbar_paint(gui_program_t *program, gui_desktop_t *desktop, gui_surface_t *surface) {
    deskbar_state_t *state = (deskbar_state_t *)program->state;
    gui_rect_t bar;
    gui_rect_t button;
    gui_rect_t status;
    char clock_text[9];
    int sub_count = 0;

    if (!desktop || !surface || !state) return;

    bar = deskbar_bar_rect(desktop);
    button = deskbar_button_rect(desktop);
    status = deskbar_status_rect(desktop);

    gui_gfx_fill_gradient(surface, bar, 0x00D9DFDD, 0x00B4BAB8);
    gui_gfx_fill_rect(surface, (gui_rect_t){0, bar.y, bar.w, 1}, 0x00FFFFFF);
    gui_gfx_fill_rect(surface, (gui_rect_t){0, bar.y + 1, bar.w, 1}, 0x00ECEEEA);
    gui_gfx_fill_rect(surface, (gui_rect_t){0, bar.y + bar.h - 2, bar.w, 1}, 0x00807C74);
    gui_gfx_fill_rect(surface, (gui_rect_t){0, bar.y + bar.h - 1, bar.w, 1}, 0x00484840);

    deskbar_draw_panel(surface, button, state->button_hovered ? 0x00D1D8A8 : 0x00C1CB94, state->button_pressed || state->menu_open);
    deskbar_draw_logo(surface, button.x + 6, button.y + 3);
    gui_font_draw_string(surface, button.x + 24, button.y + 6, "Bles", 0x0035432D, 0, false);

    deskbar_draw_windows(desktop, surface, state);

    deskbar_draw_panel(surface, status,
                       state->clock_hovered ? 0x00E4E8E6 : 0x00D8DCD8,
                       state->clock_pressed);
    {
        rtc_time_t rtc;
        if (!rtc_get_time(&rtc)) {
            rtc.hour = 0;
            rtc.minute = 0;
            rtc.second = 0;
        }
        char *out = clock_text;
        *out++ = (char)('0' + (rtc.hour / 10));
        *out++ = (char)('0' + (rtc.hour % 10));
        *out++ = ':';
        *out++ = (char)('0' + (rtc.minute / 10));
        *out++ = (char)('0' + (rtc.minute % 10));
        *out++ = ':';
        *out++ = (char)('0' + (rtc.second / 10));
        *out++ = (char)('0' + (rtc.second % 10));
        *out = '\0';
    }
    gui_font_draw_string(surface, status.x + 15, status.y + 5,
                         clock_text, 0x0035454A, 0, false);

    if (!state->menu_open) return;

    {
        gui_rect_t menu = deskbar_menu_rect(desktop);
        deskbar_draw_panel(surface, menu, 0x00C8C8C0, false);

        for (int i = 0; i < DESKBAR_MAIN_ITEMS; i++) {
            gui_rect_t item = deskbar_main_item_rect(desktop, i);
            bool active = i == state->hovered_main_item ||
                          (i == state->open_section &&
                           deskbar_main_item_has_submenu(i));
            deskbar_draw_menu_item(surface, item, g_main_items[i], active,
                                   i == state->pressed_main_item, true,
                                   deskbar_main_item_has_submenu(i));
            if (i == 0) {
                gui_gfx_fill_rect(surface, (gui_rect_t){item.x + 2, item.y + item.h - 1, item.w - 4, 1}, 0x00807C74);
                gui_gfx_fill_rect(surface, (gui_rect_t){item.x + 2, item.y + item.h, item.w - 4, 1}, 0x00FFFFFF);
            }
            if (i == DESKBAR_MAIN_ITEMS - 2) {
                gui_gfx_fill_rect(surface,
                    (gui_rect_t){item.x + 2, item.y + item.h - 1,
                                 item.w - 4, 1},
                    0x00807C74);
                gui_gfx_fill_rect(surface,
                    (gui_rect_t){item.x + 2, item.y + item.h,
                                 item.w - 4, 1},
                    0x00FFFFFF);
            }
        }

        if (deskbar_main_item_has_submenu(state->open_section)) {
            deskbar_cache_submenu(state, state->open_section);
            sub_count = state->cached_count;
            if (sub_count > 0) {
                gui_rect_t submenu = deskbar_submenu_rect(desktop, state->open_section, sub_count);
                deskbar_draw_panel(surface, submenu, 0x00C8C8C0, false);
                for (int i = 0; i < sub_count; i++) {
                    gui_rect_t item = deskbar_sub_item_rect(desktop, state->open_section, i, sub_count);
                    bool active = i == state->hovered_sub_item;
                    deskbar_draw_menu_item(surface, item,
                        state->cached_items[i].label, active,
                        i == state->pressed_sub_item,
                        state->cached_items[i].enabled, false);
                }
            }
        }
    }
}

static bool deskbar_handle_event(gui_program_t *program, gui_desktop_t *desktop, const gui_event_t *event) {
    deskbar_state_t *state = (deskbar_state_t *)program->state;
    gui_rect_t bar;
    gui_rect_t button;
    gui_rect_t status;
    gui_rect_t menu;
    bool on_bar;
    bool on_button;
    bool on_status;
    bool on_menu = false;
    bool on_submenu = false;
    int main_hit = -1;
    int sub_hit = -1;
    int sub_count = 0;
    gui_window_t *hit_window;

    if (!desktop || !event || !state) return false;
    if (event->type != GUI_EVENT_MOUSE_MOVE && event->type != GUI_EVENT_MOUSE_DOWN && event->type != GUI_EVENT_MOUSE_UP) return false;
    if (desktop->drag_window && !state->menu_open && !state->button_pressed && state->pressed_window_id == 0) {
        state->button_hovered = false;
        state->clock_hovered = false;
        state->hovered_window_id = 0;
        return false;
    }

    bar = deskbar_bar_rect(desktop);
    button = deskbar_button_rect(desktop);
    status = deskbar_status_rect(desktop);
    menu = deskbar_menu_rect(desktop);
    on_bar = gui_rect_contains(bar, event->x, event->y);
    on_button = gui_rect_contains(button, event->x, event->y);
    on_status = gui_rect_contains(status, event->x, event->y);
    hit_window = deskbar_window_at(desktop, event->x, event->y);

    if (state->menu_open) {
        on_menu = gui_rect_contains(menu, event->x, event->y);
        main_hit = deskbar_hit_main_item(desktop, event->x, event->y);
        if (deskbar_main_item_has_submenu(state->open_section)) {
            deskbar_cache_submenu(state, state->open_section);
            sub_count = state->cached_count;
            if (sub_count > 0) {
                gui_rect_t submenu = deskbar_submenu_rect(desktop, state->open_section, sub_count);
                on_submenu = gui_rect_contains(submenu, event->x, event->y);
                sub_hit = deskbar_hit_sub_item(desktop, state->open_section,
                    state->cached_items, sub_count, event->x, event->y);
            }
        }
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        state->button_hovered = on_button;
        state->clock_hovered = on_status;
        state->hovered_window_id = hit_window ? hit_window->id : 0;

        if (state->menu_open) {
            state->hovered_main_item = main_hit;
            state->hovered_sub_item = sub_hit;
            if (deskbar_main_item_has_submenu(main_hit) &&
                state->open_section != main_hit) {
                state->open_section = main_hit;
                deskbar_cache_submenu(state, main_hit);
            } else if (main_hit >= 0 &&
                       !deskbar_main_item_has_submenu(main_hit)) {
                state->open_section = 0;
            }
        }

        return on_bar || on_menu || on_submenu || on_status ||
               hit_window != NULL;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (on_button) {
            state->button_pressed = true;
            state->clock_pressed = false;
            state->pressed_window_id = 0;
            state->pressed_main_item = -1;
            state->pressed_sub_item = -1;
            return true;
        }

        if (on_status) {
            state->clock_pressed = true;
            state->button_pressed = false;
            state->pressed_window_id = 0;
            state->pressed_main_item = -1;
            state->pressed_sub_item = -1;
            return true;
        }

        state->button_pressed = false;
        state->clock_pressed = false;

        if (hit_window) {
            state->pressed_window_id = hit_window->id;
            state->pressed_main_item = -1;
            state->pressed_sub_item = -1;
            return true;
        }

        if (state->menu_open) {
            if (sub_hit >= 0) {
                state->pressed_sub_item = sub_hit;
                state->pressed_main_item = -1;
                return true;
            }
            if (main_hit >= 0) {
                state->pressed_main_item = main_hit;
                state->pressed_sub_item = -1;
                return true;
            }
            state->menu_open = false;
            deskbar_reset_menu_state(state);
            return on_bar || on_status;
        }

        state->pressed_window_id = 0;
        return on_bar || on_status;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        bool handled = false;

        if (state->button_pressed) {
            if (on_button) {
                state->menu_open = !state->menu_open;
                deskbar_reset_menu_state(state);
                if (state->menu_open) {
                    state->open_section = 1;
                    deskbar_cache_submenu(state, 1);
                }
                handled = true;
            }
            state->button_pressed = false;
        } else if (state->clock_pressed) {
            if (on_status) {
                state->menu_open = false;
                deskbar_reset_menu_state(state);
                deskbar_open_clock_calendar(desktop);
                handled = true;
            }
            state->clock_pressed = false;
        } else if (state->pressed_window_id) {
            if (hit_window && hit_window->id == state->pressed_window_id) {
                deskbar_focus_window(desktop, hit_window);
                state->menu_open = false;
                deskbar_reset_menu_state(state);
                handled = true;
            }
            state->pressed_window_id = 0;
        } else if (state->pressed_sub_item >= 0) {
            if (deskbar_main_item_has_submenu(state->open_section)) {
                deskbar_cache_submenu(state, state->open_section);
                sub_count = state->cached_count;
                sub_hit = deskbar_hit_sub_item(desktop, state->open_section,
                    state->cached_items, sub_count, event->x, event->y);
                if (sub_hit == state->pressed_sub_item && sub_hit < sub_count &&
                    state->cached_items[sub_hit].enabled) {
                    // Verificar si es un ejecutable
                    if (deskbar_is_executable(
                            state->cached_items[sub_hit].path)) {
                        (void)program_execute_path(
                            desktop, state->cached_items[sub_hit].path);
                    } else {
                        texteditor_open(
                            desktop, state->cached_items[sub_hit].path);
                    }
                    state->menu_open = false;
                    deskbar_reset_menu_state(state);
                    handled = true;
                }
            }
            state->pressed_sub_item = -1;
        } else if (state->pressed_main_item >= 0) {
            main_hit = deskbar_hit_main_item(desktop, event->x, event->y);
            if (main_hit == state->pressed_main_item) {
                if (main_hit == 0) {
                    // Usar la nueva ventana que lista la raíz
                    about_open(desktop);
                    state->menu_open = false;
                    deskbar_reset_menu_state(state);
                } else if (main_hit == DESKBAR_MAIN_ITEMS - 1) {
                    state->menu_open = false;
                    deskbar_reset_menu_state(state);
                    handled = true;
                    deskbar_power_off();
                } else if (deskbar_main_item_has_submenu(main_hit)) {
                    state->open_section = main_hit;
                    deskbar_cache_submenu(state, main_hit);
                }
                handled = true;
            }
            state->pressed_main_item = -1;
        }

        if (!state->menu_open) {
            state->hovered_main_item = -1;
            state->hovered_sub_item = -1;
        } else {
            state->hovered_main_item = main_hit;
            state->hovered_sub_item = sub_hit;
        }
        state->button_hovered = on_button;
        state->clock_hovered = on_status;
        state->hovered_window_id = hit_window ? hit_window->id : 0;
        return handled || on_bar || on_menu || on_submenu || on_status ||
               hit_window != NULL;
    }

    return false;
}

static void deskbar_destroy(gui_program_t *program) {
    if (!program || !program->state) return;
    kfree(program->state);
    program->state = NULL;
}

void deskbar_install(gui_desktop_t *desktop) {
    deskbar_state_t *state;
    gui_program_t *program;

    if (!desktop) return;

    state = (deskbar_state_t *)kzalloc(sizeof(deskbar_state_t));
    if (!state) return;

    deskbar_reset_state(state);
    program = gui_desktop_register_program(desktop, "deskbar", state, deskbar_paint, deskbar_handle_event, deskbar_destroy);
    if (!program) kfree(state);
}
