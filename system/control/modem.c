#include "kernel/include/api.h"
#include "control_common.h"

#define MODEM_CONFIG_PATH "/SYSTEM/USER/CONFIG/MODEM.INI"

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_widget_t *prefix_box;
    gui_widget_t *tone_button;
    gui_widget_t *speaker_button;
    uint32_t port_ids[2];
    uint32_t speed_ids[4];
    uint32_t tone_id;
    uint32_t speaker_id;
    uint8_t port;
    uint32_t baud;
    bool tone;
    bool speaker;
    uint32_t modem_count;
    uint32_t serial_count;
    char status[112];
} modem_cpl_state_t;

static void modem_defaults(modem_cpl_state_t *st) {
    if (!st) return;
    st->port = 1U;
    st->baud = 57600U;
    st->tone = true;
    st->speaker = true;
}

static bool modem_equal(const char *left, const char *right) {
    uint32_t i = 0U;
    if (!left || !right) return false;
    while (left[i] && right[i] && left[i] == right[i]) i++;
    return left[i] == '\0' && right[i] == '\0';
}

static uint32_t modem_parse_uint(const char *text, uint32_t fallback) {
    uint32_t value = 0U;
    bool digit = false;
    while (text && *text >= '0' && *text <= '9') {
        digit = true;
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 115200U) return fallback;
    }
    return digit ? value : fallback;
}

static void modem_apply_line(modem_cpl_state_t *st, const char *line,
                             char *prefix, uint32_t prefix_capacity) {
    char key[24], value[64];
    uint32_t split = 0U, i = 0U, j = 0U;
    if (!st || !line) return;
    while (line[split] && line[split] != '=') split++;
    if (line[split] != '=') return;
    while (i < split && i + 1U < sizeof(key)) { key[i] = line[i]; i++; }
    key[i] = '\0';
    i = split + 1U;
    while (line[i] && j + 1U < sizeof(value)) value[j++] = line[i++];
    value[j] = '\0';
    if (modem_equal(key, "port")) {
        if (value[0] == 'C' && value[1] == 'O' && value[2] == 'M' &&
            (value[3] == '1' || value[3] == '2'))
            st->port = (uint8_t)(value[3] - '0');
    } else if (modem_equal(key, "baud")) {
        uint32_t baud = modem_parse_uint(value, st->baud);
        if (baud == 9600U || baud == 33600U || baud == 57600U ||
            baud == 115200U) st->baud = baud;
    } else if (modem_equal(key, "prefix") && value[0]) {
        bk_runtime_strncpy(prefix, value, prefix_capacity - 1U);
        prefix[prefix_capacity - 1U] = '\0';
    } else if (modem_equal(key, "tone")) st->tone = value[0] != '0';
    else if (modem_equal(key, "speaker")) st->speaker = value[0] != '0';
}

static void modem_load(modem_cpl_state_t *st) {
    void *raw = NULL;
    uint32_t size = 0U, position = 0U;
    char prefix[32], line[96];
    if (!st) return;
    bk_runtime_strcpy(prefix, "ATDT");
    if (bk_file_read_all(MODEM_CONFIG_PATH, &raw, &size) && raw) {
        while (position < size) {
            uint32_t used = 0U;
            while (position < size && ((char *)raw)[position] != '\n' &&
                   used + 1U < sizeof(line)) {
                char c = ((char *)raw)[position++];
                if (c != '\r') line[used++] = c;
            }
            while (position < size && ((char *)raw)[position] != '\n') position++;
            if (position < size) position++;
            line[used] = '\0';
            modem_apply_line(st, line, prefix, sizeof(prefix));
        }
        bk_sys_free(raw);
    }
    if (st->prefix_box) bk_gui_widget_set_text(st->prefix_box, prefix);
}

static void modem_detect(modem_cpl_state_t *st) {
    uint32_t count, i;
    if (!st) return;
    st->modem_count = 0U;
    st->serial_count = 0U;
    count = bk_device_pci_count();
    for (i = 0U; i < count; i++) {
        bk_pci_info_t info;
        if (!bk_device_pci_info(i, &info) || info.class_code != 0x07U)
            continue;
        if (info.subclass == 0x03U) st->modem_count++;
        if (info.subclass == 0x00U) st->serial_count++;
    }
}

static void modem_sync_buttons(modem_cpl_state_t *st) {
    if (!st) return;
    if (st->tone_button)
        bk_gui_widget_set_text(st->tone_button, st->tone ? "Tonos" : "Pulsos");
    if (st->speaker_button)
        bk_gui_widget_set_text(st->speaker_button,
                               st->speaker ? "Encendido" : "Apagado");
}

static bool modem_save_config(modem_cpl_state_t *st) {
    char prefix[32];
    char data[192];
    if (!st) return false;
    prefix[0] = '\0';
    if (st->prefix_box)
        (void)bk_gui_widget_get_text(st->prefix_box, prefix, sizeof(prefix));
    if (!prefix[0]) bk_runtime_strcpy(prefix, "ATDT");
    snprintf(data, sizeof(data),
        "port=COM%u\r\nbaud=%u\r\nprefix=%s\r\ntone=%u\r\nspeaker=%u\r\n",
        st->port, st->baud, prefix, st->tone ? 1U : 0U,
        st->speaker ? 1U : 0U);
    return bk_file_write_all(MODEM_CONFIG_PATH, data,
                             (uint32_t)bk_runtime_strlen(data));
}

static void modem_select(gui_window_t *window, uint32_t id) {
    modem_cpl_state_t *st = window
        ? (modem_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    if (id == st->port_ids[0]) st->port = 1U;
    else if (id == st->port_ids[1]) st->port = 2U;
    else if (id == st->speed_ids[0]) st->baud = 9600U;
    else if (id == st->speed_ids[1]) st->baud = 33600U;
    else if (id == st->speed_ids[2]) st->baud = 57600U;
    else if (id == st->speed_ids[3]) st->baud = 115200U;
    else if (id == st->tone_id) st->tone = !st->tone;
    else if (id == st->speaker_id) st->speaker = !st->speaker;
    modem_sync_buttons(st);
    snprintf(st->status, sizeof(st->status), "@H7BB121F9",
             st->port, st->baud);
    window->dirty = true;
}

static void modem_scan(gui_window_t *window, uint32_t id UNUSED) {
    modem_cpl_state_t *st = window
        ? (modem_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    modem_detect(st);
    if (st->modem_count) {
        snprintf(st->status, sizeof(st->status),
            "@HB822C4DB", st->modem_count);
    } else if (st->serial_count) {
        snprintf(st->status, sizeof(st->status),
            "@H3E80AB24", st->serial_count);
    } else {
        bk_runtime_strcpy(st->status,
            "@H080C846C");
    }
    window->dirty = true;
}

static void modem_save(gui_window_t *window, uint32_t id UNUSED) {
    modem_cpl_state_t *st = window
        ? (modem_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    bk_runtime_strcpy(st->status, modem_save_config(st)
        ? "@H7E52A0A4"
        : "@H7E3D9A6E");
    window->dirty = true;
}

static void modem_paint(gui_window_t *window UNUSED, gui_surface_t *s,
                        void *context) {
    modem_cpl_state_t *st = (modem_cpl_state_t *)context;
    gui_rect_t client = bk_gui_window_content_rect_raw(st->window);
    int x = client.x + 14;
    int y = client.y + 12;
    char line[112];

    cpl_draw_group(s, (gui_rect_t){x, y, 420, 78}, "Deteccion");
    snprintf(line, sizeof(line), "@HAB7220B3",
             st->modem_count, st->serial_count);
    bk_gui_font_draw_string(s, x + 16, y + 26, line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 50,
        "@H4781D6C3", CPL_SHADOW, 0, false);

    cpl_draw_group(s, (gui_rect_t){x, y + 90, 420, 106}, "@H1DBB94A6");
    bk_gui_font_draw_string(s, x + 16, y + 116, "Puerto:", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 154, "Velocidad:", CPL_TEXT, 0, false);

    cpl_draw_group(s, (gui_rect_t){x, y + 208, 420, 86}, "Marcacion");
    bk_gui_font_draw_string(s, x + 16, y + 234, "@H050FCDE9", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 220, y + 234, "Modo:", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 220, y + 263, "Altavoz:", CPL_TEXT, 0, false);

    snprintf(line, sizeof(line), "@H1E306EA0",
             st->port, st->baud);
    bk_gui_font_draw_string(s, x + 16, y + 316, line, CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 338,
        "@H594C954C", CPL_SHADOW, 0, false);
    bk_gui_font_draw_string_clipped(s, x + 4, client.y + client.h - 17,
        st->status[0] ? st->status : "@H05E2D94A", CPL_TEXT,
        (gui_rect_t){x + 4, client.y + client.h - 19, 410, 14});
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    modem_cpl_state_t *st;
    gui_widget_t *button;
    if (!desktop) return;
    st = (modem_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    modem_defaults(st);
    st->window = bk_gui_create_window(desktop, 112, 42, 452, 432, "@H8385EA8D");
    if (!st->window) { bk_sys_free(st); return; }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "@H8385EA8D", "@H1C1E4EC2", "@HE24EE5E1",
        "@H7A28E1E5", "/ICONS/MODEM.BMP"});
    bk_gui_set_window_content(st->window, modem_paint, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){92, 128, 58, 22}, "COM1", modem_select);
    st->port_ids[0] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){154, 128, 58, 22}, "COM2", modem_select);
    st->port_ids[1] = bk_gui_widget_id(button);

    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){92, 166, 58, 22}, "9600", modem_select);
    st->speed_ids[0] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){154, 166, 62, 22}, "33600", modem_select);
    st->speed_ids[1] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){220, 166, 62, 22}, "57600", modem_select);
    st->speed_ids[2] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){286, 166, 70, 22}, "115200", modem_select);
    st->speed_ids[3] = bk_gui_widget_id(button);

    st->prefix_box = bk_gui_create_textbox(desktop, st->window,
        (gui_rect_t){92, 246, 92, 23}, "ATDT", 31, NULL);
    st->tone_button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){286, 238, 78, 22}, "Tonos", modem_select);
    st->speaker_button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){286, 267, 78, 22}, "Encendido", modem_select);
    st->tone_id = bk_gui_widget_id(st->tone_button);
    st->speaker_id = bk_gui_widget_id(st->speaker_button);

    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){250, 358, 76, 23}, "Detectar", modem_scan);
    (void)bk_gui_widget_set_icon(button, "Dial");
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){334, 358, 76, 23}, "@H291705DD", modem_save);
    (void)bk_gui_widget_set_icon(button, "Save");

    modem_load(st);
    modem_detect(st);
    modem_sync_buttons(st);
    bk_runtime_strcpy(st->status,
        "@H67584FEE");
    st->window->dirty = true;

    while (!bk_proc_exit_requested() && st->window->listed) bk_sys_sleep_ticks(2);
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
