#include "include/file_dialog.h"
#include "include/keyboard.h"
#include "include/memory.h"
#include "include/mouse.h"
#include "include/pit.h"
#include "include/sound.h"
#include "include/task.h"

#define BKFD_WINDOW_W 410
#define BKFD_WINDOW_H 286
#define BKFD_ROW_H     17

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_event_queue_t events;
    gui_scrollbar_drag_t scrollbar_drag;
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint32_t entry_count;
    int selected;
    int scroll;
    int last_click;
    uint32_t last_click_tick;
    char cwd[VFS_MAX_PATH];
    char extension[16];
    char title[48];
    char status[80];
    uint32_t flags;
    bk_file_dialog_callback_t callback;
    void *callback_context;
} bk_file_dialog_state_t;

static void bkfd_copy(char *dst, size_t size, const char *src) {
    if (!dst || !size) return;
    if (!src) src = "";
    kstrncpy(dst, src, size - 1U);
    dst[size - 1U] = '\0';
}

static char bkfd_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool bkfd_extension_matches(const char *name, const char *extension) {
    const char *dot = NULL;
    if (!name || !extension || !extension[0]) return true;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return false;
    while (*dot && *extension) {
        if (bkfd_upper(*dot++) != bkfd_upper(*extension++)) return false;
    }
    return !*dot && !*extension;
}

static void bkfd_join(char *out, const char *directory, const char *name) {
    size_t length;
    bkfd_copy(out, VFS_MAX_PATH, directory);
    length = kstrlen(out);
    if (length > 1U && out[length - 1U] != '/' && length + 1U < VFS_MAX_PATH) {
        out[length++] = '/';
        out[length] = '\0';
    }
    if (length == 1U && out[0] == '/') length = 1U;
    kstrncpy(out + length, name, VFS_MAX_PATH - length - 1U);
    out[VFS_MAX_PATH - 1U] = '\0';
}

static void bkfd_parent(char *path) {
    size_t length;
    if (!path || path[0] != '/') return;
    length = kstrlen(path);
    while (length > 1U && path[length - 1U] == '/') path[--length] = '\0';
    while (length > 1U && path[length - 1U] != '/') length--;
    if (length <= 1U) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[length - 1U] = '\0';
    }
}

static gui_rect_t bkfd_list_rect(const bk_file_dialog_state_t *state) {
    gui_rect_t content = gui_window_content_rect(state->window);
    return (gui_rect_t){content.x + 8, content.y + 29,
                        content.w - 16, content.h - 70};
}

static gui_rect_t bkfd_button_rect(const bk_file_dialog_state_t *state,
                                   int button) {
    gui_rect_t content = gui_window_content_rect(state->window);
    int y = content.y + content.h - 33;
    if (button == 0) return (gui_rect_t){content.x + 8, y, 54, 23};
    if (button == 1) return (gui_rect_t){content.x + content.w - 238, y, 88, 23};
    if (button == 2) return (gui_rect_t){content.x + content.w - 144, y, 66, 23};
    return (gui_rect_t){content.x + content.w - 72, y, 64, 23};
}

static int bkfd_visible_rows(const bk_file_dialog_state_t *state) {
    int rows = bkfd_list_rect(state).h / BKFD_ROW_H;
    return rows > 0 ? rows : 1;
}

static void bkfd_clamp_scroll(bk_file_dialog_state_t *state) {
    int maximum = (int)state->entry_count - bkfd_visible_rows(state);
    if (maximum < 0) maximum = 0;
    if (state->scroll < 0) state->scroll = 0;
    if (state->scroll > maximum) state->scroll = maximum;
}

static bool bkfd_load(bk_file_dialog_state_t *state) {
    vfs_dir_entry_t raw[VFS_MAX_DIR_ENTRIES];
    uint32_t count = 0;
    state->entry_count = 0;
    state->selected = -1;
    state->scroll = 0;
    if (!vfs_listdir(state->cwd, raw, VFS_MAX_DIR_ENTRIES, &count)) {
        bkfd_copy(state->status, sizeof(state->status), "No se pudo abrir la carpeta");
        return false;
    }
    /* Directorios primero, luego archivos que cumplen el filtro. */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            bool directory = raw[i].type == VFS_NODE_DIR;
            if ((pass == 0 && !directory) || (pass == 1 && directory)) continue;
            if (!directory &&
                !bkfd_extension_matches(raw[i].name, state->extension)) continue;
            if (state->entry_count < VFS_MAX_DIR_ENTRIES)
                state->entries[state->entry_count++] = raw[i];
        }
    }
    bkfd_copy(state->status, sizeof(state->status),
              state->entry_count ? "Selecciona un archivo" : "Carpeta vacia");
    if (state->window) state->window->dirty = true;
    return true;
}

static void bkfd_draw_button(gui_surface_t *surface, gui_rect_t rect,
                             const char *text) {
    gui_gfx_fill_rect(surface, rect, 0x00D4D0C8);
    gui_gfx_draw_rect(surface, rect, 0x00404040);
    gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + 1,
                                            rect.w - 2, 1}, 0x00FFFFFF);
    gui_font_draw_string_clipped(surface, rect.x + 8, rect.y + 8,
                                 text, 0x00101010, rect);
}

static void bkfd_paint(gui_window_t *window, gui_surface_t *surface,
                       void *context) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)context;
    gui_rect_t content = gui_window_content_rect(window);
    gui_rect_t list = bkfd_list_rect(state);
    int rows = bkfd_visible_rows(state);
    gui_scrollbar_t scrollbar;
    gui_gfx_fill_rect(surface, content, 0x00D4D0C8);
    gui_gfx_fill_rect(surface, (gui_rect_t){content.x + 8, content.y + 5,
                                            content.w - 16, 19}, 0x00FFFFFF);
    gui_gfx_draw_rect(surface, (gui_rect_t){content.x + 8, content.y + 5,
                                            content.w - 16, 19}, 0x00606060);
    gui_font_draw_string_clipped(surface, content.x + 13, content.y + 11,
                                 state->cwd, 0x00101010,
                                 (gui_rect_t){content.x + 12, content.y + 6,
                                              content.w - 24, 17});
    gui_gfx_fill_rect(surface, list, 0x00FFFFFF);
    gui_gfx_draw_rect(surface, list, 0x00505050);
    for (int row = 0; row < rows; row++) {
        int index = state->scroll + row;
        gui_rect_t row_rect = {list.x + 1, list.y + 1 + row * BKFD_ROW_H,
                               list.w - GUI_SCROLLBAR_SIZE - 2, BKFD_ROW_H};
        char label[VFS_MAX_NAME + 4];
        if (index >= (int)state->entry_count) break;
        if (index == state->selected)
            gui_gfx_fill_rect(surface, row_rect, 0x000080C0);
        if (state->entries[index].type == VFS_NODE_DIR) {
            bkfd_copy(label, sizeof(label), "[+] ");
            kstrncpy(label + 4, state->entries[index].name,
                     sizeof(label) - 5U);
            label[sizeof(label) - 1U] = '\0';
        } else {
            bkfd_copy(label, sizeof(label), state->entries[index].name);
        }
        gui_font_draw_string_clipped(surface, row_rect.x + 4, row_rect.y + 5,
                                     label,
                                     index == state->selected ? 0x00FFFFFF
                                                              : 0x00101010,
                                     row_rect);
    }
    gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                     GUI_SCROLLBAR_SIZE, list.h},
        (uint32_t)state->scroll, (uint32_t)rows, state->entry_count);
    gui_scrollbar_paint_vertical(surface, &scrollbar);
    gui_font_draw_string_clipped(surface, content.x + 72,
                                 content.y + content.h - 25,
                                 state->status, 0x00404040,
                                 (gui_rect_t){content.x + 70,
                                              content.y + content.h - 31,
                                              content.w - 315, 23});
    bkfd_draw_button(surface, bkfd_button_rect(state, 0), "Subir");
    if (state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO)
        bkfd_draw_button(surface, bkfd_button_rect(state, 1), "Reproducir");
    bkfd_draw_button(surface, bkfd_button_rect(state, 2), "Abrir");
    bkfd_draw_button(surface, bkfd_button_rect(state, 3), "Cancelar");
}

static int bkfd_hit_row(bk_file_dialog_state_t *state, int x, int y) {
    gui_rect_t list = bkfd_list_rect(state);
    int row;
    if (!gui_rect_contains(list, x, y) ||
        x >= list.x + list.w - GUI_SCROLLBAR_SIZE) return -1;
    row = (y - list.y - 1) / BKFD_ROW_H;
    if (row < 0 || row >= bkfd_visible_rows(state)) return -1;
    row += state->scroll;
    return row < (int)state->entry_count ? row : -1;
}

static void bkfd_selected_path(bk_file_dialog_state_t *state, char *path) {
    if (!state || state->selected < 0 ||
        state->selected >= (int)state->entry_count) {
        if (path) path[0] = '\0';
        return;
    }
    bkfd_join(path, state->cwd, state->entries[state->selected].name);
}

static void bkfd_activate(bk_file_dialog_state_t *state, bool choose) {
    char path[VFS_MAX_PATH];
    vfs_dir_entry_t *entry;
    if (!state || state->selected < 0 ||
        state->selected >= (int)state->entry_count) return;
    entry = &state->entries[state->selected];
    bkfd_selected_path(state, path);
    if (entry->type == VFS_NODE_DIR) {
        bkfd_copy(state->cwd, sizeof(state->cwd), path);
        (void)bkfd_load(state);
        return;
    }
    if (!choose) {
        if ((state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO) && sound_play_file(path))
            bkfd_copy(state->status, sizeof(state->status), "Reproduciendo vista previa");
        else
            bkfd_copy(state->status, sizeof(state->status), "No se pudo reproducir");
        state->window->dirty = true;
        return;
    }
    if (state->callback) state->callback(path, state->callback_context);
    gui_window_close(state->window);
}

static bool bkfd_event(gui_window_t *window UNUSED,
                       const gui_event_t *event, void *context) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)context;
    if (!state || !event) return false;
    return gui_event_queue_push(&state->events, event);
}

static void bkfd_process_event(bk_file_dialog_state_t *state,
                               const gui_event_t *event) {
    int hit;
    gui_rect_t list = bkfd_list_rect(state);
    gui_scrollbar_t scrollbar;
    uint32_t new_scroll;
    gui_scrollbar_init_vertical(&scrollbar,
        (gui_rect_t){list.x + list.w - GUI_SCROLLBAR_SIZE, list.y,
                     GUI_SCROLLBAR_SIZE, list.h},
        (uint32_t)state->scroll,
        (uint32_t)bkfd_visible_rows(state), state->entry_count);
    if ((state->scrollbar_drag.active ||
         gui_rect_contains(list, event->x, event->y)) &&
        gui_scrollbar_handle_event_vertical(&scrollbar,
            &state->scrollbar_drag, event, 1U, &new_scroll)) {
        state->scroll = (int)new_scroll;
        bkfd_clamp_scroll(state);
        state->window->dirty = true;
        return;
    }
    if (event->type == GUI_EVENT_KEY) {
        if (event->key == 27) gui_window_close(state->window);
        else if (event->key == '\b') {
            bkfd_parent(state->cwd);
            (void)bkfd_load(state);
        } else if (event->key == '\n') bkfd_activate(state, true);
        else if ((uint8_t)event->key == KEY_UP && state->selected > 0)
            state->selected--;
        else if ((uint8_t)event->key == KEY_DOWN &&
                 state->selected + 1 < (int)state->entry_count)
            state->selected++;
        if (state->selected >= 0) {
            int visible = bkfd_visible_rows(state);
            if (state->selected < state->scroll)
                state->scroll = state->selected;
            if (state->selected >= state->scroll + visible)
                state->scroll = state->selected - visible + 1;
            bkfd_clamp_scroll(state);
        }
        state->window->dirty = true;
        return;
    }
    if (event->type != GUI_EVENT_MOUSE_UP ||
        event->button != MOUSE_LEFT_BUTTON) return;
    if (gui_rect_contains(bkfd_button_rect(state, 0), event->x, event->y)) {
        bkfd_parent(state->cwd);
        (void)bkfd_load(state);
        return;
    }
    if ((state->flags & BK_FILE_DIALOG_PREVIEW_AUDIO) &&
        gui_rect_contains(bkfd_button_rect(state, 1), event->x, event->y)) {
        bkfd_activate(state, false);
        return;
    }
    if (gui_rect_contains(bkfd_button_rect(state, 2), event->x, event->y)) {
        bkfd_activate(state, true);
        return;
    }
    if (gui_rect_contains(bkfd_button_rect(state, 3), event->x, event->y)) {
        gui_window_close(state->window);
        return;
    }
    hit = bkfd_hit_row(state, event->x, event->y);
    if (hit < 0) return;
    state->selected = hit;
    if (state->last_click == hit &&
        pit_get_ticks() - state->last_click_tick < pit_get_frequency_hz() / 2U) {
        state->last_click = -1;
        bkfd_activate(state, true);
    } else {
        state->last_click = hit;
        state->last_click_tick = pit_get_ticks();
        state->window->dirty = true;
    }
}

static void bkfd_task(void *argument) {
    bk_file_dialog_state_t *state = (bk_file_dialog_state_t *)argument;
    gui_event_t event;
    if (!state || !state->desktop) goto done;
    gui_event_queue_reset(&state->events);
    state->window = gui_desktop_create_window(state->desktop, 105, 62,
                                               BKFD_WINDOW_W, BKFD_WINDOW_H,
                                               state->title);
    if (!state->window) goto done;
    gui_window_set_content(state->window, bkfd_paint, state);
    gui_window_set_event_handler(state->window, bkfd_event, state);
    gui_window_set_min_size(state->window, 330, 220);
    state->window->owner_pid = task_current_pid();
    task_bind_window(state->window);
    (void)bkfd_load(state);
    while (!task_exit_requested() && state->window->listed) {
        while (gui_event_queue_pop(&state->events, &event))
            bkfd_process_event(state, &event);
        task_sleep(1);
    }
    gui_desktop_remove_window(state->desktop, state->window);
    gui_window_destroy(state->window);
    task_bind_window(NULL);
done:
    if (state) kfree(state);
    task_exit();
}

bool bk_file_dialog_open(gui_desktop_t *desktop, const char *title,
                         const char *initial_path, const char *extension,
                         uint32_t flags, bk_file_dialog_callback_t callback,
                         void *context) {
    bk_file_dialog_state_t *state;
    if (!desktop || !callback) return false;
    state = (bk_file_dialog_state_t *)kzalloc(sizeof(*state));
    if (!state) return false;
    state->desktop = desktop;
    state->selected = -1;
    state->last_click = -1;
    state->flags = flags;
    state->callback = callback;
    state->callback_context = context;
    bkfd_copy(state->title, sizeof(state->title),
              title && title[0] ? title : "Abrir archivo");
    bkfd_copy(state->cwd, sizeof(state->cwd),
              initial_path && initial_path[0] ? initial_path : "/");
    if (state->cwd[0] != '/') bkfd_copy(state->cwd, sizeof(state->cwd), "/");
    bkfd_copy(state->extension, sizeof(state->extension), extension);
    if (task_create("file-dialog", bkfd_task, state) < 0) {
        kfree(state);
        return false;
    }
    return true;
}
