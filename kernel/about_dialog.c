#include "include/about_dialog.h"
#include "include/memory.h"
#include "include/task.h"
#include "string.h"
#include "../programs/programs.h"

#define BK_ABOUT_MENU_ID 0xBA01U
#define BK_ABOUT_REGISTRATIONS 24

typedef struct {
    gui_window_t *owner;
    gui_desktop_t *desktop;
    char name[48];
    char version[24];
    char description[128];
    char copyright[64];
    char icon_path[64];
} bk_about_registration_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    bk_about_registration_t info;
    uint32_t *icon;
} bk_about_window_t;

static bk_about_registration_t g_about_registry[BK_ABOUT_REGISTRATIONS];

void bk_about_detach(gui_window_t *window) {
    if (!window) return;
    for (uint32_t i = 0; i < BK_ABOUT_REGISTRATIONS; i++)
        if (g_about_registry[i].owner == window)
            kmemset(&g_about_registry[i], 0, sizeof(g_about_registry[i]));
}

static void bk_about_copy(char *out, uint32_t capacity, const char *text) {
    if (!out || !capacity) return;
    kstrncpy(out, text ? text : "", capacity - 1U);
    out[capacity - 1U] = '\0';
}

static void bk_about_paint(gui_window_t *window, gui_surface_t *surface,
                           void *context) {
    bk_about_window_t *state = (bk_about_window_t *)context;
    gui_rect_t c;
    gui_rect_t panel;
    if (!window || !surface || !state) return;
    c = gui_window_content_rect(window);
    gui_gfx_fill_rect(surface, c, 0x00D8DCE4);
    panel = (gui_rect_t){c.x + 12, c.y + 12, c.w - 24, c.h - 24};
    gui_gfx_fill_rect(surface, panel, 0x00FFFFFF);
    gui_gfx_draw_rect(surface, panel, 0x00708090);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){panel.x + 1, panel.y + 1, panel.w - 2, 72}, 0x00EAF2FA);
    gui_gfx_fill_rect(surface,
        (gui_rect_t){panel.x + 1, panel.y + 72, panel.w - 2, 3}, 0x001B6CA8);
    if (state->icon)
        program_draw_icon_pixels(surface, panel.x + 16, panel.y + 13,
                                 state->icon, 48, 48);
    else {
        gui_gfx_fill_rect(surface,
            (gui_rect_t){panel.x + 16, panel.y + 13, 48, 48}, 0x001B6CA8);
        gui_font_draw_string_scaled(surface, panel.x + 29, panel.y + 27, "?",
                                    0x00FFFFFF, 2);
    }
    gui_font_draw_string_clipped(surface, panel.x + 80, panel.y + 18,
        state->info.name, 0x00102030,
        (gui_rect_t){panel.x + 76, panel.y + 10, panel.w - 88, 20});
    gui_font_draw_string_clipped(surface, panel.x + 80, panel.y + 42,
        state->info.version, 0x00405060,
        (gui_rect_t){panel.x + 76, panel.y + 34, panel.w - 88, 20});
    gui_font_draw_string(surface, panel.x + 16, panel.y + 88,
                         "Acerca de este programa", 0x001B4F78, 0, false);
    gui_font_draw_string_clipped(surface, panel.x + 16, panel.y + 112,
        state->info.description, 0x00202020,
        (gui_rect_t){panel.x + 12, panel.y + 104, panel.w - 24, 42});
    gui_gfx_draw_line(surface, panel.x + 14, panel.y + panel.h - 38,
                      panel.x + panel.w - 14, panel.y + panel.h - 38,
                      0x00C0C8D0);
    gui_font_draw_string_clipped(surface, panel.x + 16,
        panel.y + panel.h - 24, state->info.copyright, 0x00505050,
        (gui_rect_t){panel.x + 10, panel.y + panel.h - 30,
                     panel.w - 20, 20});
}

static void bk_about_task(void *argument) {
    bk_about_window_t *state = (bk_about_window_t *)argument;
    if (!state || !state->desktop) { if (state) kfree(state); task_exit(); }
    if (state->info.icon_path[0])
        state->icon = program_load_bmp_icon_scaled(state->info.icon_path, 48, 48);
    state->window = gui_desktop_create_window(state->desktop, 125, 72,
        430, 240, state->info.name[0] ? state->info.name : "Acerca de");
    if (state->window) {
        gui_window_set_content(state->window, bk_about_paint, state);
        gui_window_set_min_size(state->window, 350, 210);
        state->window->owner_pid = task_current_pid();
        task_bind_window(state->window);
    }
    while (!task_exit_requested() && state->window && state->window->listed)
        task_sleep(4);
    if (state->window) {
        gui_desktop_remove_window(state->desktop, state->window);
        gui_window_destroy(state->window);
        task_bind_window(NULL);
    }
    if (state->icon) kfree(state->icon);
    kfree(state);
    task_exit();
}

void bk_about_show(gui_desktop_t *desktop, const bk_about_info_t *info) {
    bk_about_window_t *state;
    if (!desktop || !info) return;
    state = (bk_about_window_t *)kzalloc(sizeof(*state));
    if (!state) return;
    state->desktop = desktop;
    bk_about_copy(state->info.name, sizeof(state->info.name), info->name);
    bk_about_copy(state->info.version, sizeof(state->info.version), info->version);
    bk_about_copy(state->info.description, sizeof(state->info.description),
                  info->description);
    bk_about_copy(state->info.copyright, sizeof(state->info.copyright),
                  info->copyright);
    bk_about_copy(state->info.icon_path, sizeof(state->info.icon_path),
                  info->icon_path);
    if (task_create("about-dialog", bk_about_task, state) < 0) kfree(state);
}

static void bk_about_menu(gui_window_t *window, uint32_t item_id,
                          void *context) {
    bk_about_registration_t *registration =
        (bk_about_registration_t *)context;
    bk_about_info_t info;
    (void)window;
    if (item_id != BK_ABOUT_MENU_ID || !registration) return;
    info.name = registration->name;
    info.version = registration->version;
    info.description = registration->description;
    info.copyright = registration->copyright;
    info.icon_path = registration->icon_path;
    bk_about_show(registration->desktop, &info);
}

bool bk_about_attach(gui_window_t *window, gui_desktop_t *desktop,
                     const bk_about_info_t *info) {
    bk_about_registration_t *slot = NULL;
    int menu;
    if (!window || !desktop || !info) return false;
    for (uint32_t i = 0; i < BK_ABOUT_REGISTRATIONS; i++) {
        if (g_about_registry[i].owner == window) {
            slot = &g_about_registry[i];
            break;
        }
        if (!slot && !g_about_registry[i].owner)
            slot = &g_about_registry[i];
    }
    if (!slot) return false;
    kmemset(slot, 0, sizeof(*slot));
    slot->owner = window;
    slot->desktop = desktop;
    bk_about_copy(slot->name, sizeof(slot->name), info->name);
    bk_about_copy(slot->version, sizeof(slot->version), info->version);
    bk_about_copy(slot->description, sizeof(slot->description), info->description);
    bk_about_copy(slot->copyright, sizeof(slot->copyright), info->copyright);
    bk_about_copy(slot->icon_path, sizeof(slot->icon_path), info->icon_path);
    menu = -1;
    for (uint8_t i = 0; i < window->menu_count; i++) {
        if (kstrcmp(window->menus[i].label, "Help") == 0) {
            menu = i;
            break;
        }
    }
    if (menu < 0) menu = gui_window_add_menu(window, "Help");
    return menu >= 0 && gui_window_add_menu_item(window, menu,
        BK_ABOUT_MENU_ID, "Acerca de...", bk_about_menu, slot);
}
