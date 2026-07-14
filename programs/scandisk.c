#include <bleskernos_api.h>

#define SCANDISK_MIN_API 7U
#define SCANDISK_CONFIRM_MS 5000U

typedef enum {
    SCANDISK_SCANNING = 0,
    SCANDISK_READY,
    SCANDISK_REPAIRING,
    SCANDISK_FAILED
} scandisk_phase_t;

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_volume_info_t volume;
    bk_volume_check_report_t check;
    bk_volume_repair_report_t repair;
    scandisk_phase_t phase;
    bool pending_scan;
    bool pending_repair;
    bool have_report;
    bool confirm_repair;
    uint32_t confirm_until;
    char status[96];
} scandisk_state_t;

static void memory_zero(void *pointer, uint32_t size) {
    uint8_t *bytes = (uint8_t *)pointer;
    while (bytes && size--) *bytes++ = 0;
}

static void text_copy(char *destination, uint32_t capacity,
                      const char *source) {
    uint32_t i = 0;
    if (!destination || !capacity) return;
    while (source && source[i] && i + 1U < capacity) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static char *append_text(char *output, char *end, const char *text) {
    while (output < end && text && *text) *output++ = *text++;
    if (output <= end) *output = '\0';
    return output;
}

static char *append_number(char *output, char *end, uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    if (!value) return append_text(output, end, "0");
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count && output < end) *output++ = digits[--count];
    if (output <= end) *output = '\0';
    return output;
}

static void make_pair(char *line, uint32_t capacity, const char *label,
                      uint32_t left, const char *separator, uint32_t right) {
    char *cursor = line;
    char *end = line + capacity - 1U;
    cursor = append_text(cursor, end, label);
    cursor = append_number(cursor, end, left);
    cursor = append_text(cursor, end, separator);
    (void)append_number(cursor, end, right);
}

static bool rect_contains(bk_gui_rect_t rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

static void scandisk_layout(scandisk_state_t *state, bk_gui_rect_t *scan,
                            bk_gui_rect_t *repair) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    *repair = (bk_gui_rect_t){content.x + content.w - 128,
                              content.y + content.h - 34, 112, 24};
    *scan = (bk_gui_rect_t){repair->x - 120, repair->y, 104, 24};
}

static void draw_button(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                        const char *label, bool enabled) {
    uint32_t face = enabled ? 0x00D4D0C8U : 0x00C8C8C0U;
    uint32_t text = enabled ? 0x00181818U : 0x00787878U;
    int text_x = rect.x + (rect.w - (int)bk_gui_text_width(label)) / 2;
    bk_gui_surface_fill_rect(surface, rect, face);
    bk_gui_surface_draw_rect(surface, rect, 0x00404040U);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1}, 0x00FFFFFFU);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2}, 0x00FFFFFFU);
    bk_gui_surface_draw_text(surface, text_x, rect.y + 8, label, text,
                             face, false);
}

static void scandisk_paint(bk_gui_window_t *window UNUSED,
                           bk_gui_surface_t *surface, void *context) {
    scandisk_state_t *state = (scandisk_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t scan_button;
    bk_gui_rect_t repair_button;
    bk_gui_rect_t panel;
    char line[96];
    int x;
    int y;
    bool repair_enabled;

    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    scandisk_layout(state, &scan_button, &repair_button);
    bk_gui_surface_fill_rect(surface, content, 0x00D4D0C8U);
    x = content.x + 16;
    y = content.y + 14;

    bk_gui_surface_draw_text(surface, x, y, "ScanDisk para BlesKernOS",
                             0x00002060U, 0, false);
    y += 20;
    bk_gui_surface_draw_text(surface, x, y, state->status,
                             state->phase == SCANDISK_FAILED ? 0x00900000U :
                             0x00202020U, 0, false);
    y += 20;

    panel = (bk_gui_rect_t){x, y, content.w - 32, content.h - 104};
    bk_gui_surface_fill_rect(surface, panel, 0x00FFFFFFU);
    bk_gui_surface_draw_rect(surface, panel, 0x00707070U);
    x += 12;
    y += 10;
    line[0] = '\0';
    append_text(line, line + sizeof(line) - 1U, "Volumen: ");
    append_text(line + bk_string_length(line), line + sizeof(line) - 1U,
                state->volume.volume_label);
    append_text(line + bk_string_length(line), line + sizeof(line) - 1U,
                "  Dispositivo: ");
    append_text(line + bk_string_length(line), line + sizeof(line) - 1U,
                state->volume.device_name);
    bk_gui_surface_draw_text(surface, x, y, line, 0x00202020U, 0, false);
    y += 18;

    if (state->have_report) {
        make_pair(line, sizeof(line), "Archivos / directorios: ",
                  state->check.files, " / ", state->check.directories);
        bk_gui_surface_draw_text(surface, x, y, line, 0x00202020U, 0, false);
        y += 16;
        make_pair(line, sizeof(line), "Clusters usados / libres: ",
                  state->check.allocated_clusters, " / ",
                  state->check.free_clusters);
        bk_gui_surface_draw_text(surface, x, y, line, 0x00202020U, 0, false);
        y += 16;
        make_pair(line, sizeof(line), "Perdidos / cruzados: ",
                  state->check.lost_clusters, " / ",
                  state->check.crosslinked_clusters);
        bk_gui_surface_draw_text(surface, x, y, line,
            state->check.lost_clusters || state->check.crosslinked_clusters ?
            0x00900000U : 0x00007020U, 0, false);
        y += 16;
        make_pair(line, sizeof(line), "Circulares / invalidas: ",
                  state->check.circular_chains, " / ",
                  state->check.invalid_chains);
        bk_gui_surface_draw_text(surface, x, y, line,
            state->check.circular_chains || state->check.invalid_chains ?
            0x00900000U : 0x00007020U, 0, false);
        y += 16;
        make_pair(line, sizeof(line), "FAT distintas / errores E/S: ",
                  state->check.fat_mismatch_sectors, " / ",
                  state->check.io_errors);
        bk_gui_surface_draw_text(surface, x, y, line,
            state->check.fat_mismatch_sectors || state->check.io_errors ?
            0x00900000U : 0x00007020U, 0, false);
        y += 16;
        make_pair(line, sizeof(line), "Fragmentados / fragmentos: ",
                  state->check.fragmented_files, " / ",
                  state->check.total_fragments);
        bk_gui_surface_draw_text(surface, x, y, line,
            state->check.fragmented_files ? 0x00806000U : 0x00007020U,
            0, false);
        y += 20;
        make_pair(line, sizeof(line), "Resultado: ", state->check.errors,
                  " errores, ", state->check.warnings);
        append_text(line + bk_string_length(line), line + sizeof(line) - 1U,
                    " advertencias");
        bk_gui_surface_draw_text(surface, x, y, line,
            state->check.errors ? 0x00900000U : 0x00007020U, 0, false);
        y += 18;
        if (state->repair.completed) {
            make_pair(line, sizeof(line), "Reparacion: ",
                      state->repair.chains_truncated, " cadenas; ",
                      state->repair.lost_clusters_freed);
            append_text(line + bk_string_length(line),
                        line + sizeof(line) - 1U, " clusters liberados");
            bk_gui_surface_draw_text(surface, x, y, line, 0x00005080U,
                                     0, false);
        }
    } else {
        bk_gui_surface_draw_text(surface, x, y,
            "El analisis estructural revisa ambas FAT y todas las cadenas.",
            0x00404040U, 0, false);
    }

    if (state->phase == SCANDISK_SCANNING ||
        state->phase == SCANDISK_REPAIRING) {
        bk_gui_rect_t bar = {panel.x + 12, panel.y + panel.h - 24,
                             panel.w - 24, 12};
        bk_gui_surface_draw_rect(surface, bar, 0x00505050U);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){bar.x + 2, bar.y + 2,
                            state->phase == SCANDISK_REPAIRING ?
                            (bar.w - 4) * 3 / 4 : (bar.w - 4) / 3,
                            bar.h - 4}, 0x000060A0U);
    }

    repair_enabled = state->phase == SCANDISK_READY && state->have_report &&
                     state->check.errors != 0U && !state->volume.read_only;
    draw_button(surface, scan_button, "Analizar",
                state->phase == SCANDISK_READY ||
                state->phase == SCANDISK_FAILED);
    draw_button(surface, repair_button,
                state->confirm_repair ? "Confirmar" : "Reparar",
                repair_enabled);
}

static bool scandisk_event(bk_gui_window_t *window UNUSED,
                           const bk_gui_event_t *event, void *context) {
    scandisk_state_t *state = (scandisk_state_t *)context;
    bk_gui_rect_t scan_button;
    bk_gui_rect_t repair_button;
    bool repair_enabled;
    uint32_t now;
    if (!state || !event || event->type != BK_GUI_EVENT_MOUSE_UP) return false;
    scandisk_layout(state, &scan_button, &repair_button);
    if (rect_contains(scan_button, event->x, event->y) &&
        (state->phase == SCANDISK_READY ||
         state->phase == SCANDISK_FAILED)) {
        state->confirm_repair = false;
        state->pending_scan = true;
        return true;
    }
    repair_enabled = state->phase == SCANDISK_READY && state->have_report &&
                     state->check.errors != 0U && !state->volume.read_only;
    if (!repair_enabled || !rect_contains(repair_button, event->x, event->y))
        return false;
    now = bk_sys_uptime_ms();
    if (!state->confirm_repair ||
        (int32_t)(now - state->confirm_until) >= 0) {
        state->confirm_repair = true;
        state->confirm_until = now + SCANDISK_CONFIRM_MS;
        text_copy(state->status, sizeof(state->status),
                  "Pulse Confirmar para modificar el volumen.");
        bk_gui_window_invalidate(state->window);
        return true;
    }
    state->confirm_repair = false;
    state->pending_repair = true;
    return true;
}

static void scandisk_run_scan(scandisk_state_t *state) {
    state->pending_scan = false;
    state->phase = SCANDISK_SCANNING;
    text_copy(state->status, sizeof(state->status),
              "Analizando sector de arranque, FAT y cadenas...");
    bk_gui_window_invalidate(state->window);
    bk_sys_sleep_ms(20);
    memory_zero(&state->check, sizeof(state->check));
    state->have_report = bk_device_check_volume(&state->check);
    state->phase = state->have_report ? SCANDISK_READY : SCANDISK_FAILED;
    if (!state->have_report)
        text_copy(state->status, sizeof(state->status),
                  "No se pudo completar el analisis del volumen.");
    else if (state->check.errors)
        text_copy(state->status, sizeof(state->status),
                  "Se encontraron problemas. Revise el informe.");
    else
        text_copy(state->status, sizeof(state->status),
                  "El volumen no presenta errores detectables.");
    bk_gui_window_invalidate(state->window);
}

static void scandisk_run_repair(scandisk_state_t *state) {
    bk_volume_check_report_t after;
    state->pending_repair = false;
    state->phase = SCANDISK_REPAIRING;
    text_copy(state->status, sizeof(state->status),
              "Aplicando reparaciones controladas...");
    bk_gui_window_invalidate(state->window);
    bk_sys_sleep_ms(20);
    memory_zero(&state->repair, sizeof(state->repair));
    memory_zero(&after, sizeof(after));
    if (!bk_device_repair_volume(&state->repair, &after)) {
        state->phase = SCANDISK_FAILED;
        text_copy(state->status, sizeof(state->status),
                  "La reparacion no pudo completarse.");
    } else {
        state->check = after;
        state->have_report = true;
        state->phase = SCANDISK_READY;
        text_copy(state->status, sizeof(state->status),
            after.errors ? "Reparacion parcial; quedan problemas manuales."
                         : "Reparacion completada y volumen verificado.");
    }
    bk_gui_window_invalidate(state->window);
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    scandisk_state_t *state;
    if (bk_sys_api_version() < SCANDISK_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (scandisk_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    memory_zero(state, sizeof(*state));
    state->desktop = desktop;
    if (!bk_device_volume_info(&state->volume)) {
        bk_sys_free(state);
        return;
    }
    state->window = bk_gui_create_window(desktop, 105, 70, 560, 390,
                                         "ScanDisk");
    if (!state->window) {
        bk_sys_free(state);
        return;
    }
    bk_gui_set_window_content(state->window, scandisk_paint, state);
    bk_gui_set_window_event_handler(state->window, scandisk_event, state);
    bk_gui_set_window_min_size(state->window, 500, 350);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());
    bk_proc_bind_window(state->window);
    state->pending_scan = true;
    state->phase = SCANDISK_SCANNING;

    while (bk_gui_window_is_open(state->window) &&
           !bk_proc_exit_requested()) {
        uint32_t now = bk_sys_uptime_ms();
        if (state->confirm_repair &&
            (int32_t)(now - state->confirm_until) >= 0) {
            state->confirm_repair = false;
            text_copy(state->status, sizeof(state->status),
                      "Confirmacion cancelada.");
            bk_gui_window_invalidate(state->window);
        }
        if (state->pending_scan) scandisk_run_scan(state);
        if (state->pending_repair) scandisk_run_repair(state);
        bk_sys_sleep_ms(10);
    }
    bk_proc_bind_window(NULL);
    if (state->window) bk_gui_destroy_window(desktop, state->window);
    bk_sys_free(state);
}
