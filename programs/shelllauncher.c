#include "programs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/shell.h"
#include "../kernel/include/task.h"
#include "../kernel/include/vga.h"
#include "../kernel/include/vfs.h"

#define TERM_COLS 52
#define TERM_ROWS 14

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_event_queue_t events;
    char lines[TERM_ROWS][TERM_COLS + 1];
    char input[SHELL_MAX_CMD];
    uint32_t input_len;
    uint32_t output_col;
    bool execute_requested;
} terminal_state_t;

static terminal_state_t *g_terminal;

static void terminal_scroll(terminal_state_t *st) {
    for (int i = 0; i < TERM_ROWS - 1; i++)
        kstrncpy(st->lines[i], st->lines[i + 1], TERM_COLS + 1);
    kmemset(st->lines[TERM_ROWS - 1], 0, TERM_COLS + 1);
    st->output_col = 0;
}

static void terminal_newline(terminal_state_t *st) {
    terminal_scroll(st);
}

static void terminal_output_char(char c, void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st || c == '\r') return;
    if (c == '\n') {
        terminal_newline(st);
        return;
    }
    if (c == '\b') {
        if (st->output_col)
            st->lines[TERM_ROWS - 1][--st->output_col] = '\0';
        return;
    }
    if (c < 32) return;
    if (st->output_col >= TERM_COLS) terminal_newline(st);
    st->lines[TERM_ROWS - 1][st->output_col++] = c;
    st->lines[TERM_ROWS - 1][st->output_col] = '\0';
}

static void terminal_output_clear(void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st) return;
    kmemset(st->lines, 0, sizeof(st->lines));
    st->output_col = 0;
}

static void terminal_write(terminal_state_t *st, const char *text) {
    while (text && *text) terminal_output_char(*text++, st);
}

static void terminal_execute(terminal_state_t *st) {
    terminal_write(st, vfs_getcwd());
    terminal_write(st, "> ");
    terminal_write(st, st->input);
    terminal_output_char('\n', st);

    vga_set_output_sink(terminal_output_char, terminal_output_clear, st);
    shell_execute_line(st->input);
    vga_set_output_sink(NULL, NULL, NULL);

    st->input_len = 0;
    st->input[0] = '\0';
    st->execute_requested = false;
    if (st->window) st->window->dirty = true;
}

static void terminal_content(gui_window_t *window UNUSED,
                             gui_surface_t *surface, void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st || !st->window || !st->window->visible) return;
    int x = st->window->bounds.x + 7;
    int y = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 5;
    int w = st->window->bounds.w - 14;
    int h = st->window->bounds.h - GUI_TITLEBAR_HEIGHT - 10;

    gui_gfx_fill_rect(surface, (gui_rect_t){x, y, w, h}, 0x00000000);
    for (int i = 0; i < TERM_ROWS; i++)
        gui_font_draw_string_clipped(surface, x + 4, y + 4 + i * 10,
                                     st->lines[i], 0x00FFFFFF,
                                     (gui_rect_t){x + 3, y + 2, w - 6, h - 18});

    gui_font_draw_string(surface, x + 4, y + h - 13, vfs_getcwd(),
                         0x00FFFFFF, 0, false);
    int prompt_x = x + 8 + (int)gui_font_text_width(vfs_getcwd());
    gui_font_draw_string(surface, prompt_x, y + h - 13, ">", 0x00FFFFFF, 0, false);
    gui_font_draw_string_clipped(surface, prompt_x + 10, y + h - 13,
                                 st->input, 0x00FFFFFF,
                                 (gui_rect_t){x + 3, y + h - 15, w - 6, 12});
}

static bool terminal_window_event(gui_window_t *window UNUSED,
                                  const gui_event_t *event,
                                  void *context) {
    terminal_state_t *st = (terminal_state_t *)context;
    if (!st || !event || event->type != GUI_EVENT_KEY) return false;
    return gui_event_queue_push(&st->events, event);
}

static void terminal_process_event(terminal_state_t *st,
                                   const gui_event_t *event) {
    if (!st || !event) return;
    if (event->key == '\n') {
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
    if (st->window) {
        gui_desktop_remove_window(st->desktop, st->window);
        gui_window_destroy(st->window);
        task_bind_window(NULL);
    }
    if (g_terminal == st) g_terminal = NULL;
    kfree(st);
}

static void terminal_main(void *argument) {
    terminal_state_t *st = (terminal_state_t *)argument;
    gui_event_t event;

    if (!st || !st->desktop) {
        terminal_cleanup(st);
        task_exit();
    }

    task_set_memory_hint(sizeof(*st));
    gui_event_queue_reset(&st->events);
    st->window = gui_desktop_create_window(st->desktop, 80, 54, 440, 210,
                                           "Shell");
    if (st->window) {
        gui_window_set_content(st->window, terminal_content, st);
        gui_window_set_event_handler(st->window, terminal_window_event, st);
        gui_window_set_min_size(st->window, 260, 150);
        st->window->owner_pid = task_current_pid();
        task_bind_window(st->window);
    }
    terminal_write(st, "BlesKernOS Shell - motor shell.c\n");
    terminal_write(st, "Escribe help para ver todos los comandos.\n");

    while (!task_exit_requested()) {
        while (gui_event_queue_pop(&st->events, &event)) {
            terminal_process_event(st, &event);
        }
        if (st->execute_requested) terminal_execute(st);
        if (!st->window || !st->window->listed) break;
        task_sleep(1);
    }

    terminal_cleanup(st);
    task_exit();
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

    st = (terminal_state_t *)kzalloc(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_terminal = st;
    if (task_create("shell", terminal_main, st) < 0) {
        g_terminal = NULL;
        kfree(st);
    }
}

void shelllauncher_install(gui_desktop_t *desktop UNUSED) {}
