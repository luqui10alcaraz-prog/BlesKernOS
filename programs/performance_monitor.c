#include "system_tools_common.h"

#define PERF_APP_MIN_API 26U
#define PERF_HISTORY 60U
#define PERF_MAX_PROCESSES 32U
#define PERF_TOP_PROCESSES 5U
#define PERF_REPORT_CAPACITY 16384U
#define PERF_SAMPLE_MS 2000U

#define PERF_COLOR_CPU     0x00008030U
#define PERF_COLOR_VIDEO   0x000050B0U
#define PERF_COLOR_COMPOSE 0x00B06000U
#define PERF_COLOR_IO      0x00900050U
#define PERF_COLOR_WARN    0x00A06000U
#define PERF_COLOR_OK      0x00007020U

typedef enum {
    PERF_TAB_SUMMARY = 0,
    PERF_TAB_DETAILS,
    PERF_TAB_IRQ_IO,
    PERF_TAB_BENCHMARK,
    PERF_TAB_COUNT
} perf_tab_t;

typedef struct {
    uint32_t pid;
    uint32_t ticks;
} perf_process_baseline_t;

typedef struct {
    uint32_t pid;
    uint32_t delta_ticks;
    uint32_t cpu_percent;
    uint32_t memory_bytes;
    bk_process_state_t state;
    char name[24];
} perf_top_process_t;

typedef struct {
    bk_gui_rect_t header;
    bk_gui_rect_t tabs;
    bk_gui_rect_t body;
    bk_gui_rect_t status;
    bk_gui_rect_t controls;
    bk_gui_rect_t tab[PERF_TAB_COUNT];
} perf_layout_t;

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *pause_button;
    bk_gui_widget_t *reset_button;
    bk_gui_widget_t *benchmark_button;
    bk_gui_widget_t *path_box;
    bk_gui_widget_t *save_button;
    uint32_t pause_id;
    uint32_t reset_id;
    uint32_t benchmark_id;
    uint32_t save_id;

    bk_perf_snapshot_t snapshot;
    bk_perf_benchmark_t benchmark;
    bool have_snapshot;
    bool have_benchmark;
    volatile bool closing;
    volatile bool paused;
    volatile bool busy;
    volatile bool pending_reset;
    volatile bool pending_benchmark;
    volatile bool pending_save;
    perf_tab_t tab;

    uint8_t cpu_history[PERF_HISTORY];
    uint8_t video_history[PERF_HISTORY];
    uint8_t compose_history[PERF_HISTORY];
    uint8_t io_history[PERF_HISTORY];
    uint8_t fps_history[PERF_HISTORY];
    uint32_t history_count;

    perf_process_baseline_t process_baseline[PERF_MAX_PROCESSES];
    uint32_t process_baseline_count;
    perf_top_process_t top[PERF_TOP_PROCESSES];
    uint32_t top_count;

    uint32_t last_sample_ms;
    uint32_t sample_ms;
    uint32_t diagnosis_level;
    char diagnosis_title[48];
    char diagnosis_detail[192];
    char status[160];
} perf_state_t;

static perf_state_t *g_perf_state;

static uint32_t perf_clamp_percent(uint32_t value) {
    return value > 100U ? 100U : value;
}

static uint32_t perf_percent(uint32_t used, uint32_t total) {
    if (!total) return 0U;
    if (used >= total) return 100U;
    return (uint32_t)(((uint64_t)used * 100ULL) / total);
}

static uint32_t perf_max_io_us(const bk_perf_snapshot_t *snapshot) {
    uint32_t maximum = 0U;
    if (!snapshot) return 0U;
    for (uint32_t i = 0; i < BK_PERF_IO_TYPE_COUNT; i++) {
        if (snapshot->io[i].read_max_us > maximum)
            maximum = snapshot->io[i].read_max_us;
        if (snapshot->io[i].write_max_us > maximum)
            maximum = snapshot->io[i].write_max_us;
    }
    return maximum;
}

static uint32_t perf_total_io_calls(const bk_perf_snapshot_t *snapshot) {
    uint32_t total = 0U;
    if (!snapshot) return 0U;
    for (uint32_t i = 0; i < BK_PERF_IO_TYPE_COUNT; i++)
        total += snapshot->io[i].read_calls + snapshot->io[i].write_calls;
    return total;
}

static uint32_t perf_total_io_errors(const bk_perf_snapshot_t *snapshot) {
    uint32_t total = 0U;
    if (!snapshot) return 0U;
    for (uint32_t i = 0; i < BK_PERF_IO_TYPE_COUNT; i++)
        total += snapshot->io[i].read_failures + snapshot->io[i].write_failures;
    return total;
}

static uint32_t perf_non_timer_irqs(const bk_perf_snapshot_t *snapshot) {
    uint32_t total = 0U;
    if (!snapshot) return 0U;
    for (uint32_t irq = 1U; irq < BK_PERF_IRQ_COUNT; irq++)
        total += snapshot->irq[irq];
    return total;
}

static uint32_t perf_dominant_irq(const bk_perf_snapshot_t *snapshot,
                                  uint32_t *count) {
    uint32_t best = 0U;
    uint32_t best_count = 0U;
    if (snapshot) {
        for (uint32_t irq = 0U; irq < BK_PERF_IRQ_COUNT; irq++) {
            if (snapshot->irq[irq] > best_count) {
                best_count = snapshot->irq[irq];
                best = irq;
            }
        }
    }
    if (count) *count = best_count;
    return best;
}

static const char *perf_process_state_name(bk_process_state_t state) {
    switch (state) {
        case BK_PROCESS_READY: return "Lista";
        case BK_PROCESS_RUNNING: return "Activa";
        case BK_PROCESS_SLEEPING: return "Dormida";
        case BK_PROCESS_ZOMBIE: return "Zombie";
        default: return "Libre";
    }
}

static bool perf_is_idle_name(const char *name) {
    return st_equal_ci(name, "idle") || st_equal_ci(name, "inactiva") ||
           st_equal_ci(name, "idle_task");
}

static void perf_append_u32(char *text, uint32_t capacity, uint32_t value) {
    char number[16];
    st_u32(number, sizeof(number), value);
    st_append(text, capacity, number);
}

static void perf_append_mb(char *text, uint32_t capacity, uint32_t bytes) {
    perf_append_u32(text, capacity, bytes / (1024U * 1024U));
    st_append(text, capacity, " MB");
}

static void perf_append_percent(char *text, uint32_t capacity,
                                uint32_t value) {
    perf_append_u32(text, capacity, perf_clamp_percent(value));
    st_append(text, capacity, "%");
}

static void perf_append_us(char *text, uint32_t capacity, uint32_t value) {
    if (value >= 1000U) {
        perf_append_u32(text, capacity, value / 1000U);
        st_append(text, capacity, " ms");
    } else {
        perf_append_u32(text, capacity, value);
        st_append(text, capacity, " us");
    }
}

static void perf_history_push(uint8_t history[PERF_HISTORY], uint32_t value) {
    for (uint32_t i = 1U; i < PERF_HISTORY; i++)
        history[i - 1U] = history[i];
    history[PERF_HISTORY - 1U] = (uint8_t)perf_clamp_percent(value);
}

static uint32_t perf_find_previous_ticks(const perf_state_t *state,
                                         uint32_t pid, bool *found) {
    for (uint32_t i = 0U; i < state->process_baseline_count; i++) {
        if (state->process_baseline[i].pid == pid) {
            if (found) *found = true;
            return state->process_baseline[i].ticks;
        }
    }
    if (found) *found = false;
    return 0U;
}

static void perf_sort_top(perf_top_process_t *rows, uint32_t count) {
    for (uint32_t i = 0U; i + 1U < count; i++) {
        uint32_t best = i;
        for (uint32_t j = i + 1U; j < count; j++)
            if (rows[j].delta_ticks > rows[best].delta_ticks) best = j;
        if (best != i) {
            perf_top_process_t temporary = rows[i];
            rows[i] = rows[best];
            rows[best] = temporary;
        }
    }
}

static void perf_sample_processes(perf_state_t *state) {
    perf_top_process_t rows[PERF_MAX_PROCESSES];
    perf_process_baseline_t next[PERF_MAX_PROCESSES];
    uint32_t total = bk_proc_count();
    uint32_t count = 0U;
    uint32_t total_delta = 0U;

    if (total > PERF_MAX_PROCESSES) total = PERF_MAX_PROCESSES;
    for (uint32_t i = 0U; i < total; i++) {
        bk_process_info_t info;
        bool found;
        uint32_t previous;
        if (!bk_proc_info(i, &info)) continue;
        previous = perf_find_previous_ticks(state, info.pid, &found);
        rows[count].pid = info.pid;
        rows[count].delta_ticks = found ? info.cpu_ticks - previous : 0U;
        rows[count].cpu_percent = 0U;
        rows[count].memory_bytes = info.memory_bytes;
        rows[count].state = info.state;
        st_copy(rows[count].name, sizeof(rows[count].name), info.name);
        next[count].pid = info.pid;
        next[count].ticks = info.cpu_ticks;
        total_delta += rows[count].delta_ticks;
        count++;
    }

    state->process_baseline_count = count;
    for (uint32_t i = 0U; i < count; i++)
        state->process_baseline[i] = next[i];
    for (uint32_t i = 0U; i < count; i++) {
        if (total_delta)
            rows[i].cpu_percent = (uint32_t)(
                ((uint64_t)rows[i].delta_ticks * 100ULL) / total_delta);
        if (perf_is_idle_name(rows[i].name)) rows[i].delta_ticks = 0U;
    }
    perf_sort_top(rows, count);
    state->top_count = count < PERF_TOP_PROCESSES ? count : PERF_TOP_PROCESSES;
    for (uint32_t i = 0U; i < state->top_count; i++) state->top[i] = rows[i];
}

static void perf_set_diagnosis(perf_state_t *state, uint32_t level,
                               const char *title, const char *detail) {
    if (level < state->diagnosis_level) return;
    state->diagnosis_level = level;
    st_copy(state->diagnosis_title, sizeof(state->diagnosis_title), title);
    st_copy(state->diagnosis_detail, sizeof(state->diagnosis_detail), detail);
}

static void perf_analyze(perf_state_t *state) {
    const bk_perf_snapshot_t *s = &state->snapshot;
    uint32_t video_share = s->present_share_percent + s->gpu_share_percent;
    uint32_t max_io = perf_max_io_us(s);
    uint32_t non_timer = perf_non_timer_irqs(s);
    uint32_t ram_percent = perf_percent(s->memory_used_bytes,
                                        s->memory_total_bytes);
    uint32_t full_percent = s->gui_frames
        ? (s->gui_full_frames * 100U) / s->gui_frames : 0U;

    state->diagnosis_level = 0U;
    st_copy(state->diagnosis_title, sizeof(state->diagnosis_title),
            "Sin cuello dominante");
    st_copy(state->diagnosis_detail, sizeof(state->diagnosis_detail),
            "Use el sistema normalmente. El monitor comparara video, dibujo, disco, IRQ, memoria y scheduler.");

    if (s->cpu_percent >= 85U)
        perf_set_diagnosis(state, 50U + s->cpu_percent / 2U,
            "CPU saturada",
            "La CPU permanece muy ocupada. Revise los procesos principales y si el compositor o el driver consumen la mayor parte del tiempo.");
    if (ram_percent >= 90U || s->memory_free_bytes < 4U * 1024U * 1024U)
        perf_set_diagnosis(state, 80U,
            "Presion de memoria",
            "Queda poca RAM libre. El sistema puede fragmentarse, fallar asignaciones o repetir trabajo por falta de buffers.");
    if (s->compose_share_percent >= 30U)
        perf_set_diagnosis(state, 70U + s->compose_share_percent / 3U,
            "Dibujo software lento",
            "El compositor consume demasiado tiempo antes de presentar. Revise transparencias, fuentes, redibujos y regiones sucias.");
    if (video_share >= 30U || s->present_max_us >= 25000U)
        perf_set_diagnosis(state, 75U + video_share / 4U,
            "Framebuffer o driver grafico",
            "La copia/presentacion de pantalla es costosa. Compare VESA y ATI Rage, profundidad de color, dirty rectangles y aceleracion 2D.");
    if (full_percent >= 50U && s->gui_frames >= 4U)
        perf_set_diagnosis(state, 78U,
            "Demasiados repintados completos",
            "La mayoria de los cuadros redibuja toda la pantalla. Revise invalidaciones globales y el seguimiento de regiones sucias.");
    if (max_io >= 20000U)
        perf_set_diagnosis(state, 75U + (max_io >= 100000U ? 20U : 5U),
            "E/S o disco lento",
            "Hay operaciones de almacenamiento con mucha latencia. Revise ATA PIO, timeouts, FAT, USB y esperas dentro de FileBrowser.");
    if (perf_total_io_errors(s))
        perf_set_diagnosis(state, 100U,
            "Errores de almacenamiento",
            "El perfilador detecto lecturas o escrituras fallidas. Antes de optimizar, revise el controlador y la integridad del volumen.");
    if (s->scheduler_ticks && non_timer > s->scheduler_ticks * 5U)
        perf_set_diagnosis(state, 90U,
            "Posible tormenta de IRQ",
            "Las interrupciones que no son del temporizador superan ampliamente los ticks normales. Revise el dispositivo e IRQ dominante.");
    if (s->fpu_share_percent >= 10U)
        perf_set_diagnosis(state, 65U + s->fpu_share_percent,
            "Cambios x87 costosos",
            "Guardar y restaurar el estado FPU consume tiempo apreciable. Revise tareas que usan TinyGL o coma flotante y cambian demasiado seguido.");
    if (s->scheduler_preempt_blocked > s->scheduler_ticks / 3U &&
        s->scheduler_preempt_blocked > 20U)
        perf_set_diagnosis(state, 72U,
            "Preempcion bloqueada",
            "El scheduler encuentra secciones no interrumpibles con demasiada frecuencia. Busque locks largos o codigo con preempcion desactivada.");

    if (state->have_benchmark) {
        if ((state->benchmark.available_mask & BK_PERF_BENCH_FILESYSTEM) &&
            state->benchmark.filesystem_score < 40U)
            perf_set_diagnosis(state, 86U,
                "Sistema de archivos lento",
                "El listado de /SYSTEM/PROGRAMS es lento incluso en una prueba aislada. Revise FAT, cache de directorios y lecturas repetidas.");
        if ((state->benchmark.available_mask & BK_PERF_BENCH_TIMER) &&
            state->benchmark.timer_score < 40U)
            perf_set_diagnosis(state, 82U,
                "Latencia del temporizador",
                "Los sleeps despiertan tarde o de forma irregular. Revise PIT, scheduler, IRQ deshabilitadas y secciones criticas largas.");
    }
}

static void perf_sample(perf_state_t *state) {
    uint32_t video;
    uint32_t io_score;
    if (!state || state->closing) return;
    if (!bk_perf_enabled()) bk_perf_set_enabled(true);
    if (!bk_perf_snapshot(&state->snapshot, true)) {
        st_copy(state->status, sizeof(state->status),
                "No se pudo capturar el perfilador.");
        return;
    }
    state->have_snapshot = true;
    perf_sample_processes(state);
    video = state->snapshot.present_share_percent +
            state->snapshot.gpu_share_percent;
    io_score = perf_max_io_us(&state->snapshot) / 1000U;
    if (io_score > 100U) io_score = 100U;
    perf_history_push(state->cpu_history, state->snapshot.cpu_percent);
    perf_history_push(state->video_history, video);
    perf_history_push(state->compose_history,
                      state->snapshot.compose_share_percent);
    perf_history_push(state->io_history, io_score);
    perf_history_push(state->fps_history,
                      state->snapshot.gui_fps > 100U ? 100U :
                      state->snapshot.gui_fps);
    if (state->history_count < PERF_HISTORY) state->history_count++;
    perf_analyze(state);
}

static void perf_layout_build(perf_state_t *state, perf_layout_t *layout) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    int tab_width;
    (void)bk_gui_window_content_rect(state->window, &content);
    layout->header = (bk_gui_rect_t){content.x + 8, content.y + 8,
                                     content.w - 16, 58};
    layout->tabs = (bk_gui_rect_t){content.x + 8, content.y + 72,
                                   content.w - 16, 24};
    layout->controls = (bk_gui_rect_t){content.x + 8,
                                       content.y + content.h - 34,
                                       content.w - 16, 26};
    layout->status = (bk_gui_rect_t){content.x + 8,
                                     layout->controls.y - 20,
                                     content.w - 16, 17};
    layout->body = (bk_gui_rect_t){content.x + 8, content.y + 100,
                                   content.w - 16,
                                   layout->status.y - (content.y + 100) - 5};
    tab_width = layout->tabs.w / PERF_TAB_COUNT;
    for (uint32_t i = 0U; i < PERF_TAB_COUNT; i++) {
        layout->tab[i] = (bk_gui_rect_t){layout->tabs.x + (int)i * tab_width,
                                         layout->tabs.y,
                                         i + 1U == PERF_TAB_COUNT
                                            ? layout->tabs.w - (int)i * tab_width
                                            : tab_width,
                                         layout->tabs.h};
    }

    bk_gui_widget_set_bounds(state->window, state->pause_button,
        (bk_gui_rect_t){8, content.h - 34, 82, 26});
    bk_gui_widget_set_bounds(state->window, state->reset_button,
        (bk_gui_rect_t){96, content.h - 34, 82, 26});
    bk_gui_widget_set_bounds(state->window, state->benchmark_button,
        (bk_gui_rect_t){184, content.h - 34, 112, 26});
    bk_gui_widget_set_bounds(state->window, state->path_box,
        (bk_gui_rect_t){content.w - 270, content.h - 34, 174, 26});
    bk_gui_widget_set_bounds(state->window, state->save_button,
        (bk_gui_rect_t){content.w - 90, content.h - 34, 82, 26});
}

static void perf_draw_meter(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                            uint32_t value, uint32_t color) {
    uint32_t clamped = perf_clamp_percent(value);
    st_draw_panel(surface, rect, 0x00E8E8E0U);
    if (rect.w > 4 && rect.h > 4) {
        int width = (int)(((uint64_t)(rect.w - 4) * clamped) / 100U);
        if (width > 0)
            bk_gui_surface_fill_rect(surface,
                (bk_gui_rect_t){rect.x + 2, rect.y + 2, width, rect.h - 4},
                color);
    }
}

static void perf_draw_card(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                           const char *label, const char *value,
                           uint32_t percent, uint32_t color) {
    st_draw_panel(surface, rect, ST_PANEL);
    bk_gui_surface_draw_text(surface, rect.x + 7, rect.y + 6,
                             label, ST_MUTED, 0, false);
    bk_gui_surface_draw_text(surface, rect.x + 7, rect.y + 21,
                             value, ST_TEXT, 0, false);
    perf_draw_meter(surface,
        (bk_gui_rect_t){rect.x + 6, rect.y + rect.h - 12, rect.w - 12, 7},
        percent, color);
}

static void perf_draw_header(perf_state_t *state, bk_gui_surface_t *surface,
                             bk_gui_rect_t rect) {
    char text[64];
    uint32_t values[5] = {0U, 0U, 0U, 0U, 0U};
    int card_w = rect.w / 5;
    uint32_t ram = 0U;
    uint32_t video = 0U;
    uint32_t io = 0U;
    const char *labels[5] = {"CPU", "RAM", "FPS", "Video", "Puntuacion"};
    uint32_t colors[5] = {PERF_COLOR_CPU, PERF_COLOR_COMPOSE,
                          PERF_COLOR_VIDEO, PERF_COLOR_VIDEO,
                          PERF_COLOR_OK};

    if (state->have_snapshot) {
        ram = perf_percent(state->snapshot.memory_used_bytes,
                           state->snapshot.memory_total_bytes);
        video = state->snapshot.present_share_percent +
                state->snapshot.gpu_share_percent;
        io = perf_max_io_us(&state->snapshot);
        values[0] = state->snapshot.cpu_percent;
        values[1] = ram;
        values[2] = state->snapshot.gui_fps > 100U ? 100U :
                    state->snapshot.gui_fps;
        values[3] = video;
        values[4] = state->have_benchmark ? state->benchmark.total_score : 0U;
    }

    for (uint32_t i = 0U; i < 5U; i++) {
        bk_gui_rect_t card = {rect.x + (int)i * card_w, rect.y,
                              i == 4U ? rect.w - (int)i * card_w - 3
                                      : card_w - 3,
                              rect.h};
        text[0] = '\0';
        if (i == 0U || i == 1U || i == 3U) {
            perf_append_percent(text, sizeof(text), values[i]);
        } else if (i == 2U) {
            perf_append_u32(text, sizeof(text),
                            state->have_snapshot ? state->snapshot.gui_fps : 0U);
            st_append(text, sizeof(text), " fps");
        } else if (state->have_benchmark) {
            perf_append_u32(text, sizeof(text), state->benchmark.total_score);
            st_append(text, sizeof(text), "/100");
        } else {
            st_copy(text, sizeof(text), "Sin prueba");
        }
        if (i == 3U && io >= 20000U) colors[i] = PERF_COLOR_WARN;
        perf_draw_card(surface, card, labels[i], text, values[i], colors[i]);
    }
}

static void perf_draw_tabs(perf_state_t *state, bk_gui_surface_t *surface,
                           const perf_layout_t *layout) {
    static const char *labels[PERF_TAB_COUNT] = {
        "Resumen", "Detalles", "IRQ y E/S", "Prueba completa"
    };
    for (uint32_t i = 0U; i < PERF_TAB_COUNT; i++) {
        bk_gui_rect_t tab = layout->tab[i];
        bool active = state->tab == (perf_tab_t)i;
        st_draw_panel(surface, tab, active ? ST_PANEL : ST_FACE);
        bk_gui_surface_draw_text(surface, tab.x + 8, tab.y + 8,
                                 labels[i], active ? ST_BLUE : ST_TEXT,
                                 0, false);
    }
}

static void perf_draw_graph_line(bk_gui_surface_t *surface,
                                 bk_gui_rect_t graph,
                                 const uint8_t history[PERF_HISTORY],
                                 uint32_t color) {
    for (uint32_t i = 1U; i < PERF_HISTORY; i++) {
        int x0 = graph.x + 3 + (int)((i - 1U) * (uint32_t)(graph.w - 6) /
                                     (PERF_HISTORY - 1U));
        int x1 = graph.x + 3 + (int)(i * (uint32_t)(graph.w - 6) /
                                     (PERF_HISTORY - 1U));
        int y0 = graph.y + graph.h - 3 -
                 (int)(history[i - 1U] * (uint32_t)(graph.h - 6) / 100U);
        int y1 = graph.y + graph.h - 3 -
                 (int)(history[i] * (uint32_t)(graph.h - 6) / 100U);
        bk_gui_surface_draw_line(surface, x0, y0, x1, y1, color);
    }
}

static void perf_draw_graph(perf_state_t *state, bk_gui_surface_t *surface,
                            bk_gui_rect_t rect) {
    st_draw_panel(surface, rect, 0x00182020U);
    for (uint32_t line = 1U; line < 4U; line++)
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + 2,
                            rect.y + (int)(line * (uint32_t)rect.h / 4U),
                            rect.w - 4, 1}, 0x00384040U);
    perf_draw_graph_line(surface, rect, state->cpu_history, PERF_COLOR_CPU);
    perf_draw_graph_line(surface, rect, state->video_history, PERF_COLOR_VIDEO);
    perf_draw_graph_line(surface, rect, state->compose_history,
                         PERF_COLOR_COMPOSE);
    bk_gui_surface_draw_text(surface, rect.x + 7, rect.y + 7,
                             "CPU", PERF_COLOR_CPU, 0, false);
    bk_gui_surface_draw_text(surface, rect.x + 48, rect.y + 7,
                             "Video", PERF_COLOR_VIDEO, 0, false);
    bk_gui_surface_draw_text(surface, rect.x + 103, rect.y + 7,
                             "Compositor", PERF_COLOR_COMPOSE, 0, false);
}

static void perf_draw_top_processes(perf_state_t *state,
                                    bk_gui_surface_t *surface,
                                    bk_gui_rect_t rect) {
    char line[96];
    char number[16];
    st_draw_panel(surface, rect, ST_PANEL);
    bk_gui_surface_draw_text(surface, rect.x + 8, rect.y + 8,
                             "Procesos con mas CPU", ST_BLUE, 0, false);
    for (uint32_t i = 0U; i < state->top_count && i < PERF_TOP_PROCESSES; i++) {
        const perf_top_process_t *task = &state->top[i];
        int y = rect.y + 27 + (int)i * 20;
        line[0] = '\0';
        st_append(line, sizeof(line), task->name);
        st_append(line, sizeof(line), "  PID ");
        perf_append_u32(line, sizeof(line), task->pid);
        bk_gui_surface_draw_text(surface, rect.x + 8, y,
                                 line, ST_TEXT, 0, false);
        number[0] = '\0';
        perf_append_percent(number, sizeof(number), task->cpu_percent);
        bk_gui_surface_draw_text(surface, rect.x + rect.w - 50, y,
                                 number, task->cpu_percent >= 50U
                                         ? ST_RED : ST_GREEN,
                                 0, false);
    }
}

static void perf_draw_summary(perf_state_t *state, bk_gui_surface_t *surface,
                              bk_gui_rect_t rect) {
    bk_gui_rect_t graph = {rect.x, rect.y, rect.w * 3 / 5 - 4, rect.h / 2};
    bk_gui_rect_t diagnosis = {graph.x + graph.w + 8, rect.y,
                               rect.w - graph.w - 8, rect.h / 2};
    bk_gui_rect_t processes = {rect.x, graph.y + graph.h + 7,
                               rect.w * 3 / 5 - 4,
                               rect.h - graph.h - 7};
    bk_gui_rect_t system = {processes.x + processes.w + 8,
                            processes.y, rect.w - processes.w - 8,
                            processes.h};
    char line[128];
    int y;

    perf_draw_graph(state, surface, graph);
    st_draw_panel(surface, diagnosis,
                  state->diagnosis_level >= 80U ? 0x00FFF0E0U : ST_PANEL);
    bk_gui_surface_draw_text(surface, diagnosis.x + 9, diagnosis.y + 9,
                             state->diagnosis_title,
                             state->diagnosis_level >= 80U ? ST_RED : ST_BLUE,
                             0, false);
    (void)st_draw_wrapped(surface,
        (bk_gui_rect_t){diagnosis.x + 8, diagnosis.y + 27,
                        diagnosis.w - 16, diagnosis.h - 35},
        diagnosis.x + 8, diagnosis.y + 29, state->diagnosis_detail,
        ST_TEXT, 14);

    perf_draw_top_processes(state, surface, processes);
    st_draw_panel(surface, system, ST_PANEL);
    bk_gui_surface_draw_text(surface, system.x + 8, system.y + 8,
                             "Sistema", ST_BLUE, 0, false);
    y = system.y + 28;
    line[0] = '\0';
    st_append(line, sizeof(line), "Grafica: ");
    st_append(line, sizeof(line), state->snapshot.gfx_driver);
    st_append(line, sizeof(line), " ");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_width);
    st_append(line, sizeof(line), "x");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_height);
    st_append(line, sizeof(line), "x");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_bpp);
    bk_gui_surface_draw_text(surface, system.x + 8, y, line, ST_TEXT, 0, false);
    y += 18;
    line[0] = '\0';
    st_append(line, sizeof(line), "RAM: ");
    perf_append_mb(line, sizeof(line), state->snapshot.memory_used_bytes);
    st_append(line, sizeof(line), " / ");
    perf_append_mb(line, sizeof(line), state->snapshot.memory_total_bytes);
    bk_gui_surface_draw_text(surface, system.x + 8, y, line, ST_TEXT, 0, false);
    y += 18;
    line[0] = '\0';
    st_append(line, sizeof(line), "Frames completos: ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_full_frames);
    st_append(line, sizeof(line), " / ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_frames);
    bk_gui_surface_draw_text(surface, system.x + 8, y, line, ST_TEXT, 0, false);
    y += 18;
    line[0] = '\0';
    st_append(line, sizeof(line), "Dirty pixels: ");
    perf_append_percent(line, sizeof(line), state->snapshot.gui_dirty_percent);
    bk_gui_surface_draw_text(surface, system.x + 8, y, line, ST_TEXT, 0, false);
    y += 18;
    line[0] = '\0';
    st_append(line, sizeof(line), "E/S: ");
    perf_append_u32(line, sizeof(line), perf_total_io_calls(&state->snapshot));
    st_append(line, sizeof(line), " ops; max ");
    perf_append_us(line, sizeof(line), perf_max_io_us(&state->snapshot));
    bk_gui_surface_draw_text(surface, system.x + 8, y, line, ST_TEXT, 0, false);
}

static void perf_draw_metric_row(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                                 const char *label, uint32_t share,
                                 uint32_t average_us, uint32_t max_us,
                                 uint32_t color) {
    char line[96];
    bk_gui_surface_draw_text(surface, rect.x, rect.y + 4,
                             label, ST_TEXT, 0, false);
    perf_draw_meter(surface,
        (bk_gui_rect_t){rect.x + 120, rect.y + 3, rect.w - 260, 12},
        share, color);
    line[0] = '\0';
    perf_append_percent(line, sizeof(line), share);
    st_append(line, sizeof(line), "  prom ");
    perf_append_us(line, sizeof(line), average_us);
    st_append(line, sizeof(line), "  max ");
    perf_append_us(line, sizeof(line), max_us);
    bk_gui_surface_draw_text(surface, rect.x + rect.w - 250, rect.y + 4,
                             line, ST_MUTED, 0, false);
}

static void perf_draw_details(perf_state_t *state, bk_gui_surface_t *surface,
                              bk_gui_rect_t rect) {
    char line[128];
    int y = rect.y + 10;
    st_draw_panel(surface, rect, ST_PANEL);
    bk_gui_surface_draw_text(surface, rect.x + 10, y,
                             "Tiempo por subsistema", ST_BLUE, 0, false);
    y += 22;
    perf_draw_metric_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Frame GUI", state->snapshot.frame_share_percent,
        state->snapshot.frame_average_us, state->snapshot.frame_max_us,
        PERF_COLOR_CPU);
    y += 22;
    perf_draw_metric_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Compositor", state->snapshot.compose_share_percent,
        state->snapshot.compose_average_us, state->snapshot.compose_max_us,
        PERF_COLOR_COMPOSE);
    y += 22;
    perf_draw_metric_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Presentacion", state->snapshot.present_share_percent,
        state->snapshot.present_average_us, state->snapshot.present_max_us,
        PERF_COLOR_VIDEO);
    y += 22;
    perf_draw_metric_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "GPU", state->snapshot.gpu_share_percent,
        state->snapshot.gpu_average_us, state->snapshot.gpu_max_us,
        0x006040A0U);
    y += 22;
    perf_draw_metric_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Cambios x87", state->snapshot.fpu_share_percent,
        state->snapshot.fpu_average_us, state->snapshot.fpu_max_us,
        PERF_COLOR_IO);
    y += 32;

    line[0] = '\0';
    st_append(line, sizeof(line), "Scheduler: ");
    perf_append_u32(line, sizeof(line), state->snapshot.scheduler_switches);
    st_append(line, sizeof(line), " cambios; quantum corto ");
    perf_append_u32(line, sizeof(line), state->snapshot.scheduler_fast_quantum);
    st_append(line, sizeof(line), "; preempcion bloqueada ");
    perf_append_u32(line, sizeof(line),
                    state->snapshot.scheduler_preempt_blocked);
    bk_gui_surface_draw_text(surface, rect.x + 10, y, line, ST_TEXT, 0, false);
    y += 20;
    line[0] = '\0';
    st_append(line, sizeof(line), "GUI: loops ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_loops);
    st_append(line, sizeof(line), "; eventos ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_events);
    st_append(line, sizeof(line), "; mouse ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_mouse_moves);
    st_append(line, sizeof(line), "; GPU frames ");
    perf_append_u32(line, sizeof(line), state->snapshot.gui_gpu_frames);
    bk_gui_surface_draw_text(surface, rect.x + 10, y, line, ST_TEXT, 0, false);
    y += 20;
    line[0] = '\0';
    st_append(line, sizeof(line), "Heap: ");
    perf_append_u32(line, sizeof(line), state->snapshot.heap_used_blocks);
    st_append(line, sizeof(line), " bloques usados, ");
    perf_append_u32(line, sizeof(line), state->snapshot.heap_free_blocks);
    st_append(line, sizeof(line), " libres. Intervalo: ");
    perf_append_u32(line, sizeof(line), state->snapshot.elapsed_ms);
    st_append(line, sizeof(line), " ms");
    bk_gui_surface_draw_text(surface, rect.x + 10, y, line, ST_TEXT, 0, false);
}

static const char *perf_io_name(uint32_t type) {
    static const char *names[BK_PERF_IO_TYPE_COUNT] = {
        "Otros", "ATA", "Floppy", "ATAPI", "USB"
    };
    return type < BK_PERF_IO_TYPE_COUNT ? names[type] : "?";
}

static void perf_draw_irq_io(perf_state_t *state, bk_gui_surface_t *surface,
                             bk_gui_rect_t rect) {
    bk_gui_rect_t irq_panel = {rect.x, rect.y, rect.w / 2 - 4, rect.h};
    bk_gui_rect_t io_panel = {irq_panel.x + irq_panel.w + 8, rect.y,
                              rect.w - irq_panel.w - 8, rect.h};
    uint32_t maximum = 1U;
    char line[112];
    char number[16];
    uint32_t dominant_count;
    uint32_t dominant_irq = perf_dominant_irq(&state->snapshot,
                                               &dominant_count);

    st_draw_panel(surface, irq_panel, ST_PANEL);
    bk_gui_surface_draw_text(surface, irq_panel.x + 8, irq_panel.y + 8,
                             "Interrupciones por intervalo", ST_BLUE, 0, false);
    for (uint32_t irq = 0U; irq < BK_PERF_IRQ_COUNT; irq++)
        if (state->snapshot.irq[irq] > maximum)
            maximum = state->snapshot.irq[irq];
    for (uint32_t irq = 0U; irq < BK_PERF_IRQ_COUNT; irq++) {
        int column = irq / 8U;
        int row = irq % 8U;
        int x = irq_panel.x + 8 + column * (irq_panel.w / 2);
        int y = irq_panel.y + 31 + row * 25;
        uint32_t percent = (uint32_t)(
            ((uint64_t)state->snapshot.irq[irq] * 100ULL) / maximum);
        line[0] = '\0';
        st_append(line, sizeof(line), "IRQ");
        perf_append_u32(line, sizeof(line), irq);
        bk_gui_surface_draw_text(surface, x, y, line, ST_TEXT, 0, false);
        perf_draw_meter(surface,
            (bk_gui_rect_t){x + 38, y - 1, irq_panel.w / 2 - 85, 11},
            percent, irq == dominant_irq ? ST_RED : PERF_COLOR_VIDEO);
        number[0] = '\0';
        perf_append_u32(number, sizeof(number), state->snapshot.irq[irq]);
        bk_gui_surface_draw_text(surface, x + irq_panel.w / 2 - 42, y,
                                 number, ST_MUTED, 0, false);
    }

    st_draw_panel(surface, io_panel, ST_PANEL);
    bk_gui_surface_draw_text(surface, io_panel.x + 8, io_panel.y + 8,
                             "Almacenamiento", ST_BLUE, 0, false);
    for (uint32_t type = 0U; type < BK_PERF_IO_TYPE_COUNT; type++) {
        const bk_perf_io_snapshot_t *io = &state->snapshot.io[type];
        int y = io_panel.y + 31 + (int)type * 48;
        line[0] = '\0';
        st_append(line, sizeof(line), perf_io_name(type));
        st_append(line, sizeof(line), "  R:");
        perf_append_u32(line, sizeof(line), io->read_calls);
        st_append(line, sizeof(line), " (max ");
        perf_append_us(line, sizeof(line), io->read_max_us);
        st_append(line, sizeof(line), ")  W:");
        perf_append_u32(line, sizeof(line), io->write_calls);
        st_append(line, sizeof(line), " (max ");
        perf_append_us(line, sizeof(line), io->write_max_us);
        st_append(line, sizeof(line), ")");
        bk_gui_surface_draw_text(surface, io_panel.x + 8, y,
                                 line, ST_TEXT, 0, false);
        line[0] = '\0';
        st_append(line, sizeof(line), "Sectores R/W: ");
        perf_append_u32(line, sizeof(line), io->read_sectors);
        st_append(line, sizeof(line), "/");
        perf_append_u32(line, sizeof(line), io->write_sectors);
        st_append(line, sizeof(line), "   errores: ");
        perf_append_u32(line, sizeof(line),
                        io->read_failures + io->write_failures);
        bk_gui_surface_draw_text(surface, io_panel.x + 8, y + 17,
            line, (io->read_failures || io->write_failures) ? ST_RED : ST_MUTED,
            0, false);
    }
    line[0] = '\0';
    st_append(line, sizeof(line), "IRQ dominante: ");
    perf_append_u32(line, sizeof(line), dominant_irq);
    st_append(line, sizeof(line), " con ");
    perf_append_u32(line, sizeof(line), dominant_count);
    st_append(line, sizeof(line), " eventos");
    bk_gui_surface_draw_text(surface, irq_panel.x + 8,
                             irq_panel.y + irq_panel.h - 22,
                             line, ST_MUTED, 0, false);
}

static void perf_draw_benchmark_row(bk_gui_surface_t *surface,
                                    bk_gui_rect_t rect, const char *label,
                                    uint32_t score, const char *detail,
                                    bool available) {
    bk_gui_surface_draw_text(surface, rect.x, rect.y + 4,
                             label, available ? ST_TEXT : ST_SHADOW,
                             0, false);
    perf_draw_meter(surface,
        (bk_gui_rect_t){rect.x + 130, rect.y + 3, rect.w - 310, 13},
        available ? score : 0U,
        score >= 60U ? PERF_COLOR_OK :
        (score >= 40U ? PERF_COLOR_WARN : ST_RED));
    if (available) {
        char number[16] = "";
        perf_append_u32(number, sizeof(number), score);
        st_append(number, sizeof(number), "/100");
        bk_gui_surface_draw_text(surface, rect.x + rect.w - 172,
                                 rect.y + 4, number, ST_TEXT, 0, false);
    }
    bk_gui_surface_draw_text(surface, rect.x + rect.w - 112,
                             rect.y + 4, detail ? detail : "",
                             available ? ST_MUTED : ST_SHADOW, 0, false);
}

static void perf_draw_benchmark(perf_state_t *state,
                                bk_gui_surface_t *surface,
                                bk_gui_rect_t rect) {
    char detail[48];
    char title[96];
    int y = rect.y + 10;
    const bk_perf_benchmark_t *b = &state->benchmark;
    st_draw_panel(surface, rect, ST_PANEL);
    title[0] = '\0';
    if (state->busy) {
        st_copy(title, sizeof(title), "Ejecutando la prueba completa...");
    } else if (state->have_benchmark) {
        st_append(title, sizeof(title), "Resultado: ");
        perf_append_u32(title, sizeof(title), b->total_score);
        st_append(title, sizeof(title), "/100 - ");
        st_append(title, sizeof(title), b->grade);
        st_append(title, sizeof(title), " | Cuello: ");
        st_append(title, sizeof(title), b->bottleneck);
    } else {
        st_copy(title, sizeof(title),
                "Pulse Prueba completa para medir el equipo.");
    }
    bk_gui_surface_draw_text(surface, rect.x + 10, y, title,
                             state->have_benchmark ? ST_BLUE : ST_TEXT,
                             0, false);
    y += 28;

#define PERF_BENCH_DETAIL(expr, suffix) do { \
    detail[0] = '\0'; \
    perf_append_u32(detail, sizeof(detail), (expr)); \
    st_append(detail, sizeof(detail), (suffix)); \
} while (0)
    PERF_BENCH_DETAIL(b->cpu_mhz, " MHz");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "CPU", b->cpu_score, detail,
        (b->available_mask & BK_PERF_BENCH_CPU) != 0U);
    y += 24;
    detail[0] = '\0';
    perf_append_u32(detail, sizeof(detail), b->memory_copy_mib_s);
    st_append(detail, sizeof(detail), " MB/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "RAM", b->memory_score, detail,
        (b->available_mask & BK_PERF_BENCH_MEMORY) != 0U);
    y += 24;
    PERF_BENCH_DETAIL(b->draw_mpix_s, " MPix/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Dibujo", b->draw_score, detail,
        (b->available_mask & BK_PERF_BENCH_DRAW) != 0U);
    y += 24;
    PERF_BENCH_DETAIL(b->video_present_mib_s, " MB/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Video", b->video_score, detail,
        (b->available_mask & BK_PERF_BENCH_VIDEO) != 0U);
    y += 24;
    PERF_BENCH_DETAIL(b->disk_read_mib_s, " MB/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Disco", b->disk_score, detail,
        (b->available_mask & BK_PERF_BENCH_DISK) != 0U);
    y += 24;
    PERF_BENCH_DETAIL(b->filesystem_lists_s, " listas/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "FAT/directorios", b->filesystem_score, detail,
        (b->available_mask & BK_PERF_BENCH_FILESYSTEM) != 0U);
    y += 24;
    PERF_BENCH_DETAIL(b->scheduler_yields_s, " yields/s");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Scheduler", b->scheduler_score, detail,
        (b->available_mask & BK_PERF_BENCH_SCHEDULER) != 0U);
    y += 24;
    detail[0] = '\0';
    perf_append_us(detail, sizeof(detail), b->timer_max_late_us);
    st_append(detail, sizeof(detail), " max");
    perf_draw_benchmark_row(surface,
        (bk_gui_rect_t){rect.x + 10, y, rect.w - 20, 18},
        "Temporizador", b->timer_score, detail,
        (b->available_mask & BK_PERF_BENCH_TIMER) != 0U);
#undef PERF_BENCH_DETAIL
}

static void perf_paint(bk_gui_window_t *window UNUSED,
                       bk_gui_surface_t *surface, void *context) {
    perf_state_t *state = (perf_state_t *)context;
    perf_layout_t layout;
    bk_gui_rect_t content = {0, 0, 0, 0};
    if (!state || state->closing || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    perf_layout_build(state, &layout);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    perf_draw_header(state, surface, layout.header);
    perf_draw_tabs(state, surface, &layout);
    if (state->tab == PERF_TAB_SUMMARY)
        perf_draw_summary(state, surface, layout.body);
    else if (state->tab == PERF_TAB_DETAILS)
        perf_draw_details(state, surface, layout.body);
    else if (state->tab == PERF_TAB_IRQ_IO)
        perf_draw_irq_io(state, surface, layout.body);
    else
        perf_draw_benchmark(state, surface, layout.body);
    bk_gui_surface_draw_text(surface, layout.status.x, layout.status.y + 3,
                             state->status,
                             state->busy ? ST_BLUE : ST_MUTED, 0, false);
    bk_gui_surface_draw_text(surface, layout.controls.x + layout.controls.w - 322,
                             layout.controls.y + 8,
                             "TXT:", ST_TEXT, 0, false);
}

static bool perf_event(bk_gui_window_t *window UNUSED,
                       const bk_gui_event_t *event, void *context) {
    perf_state_t *state = (perf_state_t *)context;
    perf_layout_t layout;
    if (!state || state->closing || !event || event->type != BK_GUI_EVENT_MOUSE_UP) return false;
    perf_layout_build(state, &layout);
    for (uint32_t i = 0U; i < PERF_TAB_COUNT; i++) {
        if (st_rect_contains(layout.tab[i], event->x, event->y)) {
            state->tab = (perf_tab_t)i;
            bk_gui_window_invalidate(state->window);
            return true;
        }
    }
    return false;
}

static void perf_widget_callback(bk_gui_window_t *window UNUSED,
                                 uint32_t widget_id) {
    perf_state_t *state = g_perf_state;
    if (!state || state->closing || state->busy) return;
    if (widget_id == state->pause_id) {
        state->paused = !state->paused;
        bk_gui_widget_set_text(state->pause_button,
                               state->paused ? "Continuar" : "Pausar");
        st_copy(state->status, sizeof(state->status),
                state->paused ? "Muestreo pausado; los contadores del kernel siguen acumulando." :
                                "Muestreo reanudado.");
        if (!state->paused) state->last_sample_ms = 0U;
    } else if (widget_id == state->reset_id) {
        state->pending_reset = true;
    } else if (widget_id == state->benchmark_id) {
        state->pending_benchmark = true;
        state->tab = PERF_TAB_BENCHMARK;
    } else if (widget_id == state->save_id) {
        state->pending_save = true;
    }
    bk_gui_window_invalidate(state->window);
}

static void perf_report_append_line(char *report, uint32_t capacity,
                                    const char *label, uint32_t value,
                                    const char *suffix) {
    st_append(report, capacity, label);
    perf_append_u32(report, capacity, value);
    if (suffix) st_append(report, capacity, suffix);
    st_append(report, capacity, "\r\n");
}

static void perf_build_report(perf_state_t *state, char *report,
                              uint32_t capacity) {
    char line[192];
    bk_datetime_t now;
    uint32_t dominant_count;
    uint32_t dominant_irq = perf_dominant_irq(&state->snapshot,
                                               &dominant_count);
    st_copy(report, capacity,
        "BlesKernOS 0.8 - Informe de rendimiento\r\n"
        "=========================================\r\n");
    if (bk_time_datetime(&now)) {
        line[0] = '\0';
        st_append(line, sizeof(line), "Fecha: ");
        perf_append_u32(line, sizeof(line), now.date.day);
        st_append(line, sizeof(line), "/");
        perf_append_u32(line, sizeof(line), now.date.month);
        st_append(line, sizeof(line), "/");
        perf_append_u32(line, sizeof(line), now.date.year);
        st_append(line, sizeof(line), " ");
        perf_append_u32(line, sizeof(line), now.time.hour);
        st_append(line, sizeof(line), ":");
        perf_append_u32(line, sizeof(line), now.time.minute);
        st_append(line, sizeof(line), ":");
        perf_append_u32(line, sizeof(line), now.time.second);
        st_append(line, sizeof(line), "\r\n");
        st_append(report, capacity, line);
    }
    line[0] = '\0';
    st_append(line, sizeof(line), "Diagnostico: ");
    st_append(line, sizeof(line), state->diagnosis_title);
    st_append(line, sizeof(line), "\r\nDetalle: ");
    st_append(line, sizeof(line), state->diagnosis_detail);
    st_append(line, sizeof(line), "\r\n\r\n");
    st_append(report, capacity, line);

    st_append(report, capacity, "[SISTEMA]\r\n");
    perf_report_append_line(report, capacity, "CPU ocupada: ",
                            state->snapshot.cpu_percent, "%");
    perf_report_append_line(report, capacity, "TSC estimado: ",
                            state->snapshot.tsc_mhz, " MHz");
    perf_report_append_line(report, capacity, "RAM total: ",
                            state->snapshot.memory_total_bytes /
                            (1024U * 1024U), " MB");
    perf_report_append_line(report, capacity, "RAM usada: ",
                            state->snapshot.memory_used_bytes /
                            (1024U * 1024U), " MB");
    line[0] = '\0';
    st_append(line, sizeof(line), "Grafica: ");
    st_append(line, sizeof(line), state->snapshot.gfx_driver);
    st_append(line, sizeof(line), " ");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_width);
    st_append(line, sizeof(line), "x");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_height);
    st_append(line, sizeof(line), "x");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_bpp);
    st_append(line, sizeof(line), " caps=0x");
    perf_append_u32(line, sizeof(line), state->snapshot.gfx_capabilities);
    st_append(line, sizeof(line), "\r\n\r\n");
    st_append(report, capacity, line);

    st_append(report, capacity, "[GUI Y VIDEO]\r\n");
    perf_report_append_line(report, capacity, "FPS: ",
                            state->snapshot.gui_fps, NULL);
    perf_report_append_line(report, capacity, "Frames: ",
                            state->snapshot.gui_frames, NULL);
    perf_report_append_line(report, capacity, "Frames completos: ",
                            state->snapshot.gui_full_frames, NULL);
    perf_report_append_line(report, capacity, "Dirty pixels: ",
                            state->snapshot.gui_dirty_percent, "%");
    perf_report_append_line(report, capacity, "Compositor: ",
                            state->snapshot.compose_share_percent, "% tiempo");
    perf_report_append_line(report, capacity, "Presentacion: ",
                            state->snapshot.present_share_percent, "% tiempo");
    perf_report_append_line(report, capacity, "GPU: ",
                            state->snapshot.gpu_share_percent, "% tiempo");
    perf_report_append_line(report, capacity, "Present max: ",
                            state->snapshot.present_max_us, " us");
    st_append(report, capacity, "\r\n[SCHEDULER]\r\n");
    perf_report_append_line(report, capacity, "Ticks: ",
                            state->snapshot.scheduler_ticks, NULL);
    perf_report_append_line(report, capacity, "Cambios de tarea: ",
                            state->snapshot.scheduler_switches, NULL);
    perf_report_append_line(report, capacity, "Quantum corto: ",
                            state->snapshot.scheduler_fast_quantum, NULL);
    perf_report_append_line(report, capacity, "Preempcion bloqueada: ",
                            state->snapshot.scheduler_preempt_blocked, NULL);
    perf_report_append_line(report, capacity, "x87/FPU: ",
                            state->snapshot.fpu_share_percent, "% tiempo");

    st_append(report, capacity, "\r\n[PROCESOS]\r\n");
    for (uint32_t i = 0U; i < state->top_count; i++) {
        line[0] = '\0';
        st_append(line, sizeof(line), state->top[i].name);
        st_append(line, sizeof(line), " pid=");
        perf_append_u32(line, sizeof(line), state->top[i].pid);
        st_append(line, sizeof(line), " cpu=");
        perf_append_percent(line, sizeof(line), state->top[i].cpu_percent);
        st_append(line, sizeof(line), " ram=");
        perf_append_mb(line, sizeof(line), state->top[i].memory_bytes);
        st_append(line, sizeof(line), " estado=");
        st_append(line, sizeof(line),
                  perf_process_state_name(state->top[i].state));
        st_append(line, sizeof(line), "\r\n");
        st_append(report, capacity, line);
    }

    st_append(report, capacity, "\r\n[IRQ]\r\n");
    line[0] = '\0';
    st_append(line, sizeof(line), "Dominante IRQ");
    perf_append_u32(line, sizeof(line), dominant_irq);
    st_append(line, sizeof(line), " = ");
    perf_append_u32(line, sizeof(line), dominant_count);
    st_append(line, sizeof(line), "\r\n");
    st_append(report, capacity, line);
    for (uint32_t irq = 0U; irq < BK_PERF_IRQ_COUNT; irq++) {
        line[0] = '\0';
        st_append(line, sizeof(line), "IRQ");
        perf_append_u32(line, sizeof(line), irq);
        st_append(line, sizeof(line), "=");
        perf_append_u32(line, sizeof(line), state->snapshot.irq[irq]);
        st_append(line, sizeof(line), (irq == 7U || irq == 15U) ? "\r\n" : "  ");
        st_append(report, capacity, line);
    }

    st_append(report, capacity, "\r\n[E/S]\r\n");
    for (uint32_t type = 0U; type < BK_PERF_IO_TYPE_COUNT; type++) {
        const bk_perf_io_snapshot_t *io = &state->snapshot.io[type];
        line[0] = '\0';
        st_append(line, sizeof(line), perf_io_name(type));
        st_append(line, sizeof(line), ": R calls=");
        perf_append_u32(line, sizeof(line), io->read_calls);
        st_append(line, sizeof(line), " sectors=");
        perf_append_u32(line, sizeof(line), io->read_sectors);
        st_append(line, sizeof(line), " avg_us=");
        perf_append_u32(line, sizeof(line), io->read_average_us);
        st_append(line, sizeof(line), " max_us=");
        perf_append_u32(line, sizeof(line), io->read_max_us);
        st_append(line, sizeof(line), " err=");
        perf_append_u32(line, sizeof(line), io->read_failures);
        st_append(line, sizeof(line), " | W calls=");
        perf_append_u32(line, sizeof(line), io->write_calls);
        st_append(line, sizeof(line), " max_us=");
        perf_append_u32(line, sizeof(line), io->write_max_us);
        st_append(line, sizeof(line), " err=");
        perf_append_u32(line, sizeof(line), io->write_failures);
        st_append(line, sizeof(line), "\r\n");
        st_append(report, capacity, line);
    }

    if (state->have_benchmark) {
        const bk_perf_benchmark_t *b = &state->benchmark;
        st_append(report, capacity, "\r\n[PRUEBA COMPLETA]\r\n");
        perf_report_append_line(report, capacity, "Total: ",
                                b->total_score, "/100");
        line[0] = '\0';
        st_append(line, sizeof(line), "Calificacion: ");
        st_append(line, sizeof(line), b->grade);
        st_append(line, sizeof(line), "\r\nCuello: ");
        st_append(line, sizeof(line), b->bottleneck);
        st_append(line, sizeof(line), "\r\n");
        st_append(report, capacity, line);
        perf_report_append_line(report, capacity, "CPU: ", b->cpu_score, "/100");
        perf_report_append_line(report, capacity, "RAM: ", b->memory_score, "/100");
        perf_report_append_line(report, capacity, "Dibujo: ", b->draw_score, "/100");
        perf_report_append_line(report, capacity, "Video: ", b->video_score, "/100");
        perf_report_append_line(report, capacity, "Disco: ", b->disk_score, "/100");
        perf_report_append_line(report, capacity, "FAT/directorios: ",
                                b->filesystem_score, "/100");
        perf_report_append_line(report, capacity, "Scheduler: ",
                                b->scheduler_score, "/100");
        perf_report_append_line(report, capacity, "Temporizador: ",
                                b->timer_score, "/100");
        perf_report_append_line(report, capacity, "CPU MHz: ", b->cpu_mhz, NULL);
        perf_report_append_line(report, capacity, "RAM copia MB/s: ",
                                b->memory_copy_mib_s, NULL);
        perf_report_append_line(report, capacity, "Video MB/s: ",
                                b->video_present_mib_s, NULL);
        perf_report_append_line(report, capacity, "Disco MB/s: ",
                                b->disk_read_mib_s, NULL);
        perf_report_append_line(report, capacity, "Listados/s: ",
                                b->filesystem_lists_s, NULL);
        perf_report_append_line(report, capacity, "Timer max tarde us: ",
                                b->timer_max_late_us, NULL);
    }
}

static void perf_reset_measurements(perf_state_t *state) {
    st_zero(&state->snapshot, sizeof(state->snapshot));
    st_zero(&state->benchmark, sizeof(state->benchmark));
    st_zero(state->cpu_history, sizeof(state->cpu_history));
    st_zero(state->video_history, sizeof(state->video_history));
    st_zero(state->compose_history, sizeof(state->compose_history));
    st_zero(state->io_history, sizeof(state->io_history));
    st_zero(state->fps_history, sizeof(state->fps_history));
    st_zero(state->process_baseline, sizeof(state->process_baseline));
    st_zero(state->top, sizeof(state->top));
    state->process_baseline_count = 0U;
    state->top_count = 0U;
    state->history_count = 0U;
    state->have_snapshot = false;
    state->have_benchmark = false;
    bk_perf_set_enabled(true);
    bk_perf_reset();
    state->last_sample_ms = 0U;
    st_copy(state->status, sizeof(state->status),
            "Mediciones reiniciadas. Use el sistema normalmente.");
}

static void perf_run_benchmark(perf_state_t *state) {
    uint32_t cpu_count;
    if (!state || state->closing) return;
    state->pending_benchmark = false;
    state->busy = true;
    bk_gui_widget_set_enabled(state->pause_button, false);
    bk_gui_widget_set_enabled(state->reset_button, false);
    bk_gui_widget_set_enabled(state->benchmark_button, false);
    bk_gui_widget_set_enabled(state->save_button, false);
    
    cpu_count = bk_proc_cpu_count();
    if (cpu_count > 1U) {
        st_copy(state->status, sizeof(state->status),
                "Benchmark deshabilitado en modo SMP para evitar deadlock. Use un CPU solo.");
        state->have_benchmark = false;
        state->busy = false;
        bk_gui_widget_set_enabled(state->pause_button, true);
        bk_gui_widget_set_enabled(state->reset_button, true);
        bk_gui_widget_set_enabled(state->benchmark_button, true);
        bk_gui_widget_set_enabled(state->save_button, true);
        bk_gui_window_invalidate(state->window);
        return;
    }
    
    st_copy(state->status, sizeof(state->status),
            "Ejecutando CPU, RAM, dibujo, video, disco, FAT, scheduler y temporizador...");
    bk_gui_window_invalidate(state->window);
    bk_sys_sleep_ms(50U);
    st_zero(&state->benchmark, sizeof(state->benchmark));
    if (bk_perf_benchmark(&state->benchmark) == 0) {
        state->have_benchmark = true;
        st_copy(state->status, sizeof(state->status),
                "Prueba terminada. Puede guardar el informe TXT.");
    } else {
        state->have_benchmark = false;
        st_copy(state->status, sizeof(state->status),
                "La prueba no pudo completarse.");
    }
    state->busy = false;
    bk_gui_widget_set_enabled(state->pause_button, true);
    bk_gui_widget_set_enabled(state->reset_button, true);
    bk_gui_widget_set_enabled(state->benchmark_button, true);
    bk_gui_widget_set_enabled(state->save_button, true);
    state->last_sample_ms = 0U;
    perf_analyze(state);
    bk_gui_window_invalidate(state->window);
}

static void perf_save_report(perf_state_t *state) {
    char path[96];
    if (!state || state->closing) return;
    char normalized[100];
    char *report;
    state->pending_save = false;
    if (!bk_gui_widget_get_text(state->path_box, path, sizeof(path)) || !path[0])
        st_copy(path, sizeof(path), "/PERFRESULT.TXT");
    if (path[0] != '/') {
        st_copy(normalized, sizeof(normalized), "/");
        st_append(normalized, sizeof(normalized), path);
    } else {
        st_copy(normalized, sizeof(normalized), path);
    }
    if (!st_contains_ci(normalized, ".TXT"))
        st_append(normalized, sizeof(normalized), ".TXT");
    report = (char *)bk_sys_alloc(PERF_REPORT_CAPACITY);
    if (!report) {
        st_copy(state->status, sizeof(state->status),
                "No hay memoria para crear el informe.");
        return;
    }
    perf_build_report(state, report, PERF_REPORT_CAPACITY);
    if (bk_file_write_all(normalized, report, st_length(report))) {
        st_copy(state->status, sizeof(state->status), "Informe guardado en ");
        st_append(state->status, sizeof(state->status), normalized);
        bk_gui_widget_set_text(state->path_box, normalized);
    } else {
        st_copy(state->status, sizeof(state->status),
                "No se pudo guardar el informe. Revise la ruta y el volumen.");
    }
    bk_sys_free(report);
    bk_gui_window_invalidate(state->window);
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    perf_state_t *state;
    if (bk_sys_api_version() < PERF_APP_MIN_API ||
        !(bk_sys_capabilities() & BK_API_CAP_PERFORMANCE)) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;

    state = (perf_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->sample_ms = PERF_SAMPLE_MS;
    state->tab = PERF_TAB_SUMMARY;
    st_copy(state->status, sizeof(state->status),
            "Preparando mediciones de rendimiento...");
    state->window = bk_gui_create_window(desktop, 18, 18, 760, 540,
                                          "Diagnostico de rendimiento");
    if (!state->window) {
        bk_sys_free(state);
        return;
    }
    g_perf_state = state;
    state->pause_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 26}, "Pausar", perf_widget_callback);
    state->reset_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 26}, "Reiniciar", perf_widget_callback);
    state->benchmark_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 110, 26}, "Prueba completa", perf_widget_callback);
    state->path_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 170, 26}, "/PERFRESULT.TXT", 90,
        perf_widget_callback);
    state->save_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 26}, "Guardar TXT", perf_widget_callback);
    state->pause_id = bk_gui_widget_id(state->pause_button);
    state->reset_id = bk_gui_widget_id(state->reset_button);
    state->benchmark_id = bk_gui_widget_id(state->benchmark_button);
    state->save_id = bk_gui_widget_id(state->save_button);
    bk_gui_set_window_content(state->window, perf_paint, state);
    bk_gui_set_window_event_handler(state->window, perf_event, state);
    bk_gui_set_window_min_size(state->window, 640, 450);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());

    perf_reset_measurements(state);
    while (bk_gui_window_is_open(state->window) && !state->closing) {
        uint32_t now = bk_sys_uptime_ms();
        if (state->pending_reset && !state->busy) {
            state->pending_reset = false;
            perf_reset_measurements(state);
            bk_gui_window_invalidate(state->window);
        }
        if (state->pending_benchmark && !state->busy)
            perf_run_benchmark(state);
        if (state->pending_save && !state->busy)
            perf_save_report(state);
        if (!state->paused && !state->busy &&
            (!state->last_sample_ms ||
             now - state->last_sample_ms >= state->sample_ms)) {
            state->last_sample_ms = now;
            perf_sample(state);
            st_copy(state->status, sizeof(state->status),
                    "Muestreo silencioso cada 2 segundos; COM1 no recibe salidas automaticas.");
            bk_gui_window_invalidate(state->window);
        }
        bk_sys_sleep_ms(20U);
    }

    state->closing = true;
    if (g_perf_state == state) g_perf_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    bk_sys_free(state);
}
