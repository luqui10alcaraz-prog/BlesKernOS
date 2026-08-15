#include "../kernel/include/api.h"

#define PM_HISTORY 36
#define PM_ROW_HEIGHT 15
#define PM_HEADER_HEIGHT 16
#define PM_BUTTON_HEIGHT 20
#define PM_BUTTON_WIDTH 108
#define PM_MAX_TASKS 32
#define PM_MAX_CPUS 16
#define PM_CPU_COLUMNS_NORMAL 2
#define PM_CPU_COLUMNS_WIDE 4

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint8_t cpu[PM_HISTORY];
    uint8_t cpu_core[PM_MAX_CPUS][PM_HISTORY];
    uint8_t ram[PM_HISTORY];
    uint32_t cpu_count;
    uint32_t last_sample;
    uint32_t last_refresh;
    uint32_t selected_pid;
    uint32_t scroll_offset;
    gui_scrollbar_drag_t scroll_drag;
    gui_image_t finish_icon;
    bool finish_icon_loaded;
    bool finish_pressed;
    volatile uint32_t closing;
} processmanager_state_t;

typedef struct {
    gui_rect_t cpu_label;
    gui_rect_t cpu_graph;
    gui_rect_t cpu_core_label[PM_MAX_CPUS];
    gui_rect_t cpu_core_graph[PM_MAX_CPUS];
    gui_rect_t ram_label;
    gui_rect_t ram_graph;
    gui_rect_t header;
    gui_rect_t rows;
    gui_rect_t scrollbar;
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

static uint32_t pm_cpu_count(void) {
    uint32_t count = bk_proc_cpu_count();
    if (!count) count = 1U;
    if (count > PM_MAX_CPUS) count = PM_MAX_CPUS;
    return count;
}

static void pm_history_shift(uint8_t values[PM_HISTORY]) {
    for (int i = 0; i < PM_HISTORY - 1; i++)
        values[i] = values[i + 1];
}

static uint32_t pm_cpu_color(uint32_t cpu) {
    static const uint32_t colors[8] = {
        0x0048E080U, 0x00E0A040U, 0x0060A8F0U, 0x00D060A8U,
        0x0090C050U, 0x00D08050U, 0x005088D0U, 0x00A870C0U
    };
    return colors[cpu & 7U];
}

static void pm_sample(processmanager_state_t *st) {
    uint32_t now = bk_sys_ticks();
    uint32_t interval = bk_sys_tick_frequency() / 4U;
    system_memory_info_t info;

    if (!interval) interval = 1;
    if (st->last_sample && now - st->last_sample < interval) return;

    st->cpu_count = pm_cpu_count();
    mm_get_system_info(&info);
    pm_history_shift(st->cpu);
    pm_history_shift(st->ram);
    for (uint32_t cpu = 0U; cpu < PM_MAX_CPUS; cpu++) {
        pm_history_shift(st->cpu_core[cpu]);
        st->cpu_core[cpu][PM_HISTORY - 1] =
            cpu < st->cpu_count
                ? (uint8_t)bk_proc_cpu_usage_core(cpu) : 0U;
    }
    st->cpu[PM_HISTORY - 1] = (uint8_t)bk_proc_cpu_usage();
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
    {
        gui_point_t points[PM_HISTORY];
        for (int i = 0; i < PM_HISTORY; i++) {
            points[i].x = r.x + 2 + i * (r.w - 4) / (PM_HISTORY - 1);
            points[i].y = r.y + r.h - 2 -
                values[i] * (r.h - 4) / 100;
        }
        bk_gui_surface_draw_polyline(surface, points, PM_HISTORY, color);
    }
}

static void pm_draw_button(gui_surface_t *surface, gui_rect_t rect,
                           const char *label, bool enabled, bool pressed,
                           const gui_image_t *icon) {
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

    int text_x;
    int group_width = (int)bk_gui_font_text_width(label);
    if (icon && icon->pixels) group_width += 20;
    text_x = rect.x + (rect.w - group_width) / 2;
    if (text_x < rect.x + 3) text_x = rect.x + 3;
    if (icon && icon->pixels) {
        bk_gui_surface_draw_image(surface,
            (gui_rect_t){text_x + (pressed ? 1 : 0),
                         rect.y + 2 + (pressed ? 1 : 0), 16, 16},
            rect, icon);
        text_x += 20;
    }
    bk_gui_font_draw_string_clipped(surface, text_x, rect.y + 5, label,
                                 enabled ? 0x00243B4E : 0x00607070,
                                 (gui_rect_t){rect.x + 3, rect.y + 2,
                                              rect.w - 6, rect.h - 4});
}

static void pm_layout_build(const processmanager_state_t *st,
                            pm_layout_t *layout) {
    gui_rect_t bounds = st->window->bounds;
    uint32_t cpu_count = st->cpu_count ? st->cpu_count : pm_cpu_count();
    int x = bounds.x + 8;
    int y = bk_gui_window_content_rect_raw(st->window).y + 6;
    int w = bounds.w - 16;
    int bottom = bounds.y + bounds.h - GUI_BORDER_SIZE - 8;
    int body_h = bottom - y;
    int graph_h;
    int cpu_bottom;
    int button_w;
    int table_w;
    uint32_t cpu_columns;

    if (cpu_count > PM_MAX_CPUS) cpu_count = PM_MAX_CPUS;
    cpu_columns = cpu_count > 4U ? PM_CPU_COLUMNS_WIDE
                                 : PM_CPU_COLUMNS_NORMAL;
    if (cpu_count > 8U) graph_h = 22;
    else if (cpu_count > 4U) graph_h = 25;
    else graph_h = body_h / (cpu_count > 2U ? 12
                              : (cpu_count > 1U ? 10 : 7));
    if (graph_h < 20) graph_h = 20;
    if (graph_h > 38) graph_h = 38;

    layout->cpu_label = (gui_rect_t){x, y, w, 10};
    layout->cpu_graph = (gui_rect_t){0, 0, 0, 0};
    for (uint32_t cpu = 0U; cpu < PM_MAX_CPUS; cpu++) {
        layout->cpu_core_label[cpu] = (gui_rect_t){0, 0, 0, 0};
        layout->cpu_core_graph[cpu] = (gui_rect_t){0, 0, 0, 0};
    }

    if (cpu_count <= 1U) {
        layout->cpu_graph = (gui_rect_t){x, y + 12, w, graph_h};
        cpu_bottom = layout->cpu_graph.y + layout->cpu_graph.h;
    } else {
        const int gap_x = 6;
        const int gap_y = 5;
        int cell_w = (w - ((int)cpu_columns - 1) * gap_x) /
                     (int)cpu_columns;
        int start_y = y + 12;
        uint32_t rows = (cpu_count + cpu_columns - 1U) / cpu_columns;

        for (uint32_t cpu = 0U; cpu < cpu_count; cpu++) {
            int column = (int)(cpu % cpu_columns);
            int row = (int)(cpu / cpu_columns);
            int cell_x = x + column * (cell_w + gap_x);
            int cell_y = start_y + row * (graph_h + 15 + gap_y);
            int width = column == (int)cpu_columns - 1
                ? x + w - cell_x : cell_w;
            layout->cpu_core_label[cpu] =
                (gui_rect_t){cell_x, cell_y, width, 10};
            layout->cpu_core_graph[cpu] =
                (gui_rect_t){cell_x, cell_y + 11, width, graph_h};
        }
        cpu_bottom = start_y + (int)rows * (graph_h + 15) +
                     ((int)rows - 1) * gap_y;
    }

    layout->ram_label = (gui_rect_t){x, cpu_bottom + 6, w, 10};
    layout->ram_graph = (gui_rect_t){x, layout->ram_label.y + 12, w, graph_h};

    table_w = w - GUI_SCROLLBAR_SIZE;
    if (table_w < 120) table_w = w;
    layout->header = (gui_rect_t){x, layout->ram_graph.y + graph_h + 8,
                                  table_w, PM_HEADER_HEIGHT};

    layout->action = (gui_rect_t){x, bottom - PM_BUTTON_HEIGHT, w,
                                  PM_BUTTON_HEIGHT};
    if (layout->action.y < layout->header.y + PM_HEADER_HEIGHT + PM_ROW_HEIGHT + 6)
        layout->action.y = layout->header.y + PM_HEADER_HEIGHT +
                           PM_ROW_HEIGHT + 6;

    layout->rows = (gui_rect_t){x, layout->header.y + PM_HEADER_HEIGHT,
                                table_w, layout->action.y -
                                (layout->header.y + PM_HEADER_HEIGHT) - 6};
    if (layout->rows.h < PM_ROW_HEIGHT) layout->rows.h = PM_ROW_HEIGHT;
    layout->scrollbar = (gui_rect_t){layout->rows.x + layout->rows.w,
                                     layout->rows.y,
                                     w - layout->rows.w,
                                     layout->rows.h};

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

static const char *pm_task_label(const bk_proc_info_t *task) {
    return task ? task->name : "";
}

static bool pm_can_terminate(const bk_proc_info_t *task) {
    return task && !task->system && task->pid != 0U;
}

static int pm_selected_index(const processmanager_state_t *st,
                             const bk_proc_info_t *rows, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        if (rows[i].pid == st->selected_pid) return (int)i;
    return -1;
}

static uint32_t pm_collect_rows(bk_proc_info_t *rows, uint32_t max) {
    return bk_proc_snapshot(rows, max);
}

static void pm_validate_selection(processmanager_state_t *st,
                                  const bk_proc_info_t *rows,
                                  uint32_t count) {
    if (pm_selected_index(st, rows, count) >= 0) return;
    st->selected_pid = 0;
}

static void pm_validate_scroll(processmanager_state_t *st, uint32_t count,
                               uint32_t visible) {
    uint32_t maximum = count > visible ? count - visible : 0U;
    if (st->scroll_offset > maximum) st->scroll_offset = maximum;
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
    if (!st || st->closing || !st->window || !st->desktop) return;

    pm_layout_t layout;
    system_memory_info_t info;
    char cpu_label[112] = "CPU sistema: ";
    char ram_label[56] = "RAM sistema: ";
    char number[12];
    char mem_text[16];
    char total_text[16];
    char footer[96];
    bk_proc_info_t rows[PM_MAX_TASKS];
    gui_scrollbar_t scrollbar;
    uint32_t count;
    int selected;

    pm_sample(st);
    pm_layout_build(st, &layout);
    count = pm_collect_rows(rows, PM_MAX_TASKS);
    pm_validate_scroll(st, count, (uint32_t)layout.row_count);
    pm_validate_selection(st, rows, count);
    selected = pm_selected_index(st, rows, count);
    bk_gui_scrollbar_init_vertical(&scrollbar, layout.scrollbar,
                                   st->scroll_offset,
                                   (uint32_t)layout.row_count, count);

    mm_get_system_info(&info);
    pm_number(number, st->cpu[PM_HISTORY - 1]);
    bk_runtime_strcat(cpu_label, number);
    bk_runtime_strcat(cpu_label, "%");
    if (st->cpu_count > 1U) {
        uint32_t rq_total = 0U;
        uint32_t steals = 0U;
        bk_runtime_strcat(cpu_label, " | ");
        pm_number(number, st->cpu_count);
        bk_runtime_strcat(cpu_label, number);
        bk_runtime_strcat(cpu_label, " nucleos");
        for (uint32_t cpu = 0U; cpu < st->cpu_count; cpu++) {
            rq_total += bk_proc_runqueue_depth(cpu);
            steals += bk_proc_scheduler_steals(cpu);
        }
        bk_runtime_strcat(cpu_label, " | RQ ");
        pm_number(number, rq_total);
        bk_runtime_strcat(cpu_label, number);
        bk_runtime_strcat(cpu_label, " | robos ");
        pm_number(number, steals);
        bk_runtime_strcat(cpu_label, number);
    }
    pm_size_label(mem_text, (uint32_t)info.used_bytes);
    pm_size_label(total_text, (uint32_t)info.total_bytes);
    bk_runtime_strcat(ram_label, mem_text);
    bk_runtime_strcat(ram_label, " / ");
    bk_runtime_strcat(ram_label, total_text);

    bk_gui_font_draw_string_clipped(surface, layout.cpu_label.x, layout.cpu_label.y,
                                 cpu_label, 0x00102020, layout.cpu_label);
    if (st->cpu_count <= 1U) {
        pm_graph(surface, layout.cpu_graph, st->cpu, pm_cpu_color(0U));
    } else {
        for (uint32_t cpu = 0U; cpu < st->cpu_count &&
             cpu < PM_MAX_CPUS; cpu++) {
            char core_label[32] = "CPU ";
            pm_number(number, cpu + 1U);
            bk_runtime_strcat(core_label, number);
            bk_runtime_strcat(core_label, ": ");
            pm_number(number, st->cpu_core[cpu][PM_HISTORY - 1]);
            bk_runtime_strcat(core_label, number);
            bk_runtime_strcat(core_label, "%");
            bk_gui_font_draw_string_clipped(
                surface, layout.cpu_core_label[cpu].x,
                layout.cpu_core_label[cpu].y, core_label,
                pm_cpu_color(cpu), layout.cpu_core_label[cpu]);
            pm_graph(surface, layout.cpu_core_graph[cpu],
                     st->cpu_core[cpu], pm_cpu_color(cpu));
        }
    }
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
                                 "@HE5796C06", 0x00FFFFFF,
                                 (gui_rect_t){name_x, layout.header.y + 2,
                                              state_x - name_x - 4, 12});
    bk_gui_font_draw_string_clipped(surface, state_x, layout.header.y + 4,
                                 "@H1CE4E6B7", 0x00FFFFFF,
                                 (gui_rect_t){state_x, layout.header.y + 2,
                                              state_w, 12});
    bk_gui_font_draw_string_clipped(surface, mem_x, layout.header.y + 4,
                                 "@H4B06B757", 0x00FFFFFF,
                                 (gui_rect_t){mem_x, layout.header.y + 2,
                                              mem_w, 12});

    for (uint32_t visible = 0;
         visible < (uint32_t)layout.row_count &&
         st->scroll_offset + visible < count; visible++) {
        uint32_t index = st->scroll_offset + visible;
        const bk_proc_info_t *task = &rows[index];
        gui_rect_t row_rect = pm_row_rect(&layout, (int)visible);
        bool is_selected = task->pid == st->selected_pid;

        pm_number(number, task->pid);
        pm_size_label(mem_text, task->memory_bytes);

        bk_gui_gfx_fill_rect(surface, row_rect,
                          is_selected ? 0x00DCE9F7
                                      : ((index & 1U) ? 0x00E8E8E0 : 0x00F8F8F0));
        bk_gui_font_draw_string_clipped(surface, row_rect.x + 4, row_rect.y + 4,
                                     number, 0x00203030,
                                     (gui_rect_t){row_rect.x + 2, row_rect.y + 2,
                                                  pid_w, 12});
        bk_gui_font_draw_string_clipped(surface, name_x, row_rect.y + 4,
                                     pm_task_label(task), 0x00203030,
                                     (gui_rect_t){name_x, row_rect.y + 2,
                                                  state_x - name_x - 4, 12});
        bk_gui_font_draw_string_clipped(surface, state_x, row_rect.y + 4,
                                     bk_proc_state_name((task_state_t)task->state),
                                     task->system ? 0x00505050 : 0x00007030,
                                     (gui_rect_t){state_x, row_rect.y + 2,
                                                  state_w, 12});
        bk_gui_font_draw_string_clipped(surface, mem_x, row_rect.y + 4,
                                     mem_text, 0x00203030,
                                     (gui_rect_t){mem_x, row_rect.y + 2,
                                                  mem_w, 12});
    }
    bk_gui_scrollbar_paint_vertical(surface, &scrollbar);

    if (selected >= 0) {
        const bk_proc_info_t *task = &rows[selected];
        pm_size_label(mem_text, task->memory_bytes);
        bk_runtime_strcpy(footer, pm_task_label(task));
        bk_runtime_strcat(footer, " | ");
        bk_runtime_strcat(footer,
            bk_proc_state_name((task_state_t)task->state));
        bk_runtime_strcat(footer, " | ");
        bk_runtime_strcat(footer, mem_text);
    } else {
        uint32_t first = count ? st->scroll_offset + 1U : 0U;
        uint32_t last = st->scroll_offset + (uint32_t)layout.row_count;
        if (last > count) last = count;
        bk_runtime_strcpy(footer, "Tareas: ");
        pm_number(number, count);
        bk_runtime_strcat(footer, number);
        bk_runtime_strcat(footer, " | Vista ");
        pm_number(number, first);
        bk_runtime_strcat(footer, number);
        bk_runtime_strcat(footer, "-");
        pm_number(number, last);
        bk_runtime_strcat(footer, number);
    }

    bk_gui_font_draw_string_clipped(surface, layout.action.x + 2, layout.action.y + 5,
                                 footer, 0x00283C4A,
                                 (gui_rect_t){layout.action.x + 2, layout.action.y + 2,
                                              layout.button.x - layout.action.x - 8,
                                              layout.action.h - 4});
    pm_draw_button(surface, layout.button, "@HE9A33973",
                   selected >= 0 && pm_can_terminate(&rows[selected]),
                   st->finish_pressed,
                   st->finish_icon_loaded ? &st->finish_icon : NULL);
}

static bool pm_event(gui_window_t *window UNUSED, const gui_event_t *event,
                     void *context) {
    processmanager_state_t *st = (processmanager_state_t *)context;
    pm_layout_t layout;
    bk_proc_info_t rows[PM_MAX_TASKS];
    gui_scrollbar_t scrollbar;
    uint32_t count;
    uint32_t new_scroll;
    int selected;
    bool inside;

    if (!st || st->closing || !st->window || !st->desktop || !event) return false;

    pm_layout_build(st, &layout);
    count = pm_collect_rows(rows, PM_MAX_TASKS);
    pm_validate_scroll(st, count, (uint32_t)layout.row_count);
    pm_validate_selection(st, rows, count);
    selected = pm_selected_index(st, rows, count);
    inside = bk_gui_rect_contains(st->window->bounds, event->x, event->y);

    bk_gui_scrollbar_init_vertical(&scrollbar, layout.scrollbar,
                                   st->scroll_offset,
                                   (uint32_t)layout.row_count, count);
    new_scroll = st->scroll_offset;
    if (bk_gui_scrollbar_handle_event_vertical(&scrollbar, &st->scroll_drag,
                                                event, 1U, &new_scroll)) {
        st->scroll_offset = new_scroll;
        st->window->dirty = true;
        return true;
    }

    if (event->type != GUI_EVENT_MOUSE_DOWN &&
        event->type != GUI_EVENT_MOUSE_UP) return false;

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (inside && selected >= 0 &&
            pm_can_terminate(&rows[selected]) &&
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
        if (pressed && selected >= 0 &&
            pm_can_terminate(&rows[selected])) {
            bk_proc_request_exit(rows[selected].pid);
            return true;
        }
    }

    if (!inside) return false;

    int row = pm_hit_row(&layout,
                         count > st->scroll_offset
                            ? count - st->scroll_offset : 0U,
                         event->x, event->y);
    if (row >= 0 && row < layout.row_count) {
        uint32_t index = st->scroll_offset + (uint32_t)row;
        if (index < count) {
            st->selected_pid = rows[index].pid;
            st->window->dirty = true;
            return true;
        }
    }
    return false;
}

static void pm_cleanup(processmanager_state_t *st) {
    if (!st) return;
    st->closing = 1U;
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (st->finish_icon_loaded) bk_gui_image_free(&st->finish_icon);
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
    st->cpu_count = pm_cpu_count();
    st->finish_icon_loaded =
        bk_graphics_icon_load("FileDelete", &st->finish_icon);
    {
        int window_w = st->cpu_count > 8U ? 650
                     : st->cpu_count > 4U ? 560
                     : st->cpu_count > 1U ? 430 : 350;
        int window_h = st->cpu_count > 8U ? 500
                     : st->cpu_count > 4U ? 430
                     : st->cpu_count > 1U ? 350 : 290;
        st->window = bk_gui_create_window(
            st->desktop, 145, 35, window_w, window_h, "@H9CAEF48D");
    }
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "@H9CAEF48D", "@H1C1E4EC2",
            "@H0AF90F4B", "@H7A28E1E5",
            "/ICONS/PROCESOS.BMP"});
        bk_gui_set_window_min_size(
            st->window, st->cpu_count > 8U ? 520
                       : st->cpu_count > 4U ? 460
                       : st->cpu_count > 1U ? 360 : 270,
            st->cpu_count > 8U ? 410
                       : st->cpu_count > 4U ? 360
                       : st->cpu_count > 1U ? 300 : 220);
        bk_gui_set_window_content(st->window, pm_content, st);
        bk_gui_set_window_event_handler(st->window, pm_event, st);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    }

    refresh_ticks = bk_sys_tick_frequency() / 4U;
    if (!refresh_ticks) refresh_ticks = 1;
    sleep_ticks = bk_sys_tick_frequency() / 60U;
    if (!sleep_ticks) sleep_ticks = 1;

    while (!bk_proc_exit_requested() && !st->closing) {
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
