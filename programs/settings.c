#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/sound.h"
#include "../kernel/include/vfs.h"

#define SETTINGS_MAX_MODES 8

enum { PAGE_WALLPAPER, PAGE_DISPLAY, PAGE_SOUND, PAGE_SCREENSAVER, SETTINGS_PAGE_COUNT };

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t page_ids[SETTINGS_PAGE_COUNT];
    uint32_t load_bmp_id;
    uint32_t saver_enable_id;
    uint32_t saver_disable_id;
    uint32_t saver_timeout_1_id;
    uint32_t saver_timeout_5_id;
    uint32_t saver_timeout_10_id;
    uint32_t saver_logo_id;
    uint32_t saver_pipes_id;
    uint32_t saver_preview_id;
    uint32_t mode_ids[SETTINGS_MAX_MODES];
    gfx_display_mode_t modes[SETTINGS_MAX_MODES];
    uint32_t mode_count;
    int page;
    bool live_modes;
    char message[96];
} settings_state_t;

static settings_state_t *g_settings;

static void settings_append_char(char *text, size_t capacity, size_t *length,
                                 char c) {
    if (!text || !length || *length + 1 >= capacity) return;
    text[*length] = c;
    (*length)++;
    text[*length] = '\0';
}

static void settings_append_uint(char *text, size_t capacity, size_t *length,
                                 uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (!text || !length) return;
    if (value == 0) {
        settings_append_char(text, capacity, length, '0');
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        settings_append_char(text, capacity, length, digits[--count]);
    }
}

static void settings_format_mode(char *text, size_t capacity,
                                 uint16_t width, uint16_t height,
                                 uint8_t bpp, bool with_bpp) {
    size_t len = 0;

    if (!text || capacity == 0) return;
    text[0] = '\0';
    settings_append_uint(text, capacity, &len, width);
    settings_append_char(text, capacity, &len, 'x');
    settings_append_uint(text, capacity, &len, height);
    if (with_bpp) {
        settings_append_char(text, capacity, &len, 'x');
        settings_append_uint(text, capacity, &len, bpp);
    }
}

static void settings_save_mode(const gfx_display_mode_t *mode) {
    char text[24];

    if (!mode) return;
    settings_format_mode(text, sizeof(text), mode->width, mode->height,
                         mode->bpp, true);
    (void)vfs_write_all("/VIDEO.INI", text, (uint32_t)kstrlen(text));
}

static void settings_load_modes(settings_state_t *st) {
    const gfx_info_t *info = gfx_get_info();
    bool found_current = false;

    if (!st) return;
    st->mode_count = 0;
    st->live_modes = gfx_list_display_modes(st->modes, SETTINGS_MAX_MODES,
                                            &st->mode_count);
    if (!info) return;

    for (uint32_t i = 0; i < st->mode_count; i++) {
        if (st->modes[i].width == info->width &&
            st->modes[i].height == info->height &&
            st->modes[i].bpp == info->bpp) {
            found_current = true;
            break;
        }
    }
    if (!found_current && st->mode_count < SETTINGS_MAX_MODES) {
        st->modes[st->mode_count].width = info->width;
        st->modes[st->mode_count].height = info->height;
        st->modes[st->mode_count].bpp = info->bpp;
        st->mode_count++;
    }
}

static void settings_update_controls(settings_state_t *st) {
    gui_widget_t *widget;

    if (!st || !st->window) return;
    widget = st->window->widgets;
    while (widget) {
        if (widget->id == st->load_bmp_id)
            widget->visible = st->page == PAGE_WALLPAPER;
        for (uint32_t i = 0; i < SETTINGS_MAX_MODES; i++) {
            if (widget->id == st->mode_ids[i]) {
                widget->visible = st->page == PAGE_DISPLAY &&
                                  i < st->mode_count;
            }
        }
        if (widget->id == st->saver_enable_id ||
            widget->id == st->saver_disable_id ||
            widget->id == st->saver_timeout_1_id ||
            widget->id == st->saver_timeout_5_id ||
            widget->id == st->saver_timeout_10_id ||
            widget->id == st->saver_logo_id ||
            widget->id == st->saver_pipes_id ||
            widget->id == st->saver_preview_id) {
            widget->visible = st->page == PAGE_SCREENSAVER;
        }
        widget = widget->next;
    }
}

static void settings_page(gui_window_t *window, uint32_t id) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    if (!st) return;
    for (int i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        if (st->page_ids[i] == id) st->page = i;
    }
    st->message[0] = '\0';
    settings_update_controls(st);
    window->dirty = true;
}

static void settings_load_bmp(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    if (!st) return;
    if (deskmanager_set_wallpaper("/WALLPAPR.BMP"))
        kstrcpy(st->message, "Fondo /WALLPAPR.BMP cargado");
    else
        kstrcpy(st->message, "Copia WALLPAPR.BMP en la raiz");
    window->dirty = true;
}

static void settings_mode(gui_window_t *window, uint32_t id) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    char label[24];

    if (!st) return;
    for (uint32_t i = 0; i < st->mode_count; i++) {
        if (st->mode_ids[i] != id) continue;
        settings_save_mode(&st->modes[i]);
        settings_format_mode(label, sizeof(label), st->modes[i].width,
                             st->modes[i].height, st->modes[i].bpp, true);
        if (gui_change_resolution(st->desktop, st->modes[i].width,
                                  st->modes[i].height)) {
            kstrcpy(st->message, "Resolucion aplicada: ");
            kstrncpy(st->message + kstrlen(st->message), label,
                     sizeof(st->message) - kstrlen(st->message) - 1);
            st->message[sizeof(st->message) - 1] = '\0';
            settings_load_modes(st);
            settings_update_controls(st);
        } else {
            kstrcpy(st->message,
                    "No se pudo aplicar en vivo; quedo guardada");
        }
        window->dirty = true;
        return;
    }
}

static void settings_saver_enable(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    screensaver_set_enabled(true);
    if (st) kstrcpy(st->message, "Protector activado");
    if (window) window->dirty = true;
}

static void settings_saver_disable(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    screensaver_set_enabled(false);
    if (st) kstrcpy(st->message, "Protector desactivado");
    if (window) window->dirty = true;
}

static void settings_saver_timeout(gui_window_t *window, uint32_t id) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    uint32_t seconds = 300;

    if (!st) return;
    if (id == st->saver_timeout_1_id) seconds = 60;
    else if (id == st->saver_timeout_5_id) seconds = 300;
    else if (id == st->saver_timeout_10_id) seconds = 600;

    screensaver_set_timeout_seconds(seconds);
    if (seconds == 60) kstrcpy(st->message, "Guardado: 1 min");
    else if (seconds == 300) kstrcpy(st->message, "Guardado: 5 min");
    else if (seconds == 600) kstrcpy(st->message, "Guardado: 10 min");
    else kstrcpy(st->message, "Guardado");
    if (window) window->dirty = true;
}

static bool settings_path_contains(const char *path, const char *needle) {
    if (!path || !needle) return false;
    while (*path) {
        const char *a = path;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b) return true;
        path++;
    }
    return false;
}

static void settings_saver_logo(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    screensaver_set_path("/PROGRAMS/SSLOGO.O");
    if (st) kstrcpy(st->message, "Protector: Logo");
    if (window) window->dirty = true;
}

static void settings_saver_pipes(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    screensaver_set_path("/PROGRAMS/SSPIPES.O");
    if (st) kstrcpy(st->message, "Protector: Pipes 3D");
    if (window) window->dirty = true;
}

static void settings_saver_preview(gui_window_t *window, uint32_t id UNUSED) {
    settings_state_t *st = window
                         ? (settings_state_t *)window->content_context : NULL;
    if (!st) return;
    if (screensaver_preview(st->desktop))
        kstrcpy(st->message, "Vista previa iniciada");
    else
        kstrcpy(st->message, "No se pudo iniciar el protector");
    if (window) window->dirty = true;
}

static uint32_t spectrum_color(int x, int y, int w, int h) {
    int segment = (x * 6) / w;
    int local = (x * 6 * 255 / w) % 255;
    int r = 0, g = 0, b = 0;
    if (segment == 0) {
        r = 255;
        g = local;
    } else if (segment == 1) {
        r = 255 - local;
        g = 255;
    } else if (segment == 2) {
        g = 255;
        b = local;
    } else if (segment == 3) {
        g = 255 - local;
        b = 255;
    } else if (segment == 4) {
        r = local;
        b = 255;
    } else {
        r = 255;
        b = 255 - local;
    }
    r = r * (h - y) / h;
    g = g * (h - y) / h;
    b = b * (h - y) / h;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void settings_draw(gui_window_t *window UNUSED, gui_surface_t *s,
                          void *ctx) {
    settings_state_t *st = (settings_state_t *)ctx;
    int wx = st->window->bounds.x;
    int wy = st->window->bounds.y + GUI_TITLEBAR_HEIGHT;
    int px = wx + 126;
    int py = wy + 12;


    if (st->page == PAGE_WALLPAPER) {
        gui_font_draw_string(s, px, py, "Fondo de pantalla",
                             0x00101010, 0, false);
        for (int y = 0; y < 72; y++) {
            for (int x = 0; x < 240; x++) {
                gui_gfx_putpixel(s, px + x, py + 24 + y,
                                 spectrum_color(x, y, 240, 72));
            }
        }
        gui_gfx_draw_rect(s, (gui_rect_t){px, py + 24, 240, 72}, 0x00202020);
        gui_font_draw_string(s, px, py + 105,
            "Haz clic en un color o carga un BMP.", 0x00303030, 0, false);
    } else if (st->page == PAGE_DISPLAY) {
        const gfx_info_t *info = gfx_get_info();
        char current[32];

        settings_format_mode(current, sizeof(current),
                             info ? info->width : 0,
                             info ? info->height : 0,
                             info ? info->bpp : 0, true);
        gui_font_draw_string(s, px, py, "Pantalla", 0x00101010, 0, false);
        gui_font_draw_string(s, px, py + 24, "Actual:", 0x00303030, 0, false);
        gui_font_draw_string(s, px + 60, py + 24, current,
                             0x00106020, 0, false);
        gui_font_draw_string(s, px, py + 48,
            st->live_modes ? "Haz clic en un modo para aplicarlo en vivo."
                           : "Cambio en vivo no detectado en este video.",
            0x00303030, 0, false);
        gui_font_draw_string(s, px, py + 68, "Modos detectados:",
                             0x00303030, 0, false);
    } else if (st->page == PAGE_SOUND) {
        gui_font_draw_string(s, px, py, "Sonido disponible",
                             0x00101010, 0, false);
        gui_font_draw_string(s, px, py + 28, "Salida PCM:",
                             0x00303030, 0, false);
        gui_font_draw_string(s, px + 95, py + 28, sound_pcm_name(),
                             0x00106020, 0, false);
        gui_font_draw_string(s, px, py + 50,
            sound_has_sb16() ? "Sound Blaster 16 detectada" :
                               "PC Speaker (SB16 no detectada)",
            0x00303030, 0, false);
        gui_font_draw_string(s, px, py + 80,
            "Panel informativo por ahora.", 0x00606060, 0, false);
    } else if (st->page == PAGE_SCREENSAVER) {
        char timeout_text[32];
        size_t len = 0;
        uint32_t seconds = screensaver_get_timeout_seconds();

        timeout_text[0] = '\0';
        settings_append_uint(timeout_text, sizeof(timeout_text), &len,
                             seconds / 60);
        settings_append_char(timeout_text, sizeof(timeout_text), &len, ' ');
        settings_append_char(timeout_text, sizeof(timeout_text), &len, 'm');
        settings_append_char(timeout_text, sizeof(timeout_text), &len, 'i');
        settings_append_char(timeout_text, sizeof(timeout_text), &len, 'n');

        gui_font_draw_string(s, px, py, "Protector de pantalla",
                             0x00101010, 0, false);
        gui_font_draw_string(s, px, py + 28, "Estado:",
                             0x00303030, 0, false);
        gui_font_draw_string(s, px + 70, py + 28,
                             screensaver_is_enabled() ? "activado" : "desactivado",
                             screensaver_is_enabled() ? 0x00106020 : 0x00602020,
                             0, false);
        gui_font_draw_string(s, px, py + 52, "Tiempo:",
                             0x00303030, 0, false);
        gui_font_draw_string(s, px + 70, py + 52, timeout_text,
                             0x00106020, 0, false);
        gui_font_draw_string(s, px, py + 76, "Actual:",
                             0x00303030, 0, false);
        gui_font_draw_string(s, px + 70, py + 76,
                             settings_path_contains(screensaver_get_path(), "SSPIPES")
                                 ? "Pipes 3D" : "Logo",
                             0x00106020, 0, false);
    }

    if (st->message[0]) {
        int msg_y = st->window->bounds.y + st->window->bounds.h - 28;
        int msg_w = st->window->bounds.w - 142;
        if (msg_w < 40) msg_w = 40;
        gui_font_draw_string_clipped(s, px, msg_y,
            st->message, 0x00602020,
            (gui_rect_t){px, msg_y - 2, msg_w, 18});
    }
}

static bool settings_event(gui_window_t *window UNUSED, const gui_event_t *e,
                           void *ctx) {
    settings_state_t *st = (settings_state_t *)ctx;
    int px = st->window->bounds.x + 126;
    int py = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 36;

    if (st->page == PAGE_WALLPAPER && e->type == GUI_EVENT_MOUSE_DOWN &&
        e->x >= px && e->x < px + 240 && e->y >= py && e->y < py + 72) {
        deskmanager_set_background(spectrum_color(e->x - px, e->y - py,
                                                  240, 72));
        st->window->dirty = true;
        return true;
    }
    return false;
}

static void settings_cleanup(settings_state_t *st) {
    if (!st) return;
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
    }
    if (g_settings == st) g_settings = NULL;
    kfree(st);
}

static void settings_main(void *arg) {
    settings_state_t *st = (settings_state_t *)arg;
    static const char *pages[SETTINGS_PAGE_COUNT] = {"Fondo", "Pantalla", "Sonido", "Protector"};

    if (!st) {
        task_exit();
        return;
    }
    settings_load_modes(st);
    st->window = gui_desktop_create_window(st->desktop, 70, 35, 468, 335,
                                           "Configuracion");
    if (st->window) {
        gui_window_set_min_size(st->window, 430, 265);
        gui_window_set_content(st->window, settings_draw, st);
        gui_window_set_event_handler(st->window, settings_event, st);
        st->window->owner_pid = task_current_pid();
        task_bind_window(st->window);

        for (int i = 0; i < SETTINGS_PAGE_COUNT; i++) {
            gui_widget_t *button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){38, 6 + i * 40, 82, 28},
                pages[i], settings_page);
            if (button) st->page_ids[i] = button->id;
        }

        {
            gui_widget_t *button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){126, 138, 110, 24},
                "Cargar BMP", settings_load_bmp);
            if (button) st->load_bmp_id = button->id;
        }

        for (uint32_t i = 0; i < st->mode_count && i < SETTINGS_MAX_MODES; i++) {
            char label[20];
            int col = (int)(i % 2);
            int row = (int)(i / 2);
            gui_widget_t *button;

            settings_format_mode(label, sizeof(label), st->modes[i].width,
                                 st->modes[i].height, 0, false);
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON,
                (gui_rect_t){126 + col * 116, 88 + row * 32, 108, 24},
                label, settings_mode);
            if (button) st->mode_ids[i] = button->id;
        }

        {
            gui_widget_t *button;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){126, 88, 82, 24},
                "Activar", settings_saver_enable);
            if (button) st->saver_enable_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){216, 88, 96, 24},
                "Desactivar", settings_saver_disable);
            if (button) st->saver_disable_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){126, 122, 64, 24},
                "1 min", settings_saver_timeout);
            if (button) st->saver_timeout_1_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){198, 122, 64, 24},
                "5 min", settings_saver_timeout);
            if (button) st->saver_timeout_5_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){270, 122, 70, 24},
                "10 min", settings_saver_timeout);
            if (button) st->saver_timeout_10_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){126, 156, 82, 24},
                "Logo", settings_saver_logo);
            if (button) st->saver_logo_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){216, 156, 96, 24},
                "Pipes 3D", settings_saver_pipes);
            if (button) st->saver_pipes_id = button->id;
            button = gui_widget_create(st->desktop, st->window,
                GUI_WIDGET_BUTTON, (gui_rect_t){126, 190, 110, 24},
                "Vista previa", settings_saver_preview);
            if (button) st->saver_preview_id = button->id;
        }
        settings_update_controls(st);
    }

    while (!task_exit_requested() && st->window && st->window->listed) {
        task_sleep(4);
    }
    settings_cleanup(st);
    task_exit();
}

void settings_open_from_desktop(gui_desktop_t *desktop) {
    settings_state_t *st;

    if (!desktop) return;
    st = (settings_state_t *)kzalloc(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_settings = st;
    if (task_create("settings", settings_main, st) < 0) settings_cleanup(st);
}

void settings_install(gui_desktop_t *desktop UNUSED) {}
