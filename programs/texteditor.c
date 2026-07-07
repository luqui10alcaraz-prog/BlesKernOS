#include "programs.h"
#include "../kernel/include/keyboard.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/task.h"
#include "../kernel/include/vfs.h"
#include "../kernel/string.h"

#define EDIT_CAPACITY      4096
#define EDIT_LINE_HEIGHT     10
#define EDIT_CHAR_ADVANCE     7
#define EDIT_TEXT_PAD_X       4
#define EDIT_TEXT_PAD_Y       4

typedef struct {
    gui_rect_t text_rect;
    gui_rect_t status_rect;
    int text_x;
    int text_y;
    int visible_rows;
    int visible_cols;
} editor_layout_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    char path[VFS_MAX_PATH];
    char *text;
    uint32_t length;
    uint32_t cursor;
    uint32_t selection_anchor;
    bool mouse_selecting;
    char status[48];
} editor_state_t;

static editor_state_t *g_editor;
static char g_editor_clipboard[EDIT_CAPACITY];
static void editor_main(void *argument);

static void editor_status(editor_state_t *st, const char *text) {
    if (!st) return;
    kstrncpy(st->status, text ? text : "", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
}

static void editor_title(editor_state_t *st) {
    const char *name = st->path;

    if (!st || !st->window) return;
    for (const char *p = st->path; *p; p++) if (*p == '/') name = p + 1;
    kstrcpy(st->window->title, "Editor: ");
    kstrncpy(st->window->title + 8, name, sizeof(st->window->title) - 9);
    st->window->title[sizeof(st->window->title) - 1] = '\0';
}

static void editor_build_layout(const editor_state_t *st,
                                editor_layout_t *layout) {
    int x;
    int y;
    int w;
    int bottom;
    int status_h = 16;
    int h;

    if (!st || !st->window || !layout) return;

    x = st->window->bounds.x + 7;
    y = st->window->bounds.y + gui_window_content_top(st->window) + 6;
    w = st->window->bounds.w - 14;
    bottom = st->window->bounds.y + st->window->bounds.h - GUI_BORDER_SIZE;
    h = bottom - y - status_h;
    if (h < 16) h = 16;

    layout->text_rect = (gui_rect_t){x, y, w, h};
    layout->status_rect = (gui_rect_t){x, y + h, w, status_h};
    layout->text_x = x + EDIT_TEXT_PAD_X;
    layout->text_y = y + EDIT_TEXT_PAD_Y;
    layout->visible_rows = (h - EDIT_TEXT_PAD_Y * 2) / EDIT_LINE_HEIGHT;
    layout->visible_cols = (w - EDIT_TEXT_PAD_X * 2) / EDIT_CHAR_ADVANCE;
    if (layout->visible_rows < 1) layout->visible_rows = 1;
    if (layout->visible_cols < 1) layout->visible_cols = 1;
}

static bool editor_has_selection(const editor_state_t *st) {
    return st && st->cursor != st->selection_anchor;
}

static void editor_selection_bounds(const editor_state_t *st,
                                    uint32_t *start_out,
                                    uint32_t *end_out) {
    uint32_t start;
    uint32_t end;

    if (!st) return;
    start = st->cursor < st->selection_anchor ? st->cursor
                                              : st->selection_anchor;
    end = st->cursor > st->selection_anchor ? st->cursor
                                            : st->selection_anchor;
    if (start_out) *start_out = start;
    if (end_out) *end_out = end;
}

static void editor_set_cursor(editor_state_t *st, uint32_t cursor,
                              bool extend_selection) {
    if (!st) return;
    if (cursor > st->length) cursor = st->length;
    st->cursor = cursor;
    if (!extend_selection) st->selection_anchor = cursor;
}

static bool editor_line_span(const editor_state_t *st, int target_row,
                             uint32_t *start_out, uint32_t *end_out,
                             uint32_t *next_out) {
    uint32_t pos = 0;
    int row = 0;

    if (!st || target_row < 0) return false;

    while (true) {
        uint32_t start = pos;
        uint32_t end = pos;
        uint32_t next;

        while (end < st->length &&
               st->text[end] != '\r' &&
               st->text[end] != '\n') {
            end++;
        }
        next = end;
        while (next < st->length &&
               (st->text[next] == '\r' || st->text[next] == '\n')) {
            next++;
        }

        if (row == target_row) {
            if (start_out) *start_out = start;
            if (end_out) *end_out = end;
            if (next_out) *next_out = next;
            return true;
        }

        if (next == pos && pos >= st->length) break;
        pos = next;
        row++;
    }

    return false;
}

static void editor_row_col_for_index(const editor_state_t *st, uint32_t index,
                                     int *row_out, uint32_t *col_out) {
    uint32_t pos = 0;
    int row = 0;
    uint32_t col = 0;

    if (!st) return;
    if (index > st->length) index = st->length;

    while (pos < index) {
        char ch = st->text[pos++];
        if (ch == '\r') continue;
        if (ch == '\n') {
            row++;
            col = 0;
            continue;
        }
        col++;
    }

    if (row_out) *row_out = row;
    if (col_out) *col_out = col;
}

static uint32_t editor_index_for_row_col(const editor_state_t *st,
                                         int row, uint32_t col) {
    uint32_t start;
    uint32_t end;
    uint32_t index;

    if (!st) return 0;
    if (!editor_line_span(st, row, &start, &end, NULL)) return st->length;

    index = start + col;
    if (index > end) index = end;
    return index;
}

static uint32_t editor_hit_test(const editor_state_t *st,
                                const editor_layout_t *layout,
                                int x, int y) {
    int rel_x;
    int rel_y;
    int row;
    int col;

    if (!st || !layout) return 0;

    rel_x = x - layout->text_x;
    rel_y = y - layout->text_y;
    if (rel_x < 0) rel_x = 0;
    if (rel_y < 0) rel_y = 0;

    row = rel_y / EDIT_LINE_HEIGHT;
    col = (rel_x + (EDIT_CHAR_ADVANCE / 2)) / EDIT_CHAR_ADVANCE;

    if (row >= layout->visible_rows) row = layout->visible_rows - 1;
    if (col >= layout->visible_cols) col = layout->visible_cols;

    return editor_index_for_row_col(st, row, (uint32_t)col);
}

static void editor_delete_range(editor_state_t *st,
                                uint32_t start, uint32_t end) {
    if (!st) return;
    if (start > end) {
        uint32_t swap = start;
        start = end;
        end = swap;
    }
    if (end > st->length) end = st->length;
    if (start >= end) {
        editor_set_cursor(st, start, false);
        return;
    }

    memmove(st->text + start, st->text + end, st->length - end + 1);
    st->length -= end - start;
    editor_set_cursor(st, start, false);
}

static bool editor_delete_selection(editor_state_t *st) {
    uint32_t start;
    uint32_t end;

    if (!editor_has_selection(st)) return false;
    editor_selection_bounds(st, &start, &end);
    editor_delete_range(st, start, end);
    return true;
}

static bool editor_insert_bytes(editor_state_t *st,
                                const char *text, uint32_t length) {
    if (!st || !text) return false;
    if (editor_has_selection(st)) editor_delete_selection(st);
    if (st->length >= EDIT_CAPACITY - 1) return false;
    if (length > (EDIT_CAPACITY - 1) - st->length)
        length = (EDIT_CAPACITY - 1) - st->length;
    if (!length) return false;

    memmove(st->text + st->cursor + length,
            st->text + st->cursor,
            st->length - st->cursor + 1);
    memcpy(st->text + st->cursor, text, length);
    st->cursor += length;
    st->length += length;
    st->selection_anchor = st->cursor;
    return true;
}

static void editor_backspace(editor_state_t *st) {
    if (!st) return;
    if (editor_delete_selection(st)) return;
    if (!st->cursor) return;
    editor_delete_range(st, st->cursor - 1, st->cursor);
}

static void editor_delete_forward(editor_state_t *st) {
    if (!st) return;
    if (editor_delete_selection(st)) return;
    if (st->cursor >= st->length) return;
    editor_delete_range(st, st->cursor, st->cursor + 1);
}

static void editor_copy_selection(editor_state_t *st) {
    uint32_t start;
    uint32_t end;
    uint32_t size;

    if (!st) return;
    if (!editor_has_selection(st)) {
        editor_status(st, "No hay seleccion");
        return;
    }

    editor_selection_bounds(st, &start, &end);
    size = end - start;
    if (size >= EDIT_CAPACITY) size = EDIT_CAPACITY - 1;
    memcpy(g_editor_clipboard, st->text + start, size);
    g_editor_clipboard[size] = '\0';
    editor_status(st, "Seleccion copiada");
}

static void editor_paste_clipboard(editor_state_t *st) {
    uint32_t size;

    if (!st) return;
    size = kstrlen(g_editor_clipboard);
    if (!size) {
        editor_status(st, "Portapapeles vacio");
        return;
    }
    if (editor_insert_bytes(st, g_editor_clipboard, size))
        editor_status(st, "Pegado");
    else
        editor_status(st, "Sin espacio");
}

static void editor_select_all(editor_state_t *st) {
    if (!st) return;
    st->selection_anchor = 0;
    st->cursor = st->length;
    editor_status(st, "Todo seleccionado");
}

static void editor_move_vertical(editor_state_t *st, int delta,
                                 bool extend_selection) {
    int row;
    uint32_t col;
    int target_row;

    if (!st) return;
    editor_row_col_for_index(st, st->cursor, &row, &col);
    target_row = row + delta;
    if (target_row < 0) target_row = 0;
    if (!editor_line_span(st, target_row, NULL, NULL, NULL)) return;
    editor_set_cursor(st, editor_index_for_row_col(st, target_row, col),
                      extend_selection);
}

static void editor_content(gui_window_t *window UNUSED,
                           gui_surface_t *surface, void *context) {
    editor_state_t *st = (editor_state_t *)context;
    editor_layout_t layout;
    uint32_t sel_start = 0;
    uint32_t sel_end = 0;
    bool has_selection;

    if (!st || !st->window || !st->window->visible) return;

    editor_build_layout(st, &layout);
    has_selection = editor_has_selection(st);
    if (has_selection) editor_selection_bounds(st, &sel_start, &sel_end);

    gui_gfx_fill_rect(surface, layout.text_rect, 0x00FFFDF4);
    gui_gfx_draw_rect(surface, layout.text_rect, 0x00808078);

    for (int row = 0; row < layout.visible_rows; row++) {
        uint32_t start;
        uint32_t end;
        int line_y = layout.text_y + row * EDIT_LINE_HEIGHT;

        if (!editor_line_span(st, row, &start, &end, NULL)) break;

        for (uint32_t index = start; index < end; index++) {
            int col = (int)(index - start);
            int cell_x;
            bool selected;

            if (col >= layout.visible_cols) break;
            cell_x = layout.text_x + col * EDIT_CHAR_ADVANCE;
            selected = has_selection &&
                       index >= sel_start &&
                       index < sel_end;

            if (selected) {
                gui_gfx_fill_rect(surface,
                                  (gui_rect_t){cell_x, line_y,
                                               EDIT_CHAR_ADVANCE, 9},
                                  0x002E6FD5);
            }

            gui_font_draw_char(surface, cell_x, line_y, st->text[index],
                               selected ? 0x00FFFFFF : 0x00202020,
                               0, false);
        }

        if (st->cursor >= start && st->cursor <= end) {
            int caret_col = (int)(st->cursor - start);
            int caret_x = layout.text_x + caret_col * EDIT_CHAR_ADVANCE;

            if (caret_col <= layout.visible_cols &&
                caret_x < layout.text_rect.x + layout.text_rect.w - 1) {
                gui_gfx_fill_rect(surface,
                                  (gui_rect_t){caret_x, line_y, 1, 8},
                                  has_selection ? 0x00FFFFFF : 0x00203050);
            }
        }
    }

    gui_gfx_fill_rect(surface, layout.status_rect, 0x00D0D0C8);
    gui_gfx_fill_rect(surface,
                      (gui_rect_t){layout.status_rect.x, layout.status_rect.y,
                                   layout.status_rect.w, 1},
                      0x00808078);
    gui_font_draw_string_clipped(surface,
                                 layout.status_rect.x + 4,
                                 layout.status_rect.y + 4,
                                 st->status, 0x00506070,
                                 (gui_rect_t){layout.status_rect.x + 2,
                                              layout.status_rect.y + 1,
                                              layout.status_rect.w - 4,
                                              layout.status_rect.h - 2});
}

static void editor_file_menu(gui_window_t *window UNUSED, uint32_t item_id,
                             void *context) {
    editor_state_t *st = (editor_state_t *)context;

    if (!st) return;
    if (item_id == 1) {
        st->length = 0;
        st->cursor = 0;
        st->selection_anchor = 0;
        st->mouse_selecting = false;
        st->text[0] = '\0';
        kstrcpy(st->path, "/NEWTEXT.TXT");
        editor_status(st, "Nuevo archivo");
        editor_title(st);
    } else if (item_id == 2) {
        if (vfs_write_all(st->path, st->text, st->length))
            editor_status(st, "Guardado");
        else
            editor_status(st, "Error al guardar");
    }
    if (st->window) st->window->dirty = true;
}

static bool editor_event(gui_window_t *window UNUSED,
                         const gui_event_t *event, void *context) {
    editor_state_t *st = (editor_state_t *)context;
    editor_layout_t layout;
    uint8_t key;

    if (!st || !st->window || !st->window->visible || !event)
        return false;

    editor_build_layout(st, &layout);
    key = (uint8_t)event->key;

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (event->button != MOUSE_LEFT_BUTTON ||
            !gui_rect_contains(layout.text_rect, event->x, event->y))
            return false;
        editor_set_cursor(st, editor_hit_test(st, &layout, event->x, event->y),
                          false);
        st->mouse_selecting = true;
        st->window->dirty = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        if (!st->mouse_selecting || !(event->buttons & MOUSE_LEFT_BUTTON))
            return false;
        editor_set_cursor(st, editor_hit_test(st, &layout, event->x, event->y),
                          true);
        st->window->dirty = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        if (!st->mouse_selecting || event->button != MOUSE_LEFT_BUTTON)
            return false;
        editor_set_cursor(st, editor_hit_test(st, &layout, event->x, event->y),
                          true);
        st->mouse_selecting = false;
        st->window->dirty = true;
        return true;
    }

    if (event->type != GUI_EVENT_KEY) return false;

    if (event->alt && (key == 'a' || key == 'A')) {
        editor_select_all(st);
    } else if (event->alt && (key == 'c' || key == 'C')) {
        editor_copy_selection(st);
    } else if (event->alt && (key == 'v' || key == 'V')) {
        editor_paste_clipboard(st);
    } else if (key == KEY_LEFT) {
        if (st->cursor > 0) editor_set_cursor(st, st->cursor - 1, event->shift);
        else if (!event->shift) st->selection_anchor = st->cursor;
    } else if (key == KEY_RIGHT) {
        if (st->cursor < st->length)
            editor_set_cursor(st, st->cursor + 1, event->shift);
        else if (!event->shift) st->selection_anchor = st->cursor;
    } else if (key == KEY_UP) {
        editor_move_vertical(st, -1, event->shift);
    } else if (key == KEY_DOWN) {
        editor_move_vertical(st, 1, event->shift);
    } else if (key == KEY_BACKSPACE) {
        editor_backspace(st);
    } else if (key == KEY_DELETE) {
        editor_delete_forward(st);
    } else if ((key == KEY_ENTER || (key >= 32 && key < 127))) {
        char ch = (char)key;
        if (editor_insert_bytes(st, &ch, 1))
            editor_status(st, "Listo");
        else
            editor_status(st, "Sin espacio");
    } else {
        return false;
    }

    if (st->window) st->window->dirty = true;
    return true;
}

static bool editor_load_path(editor_state_t *st, const char *path) {
    void *data = NULL;
    uint32_t size = 0;

    if (!st || !path) return false;
    if (!vfs_read_all(path, &data, &size)) return false;
    if (size >= EDIT_CAPACITY) size = EDIT_CAPACITY - 1;
    if (size && !data) return false;
    if (size) memcpy(st->text, data, size);
    st->text[size] = '\0';
    st->length = size;
    st->cursor = size;
    st->selection_anchor = size;
    st->mouse_selecting = false;
    editor_status(st, "Listo");
    kstrncpy(st->path, path, sizeof(st->path) - 1);
    st->path[sizeof(st->path) - 1] = '\0';
    kfree(data);

    if (st->window) {
        editor_title(st);
        st->window->dirty = true;
    }
    return true;
}

static void editor_cleanup(editor_state_t *st) {
    if (!st) return;
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
    }
    if (st->text) kfree(st->text);
    if (g_editor == st) g_editor = NULL;
    kfree(st);
}

bool texteditor_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_editor) return false;
    info->window = g_editor->window;
    info->memory_bytes = (uint32_t)sizeof(*g_editor) + EDIT_CAPACITY;
    if (g_editor->window)
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
    return true;
}

void texteditor_open(gui_desktop_t *desktop, const char *path) {
    editor_state_t *st;

    if (!desktop || !path) return;

    st = (editor_state_t *)kzalloc(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->text = (char *)kzalloc(EDIT_CAPACITY);
    if (!st->text) {
        kfree(st);
        return;
    }

    g_editor = st;
    kstrcpy(st->path, "/NEWTEXT.TXT");
    editor_status(st, "Listo");

    if (!editor_load_path(st, path)) {
        editor_cleanup(st);
        return;
    }
    if (task_create("texteditor", editor_main, st) < 0) {
        editor_cleanup(st);
    }
}

void texteditor_open_from_desktop(gui_desktop_t *desktop) {
    texteditor_open(desktop, "/README.TXT");
}

static void editor_main(void *argument) {
    editor_state_t *st = (editor_state_t *)argument;

    if (!st || !st->desktop) {
        editor_cleanup(st);
        task_exit();
    }

    task_set_memory_hint(sizeof(*st) + EDIT_CAPACITY);
    st->window = gui_desktop_create_window(st->desktop, 110, 45, 460, 250,
                                           "Editor");
    if (st->window) {
        int file_menu = gui_window_add_menu(st->window, "File");
        gui_window_add_menu_item(st->window, file_menu, 1, "Nuevo",
                                 editor_file_menu, st);
        gui_window_add_menu_item(st->window, file_menu, 2, "Guardar",
                                 editor_file_menu, st);
        (void)gui_window_add_menu(st->window, "Edit");
        gui_window_set_content(st->window, editor_content, st);
        gui_window_set_event_handler(st->window, editor_event, st);
        gui_window_set_min_size(st->window, 260, 160);
        st->window->owner_pid = task_current_pid();
        task_bind_window(st->window);
        editor_title(st);
    }

    while (!task_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        task_sleep(4);
    }

    editor_cleanup(st);
    task_exit();
}

void texteditor_install(gui_desktop_t *desktop UNUSED) {}
