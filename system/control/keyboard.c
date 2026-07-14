#include "kernel/include/api.h"
#include "control_common.h"

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t layout_ids[2];
    char layout[4];
    char typed[48];
    uint32_t typed_len;
    uint8_t last_key;
    uint32_t event_count;
    char status[80];
} keyboard_cpl_state_t;

static void keyboard_layout(gui_window_t *window, uint32_t id) {
    keyboard_cpl_state_t *st = window ? (keyboard_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    if (id == st->layout_ids[0]) bk_runtime_strcpy(st->layout, "ES");
    else bk_runtime_strcpy(st->layout, "US");
    cpl_write_text("/KEYBD.INI", id == st->layout_ids[0]
        ? "layout=ES\r\n" : "layout=US\r\n");
    snprintf(st->status, sizeof(st->status), "Distribucion %s guardada", st->layout);
    window->dirty = true;
}

static bool keyboard_event(gui_window_t *window UNUSED, const gui_event_t *event,
                           void *context) {
    keyboard_cpl_state_t *st = (keyboard_cpl_state_t *)context;
    if (!st || event->type != GUI_EVENT_KEY) return false;
    st->last_key = event->key;
    st->event_count++;
    if (event->key == KEY_BACKSPACE && st->typed_len) {
        st->typed[--st->typed_len] = '\0';
    } else if (event->key >= 32 && event->key < 127 &&
               st->typed_len + 1 < sizeof(st->typed)) {
        st->typed[st->typed_len++] = (char)event->key;
        st->typed[st->typed_len] = '\0';
    }
    st->window->dirty = true;
    return true;
}

static void keyboard_paint(gui_window_t *window UNUSED, gui_surface_t *s,
                           void *context) {
    keyboard_cpl_state_t *st = (keyboard_cpl_state_t *)context;
    int x = st->window->bounds.x + 16;
    int y = bk_gui_window_content_rect_raw(st->window).y + 16;
    char line[80];
    cpl_draw_group(s, (gui_rect_t){x, y, 390, 128}, "Prueba del teclado");
    bk_gui_font_draw_string(s, x + 14, y + 22, "Escriba aqui:", CPL_TEXT, 0, false);
    cpl_draw_bevel(s, (gui_rect_t){x + 14, y + 42, 350, 28}, CPL_WHITE, true);
    bk_gui_font_draw_string_clipped(s, x + 20, y + 50,
        st->typed[0] ? st->typed : "_", CPL_TEXT,
        (gui_rect_t){x + 20, y + 48, 338, 16});
    snprintf(line, sizeof(line), "Ultima tecla: 0x%x   Eventos: %u",
             st->last_key, st->event_count);
    bk_gui_font_draw_string(s, x + 14, y + 84, line, CPL_TEXT, 0, false);
    cpl_draw_group(s, (gui_rect_t){x, y + 144, 390, 70}, "Distribucion preferida");
    snprintf(line, sizeof(line), "Seleccion actual: %s", st->layout);
    bk_gui_font_draw_string(s, x + 14, y + 164, line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 14, y + 188,
        "La distribucion del driver 0.6 permanece US.", CPL_SHADOW, 0, false);
    bk_gui_font_draw_string_clipped(s, x + 8,
        st->window->bounds.y + st->window->bounds.h - 18,
        st->status[0] ? st->status : "La preferencia se guarda en /KEYBD.INI.",
        CPL_TEXT, (gui_rect_t){x + 8,
        st->window->bounds.y + st->window->bounds.h - 18, 380, 12});
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    keyboard_cpl_state_t *st;
    gui_widget_t *button;
    if (!desktop) return;
    st = (keyboard_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    bk_runtime_strcpy(st->layout, "US");
    st->window = bk_gui_create_window(desktop, 102, 62, 422, 310, "Teclado");
    if (!st->window) { bk_sys_free(st); return; }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Teclado", "Version 1.0", "Configuracion y prueba del teclado.",
        "Bles.INC (C) 2026", "/ICONS/KEYBOARD.BMP"});
    bk_gui_set_window_content(st->window, keyboard_paint, st);
    bk_gui_set_window_event_handler(st->window, keyboard_event, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);
    button = bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){190, 180, 66, 23}, "Espanol", keyboard_layout);
    if (button) st->layout_ids[0] = button->id;
    button = bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){264, 180, 66, 23}, "US", keyboard_layout);
    if (button) st->layout_ids[1] = button->id;
    while (!bk_proc_exit_requested() && st->window->listed) bk_sys_sleep_ticks(2);
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
