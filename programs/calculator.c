#include "../kernel/include/api.h"

#define CALC_BUTTONS 20

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    char display[32];
    int accumulator;
    int current;
    char operation;
    bool entering;
    bool error;
    uint32_t button_ids[CALC_BUTTONS];
} calculator_state_t;

static calculator_state_t *g_calculator;
static void calculator_main(void *argument);

static const char *calc_labels[CALC_BUTTONS] = {
    "C", "<-", "%", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "+/-", "0", "x2", "="
};

static void calc_to_string(char *out, int value) {
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
    bk_runtime_strcpy(out, &tmp[pos]);
}

static void calc_clear(calculator_state_t *st) {
    st->accumulator = 0;
    st->current = 0;
    st->operation = 0;
    st->entering = false;
    st->error = false;
    bk_runtime_strcpy(st->display, "0");
}

static bool calc_apply(calculator_state_t *st, int right) {
    if (!st->operation) st->accumulator = right;
    else if (st->operation == '+') st->accumulator += right;
    else if (st->operation == '-') st->accumulator -= right;
    else if (st->operation == '*') st->accumulator *= right;
    else if (st->operation == '/') {
        if (!right) {
            bk_runtime_strcpy(st->display, "Error: division por 0");
            st->error = true;
            return false;
        }
        st->accumulator /= right;
    }
    return true;
}

static void calc_press(calculator_state_t *st, const char *key) {
    if (!st || !key) return;
    if (st->error && bk_runtime_strcmp(key, "C") != 0) calc_clear(st);

    if (key[0] >= '0' && key[0] <= '9' && key[1] == '\0') {
        int digit = key[0] - '0';
        if (!st->entering) {
            st->current = digit;
            st->entering = true;
        } else if (st->current < 100000000 && st->current > -100000000) {
            st->current = st->current * 10 +
                          (st->current < 0 ? -digit : digit);
        }
        calc_to_string(st->display, st->current);
    } else if (bk_runtime_strcmp(key, "C") == 0) {
        calc_clear(st);
    } else if (bk_runtime_strcmp(key, "<-") == 0) {
        if (st->entering) st->current /= 10;
        calc_to_string(st->display, st->current);
    } else if (bk_runtime_strcmp(key, "+/-") == 0) {
        st->current = -st->current;
        st->entering = true;
        calc_to_string(st->display, st->current);
    } else if (bk_runtime_strcmp(key, "%") == 0) {
        st->current /= 100;
        calc_to_string(st->display, st->current);
    } else if (bk_runtime_strcmp(key, "x2") == 0) {
        int value = st->entering ? st->current : st->accumulator;
        st->current = value * value;
        st->entering = true;
        calc_to_string(st->display, st->current);
    } else if (bk_runtime_strcmp(key, "=") == 0) {
        if (calc_apply(st, st->entering ? st->current : st->accumulator))
            calc_to_string(st->display, st->accumulator);
        st->operation = 0;
        st->current = st->accumulator;
        st->entering = false;
    } else {
        if (st->entering && !calc_apply(st, st->current)) return;
        st->operation = key[0];
        st->current = 0;
        st->entering = false;
        calc_to_string(st->display, st->accumulator);
    }
}

static void calculator_button(gui_window_t *window UNUSED, uint32_t id) {
    calculator_state_t *st = window ? (calculator_state_t *)window->content_context : NULL;
    if (!st) return;
    for (int i = 0; i < CALC_BUTTONS; i++)
        if (st->button_ids[i] == id) {
            calc_press(st, calc_labels[i]);
            if (st->window) st->window->dirty = true;
            return;
        }
}

static void calculator_content(gui_window_t *window UNUSED,
                               gui_surface_t *surface, void *context) {
    calculator_state_t *st = (calculator_state_t *)context;
    if (!st || !st->window || !st->window->visible) return;
    int x = st->window->bounds.x + 10;
    int y = bk_gui_window_content_rect_raw(st->window).y + 8;
    int w = st->window->bounds.w - 20;
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x, y, w, 30}, 0x00F8FFF0);
    bk_gui_gfx_draw_rect(surface, (gui_rect_t){x, y, w, 30}, 0x00405040);
    int text_w = (int)bk_gui_font_text_width(st->display);
    int tx = x + w - text_w - 7;
    if (tx < x + 5) tx = x + 5;
    bk_gui_font_draw_string_clipped(surface, tx, y + 11, st->display,
                                 st->error ? 0x00A02020 : 0x00102020,
                                 (gui_rect_t){x + 4, y + 3, w - 8, 24});
}

static bool calculator_event(gui_window_t *window UNUSED,
                             const gui_event_t *event, void *context) {
    calculator_state_t *st = (calculator_state_t *)context;
    if (!st || !st->window || !st->window->visible ||
        event->type != GUI_EVENT_KEY)
        return false;
    char key[2] = {event->key, '\0'};
    if (event->key == '\n') calc_press(st, "=");
    else if (event->key == '\b') calc_press(st, "<-");
    else if ((event->key >= '0' && event->key <= '9') ||
             event->key == '+' || event->key == '-' ||
             event->key == '*' || event->key == '/' ||
             event->key == '%') calc_press(st, key);
    else if (event->key == 27) calc_press(st, "C");
    else return false;
    if (st->window) st->window->dirty = true;
    return true;
}

static void calculator_cleanup(calculator_state_t *st) {
    if (!st) return;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (g_calculator == st) g_calculator = NULL;
    bk_sys_free(st);
}

bool calculator_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_calculator) return false;
    info->window = g_calculator->window;
    info->memory_bytes = (uint32_t)sizeof(*g_calculator);
    if (g_calculator->window) {
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
        info->memory_bytes += (uint32_t)(CALC_BUTTONS * sizeof(gui_widget_t));
    }
    return true;
}

void calculator_open_from_desktop(gui_desktop_t *desktop) {
    calculator_state_t *st;

    if (!desktop) return;

    st = (calculator_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    calc_clear(st);
    g_calculator = st;
    if (bk_proc_spawn_thread("calculator", calculator_main, st) < 0) {
        calculator_cleanup(st);
    }
}

static void calculator_main(void *argument) {
    calculator_state_t *st = (calculator_state_t *)argument;
    if (!st || !st->desktop) {
        calculator_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st) +
                         (uint32_t)(CALC_BUTTONS * sizeof(gui_widget_t)));
    st->window = bk_gui_create_window(st->desktop, 170, 55, 238, 258,
                                           "Calculadora");
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "Calculadora", "Version 1.0", "Calculadora de BlesKernOS.",
            "Bles.INC (C) 2026", "/ICONS/CALC.BMP"});
        bk_gui_set_window_content(st->window, calculator_content, st);
        bk_gui_set_window_event_handler(st->window, calculator_event, st);
        bk_gui_set_window_min_size(st->window, 238, 258);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
        for (int i = 0; i < CALC_BUTTONS; i++) {
            int col = i % 4;
            int row = i / 4;
            gui_widget_t *button = bk_gui_widget_create(
                st->desktop, st->window, GUI_WIDGET_BUTTON,
                (gui_rect_t){8 + col * 55, 43 + row * 37, 49, 30},
                calc_labels[i], calculator_button);
            if (button) st->button_ids[i] = button->id;
        }
    }

    while (!bk_proc_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        bk_sys_sleep_ticks(4);
    }

    calculator_cleanup(st);
    bk_proc_exit();
}

void calculator_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    calculator_open_from_desktop(desktop);
}
