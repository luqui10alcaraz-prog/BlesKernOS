#include "system_tools_common.h"

#define NETWORK_MIN_API 23U

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *refresh_button;
    bk_gui_widget_t *dhcp_button;
    bk_gui_widget_t *ping_button;
    bk_gui_widget_t *host_box;
    uint32_t refresh_id;
    uint32_t dhcp_id;
    uint32_t ping_id;
    bk_net_info_t info;
    bool have_info;
    bool pending_refresh;
    bool pending_dhcp;
    bool pending_ping;
    bool busy;
    uint32_t last_ping_ms;
    bool last_ping_ok;
    char last_ping_host[64];
    char status[128];
    bk_gui_image_t icon;
    bool have_icon;
} network_state_t;

static network_state_t *g_network_state;

static void network_status_text(network_state_t *state, const char *text) {
    st_copy(state->status, sizeof(state->status), text);
    if (state->window) bk_gui_window_invalidate(state->window);
}

static void network_layout(network_state_t *state, bk_gui_rect_t *panel,
                           bk_gui_rect_t *status) {
    bk_gui_rect_t content = {0, 0, 0, 0};
    (void)bk_gui_window_content_rect(state->window, &content);
    *panel = (bk_gui_rect_t){content.x + 14, content.y + 14,
                            content.w - 28, content.h - 92};
    *status = (bk_gui_rect_t){content.x + 14, content.y + content.h - 70,
                             content.w - 28, 18};
    bk_gui_widget_set_bounds(state->window, state->refresh_button,
        (bk_gui_rect_t){14, content.h - 38, 84, 24});
    bk_gui_widget_set_bounds(state->window, state->dhcp_button,
        (bk_gui_rect_t){104, content.h - 38, 102, 24});
    bk_gui_widget_set_bounds(state->window, state->host_box,
        (bk_gui_rect_t){content.w - 218, content.h - 38, 128, 24});
    bk_gui_widget_set_bounds(state->window, state->ping_button,
        (bk_gui_rect_t){content.w - 84, content.h - 38, 70, 24});
}

static void network_refresh(network_state_t *state) {
    state->pending_refresh = false;
    state->busy = true;
    st_zero(&state->info, sizeof(state->info));
    state->have_info = bk_net_get_info(&state->info);
    if (!state->have_info)
        network_status_text(state, "No se pudo consultar la pila de red.");
    else if (!state->info.stack_ready)
        network_status_text(state, "NETSTACK.DVR no esta disponible.");
    else if (!state->info.link_up)
        network_status_text(state, "Adaptador detectado, pero el enlace esta caido.");
    else if (!state->info.configured)
        network_status_text(state, "Enlace activo, sin configuracion IPv4.");
    else
        network_status_text(state, "Red configurada correctamente.");
    state->busy = false;
}

static void network_dhcp(network_state_t *state) {
    state->pending_dhcp = false;
    state->busy = true;
    network_status_text(state, "Solicitando configuracion DHCP...");
    if (!bk_net_dhcp(10000U)) {
        network_status_text(state, "DHCP fallo o no recibio respuesta.");
    } else {
        st_zero(&state->info, sizeof(state->info));
        state->have_info = bk_net_get_info(&state->info);
        network_status_text(state, "Configuracion DHCP renovada.");
    }
    state->busy = false;
}

static void network_ping(network_state_t *state) {
    char host[64];
    uint8_t address[4];
    uint32_t round_trip = 0;
    char detail[128];
    char number[16];
    state->pending_ping = false;
    state->busy = true;
    (void)bk_gui_widget_get_text(state->host_box, host, sizeof(host));
    if (!host[0]) st_copy(host, sizeof(host), "8.8.8.8");
    st_copy(state->last_ping_host, sizeof(state->last_ping_host), host);
    network_status_text(state, "Resolviendo destino...");
    if (!st_parse_ipv4(host, address) &&
        !bk_net_resolve(host, address, 5000U)) {
        state->last_ping_ok = false;
        network_status_text(state, "No se pudo resolver el nombre.");
        state->busy = false;
        return;
    }
    network_status_text(state, "Enviando ping...");
    state->last_ping_ok = bk_net_ping(address, 3000U, &round_trip);
    state->last_ping_ms = round_trip;
    if (state->last_ping_ok) {
        detail[0] = '\0';
        st_append(detail, sizeof(detail), "Respuesta de ");
        st_append(detail, sizeof(detail), host);
        st_append(detail, sizeof(detail), " en ");
        st_u32(number, sizeof(number), round_trip);
        st_append(detail, sizeof(detail), number);
        st_append(detail, sizeof(detail), " ms.");
        network_status_text(state, detail);
    } else {
        network_status_text(state, "Tiempo de espera agotado o destino inaccesible.");
    }
    st_zero(&state->info, sizeof(state->info));
    state->have_info = bk_net_get_info(&state->info);
    state->busy = false;
}

static void network_widget_callback(bk_gui_window_t *window UNUSED,
                                    uint32_t widget_id) {
    network_state_t *state = g_network_state;
    if (!state || state->busy) return;
    if (widget_id == state->refresh_id) state->pending_refresh = true;
    else if (widget_id == state->dhcp_id) state->pending_dhcp = true;
    else if (widget_id == state->ping_id) state->pending_ping = true;
}

static void network_draw_pair(bk_gui_surface_t *surface, int x, int y,
                              const char *label, const char *value,
                              uint32_t color) {
    bk_gui_surface_draw_text(surface, x, y, label, ST_MUTED, 0, false);
    bk_gui_surface_draw_text(surface, x + 112, y, value, color, 0, false);
}

static void network_paint(bk_gui_window_t *window UNUSED,
                          bk_gui_surface_t *surface, void *context) {
    network_state_t *state = (network_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t panel;
    bk_gui_rect_t status;
    char address[32];
    char line[96];
    char number[24];
    int x;
    int y;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    network_layout(state, &panel, &status);
    bk_gui_surface_fill_rect(surface, content, ST_FACE);
    st_draw_panel(surface, panel, ST_PANEL);
    x = panel.x + 16;
    y = panel.y + 14;
    if (state->have_icon)
        bk_gui_surface_draw_image(
            surface, (bk_gui_rect_t){x, y - 6, 24, 24}, panel, &state->icon);
    bk_gui_surface_draw_text(surface, x + 32, y, "Estado del adaptador",
                             ST_BLUE, 0, false);
    y += 24;
    if (!state->have_info) {
        bk_gui_surface_draw_text(surface, x, y,
                                 "Informacion no disponible.", ST_RED, 0, false);
    } else {
        network_draw_pair(surface, x, y, "Dispositivo:",
                          state->info.device[0] ? state->info.device : "Ninguno",
                          ST_TEXT);
        y += 18;
        network_draw_pair(surface, x, y, "Enlace:",
                          state->info.link_up ? "Activo" : "Desconectado",
                          state->info.link_up ? ST_GREEN : ST_RED);
        y += 18;
        network_draw_pair(surface, x, y, "Pila IPv4:",
                          state->info.stack_ready ? "Cargada" : "No cargada",
                          state->info.stack_ready ? ST_GREEN : ST_RED);
        y += 18;
        network_draw_pair(surface, x, y, "TLS:",
                          state->info.tls_ready ? "Disponible" : "No disponible",
                          state->info.tls_ready ? ST_GREEN : ST_MUTED);
        y += 24;
        st_ipv4_text(address, sizeof(address), state->info.address);
        network_draw_pair(surface, x, y, "Direccion IPv4:", address,
                          state->info.configured ? ST_TEXT : ST_RED);
        y += 18;
        st_ipv4_text(address, sizeof(address), state->info.netmask);
        network_draw_pair(surface, x, y, "Mascara:", address, ST_TEXT);
        y += 18;
        st_ipv4_text(address, sizeof(address), state->info.gateway);
        network_draw_pair(surface, x, y, "Gateway:", address, ST_TEXT);
        y += 18;
        st_ipv4_text(address, sizeof(address), state->info.dns);
        network_draw_pair(surface, x, y, "DNS:", address, ST_TEXT);
        y += 18;
        st_mac_text(address, sizeof(address), state->info.mac);
        network_draw_pair(surface, x, y, "MAC:", address, ST_TEXT);
        y += 24;
        line[0] = '\0';
        st_append(line, sizeof(line), "RX ");
        st_u32(number, sizeof(number), state->info.rx_packets);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), "   TX ");
        st_u32(number, sizeof(number), state->info.tx_packets);
        st_append(line, sizeof(line), number);
        st_append(line, sizeof(line), "   descartados ");
        st_u32(number, sizeof(number), state->info.rx_dropped);
        st_append(line, sizeof(line), number);
        bk_gui_surface_draw_text(surface, x, y, line, ST_MUTED, 0, false);
    }
    bk_gui_surface_draw_text(surface, status.x, status.y + 4,
                             state->status,
                             state->busy ? ST_BLUE :
                             (state->last_ping_ok ? ST_GREEN : ST_MUTED),
                             0, false);
    bk_gui_surface_draw_text(surface, content.x + content.w - 270,
                             content.y + content.h - 31,
                             "Destino:", ST_TEXT, 0, false);
}

static bool network_event(bk_gui_window_t *window UNUSED,
                          const bk_gui_event_t *event, void *context) {
    network_state_t *state = (network_state_t *)context;
    if (!state || !event) return false;
    if (event->type == BK_GUI_EVENT_KEY &&
        (uint8_t)event->key == BK_KEY_ENTER &&
        bk_gui_widget_is_focused(state->window, state->host_box)) {
        state->pending_ping = true;
        return true;
    }
    return false;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    network_state_t *state;
    if (bk_sys_api_version() < NETWORK_MIN_API) return;
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;
    state = (network_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) return;
    st_zero(state, sizeof(*state));
    state->desktop = desktop;
    state->have_icon = bk_graphics_icon_load("Network", &state->icon);
    st_copy(state->status, sizeof(state->status), "Consultando red...");
    state->window = bk_gui_create_window(desktop, 105, 65, 590, 430,
                                         "Network Status");
    if (!state->window) {
        if (state->have_icon) bk_gui_image_free(&state->icon);
        bk_sys_free(state);
        return;
    }
    g_network_state = state;
    state->refresh_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 24}, "Actualizar", network_widget_callback);
    state->dhcp_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 96, 24}, "Renovar DHCP", network_widget_callback);
    state->host_box = bk_gui_create_textbox(desktop, state->window,
        (bk_gui_rect_t){0, 0, 120, 24}, "8.8.8.8", 63,
        network_widget_callback);
    state->ping_button = bk_gui_create_button(desktop, state->window,
        (bk_gui_rect_t){0, 0, 80, 24}, "Ping", network_widget_callback);
    (void)bk_gui_widget_set_icon(state->refresh_button, "Refresh");
    (void)bk_gui_widget_set_icon(state->dhcp_button, "Network2");
    (void)bk_gui_widget_set_icon(state->ping_button, "Globe");
    state->refresh_id = bk_gui_widget_id(state->refresh_button);
    state->dhcp_id = bk_gui_widget_id(state->dhcp_button);
    state->ping_id = bk_gui_widget_id(state->ping_button);
    bk_gui_set_window_content(state->window, network_paint, state);
    bk_gui_set_window_event_handler(state->window, network_event, state);
    bk_gui_set_window_min_size(state->window, 520, 360);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());
    state->pending_refresh = true;

    while (bk_gui_window_is_open(state->window)) {
        if (state->pending_refresh && !state->busy) network_refresh(state);
        if (state->pending_dhcp && !state->busy) network_dhcp(state);
        if (state->pending_ping && !state->busy) network_ping(state);
        bk_sys_sleep_ms(10);
    }
    if (g_network_state == state) g_network_state = NULL;
    bk_gui_destroy_window(desktop, state->window);
    if (state->have_icon) bk_gui_image_free(&state->icon);
    bk_sys_free(state);
}
