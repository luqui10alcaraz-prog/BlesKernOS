#include "system_tools_common.h"

#define CLIPBOARD_MIN_API 23U
#define CLIPBOARD_VIEW_CAPACITY 4096U

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *input_box;
    bk_gui_widget_t *set_button;
    bk_gui_widget_t *refresh_button;
    bk_gui_widget_t *clear_button;
    uint32_t set_id;
    uint32_t refresh_id;
    uint32_t clear_id;
    char text[CLIPBOARD_VIEW_CAPACITY];
    uint32_t generation;
    uint32_t scroll_lines;
    char status[96];
} clipboard_state_t;

static clipboard_state_t *g_clipboard_state;

static void clipboard_status(clipboard_state_t *state, const char *text) {
    st_copy(state->status, sizeof(state->status), text);
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void clipboard_refresh(clipboard_state_t *state, bool update_input) {
    uint32_t copied = bk_clipboard_get_text(state->text, sizeof(state->text));
    state->generation = bk_clipboard_generation();
    state->scroll_lines = 0;
    if (update_input)
        bk_gui_widget_set_text(state->input_box, state->text);
    if (copied)
        clipboard_status(state, "Portapapeles actualizado.");
    else
        clipboard_status(state, "El portapapeles esta vacio.");
}

static void clipboard_layout(clipboard_state_t *state, bk_gui_rect_t *panel,
                             bk_gui_rect_t *status) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    *panel = (bk_gui_rect_t){content.x + 14, content.y + 46,
                            content.w - 28, content.h - 92};
    *status = (bk_gui_rect_t){content.x + 14, content.y + content.h - 38,
                             content.w - 28, 18};
    bk_gui_widget_set_bounds(state->window, state->input_box,
        (bk_gui_rect_t){14, 12, content.w - 270, 24});
    bk_gui_widget_set_bounds(state->window, state->set_button,
        (bk_gui_rect_t){content.w - 248, 12, 72, 24});
    bk_gui_widget_set_bounds(state->window, state->refresh_button,
        (bk_gui_rect_t){content.w - 170, 12, 76, 24});
    bk_gui_widget_set_bounds(state->window, state->clear_button,
        (bk_gui_rect_t){content.w - 88, 12, 74, 24});
}

static void clipboard_widget_callback(bk_gui_window_t *window UNUSED,
                                      uint32_t widget_id) {
    clipboard_state_t *state = g_clipboard_state;
    char input[1024];
    if (!state) return;
    if (widget_id == state->set_id) {
        (void)bk_gui_widget_get_text(state->input_box, input, sizeof(input));
        if (bk_clipboard_set_text(input)) {
            clipboard_refresh(state, false);
            clipboard_status(state, "Texto guardado en el portapapeles global.");
        } else {
            clipboard_status(state, "No se pudo guardar el texto.");
        }
    } else if (widget_id == state->refresh_id) {
        clipboard_refresh(state, true);
    } else if (widget_id == state->clear_id) {
        bk_clipboard_clear();
        clipboard_refresh(state, true);
        clipboard_status(state, "Portapapeles vaciado.");
    }
}

static void clipboard_paint(bk_gui_window_t *window UNUSED,
                            bk_gui_surface_t *surface, void *context) {
    clipboard_state_t *state = (clipboard_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t panel;
    bk_gui_rect_t status;
    char generation[48];
    char number[16];
    const char *body;
    uint32_t skip;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    clipboard_layout(state, &panel, &status);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    st_draw_panel(surface, panel, ST_PANEL);
    generation[0] = '\0';
    st_append(generation, sizeof(generation), "Contenido actual - revision ");
    st_u32(number, sizeof(number), state->generation);
    st_append(generation, sizeof(generation), number);
    bk_gui_surface_draw_text(surface, panel.x + 10, panel.y + 10,
                             generation, ST_BLUE, 0, false);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){panel.x + 9, panel.y + 28, panel.w - 18, 1}, ST_SHADOW);
    body = state->text[0] ? state->text : "(vacio)";
    skip = state->scroll_lines;
    while (*body && skip) {
        if (*body == '\n') skip--;
        body++;
    }
    (void)st_draw_wrapped(surface,
        (bk_gui_rect_t){panel.x + 8, panel.y + 34,
                        panel.w - 16, panel.h - 42},
        panel.x + 10, panel.y + 38, body,
        state->text[0] ? ST_TEXT : ST_MUTED, 15);
    bk_gui_surface_draw_text(surface, status.x, status.y + 4,
                             state->status, ST_MUTED, 0, false);
}

static bool clipboard_event(bk_gui_window_t *window UNUSED,
                            const bk_gui_event_t *event, void *context) {
    clipboard_state_t *state = (clipboard_state_t *)context;
    bk_gui_rect_t panel;
    bk_gui_rect_t status;
    if (!state || !event) return false;
    clipboard_layout(state, &panel, &status);
    if (event->type == BK_GUI_EVENT_MOUSE_WHEEL &&
        st_rect_contains(panel, event->x, event->y)) {
        if (event->dy < 0) state->scroll_lines++;
        else if (state->scroll_lines) state->scroll_lines--;
        bk_gui_window_invalidate(state->window);
        return true;
    } else if (event->type == BK_GUI_EVENT_KEY &&
               (uint8_t)event->key == BK_KEY_ENTER &&
               bk_gui_widget_is_focused(state->window, state->input_box)) {
        clipboard_widget_callback(state->window, state->set_id);
        return true;
    }
    (void)status;
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    clipboard_state_t *state;
    if (bk_sys_api_version() < CLIPBOARD_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (clipboard_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->window = bk_gui_create_window(desktop, 100, 70, 600, 390,
                                         "Clipboard Viewer");
    if (!state->window) {
        bk_sys_free(state);
        return;
    }
    g_clipboard_state = state;
    state->input_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 200, 24}, "", 1023,
        clipboard_widget_callback);
    state->set_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 76, 24}, "Guardar", clipboard_widget_callback);
    state->refresh_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 24}, "Actualizar",
        clipboard_widget_callback);
    state->clear_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 78, 24}, "Vaciar", clipboard_widget_callback);
    (void)bk_gui_widget_set_icon(state->set_button, "Paste");
    (void)bk_gui_widget_set_icon(state->refresh_button, "Refresh");
    (void)bk_gui_widget_set_icon(state->clear_button, "Delete");
    state->set_id = bk_gui_widget_id(state->set_button);
    state->refresh_id = bk_gui_widget_id(state->refresh_button);
    state->clear_id = bk_gui_widget_id(state->clear_button);
    bk_gui_set_window_content(state->window, clipboard_paint, state);
    bk_gui_set_window_event_handler(state->window, clipboard_event, state);
    bk_gui_set_window_min_size(state->window, 500, 320);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());
    clipboard_refresh(state, true);

    while (bk_gui_window_is_open(state->window)) {
        uint32_t generation = bk_clipboard_generation();
        if (generation != state->generation) clipboard_refresh(state, false);
        bk_sys_sleep_ms(25);
    }
    if (g_clipboard_state == state) g_clipboard_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    bk_sys_free(state);
}
