#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"
#include "../kernel/include/vfs.h"

#define RUNBOX_W 390
#define RUNBOX_H 150
#define RUNBOX_INPUT_MAX 128
#define RUNBOX_STATUS_MAX 96

#define RUNBOX_BTN_W 72
#define RUNBOX_BTN_H 22

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    char input[RUNBOX_INPUT_MAX];
    char status[RUNBOX_STATUS_MAX];
    bool focused;
} runbox_state_t;

static runbox_state_t *g_runbox;

static void runbox_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void runbox_append(char *dst, size_t dst_len, const char *src) {
    size_t len;
    if (!dst || !dst_len || !src) return;
    len = kstrlen(dst);
    if (len >= dst_len) return;
    kstrncpy(dst + len, src, dst_len - len - 1);
    dst[dst_len - 1] = '\0';
}

static bool runbox_has_char(const char *s, char c) {
    if (!s) return false;
    while (*s) {
        if (*s == c) return true;
        s++;
    }
    return false;
}

static char runbox_upper_char(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static void runbox_append_upper(char *dst, size_t dst_len, const char *src) {
    size_t len;
    if (!dst || !dst_len || !src) return;
    len = kstrlen(dst);
    while (*src && len + 1 < dst_len) {
        dst[len++] = runbox_upper_char(*src++);
    }
    dst[len] = '\0';
}

static const char *runbox_extension(const char *path) {
    const char *dot = NULL;
    if (!path) return "";
    while (*path) {
        if (*path == '.') dot = path;
        path++;
    }
    return dot ? dot : "";
}

static bool runbox_ext_is(const char *ext, const char *a, const char *b) {
    return kstrcmp(ext, a) == 0 || kstrcmp(ext, b) == 0;
}

static void runbox_make_path(const char *input, char *out, size_t out_len) {
    if (!out || !out_len) return;
    out[0] = '\0';
    if (!input) return;

    while (*input == ' ') input++;

    if (*input == '/') {
        runbox_copy(out, out_len, input);
        return;
    }

    runbox_copy(out, out_len, "/PROGRAMS/");
    if (runbox_has_char(input, '.')) {
        runbox_append_upper(out, out_len, input);
    } else {
        runbox_append_upper(out, out_len, input);
        runbox_append(out, out_len, ".O");
    }
}

static void runbox_set_status_path(runbox_state_t *st,
                                   const char *prefix,
                                   const char *path) {
    if (!st) return;
    runbox_copy(st->status, sizeof(st->status), prefix ? prefix : "");
    runbox_append(st->status, sizeof(st->status), path ? path : "");
    if (st->window) st->window->dirty = true;
}

static bool runbox_path_is_dir(const char *path) {
    vfs_dir_entry_t dummy[1];
    uint32_t count = 0;
    if (!path || !*path) return false;
    return vfs_listdir(path, dummy, 1, &count);
}

static void runbox_close(runbox_state_t *st) {
    if (!st) return;
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
        st->window = NULL;
    }
    if (g_runbox == st) g_runbox = NULL;
    kfree(st);
    gui_request_paint();
    task_exit();
}

static void runbox_execute(runbox_state_t *st) {
    char path[VFS_MAX_PATH];
    const char *ext;

    if (!st) return;
    if (!st->input[0]) {
        runbox_copy(st->status, sizeof(st->status), "Escribi una ruta o programa");
        if (st->window) st->window->dirty = true;
        return;
    }

    runbox_make_path(st->input, path, sizeof(path));
    ext = runbox_extension(path);

    if (runbox_path_is_dir(path)) {
        filebrowser_open_path(st->desktop, path);
        runbox_set_status_path(st, "Abriendo carpeta: ", path);
        return;
    }

    if (program_is_object(path)) {
        if (program_execute_path(st->desktop, path)) {
            runbox_set_status_path(st, "Ejecutado: ", path);
            return;
        }
        runbox_set_status_path(st, "No se pudo ejecutar: ", path);
        return;
    }

    if (runbox_ext_is(ext, ".BMP", ".bmp") ||
        runbox_ext_is(ext, ".GIF", ".gif")) {
        imageviewer_open(st->desktop, path);
        runbox_set_status_path(st, "Abriendo imagen: ", path);
        return;
    }

    if (runbox_ext_is(ext, ".TXT", ".txt") ||
        runbox_ext_is(ext, ".INI", ".ini") ||
        runbox_ext_is(ext, ".MD", ".md") ||
        runbox_ext_is(ext, ".C", ".c") ||
        runbox_ext_is(ext, ".H", ".h") ||
        runbox_ext_is(ext, ".ASM", ".asm")) {
        texteditor_open(st->desktop, path);
        runbox_set_status_path(st, "Abriendo texto: ", path);
        return;
    }

    /* Ultimo recurso: intentar abrir como texto. Si no existe, el editor deberia avisar. */
    texteditor_open(st->desktop, path);
    runbox_set_status_path(st, "Intentando abrir: ", path);
}

static gui_rect_t runbox_input_rect(runbox_state_t *st) {
    int x = st->window->bounds.x + 18;
    int y = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 44;
    return (gui_rect_t){x, y, st->window->bounds.w - 36, 22};
}

static gui_rect_t runbox_ok_rect(runbox_state_t *st) {
    int y = st->window->bounds.y + st->window->bounds.h - 34;
    return (gui_rect_t){st->window->bounds.x + st->window->bounds.w - 168,
                        y, RUNBOX_BTN_W, RUNBOX_BTN_H};
}

static gui_rect_t runbox_cancel_rect(runbox_state_t *st) {
    int y = st->window->bounds.y + st->window->bounds.h - 34;
    return (gui_rect_t){st->window->bounds.x + st->window->bounds.w - 88,
                        y, RUNBOX_BTN_W, RUNBOX_BTN_H};
}

static void runbox_draw_button(gui_surface_t *s, gui_rect_t r,
                               const char *text) {
    gui_gfx_fill_rect(s, r, 0x00D8D8D0);
    gui_gfx_fill_rect(s, (gui_rect_t){r.x, r.y, r.w, 1}, 0x00FFFFFF);
    gui_gfx_fill_rect(s, (gui_rect_t){r.x, r.y, 1, r.h}, 0x00FFFFFF);
    gui_gfx_fill_rect(s, (gui_rect_t){r.x + r.w - 1, r.y, 1, r.h}, 0x00484840);
    gui_gfx_fill_rect(s, (gui_rect_t){r.x, r.y + r.h - 1, r.w, 1}, 0x00484840);
    gui_font_draw_string(s, r.x + 12, r.y + 7, text, 0x00202020, 0, false);
}

static void runbox_content(gui_window_t *window, gui_surface_t *s,
                           void *ctx) {
    runbox_state_t *st = (runbox_state_t *)ctx;
    gui_rect_t input;
    gui_rect_t clip;
    int x;
    int y;

    if (!st || !window || !s) return;

    x = window->bounds.x;
    y = window->bounds.y + GUI_TITLEBAR_HEIGHT;
    clip = (gui_rect_t){x + 8, y + 4, window->bounds.w - 16,
                        window->bounds.h - GUI_TITLEBAR_HEIGHT - 8};

    gui_gfx_fill_rect(s, clip, 0x00D8D8D0);
    gui_font_draw_string(s, x + 18, y + 14,
                         "Escribi el nombre de un programa, carpeta o archivo:",
                         0x00202020, 0, false);

    input = runbox_input_rect(st);
    gui_gfx_fill_rect(s, input, 0x00FFFFFF);
    gui_gfx_draw_rect(s, input, st->focused ? 0x000070C0 : 0x00606060);
    gui_font_draw_string_clipped(s, input.x + 5, input.y + 7,
                                 st->input, 0x00101010,
                                 (gui_rect_t){input.x + 4, input.y + 2,
                                              input.w - 8, input.h - 4});

    if (st->focused) {
        int cursor_x = input.x + 6 + (int)gui_font_text_width(st->input);
        if (cursor_x < input.x + input.w - 5) {
            gui_gfx_fill_rect(s, (gui_rect_t){cursor_x, input.y + 5, 1, 13},
                              0x00101010);
        }
    }

    gui_font_draw_string_clipped(s, x + 18, y + 74,
                                 st->status, 0x00602020,
                                 (gui_rect_t){x + 18, y + 70,
                                              window->bounds.w - 36, 18});

    runbox_draw_button(s, runbox_ok_rect(st), "Ejecutar");
    runbox_draw_button(s, runbox_cancel_rect(st), "Cancelar");
}

static bool runbox_event(gui_window_t *window, const gui_event_t *event,
                         void *ctx) {
    runbox_state_t *st = (runbox_state_t *)ctx;
    size_t len;

    if (!st || !window || !event) return false;

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (gui_rect_contains(runbox_input_rect(st), event->x, event->y)) {
            st->focused = true;
            window->dirty = true;
            return true;
        }
        if (gui_rect_contains(runbox_ok_rect(st), event->x, event->y)) {
            runbox_execute(st);
            return true;
        }
        if (gui_rect_contains(runbox_cancel_rect(st), event->x, event->y)) {
            runbox_close(st);
            return true;
        }
        st->focused = false;
        window->dirty = true;
        return gui_window_contains(window, event->x, event->y);
    }

    if (event->type != GUI_EVENT_KEY) return false;

    if (event->key == 27) {
        runbox_close(st);
        return true;
    }

    if (event->key == '\n' || event->key == '\r') {
        runbox_execute(st);
        return true;
    }

    len = kstrlen(st->input);
    if (event->key == '\b') {
        if (len) st->input[len - 1] = '\0';
        window->dirty = true;
        return true;
    }

    if (event->key >= 32 && event->key < 127 &&
        len + 1 < sizeof(st->input)) {
        st->input[len] = (char)event->key;
        st->input[len + 1] = '\0';
        st->status[0] = '\0';
        window->dirty = true;
        return true;
    }

    return false;
}

static void runbox_cleanup(runbox_state_t *st) {
    if (!st) return;
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
        st->window = NULL;
    }
    if (g_runbox == st) g_runbox = NULL;
    kfree(st);
}

static void runbox_main(void *arg) {
    runbox_state_t *st = (runbox_state_t *)arg;
    int x;
    int y;

    if (!st || !st->desktop) {
        runbox_cleanup(st);
        task_exit();
    }

    x = ((int)st->desktop->surface.width - RUNBOX_W) / 2;
    y = ((int)st->desktop->surface.height - RUNBOX_H) / 2;
    if (x < 8) x = 8;
    if (y < 8) y = 8;

    st->window = gui_desktop_create_window(st->desktop, x, y,
                                           RUNBOX_W, RUNBOX_H,
                                           "Ejecutar");
    if (!st->window) {
        runbox_cleanup(st);
        task_exit();
    }

    st->focused = true;
    runbox_copy(st->status, sizeof(st->status),
                "Ej: calc, SHELL.O, /README.TXT, /PROGRAMS");

    gui_window_set_content(st->window, runbox_content, st);
    gui_window_set_event_handler(st->window, runbox_event, st);
    st->window->owner_pid = task_current_pid();
    task_bind_window(st->window);

    gui_desktop_raise_window(st->desktop, st->window);
    gui_desktop_focus_window(st->desktop, st->window);
    st->window->dirty = true;
    gui_request_paint();

    while (!task_exit_requested()) {
        task_sleep(20);
    }

    runbox_cleanup(st);
    task_exit();
}

void runbox_open_from_desktop(gui_desktop_t *desktop) {
    runbox_state_t *st;

    if (!desktop) return;

    if (g_runbox && g_runbox->window) {
        gui_window_restore(g_runbox->window);
        gui_desktop_raise_window(desktop, g_runbox->window);
        gui_desktop_focus_window(desktop, g_runbox->window);
        g_runbox->window->dirty = true;
        return;
    }

    st = (runbox_state_t *)kzalloc(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_runbox = st;

    if (task_create("runbox", runbox_main, st) < 0) {
        g_runbox = NULL;
        kfree(st);
    }
}

bool runbox_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_runbox) return false;
    info->window = g_runbox->window;
    info->memory_bytes = sizeof(*g_runbox);
    return true;
}
