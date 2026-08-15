#include "kernel/include/api.h"
#include "control_common.h"

#define INTERNET_CONFIG_PATH "/SYSTEM/USER/CONFIG/INTERNET.INI"
#define INTERNET_DEFAULT_HOME "https://www.google.com/"

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_widget_t *home_box;
    gui_widget_t *images_button;
    gui_widget_t *css_button;
    gui_widget_t *cookies_button;
    uint32_t images_id;
    uint32_t css_id;
    uint32_t cookies_id;
    uint32_t timeout_ids[3];
    bool images;
    bool css;
    bool cookies;
    uint32_t timeout_ms;
    uint32_t ethernet_count;
    bool dns_tested;
    bool dns_ok;
    uint8_t dns_address[4];
    char status[112];
} internet_cpl_state_t;

static bool internet_equal(const char *left, const char *right) {
    uint32_t i = 0U;
    if (!left || !right) return false;
    while (left[i] && right[i] && left[i] == right[i]) i++;
    return left[i] == '\0' && right[i] == '\0';
}

static uint32_t internet_parse_uint(const char *text, uint32_t fallback) {
    uint32_t value = 0U;
    bool digit = false;
    while (text && *text >= '0' && *text <= '9') {
        digit = true;
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 30000U) return fallback;
    }
    return digit ? value : fallback;
}

static void internet_defaults(internet_cpl_state_t *st) {
    if (!st) return;
    st->images = true;
    st->css = true;
    st->cookies = true;
    st->timeout_ms = 10000U;
}

static void internet_apply_line(internet_cpl_state_t *st, const char *line,
                                char *home, uint32_t home_capacity) {
    char key[24];
    char value[256];
    uint32_t split = 0U, i = 0U, j = 0U;
    if (!st || !line) return;
    while (line[split] && line[split] != '=') split++;
    if (line[split] != '=') return;
    while (i < split && i + 1U < sizeof(key)) {
        key[i] = line[i];
        i++;
    }
    key[i] = '\0';
    i = split + 1U;
    while (line[i] && j + 1U < sizeof(value)) value[j++] = line[i++];
    value[j] = '\0';
    if (internet_equal(key, "home") && value[0]) {
        bk_runtime_strncpy(home, value, home_capacity - 1U);
        home[home_capacity - 1U] = '\0';
    } else if (internet_equal(key, "images")) {
        st->images = value[0] != '0';
    } else if (internet_equal(key, "css")) {
        st->css = value[0] != '0';
    } else if (internet_equal(key, "cookies")) {
        st->cookies = value[0] != '0';
    } else if (internet_equal(key, "timeout")) {
        uint32_t timeout = internet_parse_uint(value, st->timeout_ms);
        if (timeout >= 2000U && timeout <= 30000U) st->timeout_ms = timeout;
    }
}

static void internet_load(internet_cpl_state_t *st) {
    void *raw = NULL;
    uint32_t size = 0U, position = 0U;
    char home[256];
    char line[320];
    internet_defaults(st);
    bk_runtime_strcpy(home, INTERNET_DEFAULT_HOME);
    if (bk_file_read_all(INTERNET_CONFIG_PATH, &raw, &size) && raw) {
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
            internet_apply_line(st, line, home, sizeof(home));
        }
        bk_sys_free(raw);
    }
    if (st->home_box) bk_gui_widget_set_text(st->home_box, home);
}

static void internet_scan_adapters(internet_cpl_state_t *st) {
    uint32_t count, i;
    if (!st) return;
    st->ethernet_count = 0U;
    count = bk_device_pci_count();
    for (i = 0U; i < count; i++) {
        bk_pci_info_t info;
        if (bk_device_pci_info(i, &info) && info.class_code == 0x02U)
            st->ethernet_count++;
    }
}

static void internet_sync_buttons(internet_cpl_state_t *st) {
    if (!st) return;
    if (st->images_button)
        bk_gui_widget_set_text(st->images_button, st->images ? "@H40025F11" : "@H61E6BE1A");
    if (st->css_button)
        bk_gui_widget_set_text(st->css_button, st->css ? "@H40025F11" : "@H61E6BE1A");
    if (st->cookies_button)
        bk_gui_widget_set_text(st->cookies_button, st->cookies ? "@H40025F11" : "@H61E6BE1A");
}

static bool internet_save_config(internet_cpl_state_t *st) {
    char home[256];
    char data[480];
    if (!st || !st->home_box) return false;
    home[0] = '\0';
    if (!bk_gui_widget_get_text(st->home_box, home, sizeof(home)) || !home[0])
        bk_runtime_strcpy(home, INTERNET_DEFAULT_HOME);
    snprintf(data, sizeof(data),
        "home=%s\r\nimages=%u\r\ncss=%u\r\ncookies=%u\r\ntimeout=%u\r\n",
        home, st->images ? 1U : 0U, st->css ? 1U : 0U,
        st->cookies ? 1U : 0U, st->timeout_ms);
    return bk_file_write_all(INTERNET_CONFIG_PATH, data,
                             (uint32_t)bk_runtime_strlen(data));
}

static void internet_toggle(gui_window_t *window, uint32_t id) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    if (id == st->images_id) st->images = !st->images;
    else if (id == st->css_id) st->css = !st->css;
    else if (id == st->cookies_id) st->cookies = !st->cookies;
    internet_sync_buttons(st);
    bk_runtime_strcpy(st->status, "@H00CDDE8C");
    window->dirty = true;
}

static void internet_timeout(gui_window_t *window, uint32_t id) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    if (id == st->timeout_ids[0]) st->timeout_ms = 5000U;
    else if (id == st->timeout_ids[1]) st->timeout_ms = 10000U;
    else if (id == st->timeout_ids[2]) st->timeout_ms = 20000U;
    snprintf(st->status, sizeof(st->status), "@H4116837A",
             st->timeout_ms / 1000U);
    window->dirty = true;
}

static void internet_save(gui_window_t *window, uint32_t id UNUSED) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    bk_runtime_strcpy(st->status, internet_save_config(st)
        ? "@H1C5CE0C1"
        : "@H360DBAF7");
    window->dirty = true;
}

static void internet_default(gui_window_t *window, uint32_t id UNUSED) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    internet_defaults(st);
    bk_gui_widget_set_text(st->home_box, INTERNET_DEFAULT_HOME);
    internet_sync_buttons(st);
    bk_runtime_strcpy(st->status, "@H82EBD102");
    window->dirty = true;
}

static void internet_test(gui_window_t *window, uint32_t id UNUSED) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    if (!st) return;
    bk_runtime_strcpy(st->status, "@HD11129A2");
    window->dirty = true;
    bk_gui_request_paint();
    bk_sys_sleep_ms(1U);
    st->dns_tested = true;
    st->dns_ok = bk_net_resolve("www.google.com", st->dns_address,
                                st->timeout_ms);
    if (st->dns_ok) {
        snprintf(st->status, sizeof(st->status),
            "@H1C703501", st->dns_address[0],
            st->dns_address[1], st->dns_address[2], st->dns_address[3]);
    } else {
        bk_runtime_strcpy(st->status,
            "@H6700B719");
    }
    window->dirty = true;
}

static void internet_open_browser(gui_window_t *window, uint32_t id UNUSED) {
    internet_cpl_state_t *st = window
        ? (internet_cpl_state_t *)window->content_context : NULL;
    char home[256];
    if (!st) return;
    home[0] = '\0';
    (void)bk_gui_widget_get_text(st->home_box, home, sizeof(home));
    if (!home[0]) bk_runtime_strcpy(home, INTERNET_DEFAULT_HOME);
    bk_runtime_strcpy(st->status,
        bk_app_launch("/SYSTEM/PROGRAMS/NETSURF.BEX", home)
            ? "@HADE153A8" : "@H2E4349A3");
    window->dirty = true;
}

static void internet_paint(gui_window_t *window UNUSED, gui_surface_t *s,
                           void *context) {
    internet_cpl_state_t *st = (internet_cpl_state_t *)context;
    gui_rect_t client = bk_gui_window_content_rect_raw(st->window);
    int x = client.x + 14;
    int y = client.y + 12;
    char line[112];

    cpl_draw_group(s, (gui_rect_t){x, y, 426, 78}, "@H550F20E9");
    bk_gui_font_draw_string(s, x + 14, y + 24,
        "@H872DADF6", CPL_TEXT, 0, false);

    cpl_draw_group(s, (gui_rect_t){x, y + 90, 426, 100}, "@H2B3F8816");
    bk_gui_font_draw_string(s, x + 16, y + 116, "@H434A0D72", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 16, y + 144, "@HF6EFB0A9", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 224, y + 116, "@HB86503E4", CPL_TEXT, 0, false);
    bk_gui_font_draw_string(s, x + 224, y + 144, "@H9E3F3870", CPL_SHADOW, 0, false);

    cpl_draw_group(s, (gui_rect_t){x, y + 202, 426, 82}, "Conexion");
    snprintf(line, sizeof(line), "@H55FF331B",
             st->ethernet_count);
    bk_gui_font_draw_string(s, x + 16, y + 226, line, CPL_TEXT, 0, false);
    snprintf(line, sizeof(line), "@H01FE8B94",
             st->timeout_ms / 1000U);
    bk_gui_font_draw_string(s, x + 16, y + 250, line, CPL_TEXT, 0, false);

    bk_gui_font_draw_string_clipped(s, x + 4, client.y + client.h - 17,
        st->status[0] ? st->status : "@H05E2D94A", CPL_TEXT,
        (gui_rect_t){x + 4, client.y + client.h - 19, 414, 14});
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    internet_cpl_state_t *st;
    gui_widget_t *button;
    if (!desktop) return;
    st = (internet_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->window = bk_gui_create_window(desktop, 96, 48, 458, 392,
                                      "@H12C72607");
    if (!st->window) { bk_sys_free(st); return; }
    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "@HAF34335C", "@H1C1E4EC2", "@HB4A69B05",
        "@H7A28E1E5", "/ICONS/INTERNET.BMP"});
    bk_gui_set_window_content(st->window, internet_paint, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    st->home_box = bk_gui_create_textbox(desktop, st->window,
        (gui_rect_t){30, 50, 398, 23}, INTERNET_DEFAULT_HOME, 255, NULL);
    st->images_button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){146, 128, 48, 22}, "@H40025F11", internet_toggle);
    st->css_button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){146, 156, 48, 22}, "@H40025F11", internet_toggle);
    st->cookies_button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){350, 128, 48, 22}, "@H40025F11", internet_toggle);
    st->images_id = bk_gui_widget_id(st->images_button);
    st->css_id = bk_gui_widget_id(st->css_button);
    st->cookies_id = bk_gui_widget_id(st->cookies_button);

    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){178, 260, 48, 22}, "@H6EC01B99", internet_timeout);
    st->timeout_ids[0] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){230, 260, 54, 22}, "@H656F91CD", internet_timeout);
    st->timeout_ids[1] = bk_gui_widget_id(button);
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){288, 260, 54, 22}, "@H9356DFDA", internet_timeout);
    st->timeout_ids[2] = bk_gui_widget_id(button);

    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){22, 310, 88, 23}, "@H888D0292", internet_test);
    (void)bk_gui_widget_set_icon(button, "Network");
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){116, 310, 90, 23}, "@H4C997368", internet_open_browser);
    (void)bk_gui_widget_set_icon(button, "WebOpen");
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){212, 310, 96, 23}, "Predeterminada", internet_default);
    (void)bk_gui_widget_set_icon(button, "Undo");
    button = bk_gui_create_button(desktop, st->window,
        (gui_rect_t){334, 310, 78, 23}, "@H291705DD", internet_save);
    (void)bk_gui_widget_set_icon(button, "Save");

    internet_load(st);
    internet_scan_adapters(st);
    internet_sync_buttons(st);
    bk_runtime_strcpy(st->status,
        "@H859D4BF6");
    st->window->dirty = true;

    while (!bk_proc_exit_requested() && st->window->listed) bk_sys_sleep_ticks(2);
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
