#include "kernel/include/api.h"
#include "control_common.h"

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t button_ids[3];
    char status[96];
    uint32_t seen_generation;
} language_cpl_state_t;

static const char *language_name(const char *code) {
    if (!code) return "";
    if (bk_runtime_strcmp(code, "EN") == 0) return bk_lang_get("LANGUAGE.ENGLISH");
    if (bk_runtime_strcmp(code, "IT") == 0) return bk_lang_get("LANGUAGE.ITALIAN");
    return bk_lang_get("LANGUAGE.SPANISH");
}

static void language_choose(gui_window_t *window, uint32_t id) {
    language_cpl_state_t *st = window
        ? (language_cpl_state_t *)window->content_context : NULL;
    const char *code = "ES";
    if (!st) return;
    if (id == st->button_ids[1]) code = "EN";
    else if (id == st->button_ids[2]) code = "IT";
    if (bk_lang_set(code)) {
        snprintf(st->status, sizeof(st->status),
                 bk_lang_get("LANGUAGE.CHANGED"), language_name(code));
        st->seen_generation = bk_lang_generation();
    } else {
        bk_runtime_strncpy(st->status, bk_lang_get("LANGUAGE.ERROR"),
                           sizeof(st->status) - 1U);
        st->status[sizeof(st->status) - 1U] = '\0';
    }
    window->dirty = true;
}

static void language_paint(gui_window_t *window UNUSED, gui_surface_t *surface,
                           void *context) {
    language_cpl_state_t *st = (language_cpl_state_t *)context;
    gui_rect_t client = bk_gui_window_content_rect_raw(st->window);
    int x = client.x + 18;
    int y = client.y + 18;
    char current[96];

    cpl_draw_group(surface, (gui_rect_t){x, y, client.w - 36, 108},
                   bk_lang_get("LANGUAGE.GROUP"));
    bk_gui_font_draw_string(surface, x + 16, y + 22,
        bk_lang_get("LANGUAGE.DESCRIPTION"), CPL_TEXT, 0, false);
    snprintf(current, sizeof(current), bk_lang_get("LANGUAGE.CURRENT"),
             language_name(bk_lang_current()));
    bk_gui_font_draw_string(surface, x + 16, y + 48, current,
                            CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, x + 16, y + 72,
        bk_lang_get("LANGUAGE.LIVE_NOTE"), CPL_SHADOW, 0, false);

    cpl_draw_group(surface, (gui_rect_t){x, y + 128, client.w - 36, 76},
                   bk_lang_get("LANGUAGE.FILES_GROUP"));
    bk_gui_font_draw_string(surface, x + 16, y + 151,
        bk_lang_get("LANGUAGE.FILES_PATH"), CPL_TEXT, 0, false);
    bk_gui_font_draw_string(surface, x + 16, y + 174,
        bk_lang_get("LANGUAGE.RESTART_NOTE"), CPL_SHADOW, 0, false);

    bk_gui_font_draw_string_clipped(surface, client.x + 10,
        client.y + client.h - 18,
        st->status[0] ? st->status : bk_lang_get("COMMON.READY"),
        CPL_TEXT, (gui_rect_t){client.x + 10, client.y + client.h - 20,
                              client.w - 20, 16});
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    language_cpl_state_t *st;
    gui_widget_t *button;
    if (!desktop) return;
    st = (language_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->seen_generation = bk_lang_generation();
    st->window = bk_gui_create_window(desktop, 112, 70, 454, 326,
                                      "@LANGUAGE.TITLE");
    if (!st->window) { bk_sys_free(st); return; }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        bk_lang_get("LANGUAGE.TITLE"), bk_lang_get("COMMON.VERSION_1"),
        bk_lang_get("LANGUAGE.ABOUT"), "@H7A28E1E5",
        "/ICONS/LANGUAGE.BMP"});
    bk_gui_set_window_content(st->window, language_paint, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    button = bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){22, 100, 112, 24}, "@LANGUAGE.SPANISH",
        language_choose);
    if (button) st->button_ids[0] = button->id;
    button = bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){142, 100, 112, 24}, "@LANGUAGE.ENGLISH",
        language_choose);
    if (button) st->button_ids[1] = button->id;
    button = bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){262, 100, 112, 24}, "@LANGUAGE.ITALIAN",
        language_choose);
    if (button) st->button_ids[2] = button->id;

    while (!bk_proc_exit_requested() && st->window->listed) {
        uint32_t generation = bk_lang_generation();
        if (generation != st->seen_generation) {
            st->seen_generation = generation;
            if (st->window) st->window->dirty = true;
        }
        bk_sys_sleep_ticks(2);
    }
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
