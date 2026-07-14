#include "../kernel/include/api.h"

#define TERM_MAX_COLS 96
#define TERM_SCROLLBACK_LINES 256
#define TERM_LINE_HEIGHT 10
#define TERM_HISTORY_LINES 32

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_event_queue_t events;
    char lines[TERM_SCROLLBACK_LINES][TERM_MAX_COLS + 1];
    char input[SHELL_MAX_CMD];
    uint32_t input_len;
    uint32_t output_col;
    uint32_t line_count;
    uint32_t scroll_row;
    uint32_t visible_rows;
    char history[TERM_HISTORY_LINES][SHELL_MAX_CMD];
    uint32_t history_count;
    uint32_t history_index;
    gui_scrollbar_drag_t scrollbar_drag;
    bool execute_requested;
} terminal_state_t;

typedef struct {
    gui_rect_t background;
    gui_rect_t output;
    gui_rect_t scrollbar;
    gui_rect_t input;
    uint32_t columns;
    uint32_t output_rows;
    uint32_t input_rows;
} terminal_layout_t;

static terminal_state_t *g_terminal;

static void terminal_prompt(char *out, uint32_t capacity) {
    const char *cwd = bk_file_getcwd();
    if (!out || !capacity) return;
    bk_runtime_strncpy(out, "bles@bleskernos:", capacity - 1U);
    out[capacity - 1U] = '\0';
    if (cwd) bk_runtime_strcat(out, cwd);
    bk_runtime_strcat(out, "> ");
}

static uint32_t terminal_columns(const terminal_state_t *st) {
    int width = st && st->window ? st->window->bounds.w - 34 : 400;
    uint32_t columns = width > 16 ? (uint32_t)(width / 8) : 1U;
    if (columns > TERM_MAX_COLS) columns = TERM_MAX_COLS;
    return columns ? columns : 1U;
}

static uint32_t terminal_input_total_rows(const terminal_state_t *st,
                                          uint32_t columns) {
    uint32_t rows = 1;
    uint32_t column = 0;
    char prompt[VFS_MAX_PATH + 24];
    if (!columns) columns = 1;
    terminal_prompt(prompt, sizeof(prompt));
    for (uint32_t i = 0; prompt[i]; i++) {
        if (column >= columns) { rows++; column = 0; }
        column++;
    }
    for (uint32_t i = 0; st && i < st->input_len; i++) {
        if (st->input[i] == '\n') { rows++; column = 0; continue; }
        if (column >= columns) { rows++; column = 0; }
        column++;
    }
    return rows;
}

static void terminal_build_layout(const terminal_state_t *st,
                                  terminal_layout_t *layout) {
    gui_rect_t content = bk_gui_window_content_rect_raw(st->window);
    uint32_t total_rows;
    uint32_t screen_rows;
    layout->background = (gui_rect_t){content.x + 5, content.y + 5,
                                      content.w - 10, content.h - 10};
    layout->columns = terminal_columns(st);
    screen_rows = layout->background.h > 8
                ? (uint32_t)((layout->background.h - 8) / TERM_LINE_HEIGHT) : 1U;
    total_rows = terminal_input_total_rows(st, layout->columns);
    layout->input_rows = total_rows;
    if (layout->input_rows > screen_rows / 2U)
        layout->input_rows = screen_rows / 2U;
    if (!layout->input_rows) layout->input_rows = 1;
    layout->output_rows = screen_rows > layout->input_rows + 1U
                        ? screen_rows - layout->input_rows - 1U : 1U;
    layout->output = (gui_rect_t){layout->background.x + 4,
        layout->background.y + 3,
        layout->background.w - GUI_SCROLLBAR_SIZE - 10,
        (int)layout->output_rows * TERM_LINE_HEIGHT};
    layout->scrollbar = (gui_rect_t){
        layout->background.x + layout->background.w - GUI_SCROLLBAR_SIZE,
        layout->background.y, GUI_SCROLLBAR_SIZE,
        layout->output.h + 5};
    layout->input = (gui_rect_t){layout->output.x,
        layout->output.y + layout->output.h + 3,
        layout->output.w,
        (int)layout->input_rows * TERM_LINE_HEIGHT};
}

static void terminal_clamp_scroll(terminal_state_t *st,
                                  uint32_t visible_rows) {
    uint32_t maximum = st->line_count > visible_rows
                     ? st->line_count - visible_rows : 0;
    if (st->scroll_row > maximum) st->scroll_row = maximum;
}

static void terminal_newline(terminal_state_t *st) {
    uint32_t visible;
    bool follow;
    if (!st) return;
    visible = st->visible_rows ? st->visible_rows : 10U;
    follow = st->scroll_row + visible >= st->line_count;
    if (st->line_count < TERM_SCROLLBACK_LINES) {
        st->line_count++;
    } else {
        for (uint32_t i = 0; i + 1U < TERM_SCROLLBACK_LINES; i++)
            bk_runtime_strncpy(st->lines[i], st->lines[i + 1],
                               TERM_MAX_COLS + 1);
        if (st->scroll_row) st->scroll_row--;
    }
    bk_runtime_memset(st->lines[st->line_count - 1U], 0,
                      TERM_MAX_COLS + 1);
    st->output_col = 0;
    if (follow) st->scroll_row = st->line_count > visible
                               ? st->line_count - visible : 0;
}

static void terminal_output_char(char c, void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st || c == '\r') return;
    if (c == '\n') {
        terminal_newline(st);
        if (st->window) {
            bk_gui_window_invalidate(st->window);
        }
        return;
    }
    if (c == '\b') {
        if (st->output_col)
            st->lines[st->line_count - 1U][--st->output_col] = '\0';
        return;
    }
    if (c < 32) return;
    if (st->output_col >= terminal_columns(st)) terminal_newline(st);
    st->lines[st->line_count - 1U][st->output_col++] = c;
    st->lines[st->line_count - 1U][st->output_col] = '\0';
}

static void terminal_output_clear(void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st) return;
    bk_runtime_memset(st->lines, 0, sizeof(st->lines));
    st->output_col = 0;
    st->line_count = 1;
    st->scroll_row = 0;
}

static void terminal_write(terminal_state_t *st, const char *text) {
    while (text && *text) terminal_output_char(*text++, st);
}

static void terminal_execute(terminal_state_t *st) {
    char prompt[VFS_MAX_PATH + 24];
    uint32_t visible = st->visible_rows ? st->visible_rows : 10U;
    st->scroll_row = st->line_count > visible
                   ? st->line_count - visible : 0;
    terminal_prompt(prompt, sizeof(prompt));
    terminal_write(st, prompt);
    terminal_write(st, st->input);
    terminal_output_char('\n', st);

    if (st->input[0]) {
        uint32_t slot = st->history_count % TERM_HISTORY_LINES;
        bk_runtime_strncpy(st->history[slot], st->input, SHELL_MAX_CMD - 1U);
        st->history[slot][SHELL_MAX_CMD - 1U] = '\0';
        st->history_count++;
    }
    st->history_index = st->history_count;

    bk_shell_execute_line(st->input);

    st->input_len = 0;
    st->input[0] = '\0';
    st->execute_requested = false;
    if (bk_shell_take_exit_request() && st->window)
        bk_gui_window_close(st->window);
    if (st->window) bk_gui_window_invalidate(st->window);
}

static void terminal_content(gui_window_t *window UNUSED,
                             gui_surface_t *surface, void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    terminal_layout_t layout;
    gui_scrollbar_t scrollbar;
    if (!st || !st->window || !st->window->visible) return;
    terminal_build_layout(st, &layout);
    st->visible_rows = layout.output_rows;
    terminal_clamp_scroll(st, layout.output_rows);
    bk_gui_gfx_fill_rect(surface, layout.background, 0x000B1220);
    for (uint32_t i = 0; i < layout.output_rows; i++) {
        uint32_t line = st->scroll_row + i;
        if (line >= st->line_count) break;
        bk_gui_font_draw_string_clipped(surface, layout.output.x,
            layout.output.y + (int)i * TERM_LINE_HEIGHT,
            st->lines[line], 0x00D8DEE9, layout.output);
    }

    bk_gui_gfx_fill_rect(surface,
        (gui_rect_t){layout.output.x, layout.input.y - 2,
                     layout.output.w, 1}, 0x002F81F7);

    {
        char prompt[VFS_MAX_PATH + SHELL_MAX_CMD + 4];
        char segment[TERM_MAX_COLS + 1];
        uint32_t total_rows = terminal_input_total_rows(st, layout.columns);
        uint32_t skip = total_rows > layout.input_rows
                      ? total_rows - layout.input_rows : 0;
        uint32_t row = 0, column = 0;
        terminal_prompt(prompt, sizeof(prompt));
        {
            uint32_t used = (uint32_t)bk_runtime_strlen(prompt);
            uint32_t i = 0;
            while (st->input[i] && used + 1U < sizeof(prompt))
                prompt[used++] = st->input[i++];
            prompt[used] = '\0';
        }
        bk_runtime_memset(segment, 0, sizeof(segment));
        for (uint32_t i = 0;; i++) {
            char c = prompt[i];
            bool end_row = !c || c == '\n' || column >= layout.columns;
            if (end_row) {
                segment[column] = '\0';
                if (row >= skip && row - skip < layout.input_rows)
                    bk_gui_font_draw_string_clipped(surface, layout.input.x,
                        layout.input.y + (int)(row - skip) * TERM_LINE_HEIGHT,
                        segment, 0x0088C0D0, layout.input);
                row++; column = 0;
                bk_runtime_memset(segment, 0, sizeof(segment));
                if (!c) break;
                if (c == '\n') continue;
            }
            segment[column++] = c;
        }
    }

    bk_gui_scrollbar_init_vertical(&scrollbar, layout.scrollbar,
        st->scroll_row, layout.output_rows, st->line_count);
    bk_gui_scrollbar_paint_vertical(surface, &scrollbar);
}

static bool terminal_window_event(gui_window_t *window UNUSED,
                                  const gui_event_t *event,
                                  void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st || !event) return false;
    if (event->type != GUI_EVENT_KEY &&
        event->type != GUI_EVENT_MOUSE_WHEEL &&
        event->type != GUI_EVENT_MOUSE_DOWN &&
        event->type != GUI_EVENT_MOUSE_MOVE &&
        event->type != GUI_EVENT_MOUSE_UP) return false;
    return bk_gui_event_queue_push(&st->events, event);
}

static void terminal_process_event(terminal_state_t *st,
                                   const gui_event_t *event) {
    if (!st || !event) return;
    if (event->type != GUI_EVENT_KEY) {
        terminal_layout_t layout;
        gui_scrollbar_t scrollbar;
        uint32_t new_scroll;
        terminal_build_layout(st, &layout);
        bk_gui_scrollbar_init_vertical(&scrollbar, layout.scrollbar,
            st->scroll_row, layout.output_rows, st->line_count);
        if ((st->scrollbar_drag.active ||
             bk_gui_rect_contains((gui_rect_t){layout.output.x,
                 layout.output.y, layout.output.w + layout.scrollbar.w,
                 layout.output.h}, event->x, event->y)) &&
            bk_gui_scrollbar_handle_event_vertical(&scrollbar,
                &st->scrollbar_drag, event, 3, &new_scroll)) {
            st->scroll_row = new_scroll;
            terminal_clamp_scroll(st, layout.output_rows);
            if (st->window) st->window->dirty = true;
        }
        return;
    }
    if (event->key == KEY_UP || event->key == KEY_DOWN) {
        uint32_t oldest = st->history_count > TERM_HISTORY_LINES
                        ? st->history_count - TERM_HISTORY_LINES : 0U;
        if (event->key == KEY_UP && st->history_index > oldest)
            st->history_index--;
        if (event->key == KEY_DOWN && st->history_index < st->history_count)
            st->history_index++;
        st->input[0] = '\0';
        st->input_len = 0;
        if (st->history_index < st->history_count) {
            const char *item = st->history[st->history_index % TERM_HISTORY_LINES];
            bk_runtime_strncpy(st->input, item, sizeof(st->input) - 1U);
            st->input[sizeof(st->input) - 1U] = '\0';
            st->input_len = (uint32_t)bk_runtime_strlen(st->input);
        }
    } else if (event->key == '\n' && event->shift) {
        if (st->input_len + 1 < sizeof(st->input)) {
            st->input[st->input_len++] = '\n';
            st->input[st->input_len] = '\0';
        }
    } else if (event->key == '\n') {
        st->execute_requested = true;
    } else if (event->key == '\b') {
        if (st->input_len) st->input[--st->input_len] = '\0';
    } else if (event->key >= 32 && event->key < 127 &&
               st->input_len + 1 < sizeof(st->input)) {
        st->input[st->input_len++] = event->key;
        st->input[st->input_len] = '\0';
    }
    if (st->window) st->window->dirty = true;
}

static void terminal_cleanup(terminal_state_t *st) {
    if (!st) return;
    bk_console_set_output_sink(NULL, NULL, NULL);
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (g_terminal == st) g_terminal = NULL;
    bk_sys_free(st);
}

static void terminal_main(void *argument) {
    terminal_state_t *st = (terminal_state_t *)argument;
    gui_event_t event;

    if (!st || !st->desktop) {
        terminal_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st));
    st->line_count = 1;
    st->visible_rows = 10;
    bk_gui_event_queue_reset(&st->events);
    st->window = bk_gui_create_window(st->desktop, 70, 42, 590, 340,
                                           "BlesKernOS Terminal");
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "BlesKernOS Terminal", "Version 1.1", "Consola de comandos de BlesKernOS.",
            "Bles.INC (C) 2026", "/ICONS/SHELL.BMP"});
        bk_gui_set_window_content(st->window, terminal_content, st);
        bk_gui_set_window_event_handler(st->window, terminal_window_event, st);
        bk_gui_set_window_min_size(st->window, 260, 150);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    }
    terminal_write(st, "BlesKernOS Terminal 1.1 (i386)\n");
    terminal_write(st, "Escriba 'help' para obtener ayuda. Flechas: historial.\n\n");
    bk_console_set_output_sink(terminal_output_char, terminal_output_clear, st);

    while (!bk_proc_exit_requested()) {
        while (bk_gui_event_queue_pop(&st->events, &event)) {
            terminal_process_event(st, &event);
        }
        if (st->execute_requested) terminal_execute(st);
        if (!st->window || !st->window->listed) break;
        bk_sys_sleep_ticks(1);
    }

    terminal_cleanup(st);
    bk_proc_exit();
}

bool shelllauncher_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_terminal) return false;
    info->window = g_terminal->window;
    info->memory_bytes = sizeof(*g_terminal) +
                         (g_terminal->window ? (uint32_t)sizeof(gui_window_t) : 0);
    return true;
}

void shelllauncher_open_from_desktop(gui_desktop_t *desktop) {
    terminal_state_t *st;

    if (!desktop) return;

    st = (terminal_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_terminal = st;
    if (bk_proc_spawn_thread("shell", terminal_main, st) < 0) {
        g_terminal = NULL;
        bk_sys_free(st);
    }
}

void shelllauncher_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    shelllauncher_open_from_desktop(desktop);
}
