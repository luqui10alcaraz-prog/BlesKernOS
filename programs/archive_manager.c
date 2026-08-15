#include "system_tools_common.h"

#define ARCHIVE_MIN_API 23U
#define ARCHIVE_MAX_INPUT (16U * 1024U * 1024U)

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *input_box;
    bk_gui_widget_t *output_box;
    bk_gui_widget_t *inspect_button;
    bk_gui_widget_t *compress_button;
    bk_gui_widget_t *extract_button;
    uint32_t inspect_id;
    uint32_t compress_id;
    uint32_t extract_id;
    bool pending_inspect;
    bool pending_compress;
    bool pending_extract;
    bool busy;
    char status[128];
    char detail[256];
    bk_gui_image_t icon;
    bool have_icon;
} archive_state_t;

static archive_state_t *g_archive_state;

static uint32_t archive_read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void archive_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool archive_is_bkz(const void *data, uint32_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    return data && size >= 8U && bytes[0] == 'B' && bytes[1] == 'K' &&
           bytes[2] == 'Z' && bytes[3] == '1';
}

static void archive_status(archive_state_t *state, const char *status,
                           const char *detail) {
    st_copy(state->status, sizeof(state->status), status);
    st_copy(state->detail, sizeof(state->detail), detail ? detail : "");
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void archive_layout(archive_state_t *state, bk_gui_rect_t *panel) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    bk_gui_widget_set_bounds(state->window, state->input_box,
        (bk_gui_rect_t){80, 14, content.w - 94, 22});
    bk_gui_widget_set_bounds(state->window, state->output_box,
        (bk_gui_rect_t){80, 44, content.w - 94, 22});
    bk_gui_widget_set_bounds(state->window, state->inspect_button,
        (bk_gui_rect_t){14, 78, 92, 24});
    bk_gui_widget_set_bounds(state->window, state->compress_button,
        (bk_gui_rect_t){112, 78, 106, 24});
    bk_gui_widget_set_bounds(state->window, state->extract_button,
        (bk_gui_rect_t){224, 78, 100, 24});
    *panel = (bk_gui_rect_t){content.x + 14, content.y + 116,
                            content.w - 28, content.h - 130};
}

static void archive_default_output(char *output, uint32_t capacity,
                                   const char *input, bool extracting) {
    uint32_t length;
    st_copy(output, capacity, input);
    length = st_length(output);
    if (extracting && length >= 4U &&
        st_equal_ci(output + length - 4U, ".BKZ")) {
        output[length - 4U] = '\0';
        st_append(output, capacity, ".OUT");
    } else {
        st_append(output, capacity, extracting ? ".OUT" : ".BKZ");
    }
}

static bool archive_get_paths(archive_state_t *state, char input[BK_PATH_MAX],
                              char output[BK_PATH_MAX], bool extracting) {
    (void)bk_gui_widget_get_text(state->input_box, input, BK_PATH_MAX);
    (void)bk_gui_widget_get_text(state->output_box, output, BK_PATH_MAX);
    if (!input[0]) {
        archive_status(state, "Falta el archivo de entrada.",
                       "Escriba una ruta completa, por ejemplo /DOCS/README.TXT");
        return false;
    }
    if (!output[0]) {
        archive_default_output(output, BK_PATH_MAX, input, extracting);
        bk_gui_widget_set_text(state->output_box, output);
    }
    if (st_equal_ci(input, output)) {
        archive_status(state, "Entrada y salida no pueden ser iguales.",
                       "Use otro nombre para evitar perder el archivo original.");
        return false;
    }
    return true;
}

static void archive_inspect(archive_state_t *state) {
    char input[BK_PATH_MAX];
    void *data = NULL;
    uint32_t size = 0;
    char number[24];
    state->pending_inspect = false;
    state->busy = true;
    (void)bk_gui_widget_get_text(state->input_box, input, sizeof(input));
    if (!input[0] || !bk_file_read_all(input, &data, &size)) {
        archive_status(state, "No se pudo abrir el archivo.", input);
        state->busy = false;
        return;
    }
    if (!archive_is_bkz(data, size)) {
        archive_status(state, "No es un archivo BKZ1.",
                       "Archive Manager usa el formato RLE BKZ1 de BlesKernOS.");
        bk_sys_free(data);
        state->busy = false;
        return;
    }
    {
        uint32_t original = archive_read_u32((const uint8_t *)data + 4U);
        state->detail[0] = '\0';
        st_append(state->detail, sizeof(state->detail), "Tamano comprimido: ");
        st_u32(number, sizeof(number), size);
        st_append(state->detail, sizeof(state->detail), number);
        st_append(state->detail, sizeof(state->detail), " bytes\nTamano original: ");
        st_u32(number, sizeof(number), original);
        st_append(state->detail, sizeof(state->detail), number);
        st_append(state->detail, sizeof(state->detail), " bytes\nFormato: BKZ1 / RLE de un archivo");
        archive_status(state, "Archivo valido.", state->detail);
    }
    bk_sys_free(data);
    state->busy = false;
}

static void archive_compress(archive_state_t *state) {
    char input[BK_PATH_MAX];
    char output[BK_PATH_MAX];
    void *raw = NULL;
    uint8_t *packed;
    uint32_t size = 0;
    uint32_t out = 8U;
    char detail[160];
    char number[24];
    state->pending_compress = false;
    state->busy = true;
    if (!archive_get_paths(state, input, output, false)) {
        state->busy = false;
        return;
    }
    archive_status(state, "Comprimiendo...", input);
    if (!bk_file_read_all(input, &raw, &size)) {
        archive_status(state, "No se pudo leer la entrada.", input);
        state->busy = false;
        return;
    }
    if (size > ARCHIVE_MAX_INPUT || size > 0x7FFFFFF0U) {
        bk_sys_free(raw);
        archive_status(state, "El archivo es demasiado grande.",
                       "Limite actual: 16 MB por archivo BKZ.");
        state->busy = false;
        return;
    }
    packed = (uint8_t *)bk_sys_alloc(size * 2U + 8U);
    if (!packed) {
        bk_sys_free(raw);
        archive_status(state, "No hay memoria suficiente.", "");
        state->busy = false;
        return;
    }
    packed[0] = 'B'; packed[1] = 'K'; packed[2] = 'Z'; packed[3] = '1';
    archive_write_u32(packed + 4U, size);
    for (uint32_t i = 0; i < size;) {
        uint8_t value = ((uint8_t *)raw)[i];
        uint8_t count = 1U;
        while (i + count < size && count < 255U &&
               ((uint8_t *)raw)[i + count] == value) count++;
        packed[out++] = count;
        packed[out++] = value;
        i += count;
    }
    if (!bk_file_write_all(output, packed, out)) {
        archive_status(state, "No se pudo escribir la salida.", output);
    } else {
        detail[0] = '\0';
        st_u32(number, sizeof(number), size);
        st_append(detail, sizeof(detail), number);
        st_append(detail, sizeof(detail), " -> ");
        st_u32(number, sizeof(number), out);
        st_append(detail, sizeof(detail), number);
        st_append(detail, sizeof(detail), " bytes\nSalida: ");
        st_append(detail, sizeof(detail), output);
        archive_status(state, "Compresion completada.", detail);
    }
    bk_sys_free(packed);
    bk_sys_free(raw);
    state->busy = false;
}

static void archive_extract(archive_state_t *state) {
    char input[BK_PATH_MAX];
    char output[BK_PATH_MAX];
    void *raw = NULL;
    uint8_t *unpacked;
    uint32_t size = 0;
    uint32_t expected;
    uint32_t in = 8U;
    uint32_t out = 0;
    state->pending_extract = false;
    state->busy = true;
    if (!archive_get_paths(state, input, output, true)) {
        state->busy = false;
        return;
    }
    archive_status(state, "Extrayendo...", input);
    if (!bk_file_read_all(input, &raw, &size) || !archive_is_bkz(raw, size)) {
        if (raw) bk_sys_free(raw);
        archive_status(state, "Archivo BKZ invalido.", input);
        state->busy = false;
        return;
    }
    expected = archive_read_u32((const uint8_t *)raw + 4U);
    if (expected > ARCHIVE_MAX_INPUT) {
        bk_sys_free(raw);
        archive_status(state, "La salida declarada supera 16 MB.",
                       "Se cancelo para proteger la memoria del sistema.");
        state->busy = false;
        return;
    }
    unpacked = (uint8_t *)bk_sys_alloc(expected ? expected : 1U);
    if (!unpacked) {
        bk_sys_free(raw);
        archive_status(state, "No hay memoria suficiente.", "");
        state->busy = false;
        return;
    }
    while (in + 1U < size && out < expected) {
        uint8_t count = ((uint8_t *)raw)[in++];
        uint8_t value = ((uint8_t *)raw)[in++];
        if (!count || out + count > expected) break;
        for (uint32_t n = 0; n < count; n++) unpacked[out++] = value;
    }
    if (out != expected || !bk_file_write_all(output, unpacked, expected)) {
        archive_status(state, "Extraccion fallida.",
                       out != expected ? "Flujo BKZ truncado o corrupto." : output);
    } else {
        char detail[160];
        char number[24];
        detail[0] = '\0';
        st_u32(number, sizeof(number), expected);
        st_append(detail, sizeof(detail), number);
        st_append(detail, sizeof(detail), " bytes escritos en\n");
        st_append(detail, sizeof(detail), output);
        archive_status(state, "Extraccion completada.", detail);
    }
    bk_sys_free(unpacked);
    bk_sys_free(raw);
    state->busy = false;
}

static void archive_widget_callback(bk_gui_window_t *window UNUSED,
                                    uint32_t widget_id) {
    archive_state_t *state = g_archive_state;
    if (!state || state->busy) return;
    if (widget_id == state->inspect_id) state->pending_inspect = true;
    else if (widget_id == state->compress_id) state->pending_compress = true;
    else if (widget_id == state->extract_id) state->pending_extract = true;
}

static void archive_paint(bk_gui_window_t *window UNUSED,
                          bk_gui_surface_t *surface, void *context) {
    archive_state_t *state = (archive_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t panel;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    archive_layout(state, &panel);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    bk_gui_surface_draw_text(surface, content.x + 14, content.y + 21,
                             "Entrada:", ST_TEXT, 0, false);
    bk_gui_surface_draw_text(surface, content.x + 14, content.y + 51,
                             "Salida:", ST_TEXT, 0, false);
    st_draw_panel(surface, panel, ST_PANEL);
    if (state->have_icon)
        bk_gui_surface_draw_image(
            surface, (bk_gui_rect_t){panel.x + 10, panel.y + 8, 24, 24},
            panel, &state->icon);
    bk_gui_surface_draw_text(surface, panel.x + 42, panel.y + 12,
                             state->status,
                             state->busy ? ST_BLUE : ST_TEXT, 0, false);
    (void)st_draw_wrapped(surface,
        (bk_gui_rect_t){panel.x + 10, panel.y + 32,
                        panel.w - 20, panel.h - 42},
        panel.x + 12, panel.y + 38, state->detail, ST_MUTED, 16);
    bk_gui_surface_draw_text(surface, panel.x + 12,
                             panel.y + panel.h - 18,
                             "BKZ1 comprime un archivo mediante RLE.",
                             ST_MUTED, 0, false);
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    archive_state_t *state;
    char launch[BK_PATH_MAX];
    if (bk_sys_api_version() < ARCHIVE_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (archive_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->have_icon = bk_graphics_icon_load("Packager", &state->icon);
    st_copy(state->status, sizeof(state->status), "Listo.");
    st_copy(state->detail, sizeof(state->detail),
            "Seleccione una entrada. La salida se completa automaticamente si queda vacia.");
    state->window = bk_gui_create_window(desktop, 110, 80, 560, 350,
                                         "Archive Manager");
    if (!state->window) {
        if (state->have_icon) bk_gui_image_free(&state->icon);
        bk_sys_free(state);
        return;
    }
    g_archive_state = state;
    launch[0] = '\0';
    (void)bk_proc_launch_arg_copy(launch, sizeof(launch));
    state->input_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 22}, launch, BK_PATH_MAX - 1,
        archive_widget_callback);
    state->output_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 22}, "", BK_PATH_MAX - 1,
        archive_widget_callback);
    state->inspect_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 90, 24}, "Inspeccionar",
        archive_widget_callback);
    state->compress_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 100, 24}, "Comprimir",
        archive_widget_callback);
    state->extract_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 90, 24}, "Extraer",
        archive_widget_callback);
    (void)bk_gui_widget_set_icon(state->inspect_button, "FileFind");
    (void)bk_gui_widget_set_icon(state->compress_button, "Packager");
    (void)bk_gui_widget_set_icon(state->extract_button, "FolderOpen");
    state->inspect_id = bk_gui_widget_id(state->inspect_button);
    state->compress_id = bk_gui_widget_id(state->compress_button);
    state->extract_id = bk_gui_widget_id(state->extract_button);
    bk_gui_set_window_content(state->window, archive_paint, state);
    bk_gui_set_window_min_size(state->window, 470, 300);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());

    while (bk_gui_window_is_open(state->window)) {
        if (state->pending_inspect && !state->busy) archive_inspect(state);
        if (state->pending_compress && !state->busy) archive_compress(state);
        if (state->pending_extract && !state->busy) archive_extract(state);
        bk_sys_sleep_ms(10);
    }
    if (g_archive_state == state) g_archive_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    if (state->have_icon) bk_gui_image_free(&state->icon);
    bk_sys_free(state);
}
