#include "kernel/include/api.h"
#include "control_common.h"

#define MOUSE_CPL_WINDOW_W    430
#define MOUSE_CPL_WINDOW_H    340
#define MOUSE_CPL_ICON_SIZE    48
#define MOUSE_CPL_TAB_LEFT     12
#define MOUSE_CPL_TAB_TOP       8
#define MOUSE_CPL_TAB_COUNT     2

typedef enum {
    MOUSE_TAB_POINTER = 0,
    MOUSE_TAB_OPTIONS = 1,
} mouse_cpl_tab_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    mouse_state_t mouse;
    uint8_t current_tab;
    int sensitivity;
    bool draw_trail;
    gui_widget_t *sensitivity_buttons[5];
    gui_widget_t *trail_buttons[2];
    uint32_t *mouse_icon;
    int preview_x;
    int preview_y;
    uint32_t next_refresh;
    char status[80];
} mouse_cpl_state_t;

static const cpl_tab_spec_t g_mouse_tabs[MOUSE_CPL_TAB_COUNT] = {
    {"Puntero", 72},
    {"Opciones", 82},
};

static gui_rect_t mouse_cpl_client_rect(const mouse_cpl_state_t *st) {
    return st && st->window
        ? bk_gui_window_content_rect_raw(st->window)
        : (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t mouse_cpl_page_rect(const mouse_cpl_state_t *st) {
    gui_rect_t client = mouse_cpl_client_rect(st);
    return (gui_rect_t){client.x + 8, client.y + 30, client.w - 16, client.h - 50};
}

static gui_rect_t mouse_cpl_status_rect(const mouse_cpl_state_t *st) {
    gui_rect_t client = mouse_cpl_client_rect(st);
    return (gui_rect_t){client.x + 6, client.y + client.h - 18,
                        client.w - 12, 16};
}

static gui_rect_t mouse_cpl_preview_group_rect(const mouse_cpl_state_t *st) {
    gui_rect_t page = mouse_cpl_page_rect(st);
    return (gui_rect_t){page.x + 36, page.y + 16, page.w - 72, page.h - 52};
}

static gui_rect_t mouse_cpl_preview_canvas_rect(const mouse_cpl_state_t *st) {
    gui_rect_t group = mouse_cpl_preview_group_rect(st);
    return (gui_rect_t){group.x + (group.w - 156) / 2, group.y + 24, 156, 112};
}

static gui_rect_t mouse_cpl_preview_inner_local_rect(const mouse_cpl_state_t *st) {
    gui_rect_t client = mouse_cpl_client_rect(st);
    gui_rect_t canvas = mouse_cpl_preview_canvas_rect(st);
    return (gui_rect_t){canvas.x - client.x + 12, canvas.y - client.y + 12,
                        canvas.w - 24, canvas.h - 24};
}

static gui_rect_t mouse_cpl_speed_group_rect(const mouse_cpl_state_t *st) {
    gui_rect_t page = mouse_cpl_page_rect(st);
    return (gui_rect_t){page.x + 18, page.y + 20, 184, 118};
}

static gui_rect_t mouse_cpl_trail_group_rect(const mouse_cpl_state_t *st) {
    gui_rect_t page = mouse_cpl_page_rect(st);
    return (gui_rect_t){page.x + 208, page.y + 20, 184, 118};
}

static const char *mouse_cpl_default_status(const mouse_cpl_state_t *st) {
    if (!st) return "";
    return st->current_tab == MOUSE_TAB_POINTER
        ? "Mueva el mouse para ver la muestra."
        : "Ajuste velocidad y estela del cursor.";
}

static void mouse_cpl_set_status(mouse_cpl_state_t *st, const char *text) {
    if (!st) return;
    bk_runtime_strncpy(st->status, text ? text : "", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
}

static void mouse_cpl_reset_preview(mouse_cpl_state_t *st) {
    gui_rect_t inner;

    if (!st) return;
    inner = mouse_cpl_preview_inner_local_rect(st);
    st->preview_x = inner.x + (inner.w - MOUSE_CPL_ICON_SIZE) / 2;
    st->preview_y = inner.y + (inner.h - MOUSE_CPL_ICON_SIZE) / 2;
}

static void mouse_cpl_sync_buttons(mouse_cpl_state_t *st) {
    if (!st) return;
    for (int i = 0; i < 5; i++) {
        bk_gui_widget_set_selected(st->sensitivity_buttons[i],
                                st->sensitivity == i + 1);
    }
    bk_gui_widget_set_selected(st->trail_buttons[0], st->draw_trail);
    bk_gui_widget_set_selected(st->trail_buttons[1], !st->draw_trail);
}

static void mouse_cpl_sync_widget_visibility(mouse_cpl_state_t *st) {
    bool options_visible;

    if (!st) return;
    options_visible = st->current_tab == MOUSE_TAB_OPTIONS;
    for (int i = 0; i < 5; i++) {
        if (st->sensitivity_buttons[i])
            st->sensitivity_buttons[i]->visible = options_visible;
    }
    for (int i = 0; i < 2; i++) {
        if (st->trail_buttons[i])
            st->trail_buttons[i]->visible = options_visible;
    }
}

static void mouse_cpl_save_config(mouse_cpl_state_t *st) {
    char config[48];

    if (!st) return;
    snprintf(config, sizeof(config), "sensitivity=%d\r\ntrail=%d\r\n",
             st->sensitivity, st->draw_trail ? 1 : 0);
    (void)bk_user_config_write_text(BK_MOUSE_CONFIG_PATH, config);
}

static int mouse_cpl_parse_int(const char *text, int fallback) {
    int value = 0;
    bool seen_digit = false;

    if (!text) return fallback;
    if (*text == '+') text++;
    while (*text >= '0' && *text <= '9') {
        seen_digit = true;
        value = value * 10 + (*text - '0');
        text++;
    }
    return seen_digit ? value : fallback;
}

static void mouse_cpl_apply_system_state(mouse_cpl_state_t *st) {
    if (!st) return;
    bk_input_mouse_set_sensitivity((uint8_t)st->sensitivity);
    bk_gui_desktop_set_cursor_trail(st->desktop, st->draw_trail);
}

static void mouse_cpl_load_config(mouse_cpl_state_t *st) {
    void *config = NULL;
    uint32_t size = 0;

    if (!st) return;
    st->sensitivity = (int)bk_input_mouse_get_sensitivity();
    if (st->sensitivity < 1 || st->sensitivity > 5)
        st->sensitivity = 3;
    st->draw_trail = bk_gui_desktop_cursor_trail_enabled(st->desktop);

    if (!bk_user_config_read_all(BK_MOUSE_CONFIG_PATH,
                                 BK_MOUSE_CONFIG_LEGACY_PATH,
                                 &config, &size) || !config) {
        mouse_cpl_apply_system_state(st);
        return;
    }
    (void)size;

    {
        char *line = (char *)config;

        while (*line) {
            char *end = line;
            char *eq = NULL;
            char saved;

            while (*end && *end != '\r' && *end != '\n') {
                if (*end == '=' && !eq) eq = end;
                end++;
            }
            saved = *end;
            *end = '\0';

            if (eq) {
                int value;
                *eq++ = '\0';
                value = mouse_cpl_parse_int(eq, 0);
                if (bk_runtime_strcmp(line, "sensitivity") == 0) {
                    if (value >= 1 && value <= 5) st->sensitivity = value;
                } else if (bk_runtime_strcmp(line, "trail") == 0) {
                    st->draw_trail = value != 0;
                }
            }

            *end = saved;
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
    }

    bk_sys_free(config);
    mouse_cpl_apply_system_state(st);
}

static void mouse_cpl_switch_tab(mouse_cpl_state_t *st, uint8_t tab) {
    if (!st || tab >= MOUSE_CPL_TAB_COUNT || st->current_tab == tab) return;
    st->current_tab = tab;
    mouse_cpl_sync_widget_visibility(st);
    mouse_cpl_set_status(st, mouse_cpl_default_status(st));
    st->window->dirty = true;
}

static void mouse_cpl_sensitivity(gui_window_t *window, uint32_t id) {
    mouse_cpl_state_t *st = window
        ? (mouse_cpl_state_t *)window->content_context : NULL;

    if (!st) return;
    for (int i = 0; i < 5; i++) {
        if (!st->sensitivity_buttons[i] ||
            st->sensitivity_buttons[i]->id != id) continue;
        st->sensitivity = i + 1;
        mouse_cpl_apply_system_state(st);
        mouse_cpl_save_config(st);
        mouse_cpl_sync_buttons(st);
        snprintf(st->status, sizeof(st->status),
                 "Sensibilidad aplicada: nivel %d.", st->sensitivity);
        window->dirty = true;
        return;
    }
}

static void mouse_cpl_toggle_trail(gui_window_t *window, uint32_t id) {
    mouse_cpl_state_t *st = window
        ? (mouse_cpl_state_t *)window->content_context : NULL;

    if (!st) return;
    if (st->trail_buttons[0] && st->trail_buttons[0]->id == id)
        st->draw_trail = true;
    else if (st->trail_buttons[1] && st->trail_buttons[1]->id == id)
        st->draw_trail = false;
    else
        return;

    mouse_cpl_apply_system_state(st);
    mouse_cpl_save_config(st);
    mouse_cpl_sync_buttons(st);
    snprintf(st->status, sizeof(st->status), "Estela del cursor %s.",
             st->draw_trail ? "activada" : "desactivada");
    window->dirty = true;
}

static void mouse_cpl_update_preview(mouse_cpl_state_t *st) {
    gui_rect_t inner;

    if (!st) return;
    inner = mouse_cpl_preview_inner_local_rect(st);
    st->preview_x += st->mouse.dx;
    st->preview_y -= st->mouse.dy;
    if (st->preview_x < inner.x) st->preview_x = inner.x;
    if (st->preview_y < inner.y) st->preview_y = inner.y;
    if (st->preview_x + MOUSE_CPL_ICON_SIZE > inner.x + inner.w)
        st->preview_x = inner.x + inner.w - MOUSE_CPL_ICON_SIZE;
    if (st->preview_y + MOUSE_CPL_ICON_SIZE > inner.y + inner.h)
        st->preview_y = inner.y + inner.h - MOUSE_CPL_ICON_SIZE;
}

static void mouse_cpl_draw_fallback_icon(gui_surface_t *surface, int x, int y) {
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 13, y + 6, 10, 24}, CPL_LIGHT);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 24, y + 6, 10, 24}, CPL_LIGHT);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 20, y + 18, 8, 16}, CPL_SHADOW);
    bk_gui_gfx_draw_line(surface, x + 12, y + 5, x + 23, y + 39, CPL_DARK);
    bk_gui_gfx_draw_line(surface, x + 35, y + 5, x + 24, y + 39, CPL_DARK);
    bk_gui_gfx_draw_line(surface, x + 18, y + 40, x + 30, y + 40, CPL_DARK);
}

static void mouse_cpl_draw_preview_icon(const mouse_cpl_state_t *st,
                                        gui_surface_t *surface,
                                        int x, int y) {
    if (st->mouse_icon) {
        bk_app_draw_icon(surface, x, y, st->mouse_icon,
                                 MOUSE_CPL_ICON_SIZE, MOUSE_CPL_ICON_SIZE);
    } else {
        mouse_cpl_draw_fallback_icon(surface, x, y);
    }
}

static void mouse_cpl_paint_pointer_tab(mouse_cpl_state_t *st,
                                        gui_surface_t *surface) {
    gui_rect_t preview = mouse_cpl_preview_group_rect(st);
    gui_rect_t canvas = mouse_cpl_preview_canvas_rect(st);
    int icon_x;
    int icon_y;
    char line[64];

    cpl_draw_group(surface, preview, "Muestra");
    cpl_draw_bevel(surface, canvas, CPL_WHITE, true);
    bk_gui_gfx_fill_rect(surface,
        (gui_rect_t){canvas.x + 1, canvas.y + 1, canvas.w - 2, canvas.h - 2},
        0x00F6F6F6);

    icon_x = mouse_cpl_client_rect(st).x + st->preview_x;
    icon_y = mouse_cpl_client_rect(st).y + st->preview_y;
    mouse_cpl_draw_preview_icon(st, surface, icon_x, icon_y);

    snprintf(line, sizeof(line), "Posicion: %d, %d", st->mouse.x, st->mouse.y);
    bk_gui_font_draw_string(surface, preview.x + 18, preview.y + preview.h - 38,
                         line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, preview.x + 18, preview.y + preview.h - 20,
        st->mouse.present ? "Mouse PS/2 detectado" : "Mouse no detectado",
        st->mouse.present ? 0x00006020 : 0x00800000, 0, false);
}

static void mouse_cpl_paint_options_tab(mouse_cpl_state_t *st,
                                        gui_surface_t *surface) {
    gui_rect_t speed = mouse_cpl_speed_group_rect(st);
    gui_rect_t trail = mouse_cpl_trail_group_rect(st);
    char line[48];

    cpl_draw_group(surface, speed, "Sensibilidad");
    snprintf(line, sizeof(line), "Nivel actual: %d", st->sensitivity);
    bk_gui_font_draw_string(surface, speed.x + 16, speed.y + 20,
                         line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, speed.x + 16, speed.y + 46,
                         "Lenta", CPL_SHADOW, 0, false);
    bk_gui_font_draw_string(surface, speed.x + speed.w - 46, speed.y + 46,
                         "Rapida", CPL_SHADOW, 0, false);

    cpl_draw_group(surface, trail, "Estela");
    bk_gui_font_draw_string(surface, trail.x + 16, trail.y + 20,
                         "Activar en todo el sistema:",
                         CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, trail.x + 16, trail.y + 74,
        st->draw_trail ? "La estela esta encendida." : "La estela esta apagada.",
        CPL_SHADOW, 0, false);
}

static void mouse_cpl_paint(gui_window_t *window, gui_surface_t *surface,
                            void *context) {
    mouse_cpl_state_t *st = (mouse_cpl_state_t *)context;
    gui_rect_t client = bk_gui_window_content_rect_raw(window);
    gui_rect_t page = mouse_cpl_page_rect(st);
    gui_rect_t status = mouse_cpl_status_rect(st);

    cpl_draw_tabs(surface, client, g_mouse_tabs, MOUSE_CPL_TAB_COUNT,
                  st->current_tab, page, MOUSE_CPL_TAB_LEFT, MOUSE_CPL_TAB_TOP);

    if (st->current_tab == MOUSE_TAB_POINTER)
        mouse_cpl_paint_pointer_tab(st, surface);
    else
        mouse_cpl_paint_options_tab(st, surface);

    cpl_draw_bevel(surface, status, CPL_FACE, true);
    bk_gui_font_draw_string_clipped(surface, status.x + 4, status.y + 4,
        st->status[0] ? st->status : mouse_cpl_default_status(st),
        CPL_TEXT, (gui_rect_t){status.x + 4, status.y + 2,
        status.w - 8, status.h - 4});
}

static bool mouse_cpl_event(gui_window_t *window,
                            const gui_event_t *event,
                            void *context) {
    mouse_cpl_state_t *st = (mouse_cpl_state_t *)context;
    int tab_hit;

    if (!st || !event) return false;
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        tab_hit = cpl_hit_tab(mouse_cpl_client_rect(st), g_mouse_tabs,
                              MOUSE_CPL_TAB_COUNT, MOUSE_CPL_TAB_LEFT,
                              MOUSE_CPL_TAB_TOP, event->x, event->y);
        if (tab_hit >= 0) {
            mouse_cpl_switch_tab(st, (uint8_t)tab_hit);
            return true;
        }
    }
    if (event->type == GUI_EVENT_KEY && event->key == '\t') {
        mouse_cpl_switch_tab(st,
            (uint8_t)((st->current_tab + 1) % MOUSE_CPL_TAB_COUNT));
        return true;
    }
    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    mouse_cpl_state_t *st;

    if (!desktop) return;
    st = (mouse_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->window = bk_gui_create_window(desktop, 100, 60,
                                           MOUSE_CPL_WINDOW_W,
                                           MOUSE_CPL_WINDOW_H,
                                           "Propiedades del mouse");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }

    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Mouse", "Version 1.0", "Configuracion del puntero y botones.",
        "Bles.INC (C) 2026", "/ICONS/MOUSE.BMP"});

    bk_gui_set_window_min_size(st->window, MOUSE_CPL_WINDOW_W, MOUSE_CPL_WINDOW_H);
    bk_gui_set_window_content(st->window, mouse_cpl_paint, st);
    bk_gui_set_window_event_handler(st->window, mouse_cpl_event, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    st->current_tab = MOUSE_TAB_POINTER;
    mouse_cpl_load_config(st);
    st->mouse_icon = bk_app_load_icon("/ICONS/MOUSE.BMP",
                                                  MOUSE_CPL_ICON_SIZE,
                                                  MOUSE_CPL_ICON_SIZE);
    mouse_cpl_reset_preview(st);

    for (int i = 0; i < 5; i++) {
        char label[4];
        snprintf(label, sizeof(label), "%d", i + 1);
        st->sensitivity_buttons[i] = bk_gui_widget_create_selectable_button(
            desktop, st->window,
            (gui_rect_t){32 + i * 36, 100, 28, 22},
            label, mouse_cpl_sensitivity);
    }

    st->trail_buttons[0] = bk_gui_widget_create_selectable_button(
        desktop, st->window, (gui_rect_t){252, 100, 54, 22},
        "Si", mouse_cpl_toggle_trail);
    st->trail_buttons[1] = bk_gui_widget_create_selectable_button(
        desktop, st->window, (gui_rect_t){314, 100, 54, 22},
        "No", mouse_cpl_toggle_trail);
    mouse_cpl_sync_buttons(st);
    mouse_cpl_sync_widget_visibility(st);
    mouse_cpl_set_status(st, mouse_cpl_default_status(st));

    st->next_refresh = bk_sys_uptime_ms();
    while (!bk_proc_exit_requested() && st->window->listed) {
        uint32_t now = bk_sys_uptime_ms();

        if ((int32_t)(now - st->next_refresh) >= 0) {
            (void)bk_input_mouse(&st->mouse);
            mouse_cpl_update_preview(st);
            st->window->dirty = true;
            st->next_refresh = now + 50;
        }
        bk_sys_sleep_ticks(2);
    }

    if (st->mouse_icon) bk_sys_free(st->mouse_icon);
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
