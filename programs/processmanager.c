#include "../kernel/include/api.h"

#define PM_HISTORY 36
#define PM_ROW_HEIGHT 15
#define PM_HEADER_HEIGHT 16
#define PM_BUTTON_HEIGHT 20
#define PM_BUTTON_WIDTH 108

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint8_t cpu[PM_HISTORY];
    uint8_t ram[PM_HISTORY];
    uint32_t last_sample;
    uint32_t last_refresh;
    uint32_t selected_pid;
    bool finish_pressed;
} processmanager_state_t;

typedef struct {
    gui_rect_t cpu_label;
    gui_rect_t cpu_graph;
    gui_rect_t ram_label;
    gui_rect_t ram_graph;
    gui_rect_t header;
    gui_rect_t rows;
    gui_rect_t action;
    gui_rect_t button;
    int row_height;
    int row_count;
} pm_layout_t;

static processmanager_state_t *g_processmanager;

static void pm_number(char *out, uint32_t value) {
    char tmp[12];
    int pos = 11;
    tmp[pos] = '\0';
    if (!value) tmp[--pos] = '0';
    while (value) {
        tmp[--pos] = (char)('0' + value % 10);
        value /= 10;
    }
    bk_runtime_strcpy(out, &tmp[pos]);
}

static void pm_kb_label(char *out, uint32_t bytes) {
    char number[12];
    uint32_t kb = (bytes + 1023U) / 1024U;
    pm_number(number, kb);
    bk_runtime_strcpy(out, number);
    bk_runtime_strcat(out, " KB");
}

static void pm_size_label(char *out, uint32_t bytes) {
    char number[12];

    if (bytes > MEMORY_DISPLAY_MB_THRESHOLD) {
        pm_number(number, (bytes + ((1024U * 1024U) - 1U)) / (1024U * 1024U));
        bk_runtime_strcpy(out, number);
        bk_runtime_strcat(out, " MB");
        return;
    }

    pm_kb_label(out, bytes);
}

static uint8_t pm_percent(uint32_t used, uint32_t total) {
    uint32_t step;
    uint32_t percent;

    if (!total) return 0;
    if (used >= total) return 100;
    if (total < 100U) return (uint8_t)((used * 100U) / total);

    step = (total + 99U) / 100U;
    percent = used / step;
    if (percent > 100U) percent = 100U;
    return (uint8_t)percent;
}

static void pm_sample(processmanager_state_t *st) {
    uint32_t now = bk_sys_ticks();
    uint32_t interval = bk_sys_tick_frequency() / 4U;
    if (!interval) interval = 1;
    if (st->last_sample && now - st->last_sample < interval) return;
    system_memory_info_t info;
    mm_get_system_info(&info);
    for (int i = 0; i < PM_HISTORY - 1; i++) {
        st->cpu[i] = st->cpu[i + 1];
        st->ram[i] = st->ram[i + 1];
    }
    st->cpu[PM_HISTORY - 1] = bk_proc_cpu_usage();
    st->ram[PM_HISTORY - 1] = pm_percent((uint32_t)info.used_bytes,
                                         (uint32_t)info.total_bytes);
    st->last_sample = now;
}

static void pm_graph(gui_surface_t *surface, gui_rect_t r,
                     const uint8_t *values, uint32_t color) {
    if (r.w < 4 || r.h < 4) return;
    bk_gui_gfx_fill_rect(surface, r, 0x00182020);
    bk_gui_gfx_draw_rect(surface, r, 0x00607070);
    for (int line = 1; line < 4; line++) {
        bk_gui_gfx_fill_rect(surface,
            (gui_rect_t){r.x + 1, r.y + line * r.h / 4, r.w - 2, 1},
            0x00283838);
    }
    for (int i = 1; i < PM_HISTORY; i++) {
        int x0 = r.x + 2 + (i - 1) * (r.w - 4) / (PM_HISTORY - 1);
        int x1 = r.x + 2 + i * (r.w - 4) / (PM_HISTORY - 1);
        int y0 = r.y + r.h - 2 - values[i - 1] * (r.h - 4) / 100;
        int y1 = r.y + r.h - 2 - values[i] * (r.h - 4) / 100;
        bk_gui_gfx_draw_line(surface, x0, y0, x1, y1, color);
    }
}

static void pm_draw_button(gui_surface_t *surface, gui_rect_t rect,
                           const char *label, bool enabled, bool pressed) {
    uint32_t border = enabled ? 0x00404040 : 0x00707070;
    uint32_t face = enabled ? (pressed ? 0x00B8B8B0 : 0x00D0D0C8) : 0x00C8C8C0;
    gui_rect_t inner = {rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2};

    bk_gui_gfx_fill_rect(surface, rect, border);
    bk_gui_gfx_fill_rect(surface, inner, face);
    if (pressed) {
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, inner.w, 1},
                          0x00606060);
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, 1, inner.h},
                          0x00606060);
    } else {
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, inner.w, 1},
                          0x00FFFFFF);
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){inner.x, inner.y, 1, inner.h},
                          0x00FFFFFF);
        bk_gui_gfx_fill_rect(surface,
                          (gui_rect_t){inner.x, inner.y + inner.h - 1,
                                       inner.w, 1},
                          0x00707070);
        bk_gui_gfx_fill_rect(surface,
                          (gui_rect_t){inner.x + inner.w - 1, inner.y,
                                       1, inner.h},
                          0x00707070);
    }

    int text_x = rect.x + (rect.w - (int)bk_gui_font_text_width(label)) / 2;
    if (text_x < rect.x + 3) text_x = rect.x + 3;
    bk_gui_font_draw_string_clipped(surface, text_x, rect.y + 5, label,
                                 enabled ? 0x00243B4E : 0x00607070,
                                 (gui_rect_t){rect.x + 3, rect.y + 2,
                                              rect.w - 6, rect.h - 4});
}

static void pm_layout_build(const processmanager_state_t *st,
                            pm_layout_t *layout) {
    gui_rect_t bounds = st->window->bounds;
    int x = bounds.x + 8;
    int y = bk_gui_window_content_rect_raw(st->window).y + 6;
    int w = bounds.w - 16;
    int bottom = bounds.y + bounds.h - GUI_BORDER_SIZE - 8;
    int body_h = bottom - y;
    int graph_h = body_h / 7;
    int button_w;

    if (graph_h < 22) graph_h = 22;
    if (graph_h > 42) graph_h = 42;

    layout->cpu_label = (gui_rect_t){x, y, w, 10};
    layout->cpu_graph = (gui_rect_t){x, y + 12, w, graph_h};
    layout->ram_label = (gui_rect_t){x, layout->cpu_graph.y + graph_h + 6, w, 10};
    layout->ram_graph = (gui_rect_t){x, layout->ram_label.y + 12, w, graph_h};
    layout->header = (gui_rect_t){x, layout->ram_graph.y + graph_h + 8,
                                  w, PM_HEADER_HEIGHT};

    layout->action = (gui_rect_t){x, bottom - PM_BUTTON_HEIGHT, w,
                                  PM_BUTTON_HEIGHT};
    if (layout->action.y < layout->header.y + PM_HEADER_HEIGHT + PM_ROW_HEIGHT + 6)
        layout->action.y = layout->header.y + PM_HEADER_HEIGHT +
                           PM_ROW_HEIGHT + 6;

    layout->rows = (gui_rect_t){x, layout->header.y + PM_HEADER_HEIGHT,
                                w, layout->action.y -
                                (layout->header.y + PM_HEADER_HEIGHT) - 6};
    if (layout->rows.h < PM_ROW_HEIGHT) layout->rows.h = PM_ROW_HEIGHT;

    button_w = w < 240 ? 90 : PM_BUTTON_WIDTH;
    if (button_w > w - 10) button_w = w - 10;
    if (button_w < 72) button_w = 72;
    layout->button = (gui_rect_t){x + w - button_w, layout->action.y,
                                  button_w, PM_BUTTON_HEIGHT};
    layout->row_height = PM_ROW_HEIGHT;
    layout->row_count = layout->rows.h / PM_ROW_HEIGHT;
    if (layout->row_count < 1) layout->row_count = 1;
}

static gui_rect_t pm_row_rect(const pm_layout_t *layout, int row) {
    return (gui_rect_t){layout->rows.x,
                        layout->rows.y + row * layout->row_height,
                        layout->rows.w, layout->row_height};
}

static int pm_hit_row(const pm_layout_t *layout, uint32_t count,
                      int x, int y) {
    for (uint32_t row = 0; row < count && (int)row < layout->row_count; row++)
        if (bk_gui_rect_contains(pm_row_rect(layout, (int)row), x, y))
            return (int)row;
    return -1;
}

static const char *pm_task_label(const task_t *task) {
    if (!task) return "";
    if (task->window && task->window->title[0]) return task->window->title;
    return task->name;
}

static bool pm_can_terminate(const task_t *task) {
    return task && !task->system && !task->idle;
}

static int pm_selected_index(const processmanager_state_t *st,
                             uint32_t *row_to_task, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const task_t *task = bk_proc_get_legacy(row_to_task[i]);
        if (task && task->pid == st->selected_pid) return (int)i;
    }
    return -1;
}

static uint32_t pm_collect_rows(uint32_t *row_to_task, uint32_t max) {
    uint32_t total = bk_proc_count();
    uint32_t count = 0;
    for (uint32_t i = 0; i < total && count < max; i++) {
        if (!bk_proc_get_legacy(i)) continue;
        row_to_task[count++] = i;
    }
    return count;
}

static void pm_validate_selection(processmanager_state_t *st,
                                  uint32_t *row_to_task, uint32_t count) {
    if (pm_selected_index(st, row_to_task, count) >= 0) return;
    st->selected_pid = 0;
}

bool processmanager_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_processmanager) return false;
    info->window = g_processmanager->window;
    info->memory_bytes = (uint32_t)sizeof(*g_processmanager);
    if (g_processmanager->window)
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
    return true;
}

static void pm_content(gui_window_t *window UNUSED, gui_surface_t *surface,
                       void *context) {
    processmanager_state_t *st = (processmanager_state_t *)context;
    if (!st || !st->window || !st->desktop) return;

    pm_layout_t layout;
    system_memory_info_t info;
    char cpu_label[24] = "CPU sistema: ";
    char ram_label[56] = "RAM sistema: ";
    char number[12];
    char mem_text[16];
    char total_text[16];
    char footer[96];
    uint32_t rows[16];
    uint32_t count;
    int selected;

    pm_sample(st);
    pm_layout_build(st, &layout);
    count = pm_collect_rows(rows, layout.row_count > 16 ? 16U : (uint32_t)layout.row_count);
    pm_validate_selection(st, rows, count);
    selected = pm_selected_index(st, rows, count);

    mm_get_system_info(&info);
    pm_number(number, bk_proc_cpu_usage());
    bk_runtime_strcat(cpu_label, number);
    bk_runtime_strcat(cpu_label, "%");
    pm_size_label(mem_text, (uint32_t)info.used_bytes);
    pm_size_label(total_text, (uint32_t)info.total_bytes);
    bk_runtime_strcat(ram_label, mem_text);
    bk_runtime_strcat(ram_label, " / ");
    bk_runtime_strcat(ram_label, total_text);

    bk_gui_font_draw_string_clipped(surface, layout.cpu_label.x, layout.cpu_label.y,
                                 cpu_label, 0x00102020, layout.cpu_label);
    pm_graph(surface, layout.cpu_graph, st->cpu, 0x0048E080);
    bk_gui_font_draw_string_clipped(surface, layout.ram_label.x, layout.ram_label.y,
                                 ram_label, 0x00102020, layout.ram_label);
    pm_graph(surface, layout.ram_graph, st->ram, 0x0060A8F0);

    bk_gui_gfx_fill_rect(surface, layout.header, 0x00889098);

    int pid_w = layout.header.w < 250 ? 34 : 38;
    int mem_w = layout.header.w < 300 ? 58 : 70;
    int state_w = layout.header.w < 300 ? 74 : 88;
    int state_x = layout.header.x + layout.header.w - state_w - mem_w - 6;
    int mem_x = layout.header.x + layout.header.w - mem_w - 4;
    int name_x = layout.header.x + pid_w + 8;

    bk_gui_font_draw_string_clipped(surface, layout.header.x + 4, layout.header.y + 4,
                                 "PID", 0x00FFFFFF,
                                 (gui_rect_t){layout.header.x + 2, layout.header.y + 2,
                                              pid_w, 12});
    bk_gui_font_draw_string_clipped(surface, name_x, layout.header.y + 4,
                                 "PROCESO", 0x00FFFFFF,
                                 (gui_rect_t){name_x, layout.header.y + 2,
                                              state_x - name_x - 4, 12});
    bk_gui_font_draw_string_clipped(surface, state_x, layout.header.y + 4,
                                 "ESTADO", 0x00FFFFFF,
                                 (gui_rect_t){state_x, layout.header.y + 2,
                                              state_w, 12});
    bk_gui_font_draw_string_clipped(surface, mem_x, layout.header.y + 4,
                                 "RAM EST.", 0x00FFFFFF,
                                 (gui_rect_t){mem_x, layout.header.y + 2,
                                              mem_w, 12});

    for (uint32_t i = 0; i < count && (int)i < layout.row_count; i++) {
        const task_t *task = bk_proc_get_legacy(rows[i]);
        gui_rect_t row_rect = pm_row_rect(&layout, (int)i);
        bool is_selected = task && task->pid == st->selected_pid;
        if (!task) continue;
        pm_number(number, task->pid);
        pm_size_label(mem_text, task->memory_bytes);

        bk_gui_gfx_fill_rect(surface, row_rect,
                          is_selected ? 0x00DCE9F7
                                      : ((i & 1U) ? 0x00E8E8E0 : 0x00F8F8F0));
        bk_gui_font_draw_string_clipped(surface, row_rect.x + 4, row_rect.y + 4,
                                     number, 0x00203030,
                                     (gui_rect_t){row_rect.x + 2, row_rect.y + 2,
                                                  pid_w, 12});
        bk_gui_font_draw_string_clipped(surface, name_x, row_rect.y + 4,
                                     pm_task_label(task), 0x00203030,
                                     (gui_rect_t){name_x, row_rect.y + 2,
                                                  state_x - name_x - 4, 12});
        bk_gui_font_draw_string_clipped(surface, state_x, row_rect.y + 4,
                                     bk_proc_state_name(task->state),
                                     task->system ? 0x00505050 : 0x00007030,
                                     (gui_rect_t){state_x, row_rect.y + 2,
                                                  state_w, 12});
        bk_gui_font_draw_string_clipped(surface, mem_x, row_rect.y + 4,
                                     mem_text, 0x00203030,
                                     (gui_rect_t){mem_x, row_rect.y + 2,
                                                  mem_w, 12});
    }

    if (selected >= 0) {
        const task_t *task = bk_proc_get_legacy(rows[selected]);
        pm_size_label(mem_text, task ? task->memory_bytes : 0);
        bk_runtime_strcpy(footer, task ? pm_task_label(task) : "");
        bk_runtime_strcat(footer, " | ");
        bk_runtime_strcat(footer, task ? bk_proc_state_name(task->state) : "");
        bk_runtime_strcat(footer, " | ");
        bk_runtime_strcat(footer, mem_text);
    } else {
        bk_runtime_strcpy(footer, "Tareas: ");
        pm_number(number, bk_proc_count());
        bk_runtime_strcat(footer, number);
    }

    bk_gui_font_draw_string_clipped(surface, layout.action.x + 2, layout.action.y + 5,
                                 footer, 0x00283C4A,
                                 (gui_rect_t){layout.action.x + 2, layout.action.y + 2,
                                              layout.button.x - layout.action.x - 8,
                                              layout.action.h - 4});
    pm_draw_button(surface, layout.button, "Finalizar",
                   selected >= 0 && pm_can_terminate(bk_proc_get_legacy(rows[selected])),
                   st->finish_pressed);
}

static bool pm_event(gui_window_t *window UNUSED, const gui_event_t *event,
                     void *context) {
    processmanager_state_t *st = (processmanager_state_t *)context;
    pm_layout_t layout;
    uint32_t rows[16];
    uint32_t count;
    int selected;
    bool inside;

    if (!st || !st->window || !st->desktop || !event) return false;
    if (event->type != GUI_EVENT_MOUSE_DOWN &&
        event->type != GUI_EVENT_MOUSE_UP) return false;

    pm_layout_build(st, &layout);
    count = pm_collect_rows(rows, layout.row_count > 16 ? 16U : (uint32_t)layout.row_count);
    pm_validate_selection(st, rows, count);
    selected = pm_selected_index(st, rows, count);
    inside = bk_gui_rect_contains(st->window->bounds, event->x, event->y);

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (inside && selected >= 0 &&
            pm_can_terminate(bk_proc_get_legacy(rows[selected])) &&
            bk_gui_rect_contains(layout.button, event->x, event->y)) {
            st->finish_pressed = true;
            st->window->dirty = true;
        } else if (st->finish_pressed) {
            st->finish_pressed = false;
            st->window->dirty = true;
        }
        return false;
    }

    if (st->finish_pressed) {
        bool pressed = bk_gui_rect_contains(layout.button, event->x, event->y);
        st->finish_pressed = false;
        st->window->dirty = true;
        if (pressed && selected >= 0) {
            const task_t *task = bk_proc_get_legacy(rows[selected]);
            if (pm_can_terminate(task)) {
                bk_proc_request_exit(task->pid);
                return true;
            }
        }
    }

    if (!inside) return false;

    int row = pm_hit_row(&layout, count, event->x, event->y);
    if (row >= 0 && (uint32_t)row < count) {
        const task_t *task = bk_proc_get_legacy(rows[row]);
        if (task) {
            st->selected_pid = task->pid;
            st->window->dirty = true;
            return true;
        }
    }
    return false;
}

static void pm_cleanup(processmanager_state_t *st) {
    if (!st) return;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (g_processmanager == st) g_processmanager = NULL;
    bk_sys_free(st);
}

static void pm_main(void *argument) {
    processmanager_state_t *st = (processmanager_state_t *)argument;
    uint32_t refresh_ticks;
    uint32_t sleep_ticks;
    if (!st || !st->desktop) {
        pm_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st));
    st->window = bk_gui_create_window(st->desktop, 145, 35, 350, 290,
                                           "Administrador de procesos");
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "Administrador de procesos", "Version 1.0",
            "Procesos y uso de memoria del sistema.", "Bles.INC (C) 2026",
            "/ICONS/PROCESOS.BMP"});
        bk_gui_set_window_min_size(st->window, 270, 220);
        bk_gui_set_window_content(st->window, pm_content, st);
        bk_gui_set_window_event_handler(st->window, pm_event, st);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    }

    refresh_ticks = bk_sys_tick_frequency() / 4U;
    if (!refresh_ticks) refresh_ticks = 1;
    sleep_ticks = bk_sys_tick_frequency() / 60U;
    if (!sleep_ticks) sleep_ticks = 1;

    while (!bk_proc_exit_requested()) {
        uint32_t now;
        if (!st->window || !st->window->listed) break;
        now = bk_sys_ticks();
        if (!st->last_refresh || now - st->last_refresh >= refresh_ticks) {
            st->last_refresh = now;
            bk_gui_window_invalidate(st->window);
        }
        bk_sys_sleep_ticks(sleep_ticks);
    }

    pm_cleanup(st);
    bk_proc_exit();
}

void processmanager_open_from_desktop(gui_desktop_t *desktop) {
    processmanager_state_t *st;

    if (!desktop) return;

    st = (processmanager_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_processmanager = st;
    if (bk_proc_spawn_thread("processmgr", pm_main, st) < 0) {
        pm_cleanup(st);
    }
}

void processmanager_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    processmanager_open_from_desktop(desktop);
}
