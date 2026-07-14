#include "kernel/include/api.h"
#include "control_common.h"
#include <math.h>

#define DATETIME_WINDOW_W   486
#define DATETIME_WINDOW_H   338
#define DATETIME_TAB_LEFT    12
#define DATETIME_TAB_TOP      8
#define DATETIME_TAB_COUNT    2

typedef enum {
    DATETIME_TAB_VIEW = 0,
    DATETIME_TAB_ZONE = 1,
} datetime_tab_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    rtc_datetime_t now;
    rtc_datetime_t shown;
    bool rtc_ok;
    uint8_t current_tab;
    bk_datetime_preferences_t prefs;
    gui_widget_t *format_buttons[2];
    gui_widget_t *refresh_button;
    gui_widget_t *timezone_dropdown;
    uint32_t next_refresh;
    char status[96];
} datetime_state_t;

static const cpl_tab_spec_t g_datetime_tabs[DATETIME_TAB_COUNT] = {
    {"Vista", 64},
    {"Zona horaria", 102},
};

static const char *g_datetime_months[12] = {
    "Enero", "Febrero", "Marzo", "Abril",
    "Mayo", "Junio", "Julio", "Agosto",
    "Septiembre", "Octubre", "Noviembre", "Diciembre"
};

static const char *g_datetime_weekdays[7] = {
    "Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"
};

static gui_rect_t datetime_client_rect(const datetime_state_t *st) {
    return st && st->window
        ? bk_gui_window_content_rect_raw(st->window)
        : (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t datetime_page_rect(const datetime_state_t *st) {
    gui_rect_t client = datetime_client_rect(st);
    return (gui_rect_t){client.x + 8, client.y + 30,
                        client.w - 16, client.h - 50};
}

static gui_rect_t datetime_status_rect(const datetime_state_t *st) {
    gui_rect_t client = datetime_client_rect(st);
    return (gui_rect_t){client.x + 6, client.y + client.h - 18,
                        client.w - 12, 16};
}

static gui_rect_t datetime_calendar_group_rect(const datetime_state_t *st) {
    gui_rect_t page = datetime_page_rect(st);
    int width = (page.w - 54) / 2;
    if (width < 192) width = 192;
    return (gui_rect_t){page.x + 18, page.y + 20, width, 212};
}

static gui_rect_t datetime_clock_group_rect(const datetime_state_t *st) {
    gui_rect_t page = datetime_page_rect(st);
    int width = (page.w - 54) / 2;
    if (width < 192) width = 192;
    int x = page.x + 36 + width;
    return (gui_rect_t){x, page.y + 20, width, 212};
}

static gui_rect_t datetime_zone_group_rect(const datetime_state_t *st) {
    gui_rect_t page = datetime_page_rect(st);
    return (gui_rect_t){page.x + 18, page.y + 20, page.w - 36, 112};
}

static gui_rect_t datetime_format_group_rect(const datetime_state_t *st) {
    gui_rect_t page = datetime_page_rect(st);
    return (gui_rect_t){page.x + 18, page.y + 146, page.w - 36, 86};
}

static const char *datetime_default_status(const datetime_state_t *st) {
    if (!st) return "";
    return st->current_tab == DATETIME_TAB_VIEW
        ? "Vista previa de fecha y hora."
        : "El reloj del deskbar usa esta misma configuracion.";
}

static void datetime_set_status(datetime_state_t *st, const char *text) {
    if (!st) return;
    bk_runtime_strncpy(st->status, text ? text : "", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
}

static void datetime_apply_preferences(datetime_state_t *st) {
    if (!st) return;
    if (st->rtc_ok)
        bk_datetime_apply_preferences(&st->prefs, &st->now, &st->shown);
    else
        st->shown = st->now;
}

static void datetime_load_preferences(datetime_state_t *st) {
    if (!st) return;
    if (bk_datetime_runtime_preferences_get(&st->prefs)) return;
    bk_datetime_preferences_load(&st->prefs);
    bk_datetime_runtime_preferences_set(&st->prefs);
}

static void datetime_save_preferences(datetime_state_t *st) {
    char text[64];

    if (!st) return;
    snprintf(text, sizeof(text), "format=%d\r\ntimezone=%d\r\n",
             st->prefs.format_24h ? 24 : 12,
             st->prefs.timezone_index);
    bk_datetime_runtime_preferences_set(&st->prefs);
    (void)bk_user_config_write_text(BK_DATETIME_CONFIG_PATH, text);
}

static void datetime_sync_widgets(datetime_state_t *st) {
    if (!st) return;
    bk_gui_widget_set_selected(st->format_buttons[0], st->prefs.format_24h);
    bk_gui_widget_set_selected(st->format_buttons[1], !st->prefs.format_24h);
    bk_gui_widget_dropdown_set_selected(st->timezone_dropdown,
                                     st->prefs.timezone_index);
}

static void datetime_sync_visibility(datetime_state_t *st) {
    bool zone_tab;

    if (!st) return;
    zone_tab = st->current_tab == DATETIME_TAB_ZONE;
    if (st->timezone_dropdown) st->timezone_dropdown->visible = zone_tab;
    if (st->format_buttons[0]) st->format_buttons[0]->visible = zone_tab;
    if (st->format_buttons[1]) st->format_buttons[1]->visible = zone_tab;
    if (st->refresh_button) st->refresh_button->visible = !zone_tab;
}

static void datetime_refresh(datetime_state_t *st) {
    if (!st) return;
    st->rtc_ok = bk_time_datetime(&st->now);
    if (!st->rtc_ok) {
        st->now.date.year = 2026;
        st->now.date.month = 1;
        st->now.date.day = 1;
        st->now.time.hour = 0;
        st->now.time.minute = 0;
        st->now.time.second = 0;
    }
    datetime_apply_preferences(st);
    if (st->window) st->window->dirty = true;
}

static void datetime_switch_tab(datetime_state_t *st, uint8_t tab) {
    if (!st || tab >= DATETIME_TAB_COUNT || st->current_tab == tab) return;
    st->current_tab = tab;
    datetime_sync_visibility(st);
    datetime_set_status(st, datetime_default_status(st));
    st->window->dirty = true;
}

static void datetime_make_month_title(char *text,
                                      size_t text_size,
                                      const rtc_date_t *date) {
    if (!text || !text_size) return;
    if (!date || date->month < 1 || date->month > 12) {
        text[0] = '\0';
        return;
    }
    snprintf(text, text_size, "%s %u",
             g_datetime_months[date->month - 1], date->year);
}

static void datetime_make_date_text(datetime_state_t *st,
                                    char *text,
                                    size_t text_size) {
    const bk_timezone_option_t *zone;

    if (!text || !text_size) return;
    zone = bk_timezone_option(st ? st->prefs.timezone_index : 0);
    if (!st) {
        text[0] = '\0';
        return;
    }
    snprintf(text, text_size, "%02u/%02u/%04u  %s",
             st->shown.date.day, st->shown.date.month, st->shown.date.year,
             zone->label);
}

static void datetime_make_time_text(datetime_state_t *st,
                                    char *text,
                                    size_t text_size) {
    uint8_t hour;
    const char *suffix = "";

    if (!text || !text_size || !st) return;
    hour = st->shown.time.hour;
    if (!st->prefs.format_24h) {
        suffix = hour >= 12 ? " PM" : " AM";
        hour %= 12;
        if (!hour) hour = 12;
    }
    snprintf(text, text_size, "%02u:%02u:%02u%s",
             st->prefs.format_24h ? st->shown.time.hour : hour,
             st->shown.time.minute, st->shown.time.second, suffix);
}

static void datetime_draw_mini_calendar(gui_surface_t *surface,
                                        gui_rect_t rect,
                                        const rtc_date_t *date) {
    char title[40];
    char day_text[8];
    int header_h = 22;
    int grid_y = rect.y + 32;
    int cell_w = (rect.w - 12) / 7;
    int cell_h = 20;
    int first_weekday;
    int days;

    if (!surface || !date) return;
    cpl_draw_bevel(surface, rect, 0x00F3F3EE, true);
    datetime_make_month_title(title, sizeof(title), date);
    bk_gui_font_draw_string_clipped(surface,
        rect.x + (rect.w - (int)bk_gui_font_text_width(title)) / 2,
        rect.y + 8, title, CPL_TEXT,
        (gui_rect_t){rect.x + 4, rect.y + 6, rect.w - 8, 14});

    for (int i = 0; i < 7; i++) {
        gui_rect_t head = {rect.x + 6 + i * cell_w, rect.y + header_h,
                           cell_w - 1, 14};
        bk_gui_font_draw_string_clipped(surface, head.x + 2, head.y + 3,
                                     g_datetime_weekdays[i], CPL_SHADOW, head);
    }

    first_weekday = bk_datetime_weekday(date->year, date->month, 1);
    days = bk_datetime_days_in_month(date->year, date->month);

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            gui_rect_t cell = {rect.x + 6 + col * cell_w,
                               grid_y + row * cell_h,
                               cell_w - 1, cell_h - 1};
            bk_gui_gfx_fill_rect(surface, cell, 0x00FBFBF7);
            bk_gui_gfx_draw_rect(surface, cell, 0x00C4C4BC);
        }
    }

    for (int day = 1; day <= days; day++) {
        int index = first_weekday + day - 1;
        int row = index / 7;
        int col = index % 7;
        gui_rect_t cell = {rect.x + 6 + col * cell_w,
                           grid_y + row * cell_h,
                           cell_w - 1, cell_h - 1};

        if (day == date->day) {
            bk_gui_gfx_fill_rect(surface,
                (gui_rect_t){cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4},
                0x00FFF0A8);
            bk_gui_gfx_draw_rect(surface,
                (gui_rect_t){cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4},
                0x00A07010);
        }

        snprintf(day_text, sizeof(day_text), "%d", day);
        bk_gui_font_draw_string(surface, cell.x + 5, cell.y + 5,
                             day_text, col == 0 ? 0x00802020 : CPL_TEXT,
                             0, false);
    }
}

static void datetime_draw_analog_clock(gui_surface_t *surface,
                                       gui_rect_t rect,
                                       const rtc_time_t *time) {
    const double pi = 3.14159265358979323846;
    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2 - 10;
    int radius = rect.w < rect.h ? rect.w / 2 - 18 : rect.h / 2 - 18;
    double second_angle;
    double minute_angle;
    double hour_angle;

    if (!surface || !time || radius < 16) return;
    cpl_draw_bevel(surface, rect, 0x00F3F3EE, true);

    for (int tick = 0; tick < 60; tick++) {
        double angle = ((double)tick * 6.0 - 90.0) * (pi / 180.0);
        int outer_x = cx + (int)(cos(angle) * radius);
        int outer_y = cy + (int)(sin(angle) * radius);
        int inner_r = radius - ((tick % 5) == 0 ? 8 : 4);
        int inner_x = cx + (int)(cos(angle) * inner_r);
        int inner_y = cy + (int)(sin(angle) * inner_r);

        bk_gui_gfx_draw_line(surface, inner_x, inner_y, outer_x, outer_y,
                          (tick % 5) == 0 ? CPL_DARK : CPL_SHADOW);
    }

    hour_angle = ((((double)(time->hour % 12) * 30.0) +
                   ((double)time->minute * 0.5)) - 90.0) * (pi / 180.0);
    minute_angle = ((((double)time->minute * 6.0) +
                     ((double)time->second * 0.1)) - 90.0) * (pi / 180.0);
    second_angle = ((double)time->second * 6.0 - 90.0) * (pi / 180.0);

    bk_gui_gfx_draw_line(surface, cx, cy,
        cx + (int)(cos(hour_angle) * (radius - 22)),
        cy + (int)(sin(hour_angle) * (radius - 22)),
        0x002C3440);
    bk_gui_gfx_draw_line(surface, cx, cy,
        cx + (int)(cos(minute_angle) * (radius - 14)),
        cy + (int)(sin(minute_angle) * (radius - 14)),
        0x000080A0);
    bk_gui_gfx_draw_line(surface, cx, cy,
        cx + (int)(cos(second_angle) * (radius - 10)),
        cy + (int)(sin(second_angle) * (radius - 10)),
        0x00802020);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){cx - 2, cy - 2, 5, 5}, CPL_DARK);
}

static void datetime_paint_view_tab(datetime_state_t *st,
                                    gui_surface_t *surface) {
    gui_rect_t calendar_group = datetime_calendar_group_rect(st);
    gui_rect_t clock_group = datetime_clock_group_rect(st);
    gui_rect_t calendar_inner = {calendar_group.x + 10, calendar_group.y + 18,
                                 calendar_group.w - 20, calendar_group.h - 30};
    gui_rect_t clock_inner = {clock_group.x + 18, clock_group.y + 18,
                              clock_group.w - 36, clock_group.h - 60};
    char date_text[64];
    char time_text[32];

    cpl_draw_group(surface, calendar_group, "Fecha");
    cpl_draw_group(surface, clock_group, "Hora");

    datetime_draw_mini_calendar(surface, calendar_inner, &st->shown.date);
    datetime_draw_analog_clock(surface, clock_inner, &st->shown.time);

    datetime_make_time_text(st, time_text, sizeof(time_text));
    datetime_make_date_text(st, date_text, sizeof(date_text));
    bk_gui_font_draw_string(surface,
        clock_group.x + (clock_group.w - (int)bk_gui_font_text_width(time_text)) / 2,
        clock_group.y + 160, time_text,
        st->rtc_ok ? CPL_BLUE : 0x00800000, 0, false);
    bk_gui_font_draw_string_clipped(surface, clock_group.x + 12, clock_group.y + 182,
        date_text, CPL_TEXT,
        (gui_rect_t){clock_group.x + 12, clock_group.y + 182,
        clock_group.w - 24, 14});
}

static void datetime_paint_zone_tab(datetime_state_t *st,
                                    gui_surface_t *surface) {
    gui_rect_t zone_group = datetime_zone_group_rect(st);
    gui_rect_t format_group = datetime_format_group_rect(st);
    const bk_timezone_option_t *zone = bk_timezone_option(st->prefs.timezone_index);
    char line[48];

    cpl_draw_group(surface, zone_group, "Zona horaria");
    bk_gui_font_draw_string(surface, zone_group.x + 16, zone_group.y + 24,
                         "Seleccione una zona para ajustar la hora:",
                         CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, zone_group.x + 16, zone_group.y + 76,
                         "Zona actual:", CPL_SHADOW, 0, false);
    bk_gui_font_draw_string_clipped(surface, zone_group.x + 96, zone_group.y + 76,
                                 zone->label, CPL_BLUE,
                                 (gui_rect_t){zone_group.x + 96, zone_group.y + 76,
                                 zone_group.w - 112, 14});

    cpl_draw_group(surface, format_group, "Formato");
    snprintf(line, sizeof(line), "Modo activo: %s",
             st->prefs.format_24h ? "24 horas" : "12 horas");
    bk_gui_font_draw_string(surface, format_group.x + 16, format_group.y + 24,
                         line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, format_group.x + 16, format_group.y + 56,
                         "El deskbar y este panel usan el mismo INI.",
                         CPL_SHADOW, 0, false);
}

static void datetime_paint(gui_window_t *window,
                           gui_surface_t *surface,
                           void *context) {
    datetime_state_t *st = (datetime_state_t *)context;
    gui_rect_t client = bk_gui_window_content_rect_raw(window);
    gui_rect_t page = datetime_page_rect(st);
    gui_rect_t status = datetime_status_rect(st);

    cpl_draw_tabs(surface, client, g_datetime_tabs, DATETIME_TAB_COUNT,
                  st->current_tab, page, DATETIME_TAB_LEFT, DATETIME_TAB_TOP);

    if (st->current_tab == DATETIME_TAB_VIEW)
        datetime_paint_view_tab(st, surface);
    else
        datetime_paint_zone_tab(st, surface);

    cpl_draw_bevel(surface, status, CPL_FACE, true);
    bk_gui_font_draw_string_clipped(surface, status.x + 4, status.y + 4,
        st->status[0] ? st->status : datetime_default_status(st),
        CPL_TEXT, (gui_rect_t){status.x + 4, status.y + 2,
        status.w - 8, status.h - 4});
}

static void datetime_format_button(gui_window_t *window, uint32_t id) {
    datetime_state_t *st = window
        ? (datetime_state_t *)window->content_context : NULL;

    if (!st) return;
    if (st->format_buttons[0] && st->format_buttons[0]->id == id)
        st->prefs.format_24h = true;
    else if (st->format_buttons[1] && st->format_buttons[1]->id == id)
        st->prefs.format_24h = false;
    else
        return;

    datetime_save_preferences(st);
    datetime_apply_preferences(st);
    datetime_sync_widgets(st);
    datetime_set_status(st, "Formato de hora actualizado.");
    bk_gui_request_paint();
    window->dirty = true;
}

static void datetime_refresh_button(gui_window_t *window, uint32_t id UNUSED) {
    datetime_state_t *st = window
        ? (datetime_state_t *)window->content_context : NULL;

    if (!st) return;
    datetime_refresh(st);
    datetime_set_status(st, st->rtc_ok
        ? "Fecha y hora actualizadas desde el RTC."
        : "No se pudo leer el RTC.");
}

static void datetime_timezone_changed(gui_window_t *window, uint32_t id) {
    datetime_state_t *st = window
        ? (datetime_state_t *)window->content_context : NULL;
    int index;

    if (!st || !st->timezone_dropdown || st->timezone_dropdown->id != id)
        return;
    index = bk_gui_widget_dropdown_get_selected(st->timezone_dropdown);
    if (index < 0 || index >= bk_timezone_count()) index = 0;

    st->prefs.timezone_index = index;
    datetime_save_preferences(st);
    datetime_apply_preferences(st);
    datetime_sync_widgets(st);
    datetime_set_status(st, "Zona horaria actualizada.");
    bk_gui_request_paint();
    window->dirty = true;
}

static bool datetime_event(gui_window_t *window,
                           const gui_event_t *event,
                           void *context) {
    datetime_state_t *st = (datetime_state_t *)context;
    int tab_hit;

    if (!st || !event) return false;
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        tab_hit = cpl_hit_tab(datetime_client_rect(st), g_datetime_tabs,
                              DATETIME_TAB_COUNT, DATETIME_TAB_LEFT,
                              DATETIME_TAB_TOP, event->x, event->y);
        if (tab_hit >= 0) {
            datetime_switch_tab(st, (uint8_t)tab_hit);
            return true;
        }
    }
    if (event->type == GUI_EVENT_KEY && event->key == '\t') {
        datetime_switch_tab(st,
            (uint8_t)((st->current_tab + 1) % DATETIME_TAB_COUNT));
        return true;
    }
    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    datetime_state_t *st;

    if (!desktop) return;
    st = (datetime_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->window = bk_gui_create_window(desktop, 112, 66,
                                           DATETIME_WINDOW_W, DATETIME_WINDOW_H,
                                           "Fecha y hora");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }

    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Fecha y hora", "Version 1.0", "Configuracion de fecha y zona horaria.",
        "Bles.INC (C) 2026", "/ICONS/DATETIME.BMP"});

    bk_gui_set_window_min_size(st->window, DATETIME_WINDOW_W, DATETIME_WINDOW_H);
    bk_gui_set_window_content(st->window, datetime_paint, st);
    bk_gui_set_window_event_handler(st->window, datetime_event, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    st->current_tab = DATETIME_TAB_VIEW;
    datetime_load_preferences(st);
    datetime_refresh(st);

    st->refresh_button = bk_gui_widget_create_button(
        desktop, st->window, (gui_rect_t){330, 246, 92, 22},
        "Actualizar", datetime_refresh_button);
    st->timezone_dropdown = bk_gui_widget_create_dropdown(
        desktop, st->window, (gui_rect_t){38, 72, 254, 22},
        datetime_timezone_changed);
    for (int i = 0; i < bk_timezone_count(); i++) {
        const bk_timezone_option_t *zone = bk_timezone_option(i);
        (void)bk_gui_widget_dropdown_add_item(st->timezone_dropdown,
                                           zone->label, zone->label);
    }

    st->format_buttons[0] = bk_gui_widget_create_selectable_button(
        desktop, st->window, (gui_rect_t){46, 198, 88, 22},
        "24 horas", datetime_format_button);
    st->format_buttons[1] = bk_gui_widget_create_selectable_button(
        desktop, st->window, (gui_rect_t){142, 198, 88, 22},
        "12 horas", datetime_format_button);

    datetime_sync_widgets(st);
    datetime_sync_visibility(st);
    datetime_set_status(st, datetime_default_status(st));

    st->next_refresh = bk_sys_uptime_ms() + 500;
    while (!bk_proc_exit_requested() && st->window->listed) {
        uint32_t now = bk_sys_uptime_ms();

        if ((int32_t)(now - st->next_refresh) >= 0) {
            datetime_refresh(st);
            st->next_refresh = now + 500;
        }
        bk_sys_sleep_ticks(2);
    }

    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
