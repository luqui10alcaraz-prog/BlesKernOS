#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/rtc.h"
#include "../kernel/include/task.h"

#define CALENDAR_BUTTONS 3
#define CALENDAR_BUTTON_PREV  0
#define CALENDAR_BUTTON_TODAY 1
#define CALENDAR_BUTTON_NEXT  2

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    int initial_x;
    int initial_y;

    int view_year;
    int view_month;

    int today_year;
    int today_month;
    int today_day;

    bool has_today;
    uint32_t button_ids[CALENDAR_BUTTONS];
    gui_widget_t *buttons[CALENDAR_BUTTONS];
} calendar_state_t;

static calendar_state_t *g_calendar;
static void calendar_main(void *argument);

static const char *calendar_months[12] = {
    "Enero", "Febrero", "Marzo", "Abril",
    "Mayo", "Junio", "Julio", "Agosto",
    "Septiembre", "Octubre", "Noviembre", "Diciembre"
};

static const char *calendar_weekdays[7] = {
    "Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"
};

static void calendar_strcpy(char *dst, const char *src) {
    if (!dst) return;
    if (!src) src = "";
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void calendar_strcat(char *dst, const char *src) {
    if (!dst) return;
    while (*dst) dst++;
    calendar_strcpy(dst, src);
}

static void calendar_to_string(char *out, int value) {
    char tmp[16];
    int pos = 15;
    uint32_t number = value < 0 ? (uint32_t)(-(value + 1)) + 1U
                                : (uint32_t)value;

    tmp[pos] = '\0';
    if (!number) tmp[--pos] = '0';
    while (number) {
        tmp[--pos] = (char)('0' + number % 10);
        number /= 10;
    }
    if (value < 0) tmp[--pos] = '-';
    calendar_strcpy(out, &tmp[pos]);
}

static bool calendar_is_leap_year(int year) {
    if (year <= 0) return false;
    if ((year % 400) == 0) return true;
    if ((year % 100) == 0) return false;
    return (year % 4) == 0;
}

static int calendar_days_in_month(int year, int month) {
    static const int days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month < 1 || month > 12) return 30;
    if (month == 2 && calendar_is_leap_year(year)) return 29;
    return days[month - 1];
}

/*
 * Gregorian calendar, without year 0.
 * Returns: 0 = Sunday, 1 = Monday, ... 6 = Saturday.
 */
static int calendar_weekday(int year, int month, int day) {
    static const int table[12] = {
        0, 3, 2, 5, 0, 3,
        5, 1, 4, 6, 2, 4
    };

    if (year < 1) year = 1;
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;

    if (month < 3) year--;
    return (year + year / 4 - year / 100 + year / 400 +
            table[month - 1] + day) % 7;
}

static void calendar_make_title(char *out, int month, int year) {
    char year_text[16];

    if (month < 1 || month > 12) month = 1;
    calendar_to_string(year_text, year);
    calendar_strcpy(out, calendar_months[month - 1]);
    calendar_strcat(out, " ");
    calendar_strcat(out, year_text);
}

static void calendar_make_date_text(char *out, int day, int month, int year) {
    char tmp[16];

    calendar_to_string(tmp, day);
    calendar_strcpy(out, tmp);
    calendar_strcat(out, "/");
    calendar_to_string(tmp, month);
    calendar_strcat(out, tmp);
    calendar_strcat(out, "/");
    calendar_to_string(tmp, year);
    calendar_strcat(out, tmp);
}

static bool calendar_read_today(calendar_state_t *st) {
    rtc_date_t date;

    if (!st || !rtc_get_date(&date)) return false;
    if (date.year < 1 || date.month < 1 || date.month > 12 ||
        date.day < 1 || date.day > calendar_days_in_month(date.year, date.month))
        return false;

    st->today_year = date.year;
    st->today_month = date.month;
    st->today_day = date.day;
    st->has_today = true;
    return true;
}

static void calendar_set_initial_date(calendar_state_t *st) {
    if (!st) return;

    if (calendar_read_today(st)) {
        st->view_year = st->today_year;
        st->view_month = st->today_month;
        return;
    }

    /*
     * Fallback in case the RTC date is not available.
     * Keep it valid: Gregorian calendars do not have year 0.
     */
    st->view_year = 2026;
    st->view_month = 1;
    st->today_year = 0;
    st->today_month = 0;
    st->today_day = 0;
    st->has_today = false;
}

static void calendar_prev_month(calendar_state_t *st) {
    if (!st) return;
    st->view_month--;
    if (st->view_month < 1) {
        st->view_month = 12;
        if (st->view_year > 1) st->view_year--;
    }
    if (st->window) st->window->dirty = true;
}

static void calendar_next_month(calendar_state_t *st) {
    if (!st) return;
    st->view_month++;
    if (st->view_month > 12) {
        st->view_month = 1;
        st->view_year++;
    }
    if (st->window) st->window->dirty = true;
}

static void calendar_go_today(calendar_state_t *st) {
    if (!st) return;
    if (calendar_read_today(st)) {
        st->view_year = st->today_year;
        st->view_month = st->today_month;
    }
    if (st->window) st->window->dirty = true;
}

static int calendar_text_len(const char *text) {
    int len = 0;
    if (!text) return 0;
    while (text[len]) len++;
    return len;
}

static int calendar_centered_text_x(int area_x, int area_w, const char *text) {
    int text_w;

    if (!text || area_w <= 0) return area_x;

    /*
     * Avoid gui_font_text_width() here because external .O programs may not
     * have that symbol exported by the ELF loader. The current GUI font is
     * fixed-width 8 px, so this approximation is enough for calendar labels.
     */
    text_w = calendar_text_len(text) * 8;
    if (text_w >= area_w) return area_x;
    return area_x + ((area_w - text_w) / 2);
}

static void calendar_draw_centered(gui_surface_t *surface, gui_rect_t rect,
                                   const char *text, uint32_t color) {
    int tx;

    if (!surface || !text || rect.w <= 0 || rect.h <= 0) return;
    tx = calendar_centered_text_x(rect.x, rect.w, text);
    gui_font_draw_string_clipped(surface, tx, rect.y + 7,
                                 text, color, rect);
}

static void calendar_layout_buttons(calendar_state_t *st) {
    int content_w;
    int today_x;
    int next_x;

    if (!st || !st->window) return;
    content_w = st->window->bounds.w - (GUI_BORDER_SIZE * 2);
    if (content_w < 0) content_w = 0;

    today_x = (content_w - 76) / 2;
    if (today_x < 64) today_x = 64;
    next_x = content_w - 10 - 42;
    if (next_x < today_x + 86) next_x = today_x + 86;

    if (st->buttons[CALENDAR_BUTTON_PREV]) {
        st->buttons[CALENDAR_BUTTON_PREV]->bounds = (gui_rect_t){10, 8, 42, 24};
    }
    if (st->buttons[CALENDAR_BUTTON_TODAY]) {
        st->buttons[CALENDAR_BUTTON_TODAY]->bounds = (gui_rect_t){today_x, 8, 76, 24};
    }
    if (st->buttons[CALENDAR_BUTTON_NEXT]) {
        st->buttons[CALENDAR_BUTTON_NEXT]->bounds = (gui_rect_t){next_x, 8, 42, 24};
    }
}

static void calendar_content(gui_window_t *window UNUSED,
                             gui_surface_t *surface, void *context) {
    calendar_state_t *st = (calendar_state_t *)context;
    gui_rect_t clip;
    char text[48];
    int margin = 14;
    int x;
    int y;
    int w;
    int grid_y;
    int grid_w;
    int cell_w;
    int cell_h;
    int available_h;
    int first_weekday;
    int days;
    int today_visible;

    if (!st || !st->window || !st->window->visible) return;

    calendar_layout_buttons(st);

    x = st->window->bounds.x + margin;
    y = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 8;
    w = st->window->bounds.w - (margin * 2);
    if (w <= 0) return;

    clip = (gui_rect_t){st->window->bounds.x + 2,
                        st->window->bounds.y + GUI_TITLEBAR_HEIGHT,
                        st->window->bounds.w - 4,
                        st->window->bounds.h - GUI_TITLEBAR_HEIGHT - 2};

    /* Keep the month/year below the buttons. This avoids the old overlap
     * where "Julio 2026" could collide with the centered Hoy button. */
    calendar_make_title(text, st->view_month, st->view_year);
    gui_font_draw_string_clipped(
        surface,
        calendar_centered_text_x(x, w, text),
        y + 36,
        text,
        0x00102030,
        clip
    );

    grid_y = y + 60;
    grid_w = w;
    cell_w = grid_w / 7;
    if (cell_w < 24) return;

    available_h = (st->window->bounds.y + st->window->bounds.h - 24) -
                  (grid_y + 18);
    cell_h = available_h / 6;
    if (cell_h > 25) cell_h = 25;
    if (cell_h < 18) return;

    gui_gfx_fill_rect(surface,
                      (gui_rect_t){x, grid_y - 4, cell_w * 7, 20},
                      0x00D0D0C8);
    gui_gfx_draw_rect(surface,
                      (gui_rect_t){x, grid_y - 4, cell_w * 7, 20},
                      0x00808080);

    for (int col = 0; col < 7; col++) {
        gui_rect_t rect = {
            x + col * cell_w,
            grid_y - 2,
            cell_w,
            16
        };
        calendar_draw_centered(surface, rect, calendar_weekdays[col],
                               0x00203040);
    }

    first_weekday = calendar_weekday(st->view_year, st->view_month, 1);
    days = calendar_days_in_month(st->view_year, st->view_month);
    today_visible = st->has_today &&
                    st->view_year == st->today_year &&
                    st->view_month == st->today_month;

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            gui_rect_t cell = {
                x + col * cell_w,
                grid_y + 18 + row * cell_h,
                cell_w,
                cell_h
            };
            gui_gfx_fill_rect(surface, cell, 0x00F4F4EE);
            gui_gfx_draw_rect(surface, cell, 0x00A0A0A0);
        }
    }

    for (int day = 1; day <= days; day++) {
        int index = first_weekday + day - 1;
        int row = index / 7;
        int col = index % 7;
        gui_rect_t cell = {
            x + col * cell_w,
            grid_y + 18 + row * cell_h,
            cell_w,
            cell_h
        };

        if (today_visible && day == st->today_day) {
            gui_gfx_fill_rect(surface,
                              (gui_rect_t){cell.x + 2, cell.y + 2,
                                           cell.w - 4, cell.h - 4},
                              0x00FFF0A8);
            gui_gfx_draw_rect(surface,
                              (gui_rect_t){cell.x + 2, cell.y + 2,
                                           cell.w - 4, cell.h - 4},
                              0x00A07010);
        }

        calendar_to_string(text, day);
        calendar_draw_centered(surface, cell, text,
                               (col == 0) ? 0x00802020 : 0x00102020);
    }

    if (st->has_today) {
        char date_text[32];

        calendar_make_date_text(date_text, st->today_day,
                                st->today_month, st->today_year);
        calendar_strcpy(text, "Hoy: ");
        calendar_strcat(text, date_text);
    } else {
        calendar_strcpy(text, "RTC sin fecha valida");
    }

    gui_font_draw_string_clipped(surface, x,
                                 st->window->bounds.y + st->window->bounds.h - 18,
                                 text, 0x00304050, clip);
}

static void calendar_button(gui_window_t *window, uint32_t id) {
    calendar_state_t *st = window ? (calendar_state_t *)window->content_context : NULL;
    if (!st) return;

    if (id == st->button_ids[CALENDAR_BUTTON_PREV])
        calendar_prev_month(st);
    else if (id == st->button_ids[CALENDAR_BUTTON_TODAY])
        calendar_go_today(st);
    else if (id == st->button_ids[CALENDAR_BUTTON_NEXT])
        calendar_next_month(st);
}

static bool calendar_event(gui_window_t *window UNUSED,
                           const gui_event_t *event, void *context) {
    calendar_state_t *st = (calendar_state_t *)context;

    if (!st || !st->window || !st->window->visible ||
        event->type != GUI_EVENT_KEY)
        return false;

    if (event->key == '[' || event->key == 'a' || event->key == 'A') {
        calendar_prev_month(st);
        return true;
    }

    if (event->key == ']' || event->key == 'd' || event->key == 'D') {
        calendar_next_month(st);
        return true;
    }

    if (event->key == 'h' || event->key == 'H' ||
        event->key == 't' || event->key == 'T') {
        calendar_go_today(st);
        return true;
    }

    return false;
}

static void calendar_cleanup(calendar_state_t *st) {
    if (!st) return;
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
        st->window = NULL;
    }
    if (g_calendar == st) g_calendar = NULL;
    kfree(st);
}

bool calendar_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_calendar) return false;
    info->window = g_calendar->window;
    info->memory_bytes = (uint32_t)sizeof(*g_calendar);
    if (g_calendar->window) {
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
        info->memory_bytes += (uint32_t)(CALENDAR_BUTTONS * sizeof(gui_widget_t));
    }
    return true;
}

static int calendar_clamp_coord(int value, int size, int limit) {
    if (limit <= size) return 4;
    if (value < 4) return 4;
    if (value + size > limit - 4) return limit - size - 4;
    return value;
}

static void calendar_open_internal(gui_desktop_t *desktop, int x, int y) {
    calendar_state_t *st;
    const int win_w = 405;
    const int win_h = 285;

    if (!desktop) return;

    x = calendar_clamp_coord(x, win_w, desktop->surface.width);
    y = calendar_clamp_coord(y, win_h, desktop->surface.height);

    if (g_calendar) {
        if (g_calendar->window) {
            if (!g_calendar->window->visible)
                gui_window_restore(g_calendar->window);
            g_calendar->window->bounds.x = x;
            g_calendar->window->bounds.y = y;
            gui_desktop_raise_window(desktop, g_calendar->window);
            gui_desktop_focus_window(desktop, g_calendar->window);
            g_calendar->window->dirty = true;
        }
        return;
    }

    st = (calendar_state_t *)kzalloc(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->initial_x = x;
    st->initial_y = y;
    calendar_set_initial_date(st);
    g_calendar = st;

    if (task_create("calendar", calendar_main, st) < 0) {
        calendar_cleanup(st);
    }
}

void calendar_open_from_desktop(gui_desktop_t *desktop) {
    calendar_open_internal(desktop, 115, 50);
}

void calendar_open_at(gui_desktop_t *desktop, int x, int y) {
    calendar_open_internal(desktop, x, y);
}

static void calendar_main(void *argument) {
    calendar_state_t *st = (calendar_state_t *)argument;
    gui_widget_t *button;

    if (!st || !st->desktop) {
        calendar_cleanup(st);
        task_exit();
    }

    task_set_memory_hint(sizeof(*st) +
                         (uint32_t)(CALENDAR_BUTTONS * sizeof(gui_widget_t)));

    st->window = gui_desktop_create_window(st->desktop, st->initial_x, st->initial_y,
                                           405, 285, "Calendario");
    if (st->window) {
        gui_window_set_content(st->window, calendar_content, st);
        gui_window_set_event_handler(st->window, calendar_event, st);
        gui_window_set_min_size(st->window, 390, 260);
        st->window->owner_pid = task_current_pid();
        task_bind_window(st->window);

        button = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
                                   (gui_rect_t){10, 8, 42, 24},
                                   "<", calendar_button);
        if (button) {
            st->button_ids[CALENDAR_BUTTON_PREV] = button->id;
            st->buttons[CALENDAR_BUTTON_PREV] = button;
        }

        button = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
                                   (gui_rect_t){156, 8, 76, 24},
                                   "Hoy", calendar_button);
        if (button) {
            st->button_ids[CALENDAR_BUTTON_TODAY] = button->id;
            st->buttons[CALENDAR_BUTTON_TODAY] = button;
        }

        button = gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
                                   (gui_rect_t){334, 8, 42, 24},
                                   ">", calendar_button);
        if (button) {
            st->button_ids[CALENDAR_BUTTON_NEXT] = button->id;
            st->buttons[CALENDAR_BUTTON_NEXT] = button;
        }

        calendar_layout_buttons(st);
    }

    while (!task_exit_requested()) {
        bool had_today = st->has_today;
        int old_y = st->today_year;
        int old_m = st->today_month;
        int old_d = st->today_day;

        if (!st->window || !st->window->listed) break;

        if (calendar_read_today(st) &&
            (!had_today || old_y != st->today_year ||
             old_m != st->today_month || old_d != st->today_day)) {
            st->window->dirty = true;
        }

        task_sleep(16);
    }

    calendar_cleanup(st);
    task_exit();
}

void calendar_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    calendar_open_from_desktop(desktop);
}
