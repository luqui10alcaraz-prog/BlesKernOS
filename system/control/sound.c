#include "kernel/include/api.h"
#include "control_common.h"

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    char status[80];
    bool startup_enabled;
    uint32_t startup_button_id;
} sound_cpl_state_t;

static void sound_startup_toggle(gui_window_t *window, uint32_t id UNUSED) {
    sound_cpl_state_t *st = window
        ? (sound_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    st->startup_enabled = !st->startup_enabled;
    if (!startup_sound_set_enabled(st->startup_enabled)) {
        st->startup_enabled = !st->startup_enabled;
        bk_runtime_strcpy(st->status, "No se pudo guardar SOUND.INI");
    } else {
        bk_runtime_strcpy(st->status, st->startup_enabled
            ? "Sonido de inicio activado" : "Sonido de inicio desactivado");
    }
    for (gui_widget_t *widget = window->widgets; widget; widget = widget->next) {
        if (widget->id != st->startup_button_id) continue;
        bk_runtime_strncpy(widget->text, st->startup_enabled ? "Encendido" : "Apagado",
                 sizeof(widget->text) - 1U);
        widget->text[sizeof(widget->text) - 1U] = '\0';
        break;
    }
    window->dirty = true;
}

static void sound_test(gui_window_t *window, uint32_t id UNUSED) {
    sound_cpl_state_t *st = window ? (sound_cpl_state_t *)window->content_context : NULL;
    bool ok = bk_sound_tone(523, 350);
    if (st) snprintf(st->status, sizeof(st->status), "%s",
                     ok ? "Tono de prueba iniciado" : "No hay salida de tono disponible");
    if (window) window->dirty = true;
}

static void sound_stop_test(gui_window_t *window, uint32_t id UNUSED) {
    sound_cpl_state_t *st = window ? (sound_cpl_state_t *)window->content_context : NULL;
    bk_sound_stop();
    if (st) snprintf(st->status, sizeof(st->status), "Reproduccion detenida");
    if (window) window->dirty = true;
}

static void sound_paint(gui_window_t *window UNUSED, gui_surface_t *s,
                        void *context) {
    sound_cpl_state_t *st = (sound_cpl_state_t *)context;
    int x = st->window->bounds.x + 18;
    int y = bk_gui_window_content_rect_raw(st->window).y + 18;
    cpl_draw_group(s, (gui_rect_t){x, y, 380, 125}, "Dispositivos de sonido");
    bk_gui_font_draw_string(s, x + 16, y + 24, "Salida PCM:", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 112, y + 24, bk_sound_pcm_name(),
                         0x00006020, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 48,
        bk_sound_pcm_name() &&
        bk_runtime_strncmp(bk_sound_pcm_name(), "Intel AC'97", 11) == 0
            ? "Controlador Intel AC'97 detectado"
            : (bk_sound_has_sb16() ? "Sound Blaster 16 detectada"
                                   : "Sound Blaster 16 no detectada"),
        CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 70,
        bk_sound_pcm_available() ? "PCM disponible" : "Tonos mediante PC Speaker",
        CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 94,
        "BlesKernOS 0.6 no posee volumen maestro global.", CPL_SHADOW, 0, false);
    cpl_draw_group(s, (gui_rect_t){x, y + 136, 380, 92}, "Sonido de inicio");
    bk_gui_font_draw_string(s, x + 16, y + 161, "Estado:", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 88, y + 161,
        st->startup_enabled ? "Encendido" : "Apagado",
        st->startup_enabled ? 0x00006020 : 0x00802020, 0, false);
    bk_gui_font_draw_string_clipped(s, x + 16, y + 186,
        BK_STARTUP_SOUND_PATH, CPL_SHADOW,
        (gui_rect_t){x + 12, y + 180, 350, 18});
    bk_gui_font_draw_string_clipped(s, x + 8,
        st->window->bounds.y + st->window->bounds.h - 18,
        st->status[0] ? st->status : "Use Probar para verificar la salida.",
        CPL_TEXT, (gui_rect_t){x + 8,
        st->window->bounds.y + st->window->bounds.h - 18, 370, 12});
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    sound_cpl_state_t *st;
    if (!desktop) return;
    st = (sound_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->startup_enabled = startup_sound_enabled();
    st->window = bk_gui_create_window(desktop, 105, 48, 416, 350, "Sonido");
    if (!st->window) { bk_sys_free(st); return; }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Sonido", "Version 1.0", "Configuracion y prueba de sonido.",
        "Bles.INC (C) 2026", "/ICONS/SOUND.BMP"});
    bk_gui_set_window_content(st->window, sound_paint, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);
    {
        gui_widget_t *button = bk_gui_widget_create(desktop, st->window,
            GUI_WIDGET_BUTTON, (gui_rect_t){28, 255, 94, 23},
            st->startup_enabled ? "Encendido" : "Apagado",
            sound_startup_toggle);
        if (button) st->startup_button_id = button->id;
    }
    bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){132, 255, 80, 23}, "Probar", sound_test);
    bk_gui_widget_create(desktop, st->window, GUI_WIDGET_BUTTON,
        (gui_rect_t){220, 255, 80, 23}, "Detener", sound_stop_test);
    while (!bk_proc_exit_requested() && st->window->listed) bk_sys_sleep_ticks(2);
    bk_sound_stop();
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
