#include "system_tools_common.h"

#define DISK_MIN_API 23U
#define DISK_MAX_DEVICES 16U
#define DISK_CONFIRM_MS 5000U

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *refresh_button;
    bk_gui_widget_t *check_button;
    bk_gui_widget_t *repair_button;
    bk_gui_widget_t *mount_button;
    bk_gui_widget_t *format_button;
    bk_gui_widget_t *label_box;
    uint32_t refresh_id;
    uint32_t check_id;
    uint32_t repair_id;
    uint32_t mount_id;
    uint32_t format_id;
    bk_block_info_t devices[DISK_MAX_DEVICES];
    uint32_t device_count;
    int32_t selected;
    bk_volume_info_t volume;
    bk_volume_check_report_t check;
    bk_volume_repair_report_t repair;
    bool have_volume;
    bool have_check;
    bool pending_refresh;
    bool pending_check;
    bool pending_repair;
    bool pending_mount;
    bool pending_format;
    bool busy;
    bool confirm_repair;
    bool confirm_format;
    uint32_t confirm_until;
    char status[128];
} disk_state_t;

static disk_state_t *g_disk_state;

static void disk_status(disk_state_t *state, const char *text) {
    st_copy(state->status, sizeof(state->status), text);
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void disk_layout(disk_state_t *state, bk_gui_rect_t *devices,
                        bk_gui_rect_t *detail, bk_gui_rect_t *status) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    int left_w;
    (void)bk_gui_window_content_rect(state->window, &content);
    left_w = content.w / 3;
    if (left_w < 190) left_w = 190;
    if (left_w > 230) left_w = 230;
    *devices = (bk_gui_rect_t){content.x + 10, content.y + 10,
                              left_w, content.h - 70};
    *detail = (bk_gui_rect_t){content.x + left_w + 20, content.y + 10,
                             content.w - left_w - 30, content.h - 70};
    *status = (bk_gui_rect_t){content.x + 10, content.y + content.h - 54,
                             content.w - 20, 16};
    bk_gui_widget_set_bounds(state->window, state->refresh_button,
        (bk_gui_rect_t){10, content.h - 32, 80, 24});
    bk_gui_widget_set_bounds(state->window, state->check_button,
        (bk_gui_rect_t){96, content.h - 32, 80, 24});
    bk_gui_widget_set_bounds(state->window, state->repair_button,
        (bk_gui_rect_t){182, content.h - 32, 80, 24});
    bk_gui_widget_set_bounds(state->window, state->mount_button,
        (bk_gui_rect_t){268, content.h - 32, 80, 24});
    bk_gui_widget_set_bounds(state->window, state->label_box,
        (bk_gui_rect_t){content.w - 160, content.h - 32, 76, 24});
    bk_gui_widget_set_bounds(state->window, state->format_button,
        (bk_gui_rect_t){content.w - 78, content.h - 32, 68, 24});
}

static void disk_refresh(disk_state_t *state) {
    uint32_t count;
    state->pending_refresh = false;
    state->busy = true;
    state->device_count = 0;
    state->selected = -1;
    state->have_volume = bk_device_volume_info(&state->volume);
    count = bk_device_block_count();
    if (count > DISK_MAX_DEVICES) count = DISK_MAX_DEVICES;
    for (uint32_t i = 0; i < count; i++) {
        if (!bk_device_block_info(i, &state->devices[state->device_count]))
            continue;
        if (state->have_volume &&
            st_equal_ci(state->devices[state->device_count].name,
                        state->volume.device_name))
            state->selected = (int32_t)state->device_count;
        state->device_count++;
    }
    if (state->selected < 0 && state->device_count) state->selected = 0;
    state->have_check = false;
    state->confirm_format = false;
    state->confirm_repair = false;
    bk_gui_widget_set_text(state->format_button, "Formatear");
    bk_gui_widget_set_text(state->repair_button, "Reparar");
    disk_status(state, state->device_count ?
                "Dispositivos actualizados." : "No se detectaron discos.");
    state->busy = false;
}

static void disk_check(disk_state_t *state) {
    state->pending_check = false;
    state->busy = true;
    disk_status(state, "Comprobando el volumen FAT activo...");
    st_zero(&state->check, sizeof(state->check));
    state->have_check = bk_device_check_volume(&state->check);
    if (!state->have_check)
        disk_status(state, "No se pudo completar la comprobacion.");
    else if (state->check.errors)
        disk_status(state, "Se detectaron errores. Revise el informe.");
    else if (state->check.warnings)
        disk_status(state, "Comprobacion terminada con advertencias.");
    else
        disk_status(state, "El volumen no presenta errores.");
    state->busy = false;
}

static void disk_repair(disk_state_t *state) {
    bk_volume_check_report_t after;
    state->pending_repair = false;
    state->busy = true;
    state->confirm_repair = false;
    bk_gui_widget_set_text(state->repair_button, "Reparar");
    disk_status(state, "Reparando el volumen FAT activo...");
    st_zero(&state->repair, sizeof(state->repair));
    st_zero(&after, sizeof(after));
    if (!bk_device_repair_volume(&state->repair, &after)) {
        disk_status(state, "La reparacion no pudo completarse.");
    } else {
        state->check = after;
        state->have_check = true;
        disk_status(state, after.errors ?
                    "Reparacion parcial: aun quedan errores." :
                    "Reparacion completada correctamente.");
    }
    state->busy = false;
}

static void disk_mount(disk_state_t *state) {
    const bk_block_info_t *device;
    state->pending_mount = false;
    state->busy = true;
    if (state->selected < 0 ||
        (uint32_t)state->selected >= state->device_count) {
        disk_status(state, "Seleccione un dispositivo.");
        state->busy = false;
        return;
    }
    device = &state->devices[state->selected];
    disk_status(state, "Montando dispositivo...");
    if (!bk_device_mount_volume(device->name))
        disk_status(state, "No se pudo montar el dispositivo seleccionado.");
    else {
        state->have_volume = bk_device_volume_info(&state->volume);
        disk_status(state, "Dispositivo montado.");
    }
    state->busy = false;
}

static void disk_format(disk_state_t *state) {
    const bk_block_info_t *device;
    char label[12];
    state->pending_format = false;
    state->busy = true;
    state->confirm_format = false;
    bk_gui_widget_set_text(state->format_button, "Formatear");
    if (state->selected < 0 ||
        (uint32_t)state->selected >= state->device_count) {
        disk_status(state, "Seleccione un dispositivo.");
        state->busy = false;
        return;
    }
    device = &state->devices[state->selected];
    if (device->read_only) {
        disk_status(state, "El dispositivo seleccionado es de solo lectura.");
        state->busy = false;
        return;
    }
    if (state->have_volume &&
        st_equal_ci(device->name, state->volume.device_name)) {
        disk_status(state, "No se puede formatear el volumen FAT activo.");
        state->busy = false;
        return;
    }
    (void)bk_gui_widget_get_text(state->label_box, label, sizeof(label));
    if (!label[0]) st_copy(label, sizeof(label), "BLES_DISK");
    disk_status(state, "Formateando como FAT. No apague el equipo...");
    if (!bk_device_format_fat(device->name, label))
        disk_status(state, "Formato rechazado o fallido. El volumen activo no se formatea.");
    else
        disk_status(state, "Formato completado. Use Montar para acceder a la unidad.");
    state->busy = false;
}

static void disk_confirm_action(disk_state_t *state, bool repair) {
    uint32_t now = bk_sys_uptime_ms();
    if (repair) {
        if (!state->confirm_repair ||
            (int32_t)(now - state->confirm_until) >= 0) {
            state->confirm_repair = true;
            state->confirm_format = false;
            state->confirm_until = now + DISK_CONFIRM_MS;
            bk_gui_widget_set_text(state->repair_button, "Confirmar");
            bk_gui_widget_set_text(state->format_button, "Formatear");
            disk_status(state, "Pulse Confirmar otra vez para reparar el volumen activo.");
        } else {
            state->pending_repair = true;
        }
    } else {
        if (!state->confirm_format ||
            (int32_t)(now - state->confirm_until) >= 0) {
            state->confirm_format = true;
            state->confirm_repair = false;
            state->confirm_until = now + DISK_CONFIRM_MS;
            bk_gui_widget_set_text(state->format_button, "Confirmar");
            bk_gui_widget_set_text(state->repair_button, "Reparar");
            disk_status(state, "PELIGRO: pulse Confirmar otra vez para borrar el disco seleccionado.");
        } else {
            state->pending_format = true;
        }
    }
}

static void disk_widget_callback(bk_gui_window_t *window UNUSED,
                                 uint32_t widget_id) {
    disk_state_t *state = g_disk_state;
    if (!state || state->busy) return;
    if (widget_id == state->refresh_id) state->pending_refresh = true;
    else if (widget_id == state->check_id) state->pending_check = true;
    else if (widget_id == state->repair_id) disk_confirm_action(state, true);
    else if (widget_id == state->mount_id) state->pending_mount = true;
    else if (widget_id == state->format_id) disk_confirm_action(state, false);
}

static void disk_draw_device_list(disk_state_t *state,
                                  bk_gui_surface_t *surface,
                                  bk_gui_rect_t rect) {
    st_draw_panel(surface, rect, ST_PANEL);
    bk_gui_surface_draw_text(surface, rect.x + 8, rect.y + 8,
                             "Dispositivos", ST_BLUE, 0, false);
    for (uint32_t i = 0; i < state->device_count; i++) {
        bk_gui_rect_t row = {rect.x + 5, rect.y + 27 + (int)i * 34,
                             rect.w - 10, 31};
        char line[64];
        char number[20];
        if ((int32_t)i == state->selected)
            bk_gui_surface_fill_rect(surface, row, ST_SELECT);
        line[0] = '\0';
        st_append(line, sizeof(line), state->devices[i].name);
        st_append(line, sizeof(line), " - ");
        st_append(line, sizeof(line), state->devices[i].type_name);
        bk_gui_surface_draw_text(surface, row.x + 5, row.y + 6, line,
            (int32_t)i == state->selected ? ST_SELECT_TXT : ST_TEXT,
            0, false);
        line[0] = '\0';
        st_u32(number, sizeof(number),
               (uint32_t)(((uint64_t)state->devices[i].sector_count *
                           state->devices[i].sector_size) /
                          (1024ULL * 1024ULL)));
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), " MB  ");
        st_append(line, sizeof(line), state->devices[i].read_only ? "RO" : "RW");
        if (state->devices[i].removable) st_append(line, sizeof(line), " removible");
        bk_gui_surface_draw_text(surface, row.x + 5, row.y + 19, line,
            (int32_t)i == state->selected ? ST_SELECT_TXT : ST_MUTED,
            0, false);
    }
}

static void disk_draw_details(disk_state_t *state,
                              bk_gui_surface_t *surface,
                              bk_gui_rect_t rect) {
    int x = rect.x + 10;
    int y = rect.y + 10;
    char line[160];
    char number[24];
    st_draw_panel(surface, rect, ST_PANEL);
    bk_gui_surface_draw_text(surface, x, y, "Volumen activo", ST_BLUE, 0, false);
    y += 20;
    if (state->have_volume) {
        line[0] = '\0';
        st_append(line, sizeof(line), "Dispositivo: ");
        st_append(line, sizeof(line), state->volume.device_name);
        st_append(line, sizeof(line), "   Etiqueta: ");
        st_append(line, sizeof(line), state->volume.volume_label);
        bk_gui_surface_draw_text(surface, x, y, line, ST_TEXT, 0, false);
        y += 17;
        line[0] = '\0';
        st_append(line, sizeof(line), "Sistema: ");
        st_append(line, sizeof(line), state->volume.filesystem);
        st_append(line, sizeof(line), "   FAT");
        st_u32(number, sizeof(number), state->volume.fat_bits);
        st_append(line, sizeof(line), number);
        bk_gui_surface_draw_text(surface, x, y, line, ST_TEXT, 0, false);
        y += 17;
        line[0] = '\0';
        st_append(line, sizeof(line), "Total: ");
        st_u64_mb(number, sizeof(number), state->volume.total_bytes);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), "   Libre: ");
        st_u64_mb(number, sizeof(number), state->volume.free_bytes);
        st_append(line, sizeof(line), number);
        bk_gui_surface_draw_text(surface, x, y, line, ST_TEXT, 0, false);
    } else {
        bk_gui_surface_draw_text(surface, x, y,
                                 "No hay volumen FAT activo.", ST_RED, 0, false);
    }
    y += 30;
    bk_gui_surface_draw_text(surface, x, y, "Informe de comprobacion",
                             ST_BLUE, 0, false);
    y += 20;
    if (!state->have_check) {
        bk_gui_surface_draw_text(surface, x, y,
            "Pulse Comprobar para analizar el volumen activo.", ST_MUTED, 0, false);
        return;
    }
    line[0] = '\0';
    st_append(line, sizeof(line), "Archivos: ");
    st_u32(number, sizeof(number), state->check.files);
    st_append(line, sizeof(line), number);
    st_append(line, sizeof(line), "   Directorios: ");
    st_u32(number, sizeof(number), state->check.directories);
    st_append(line, sizeof(line), number);
    bk_gui_surface_draw_text(surface, x, y, line, ST_TEXT, 0, false);
    y += 17;
    line[0] = '\0';
    st_append(line, sizeof(line), "Perdidos: ");
    st_u32(number, sizeof(number), state->check.lost_clusters);
    st_append(line, sizeof(line), number);
    st_append(line, sizeof(line), "   Cruzados: ");
    st_u32(number, sizeof(number), state->check.crosslinked_clusters);
    st_append(line, sizeof(line), number);
    bk_gui_surface_draw_text(surface, x, y, line,
        state->check.lost_clusters || state->check.crosslinked_clusters ?
        ST_RED : ST_GREEN, 0, false);
    y += 17;
    line[0] = '\0';
    st_append(line, sizeof(line), "Errores: ");
    st_u32(number, sizeof(number), state->check.errors);
    st_append(line, sizeof(line), number);
    st_append(line, sizeof(line), "   Advertencias: ");
    st_u32(number, sizeof(number), state->check.warnings);
    st_append(line, sizeof(line), number);
    bk_gui_surface_draw_text(surface, x, y, line,
        state->check.errors ? ST_RED : ST_GREEN, 0, false);
    y += 20;
    if (state->repair.completed) {
        line[0] = '\0';
        st_append(line, sizeof(line), "Ultima reparacion: ");
        st_u32(number, sizeof(number), state->repair.chains_truncated);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), " cadenas, ");
        st_u32(number, sizeof(number), state->repair.lost_clusters_freed);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), " clusters liberados");
        bk_gui_surface_draw_text(surface, x, y, line, ST_BLUE, 0, false);
    }
}

static void disk_paint(bk_gui_window_t *window UNUSED,
                       bk_gui_surface_t *surface, void *context) {
    disk_state_t *state = (disk_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t devices;
    bk_gui_rect_t details;
    bk_gui_rect_t status;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    disk_layout(state, &devices, &details, &status);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    disk_draw_device_list(state, surface, devices);
    disk_draw_details(state, surface, details);
    bk_gui_surface_draw_text(surface, status.x, status.y + 4,
                             state->status,
                             state->confirm_format ? ST_RED :
                             (state->busy ? ST_BLUE : ST_MUTED), 0, false);
    bk_gui_surface_draw_text(surface, content.x + content.w - 294,
                             content.y + content.h - 27,
                             "Etiqueta:", ST_TEXT, 0, false);
}

static bool disk_event(bk_gui_window_t *window UNUSED,
                       const bk_gui_event_t *event, void *context) {
    disk_state_t *state = (disk_state_t *)context;
    bk_gui_rect_t devices;
    bk_gui_rect_t details;
    bk_gui_rect_t status;
    if (!state || !event) return false;
    disk_layout(state, &devices, &details, &status);
    if (event->type == BK_GUI_EVENT_MOUSE_UP &&
        st_rect_contains(devices, event->x, event->y)) {
        int row = (event->y - (devices.y + 27)) / 34;
        if (row >= 0 && (uint32_t)row < state->device_count) {
            state->selected = row;
            state->confirm_format = false;
            state->confirm_repair = false;
            bk_gui_widget_set_text(state->format_button, "Formatear");
            bk_gui_widget_set_text(state->repair_button, "Reparar");
            bk_gui_window_invalidate(state->window);
            return true;
        }
    }
    (void)details;
    (void)status;
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    disk_state_t *state;
    if (bk_sys_api_version() < DISK_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (disk_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->selected = -1;
    st_copy(state->status, sizeof(state->status), "Leyendo dispositivos...");
    state->window = bk_gui_create_window(desktop, 60, 45, 700, 470,
                                         "Disk Tools");
    if (!state->window) {
        bk_sys_free(state);
        return;
    }
    g_disk_state = state;
    state->refresh_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 24}, "Actualizar", disk_widget_callback);
    state->check_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 24}, "Comprobar", disk_widget_callback);
    state->repair_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 24}, "Reparar", disk_widget_callback);
    state->mount_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 70, 24}, "Montar", disk_widget_callback);
    state->label_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 90, 24}, "BLES_DISK", 11,
        disk_widget_callback);
    state->format_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 90, 24}, "Formatear", disk_widget_callback);
    (void)bk_gui_widget_set_icon(state->refresh_button, "Refresh");
    (void)bk_gui_widget_set_icon(state->check_button, "Qfecheck111");
    (void)bk_gui_widget_set_icon(state->repair_button, "Settings");
    (void)bk_gui_widget_set_icon(state->mount_button, "Folder");
    (void)bk_gui_widget_set_icon(state->format_button, "Format16");
    state->refresh_id = bk_gui_widget_id(state->refresh_button);
    state->check_id = bk_gui_widget_id(state->check_button);
    state->repair_id = bk_gui_widget_id(state->repair_button);
    state->mount_id = bk_gui_widget_id(state->mount_button);
    state->format_id = bk_gui_widget_id(state->format_button);
    bk_gui_set_window_content(state->window, disk_paint, state);
    bk_gui_set_window_event_handler(state->window, disk_event, state);
    bk_gui_set_window_min_size(state->window, 620, 400);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());
    state->pending_refresh = true;

    while (bk_gui_window_is_open(state->window)) {
        uint32_t now = bk_sys_uptime_ms();
        if ((state->confirm_format || state->confirm_repair) &&
            (int32_t)(now - state->confirm_until) >= 0) {
            state->confirm_format = false;
            state->confirm_repair = false;
            bk_gui_widget_set_text(state->format_button, "Formatear");
            bk_gui_widget_set_text(state->repair_button, "Reparar");
            disk_status(state, "Confirmacion vencida.");
        }
        if (state->pending_refresh && !state->busy) disk_refresh(state);
        if (state->pending_check && !state->busy) disk_check(state);
        if (state->pending_repair && !state->busy) disk_repair(state);
        if (state->pending_mount && !state->busy) disk_mount(state);
        if (state->pending_format && !state->busy) disk_format(state);
        bk_sys_sleep_ms(10);
    }
    if (g_disk_state == state) g_disk_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    bk_sys_free(state);
}
