#include "platform.h"
#include "plotters.h"

#define NSBK_WINDOW_W 720
#define NSBK_WINDOW_H 460
#define NSBK_TOOLBAR_H 46
#define NSBK_STATUS_H 22
#define NSBK_SCROLLBAR_W BK_GUI_SCROLLBAR_SIZE
#define NSBK_SCROLL_STEP 36U
#define NSBK_CHAR_W 8
#define NSBK_LINE_H 12
#define NSBK_NO_LINK 0xFFFFFFFFU
#define NSBK_NO_CONTROL 0xFFFFFFFFU
#define NSBK_FORM_BODY_MAX 2048U
#define NSBK_HISTORY_MAX 16U
#define NSBK_HOME_URL "https://www.google.com/"
#define NSBK_MIN_API_VERSION 23U
#define NSBK_INTERNET_CONFIG "/SYSTEM/USER/CONFIG/INTERNET.INI"

#define NSBK_MENU_FILE_OPEN 1001U
#define NSBK_MENU_FILE_CLOSE 1002U
#define NSBK_MENU_VIEW_RELOAD 1101U
#define NSBK_MENU_VIEW_STOP 1102U
#define NSBK_MENU_VIEW_IMAGES 1103U
#define NSBK_MENU_VIEW_CSS 1104U
#define NSBK_MENU_GO_BACK 1201U
#define NSBK_MENU_GO_FORWARD 1202U
#define NSBK_MENU_GO_HOME 1203U
#define NSBK_MENU_HELP_ABOUT 1301U

typedef struct {
    bk_gui_desktop_t *desktop;
    bk_gui_window_t *window;
    bk_gui_widget_t *back_button;
    bk_gui_widget_t *forward_button;
    bk_gui_widget_t *reload_button;
    bk_gui_widget_t *stop_button;
    bk_gui_widget_t *home_button;
    bk_gui_widget_t *address_box;
    bk_gui_widget_t *go_button;
    bk_gui_widget_t *page_textbox;
    uint32_t back_id;
    uint32_t forward_id;
    uint32_t reload_id;
    uint32_t stop_id;
    uint32_t home_id;
    uint32_t address_id;
    uint32_t go_id;
    uint32_t page_textbox_id;
    nsbk_document_t document;
    char address[NSBK_URL_MAX];
    char home_url[NSBK_URL_MAX];
    char status[128];
    char pending_url[NSBK_URL_MAX];
    char pending_method[8];
    char pending_body[NSBK_FORM_BODY_MAX];
    char worker_url[NSBK_URL_MAX];
    char worker_method[8];
    char worker_body[NSBK_FORM_BODY_MAX];
    char worker_fragment[NSBK_URL_MAX];
    char history[NSBK_HISTORY_MAX][NSBK_URL_MAX];
    uint8_t history_count;
    uint8_t history_index;
    uint32_t scroll_line;
    bk_gui_scrollbar_drag_t scrollbar_drag;
    int32_t layout_width;
    uint32_t request_timeout_ms;
    uint32_t progress_visible_until;
    uint32_t pending_body_length;
    uint32_t worker_body_length;
    int32_t worker_viewport;
    volatile uint8_t loading_progress;
    volatile bool worker_complete;
    volatile bool worker_success;
    bool loading;
    bool pending_navigation;
    bool pending_add_history;
    bool progress_failed;
    volatile bool cancel_requested;
    bool allow_stylesheets;
    bool allow_images;
    bool allow_cookies;
    uint32_t selected_link;
    uint32_t hovered_link;
    uint32_t active_control;
    uint32_t hovered_control;
} netsurf_state_t;


static netsurf_state_t *g_netsurf_state;

static void clamp_scroll(netsurf_state_t *state);
static void position_page_textbox(netsurf_state_t *state);
static void set_status(netsurf_state_t *state, const char *text);
static void netsurf_paint(bk_gui_window_t *window,
                          bk_gui_surface_t *surface, void *context);
static bk_gui_rect_t page_rect(netsurf_state_t *state,
                               bk_gui_rect_t content);
static void position_page_textbox(netsurf_state_t *state);

static uint32_t text_length(const char *text) {
    uint32_t length = 0;
    while (text && text[length]) length++;
    return length;
}

static int text_compare(const char *left, const char *right) {
    uint32_t i = 0;
    while (left && right && left[i] && left[i] == right[i]) i++;
    return (int)(uint8_t)(left ? left[i] : 0) -
           (int)(uint8_t)(right ? right[i] : 0);
}

static void text_copy(char *destination, uint32_t capacity,
                      const char *source) {
    uint32_t i = 0;
    if (!destination || capacity == 0U) return;
    while (source && source[i] && i + 1U < capacity) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void append_text(char *text, uint32_t capacity, const char *suffix) {
    uint32_t used = text_length(text);
    uint32_t index = 0;
    if (!text || capacity == 0U) return;
    while (suffix && suffix[index] && used + 1U < capacity)
        text[used++] = suffix[index++];
    text[used] = '\0';
}

static void append_uint(char *text, uint32_t capacity, uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    uint32_t used = text_length(text);
    if (value == 0U) digits[count++] = '0';
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count && used + 1U < capacity) text[used++] = digits[--count];
    text[used] = '\0';
}

static bool text_equal_nocase(const char *left, const char *right) {
    uint32_t i = 0U;
    if (!left || !right) return false;
    while (left[i] && right[i]) {
        char a = left[i], b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static uint32_t parse_uint(const char *text, uint32_t fallback) {
    uint32_t value = 0U;
    bool have_digit = false;
    while (text && *text >= '0' && *text <= '9') {
        have_digit = true;
        if (value > 100000U) return fallback;
        value = value * 10U + (uint32_t)(*text++ - '0');
    }
    return have_digit ? value : fallback;
}

static void internet_defaults(netsurf_state_t *state) {
    if (!state) return;
    text_copy(state->home_url, sizeof(state->home_url), NSBK_HOME_URL);
    /* QEMU + TLS por software puede dejar pausas superiores a 10 s entre
       fragmentos de páginas modernas. El timeout se aplica por operación de
       red, no congela el hilo de la interfaz. */
    state->request_timeout_ms = 30000U;
    state->allow_stylesheets = true;
    state->allow_images = true;
    state->allow_cookies = true;
}

static void internet_apply_line(netsurf_state_t *state, const char *line) {
    char key[32], value[NSBK_URL_MAX];
    uint32_t split = 0U, i = 0U, j = 0U;
    if (!state || !line) return;
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
    if (text_equal_nocase(key, "home") && value[0])
        text_copy(state->home_url, sizeof(state->home_url), value);
    else if (text_equal_nocase(key, "timeout")) {
        uint32_t timeout = parse_uint(value, 10000U);
        if (timeout >= 2000U && timeout <= 30000U)
            state->request_timeout_ms = timeout;
    } else if (text_equal_nocase(key, "images"))
        state->allow_images = value[0] != '0';
    else if (text_equal_nocase(key, "css"))
        state->allow_stylesheets = value[0] != '0';
    else if (text_equal_nocase(key, "cookies"))
        state->allow_cookies = value[0] != '0';
}

static void internet_load_settings(netsurf_state_t *state) {
    void *raw = NULL;
    uint32_t size = 0U, position = 0U;
    char line[320];
    internet_defaults(state);
    if (!bk_file_read_all(NSBK_INTERNET_CONFIG, &raw, &size) || !raw) return;
    while (position < size) {
        uint32_t used = 0U;
        while (position < size && ((char *)raw)[position] != '\n' &&
               used + 1U < sizeof(line)) {
            char c = ((char *)raw)[position++];
            if (c != '\r') line[used++] = c;
        }
        while (position < size && ((char *)raw)[position] != '\n') position++;
        if (position < size && ((char *)raw)[position] == '\n') position++;
        line[used] = '\0';
        internet_apply_line(state, line);
    }
    bk_sys_free(raw);
}

static void netsurf_present_progress(netsurf_state_t *state) {
    bk_gui_surface_t *surface = NULL;

    if (!state || !state->window) return;
    if (bk_gui_window_begin_immediate_paint(state->window, &surface) &&
        surface) {
        netsurf_paint(state->window, surface, state);
        bk_gui_window_end_immediate_paint(state->window, surface);
        return;
    }
    bk_gui_window_invalidate(state->window);
    bk_gui_request_paint();
}

static void netsurf_progress(void *context, uint8_t percent,
                             const char *phase) {
    netsurf_state_t *state = (netsurf_state_t *)context;
    if (!state) return;
    state->loading_progress = percent > 100U ? 100U : percent;
    if (phase && phase[0]) set_status(state, phase);
    /* The fetch runs in a worker thread. Do not enter the compositor from
       that thread: wake the GUI loop and yield so the desktop stays fluid. */
    if (state->window) bk_gui_window_invalidate(state->window);
    bk_sys_yield();
}

static bool point_in(int x, int y, bk_gui_rect_t rect) {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

static int32_t layout_screen_y(const nsbk_layout_item_t *item,
                               int32_t origin_y, uint32_t scroll) {
    if (!item) return origin_y;
    return origin_y + item->y -
           ((item->flags & NSBK_LAYOUT_FIXED) ? 0 : (int32_t)scroll);
}

static void set_status(netsurf_state_t *state, const char *text) {
    if (!state) return;
    text_copy(state->status, sizeof(state->status), text);
}

static void update_loaded_status(netsurf_state_t *state, bool success) {
    if (!state) return;
    if (!success) {
        if (state->document.error[0]) set_status(state, state->document.error);
        else set_status(state, "@HBC504817");
        return;
    }
    text_copy(state->status, sizeof(state->status),
              state->document.secure ? "HTTPS " : "HTTP ");
    append_uint(state->status, sizeof(state->status),
                state->document.http_status > 0 ?
                (uint32_t)state->document.http_status : 0U);
    if (state->document.redirect_count) {
        append_text(state->status, sizeof(state->status), " ->");
        append_uint(state->status, sizeof(state->status),
                    state->document.redirect_count);
    }
    append_text(state->status, sizeof(state->status),
                state->document.built_dom ? " DOM " : " recurso ");
    if (state->document.built_dom) {
        append_uint(state->status, sizeof(state->status),
                    state->document.node_count);
        append_text(state->status, sizeof(state->status), "/");
        append_uint(state->status, sizeof(state->status),
                    state->document.element_count);
    }
    if (state->document.layout.valid) {
        append_text(state->status, sizeof(state->status), " CSS ");
        append_uint(state->status, sizeof(state->status),
                    state->document.stylesheet_count);
        if (state->document.stylesheet_discovered_count) {
            append_text(state->status, sizeof(state->status), "/");
            append_uint(state->status, sizeof(state->status),
                        state->document.stylesheet_discovered_count);
        }
        if (state->document.stylesheet_failure_count) {
            append_text(state->status, sizeof(state->status), "!");
            append_uint(state->status, sizeof(state->status),
                        state->document.stylesheet_failure_count);
        }
        append_text(state->status, sizeof(state->status), " IMG ");
        append_uint(state->status, sizeof(state->status),
                    state->document.image_count);
        if (state->document.image_failures) {
            append_text(state->status, sizeof(state->status), "/-");
            append_uint(state->status, sizeof(state->status),
                        state->document.image_failures);
        }
    }
    append_text(state->status, sizeof(state->status), " enlaces ");
    append_uint(state->status, sizeof(state->status), state->document.link_count);
    if (state->document.form_count) {
        append_text(state->status, sizeof(state->status), " forms ");
        append_uint(state->status, sizeof(state->status), state->document.form_count);
    }
    if (state->document.cookie_count) {
        append_text(state->status, sizeof(state->status), " cookies ");
        append_uint(state->status, sizeof(state->status), state->document.cookie_count);
    }
    if (state->document.content_gzip)
        append_text(state->status, sizeof(state->status), " gzip");
    if (state->document.chunked)
        append_text(state->status, sizeof(state->status), " chunked");
    if (state->document.truncated)
        append_text(state->status, sizeof(state->status), " truncado");
}

static void show_link_status(netsurf_state_t *state, uint32_t link_index) {
    nsbk_html_link_t *link;
    if (!state || link_index >= state->document.link_count) return;
    link = &state->document.links[link_index];
    text_copy(state->status, sizeof(state->status), "Enlace: ");
    append_text(state->status, sizeof(state->status), link->href);
}

static void set_widget_enabled_safe(bk_gui_widget_t *widget, bool enabled) {
    if (widget) bk_gui_widget_set_enabled(widget, enabled);
}

static void update_navigation_buttons(netsurf_state_t *state) {
    bool has_back;
    bool has_forward;
    bool busy;
    if (!state) return;
    has_back = state->history_count > 0U && state->history_index > 0U;
    has_forward = state->history_count > 0U &&
                  state->history_index + 1U < state->history_count;
    busy = state->loading || state->pending_navigation;
    set_widget_enabled_safe(state->back_button, has_back && !busy);
    set_widget_enabled_safe(state->forward_button, has_forward && !busy);
    set_widget_enabled_safe(state->reload_button,
                            state->history_count > 0U && !busy);
    set_widget_enabled_safe(state->stop_button, state->loading);
    set_widget_enabled_safe(state->home_button, !busy);
    set_widget_enabled_safe(state->go_button, !busy);
}

static void history_push(netsurf_state_t *state, const char *url) {
    uint8_t index;
    if (!state || !url || !url[0]) return;
    if (state->history_count > 0U &&
        text_compare(state->history[state->history_index], url) == 0) return;
    if (state->history_count > 0U &&
        state->history_index + 1U < state->history_count)
        state->history_count = (uint8_t)(state->history_index + 1U);
    if (state->history_count < NSBK_HISTORY_MAX) {
        index = state->history_count++;
    } else {
        for (index = 1U; index < NSBK_HISTORY_MAX; index++)
            text_copy(state->history[index - 1U], NSBK_URL_MAX,
                      state->history[index]);
        index = NSBK_HISTORY_MAX - 1U;
    }
    text_copy(state->history[index], NSBK_URL_MAX, url);
    state->history_index = index;
}

static void split_fragment(const char *url, char *base, uint32_t base_capacity,
                           char *fragment, uint32_t fragment_capacity) {
    uint32_t i = 0U, j = 0U;
    if (base && base_capacity) base[0] = '\0';
    if (fragment && fragment_capacity) fragment[0] = '\0';
    if (!url) return;
    while (url[i] && url[i] != '#' && base && i + 1U < base_capacity) {
        base[i] = url[i]; i++;
    }
    if (base && base_capacity) base[i < base_capacity ? i : base_capacity - 1U] = '\0';
    while (url[i] && url[i] != '#') i++;
    if (url[i] == '#') i++;
    while (url[i] && fragment && j + 1U < fragment_capacity)
        fragment[j++] = url[i++];
    if (fragment && fragment_capacity) fragment[j] = '\0';
}

static bool scroll_to_fragment(netsurf_state_t *state, const char *fragment) {
    int32_t y;
    if (!state || !fragment || !fragment[0] ||
        !nsbk_layout_find_anchor(&state->document.layout, fragment, &y))
        return false;
    state->scroll_line = y > 0 ? (uint32_t)y : 0U;
    clamp_scroll(state);
    position_page_textbox(state);
    return true;
}

static void sync_address(netsurf_state_t *state, const char *url) {
    if (!state) return;
    text_copy(state->address, sizeof(state->address), url ? url : "");
    if (state->address_box)
        bk_gui_widget_set_text(state->address_box, state->address);
}

static bool queue_request(netsurf_state_t *state, const char *url,
                          const char *method, const char *body,
                          uint32_t body_length, bool add_history) {
    uint32_t i;
    if (!state || state->loading || state->pending_navigation ||
        !url || !url[0] || body_length >= sizeof(state->pending_body))
        return false;
    text_copy(state->pending_url, sizeof(state->pending_url), url);
    text_copy(state->pending_method, sizeof(state->pending_method),
              method && method[0] ? method : "GET");
    for (i = 0U; i < body_length; i++)
        state->pending_body[i] = body ? body[i] : 0;
    state->pending_body[body_length] = '\0';
    state->pending_body_length = body_length;
    state->pending_add_history = add_history;
    state->pending_navigation = true;
    set_status(state, "@HC84B370A");
    update_navigation_buttons(state);
    bk_gui_window_invalidate(state->window);
    return true;
}

static bool queue_url(netsurf_state_t *state, const char *url,
                      bool add_history) {
    return queue_request(state, url, "GET", NULL, 0U, add_history);
}

static void netsurf_fetch_worker(void *argument) {
    netsurf_state_t *state = (netsurf_state_t *)argument;
    bool success = false;
    if (state) {
        success = nsbk_document_fetch_request(&state->document,
                    state->worker_url,
                    state->worker_method[0] ? state->worker_method : "GET",
                    state->worker_body_length ? state->worker_body : NULL,
                    state->worker_body_length,
                    state->request_timeout_ms,
                    state->worker_viewport);
        state->worker_success = success;
        state->worker_complete = true;
    }
}

static void finish_load_request(netsurf_state_t *state) {
    char final_address[NSBK_URL_MAX];
    bool success;
    if (!state || !state->loading || !state->worker_complete) return;
    success = state->worker_success;
    if (state->cancel_requested) success = false;
    state->layout_width = state->document.viewport_width;
    final_address[0] = '\0';
    if (state->document.final_url[0]) {
        text_copy(final_address, sizeof(final_address), state->document.final_url);
        if (state->worker_fragment[0]) {
            append_text(final_address, sizeof(final_address), "#");
            append_text(final_address, sizeof(final_address), state->worker_fragment);
        }
        sync_address(state, final_address);
        if (state->history_count > 0U)
            text_copy(state->history[state->history_index], NSBK_URL_MAX,
                      final_address);
    }
    state->loading = false;
    state->worker_complete = false;
    state->loading_progress = 100U;
    state->progress_failed = !success && !state->cancel_requested;
    state->progress_visible_until = bk_sys_uptime_ms() + 900U;
    state->scroll_line = 0U;
    if (success && state->worker_fragment[0])
        scroll_to_fragment(state, state->worker_fragment);
    if (state->cancel_requested)
        set_status(state, "Carga detenida");
    else
        update_loaded_status(state, success);
    state->cancel_requested = false;
    update_navigation_buttons(state);
    position_page_textbox(state);
    bk_gui_window_invalidate(state->window);
}

static void load_request(netsurf_state_t *state, const char *url,
                         const char *method, const char *body,
                         uint32_t body_length, bool add_history) {
    char normalized[NSBK_URL_MAX];
    char request_url[NSBK_URL_MAX];
    char current_base[NSBK_URL_MAX];
    char fragment[NSBK_URL_MAX];
    bk_gui_rect_t content;
    int32_t viewport = 640;
    uint32_t i;
    if (!state || state->loading || !url || !url[0] ||
        body_length >= sizeof(state->worker_body)) return;
    nsbk_url_normalize(normalized, sizeof(normalized), url);
    if (!normalized[0]) return;
    split_fragment(normalized, request_url, sizeof(request_url),
                   fragment, sizeof(fragment));
    split_fragment(state->document.final_url, current_base, sizeof(current_base),
                   NULL, 0U);
    if ((!method || method[0] == 'G' || method[0] == 'g') && fragment[0] &&
        current_base[0] && text_compare(request_url, current_base) == 0 &&
        state->document.layout.valid) {
        sync_address(state, normalized);
        if (add_history) history_push(state, normalized);
        scroll_to_fragment(state, fragment);
        update_navigation_buttons(state);
        bk_gui_window_invalidate(state->window);
        return;
    }
    if (state->page_textbox) {
        bk_gui_widget_set_focus(state->window, state->page_textbox, false);
        bk_gui_widget_set_visible(state->window, state->page_textbox, false);
    }
    state->active_control = NSBK_NO_CONTROL;
    state->hovered_control = NSBK_NO_CONTROL;
    sync_address(state, normalized);
    if (add_history) history_push(state, normalized);
    text_copy(state->worker_url, sizeof(state->worker_url), request_url);
    text_copy(state->worker_method, sizeof(state->worker_method),
              method && method[0] ? method : "GET");
    text_copy(state->worker_fragment, sizeof(state->worker_fragment), fragment);
    for (i = 0U; i < body_length; i++)
        state->worker_body[i] = body ? body[i] : 0;
    state->worker_body[body_length] = '\0';
    state->worker_body_length = body_length;
    if (bk_gui_window_content_rect(state->window, &content))
        viewport = page_rect(state, content).w;
    state->worker_viewport = viewport;
    state->loading = true;
    state->cancel_requested = false;
    state->worker_complete = false;
    state->worker_success = false;
    state->scrollbar_drag.active = false;
    state->scrollbar_drag.grab_offset = 0;
    state->progress_failed = false;
    state->loading_progress = 2U;
    state->progress_visible_until = 0U;
    state->hovered_link = NSBK_NO_LINK;
    state->selected_link = NSBK_NO_LINK;
    set_status(state, method && (method[0] == 'P' || method[0] == 'p') ?
                       "@H9F0111C3" : "@HAFC3F2EA");
    update_navigation_buttons(state);
    netsurf_present_progress(state);
    if (bk_proc_spawn_thread("netsurf-fetch", netsurf_fetch_worker, state) < 0) {
        state->loading = false;
        state->progress_failed = true;
        set_status(state, "No se pudo iniciar el cargador");
        update_navigation_buttons(state);
        bk_gui_window_invalidate(state->window);
    }
}

static bool load_from_address_box(netsurf_state_t *state) {
    char address[NSBK_URL_MAX];
    bool read_ok = false;
    if (!state || state->loading || state->pending_navigation) return false;
    address[0] = '\0';
    if (state->address_box)
        read_ok = bk_gui_widget_get_text(state->address_box, address,
                                         sizeof(address));
    if (!read_ok || !address[0])
        text_copy(address, sizeof(address), state->address);
    if (!address[0])
        text_copy(address, sizeof(address), state->home_url);
    if (!address[0]) {
        set_status(state, "No hay una direccion para abrir");
        bk_gui_window_invalidate(state->window);
        return false;
    }
    if (state->address_box)
        bk_gui_widget_set_focus(state->window, state->address_box, false);
    if (!queue_url(state, address, true)) {
        set_status(state, "No se pudo iniciar la navegacion");
        bk_gui_window_invalidate(state->window);
        return false;
    }
    return true;
}

static void navigate_history(netsurf_state_t *state, int direction) {
    int target;
    if (!state || state->loading || state->history_count == 0U) return;
    target = (int)state->history_index + direction;
    if (target < 0 || target >= state->history_count) return;
    if (queue_url(state, state->history[target], false))
        state->history_index = (uint8_t)target;
}

static void open_link(netsurf_state_t *state, uint32_t link_index) {
    char resolved[NSBK_URL_MAX];
    if (!state || link_index >= state->document.link_count) return;
    nsbk_url_resolve(resolved, sizeof(resolved),
                     state->document.base_url[0] ? state->document.base_url :
                     state->address,
                     state->document.links[link_index].href);
    (void)queue_url(state, resolved, true);
}

static bk_gui_rect_t page_frame_rect(bk_gui_rect_t content) {
    return (bk_gui_rect_t){
        content.x + 7, content.y + NSBK_TOOLBAR_H,
        content.w - 14, content.h - NSBK_TOOLBAR_H - NSBK_STATUS_H - 3
    };
}

static bk_gui_rect_t page_rect(netsurf_state_t *state,
                               bk_gui_rect_t content) {
    bk_gui_rect_t page = page_frame_rect(content);
    if (state && state->document.title[0]) {
        page.y += 19;
        page.h -= 19;
    }
    page.w -= NSBK_SCROLLBAR_W;
    if (page.w < 1) page.w = 1;
    return page;
}

static bk_gui_rect_t page_scrollbar_rect(netsurf_state_t *state,
                                         bk_gui_rect_t content) {
    bk_gui_rect_t page = page_rect(state, content);
    return (bk_gui_rect_t){page.x + page.w, page.y, NSBK_SCROLLBAR_W, page.h};
}

static uint32_t page_content_height(netsurf_state_t *state,
                                    bk_gui_rect_t page) {
    uint32_t columns;
    uint32_t lines;
    if (!state) return 0U;
    if (state->document.layout.valid) {
        return state->document.layout.height > 0 ?
            (uint32_t)state->document.layout.height : 0U;
    }
    columns = page.w > 16 ? (uint32_t)(page.w - 16) / NSBK_CHAR_W : 8U;
    if (columns < 8U) columns = 8U;
    lines = nsbk_text_line_count(state->document.text_data,
                                 state->document.text_length, columns);
    return lines * NSBK_LINE_H;
}

static uint32_t maximum_scroll(netsurf_state_t *state, bk_gui_rect_t page) {
    uint32_t height = page_content_height(state, page);
    return height > (uint32_t)page.h ? height - (uint32_t)page.h : 0U;
}

static bool local_rect_contains(bk_gui_rect_t rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

static void local_scrollbar_init(bk_gui_scrollbar_t *bar,
                                 bk_gui_rect_t bounds, uint32_t value,
                                 uint32_t visible, uint32_t total) {
    uint32_t maximum;
    if (!bar) return;
    if (visible > total) visible = total;
    maximum = total > visible ? total - visible : 0U;
    if (value > maximum) value = maximum;
    bar->bounds = bounds;
    bar->value = value;
    bar->visible = visible;
    bar->total = total;
}

static bk_gui_rect_t local_scrollbar_thumb(const bk_gui_scrollbar_t *bar) {
    bk_gui_rect_t bounds;
    int track_y;
    int track_h;
    int thumb_h;
    int thumb_y;
    uint32_t maximum;
    if (!bar) return (bk_gui_rect_t){0, 0, 0, 0};
    bounds = bar->bounds;
    if (bounds.w <= 0 || bounds.h <= NSBK_SCROLLBAR_W * 2)
        return bounds;
    track_y = bounds.y + NSBK_SCROLLBAR_W;
    track_h = bounds.h - NSBK_SCROLLBAR_W * 2;
    if (!bar->total || bar->visible >= bar->total)
        return (bk_gui_rect_t){bounds.x + 2, track_y + 2,
                               bounds.w - 4, track_h - 4};
    thumb_h = (int)(((uint32_t)track_h * bar->visible) / bar->total);
    if (thumb_h < 12) thumb_h = 12;
    if (thumb_h > track_h) thumb_h = track_h;
    maximum = bar->total - bar->visible;
    thumb_y = track_y + (int)(((uint32_t)(track_h - thumb_h) * bar->value) /
                              (maximum ? maximum : 1U));
    return (bk_gui_rect_t){bounds.x + 2, thumb_y, bounds.w - 4, thumb_h};
}

static void local_scrollbar_arrow(bk_gui_surface_t *surface,
                                  bk_gui_rect_t rect, bool down) {
    int center_x;
    int center_y;
    if (!surface) return;
    center_x = rect.x + rect.w / 2;
    center_y = rect.y + rect.h / 2;
    bk_gui_surface_fill_rect(surface, rect, 0x00C0C0C0U);
    bk_gui_surface_draw_line(surface, rect.x, rect.y,
        rect.x + rect.w - 1, rect.y, 0x00FFFFFFU);
    bk_gui_surface_draw_line(surface, rect.x, rect.y,
        rect.x, rect.y + rect.h - 1, 0x00FFFFFFU);
    bk_gui_surface_draw_line(surface, rect.x, rect.y + rect.h - 1,
        rect.x + rect.w - 1, rect.y + rect.h - 1, 0x00404040U);
    bk_gui_surface_draw_line(surface, rect.x + rect.w - 1, rect.y,
        rect.x + rect.w - 1, rect.y + rect.h - 1, 0x00404040U);
    if (down) {
        bk_gui_surface_draw_line(surface, center_x - 4, center_y - 2,
            center_x + 4, center_y - 2, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 3, center_y - 1,
            center_x + 3, center_y - 1, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 2, center_y,
            center_x + 2, center_y, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 1, center_y + 1,
            center_x + 1, center_y + 1, 0x00101010U);
    } else {
        bk_gui_surface_draw_line(surface, center_x - 1, center_y - 2,
            center_x + 1, center_y - 2, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 2, center_y - 1,
            center_x + 2, center_y - 1, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 3, center_y,
            center_x + 3, center_y, 0x00101010U);
        bk_gui_surface_draw_line(surface, center_x - 4, center_y + 1,
            center_x + 4, center_y + 1, 0x00101010U);
    }
}

static void local_scrollbar_paint(bk_gui_surface_t *surface,
                                  const bk_gui_scrollbar_t *bar) {
    bk_gui_rect_t bounds;
    bk_gui_rect_t thumb;
    if (!surface || !bar) return;
    bounds = bar->bounds;
    if (bounds.w <= 0 || bounds.h <= 0) return;
    bk_gui_surface_fill_rect(surface, bounds, 0x00D8D8D8U);
    bk_gui_surface_draw_rect(surface, bounds, 0x00808080U);
    if (bounds.h >= NSBK_SCROLLBAR_W * 2) {
        local_scrollbar_arrow(surface,
            (bk_gui_rect_t){bounds.x, bounds.y, bounds.w, NSBK_SCROLLBAR_W},
            false);
        local_scrollbar_arrow(surface,
            (bk_gui_rect_t){bounds.x,
                            bounds.y + bounds.h - NSBK_SCROLLBAR_W,
                            bounds.w, NSBK_SCROLLBAR_W}, true);
    }
    thumb = local_scrollbar_thumb(bar);
    bk_gui_surface_fill_rect(surface, thumb, 0x00C0C0C0U);
    bk_gui_surface_draw_line(surface, thumb.x, thumb.y,
        thumb.x + thumb.w - 1, thumb.y, 0x00FFFFFFU);
    bk_gui_surface_draw_line(surface, thumb.x, thumb.y,
        thumb.x, thumb.y + thumb.h - 1, 0x00FFFFFFU);
    bk_gui_surface_draw_line(surface, thumb.x, thumb.y + thumb.h - 1,
        thumb.x + thumb.w - 1, thumb.y + thumb.h - 1, 0x00404040U);
    bk_gui_surface_draw_line(surface, thumb.x + thumb.w - 1, thumb.y,
        thumb.x + thumb.w - 1, thumb.y + thumb.h - 1, 0x00404040U);
}

static bool local_scrollbar_event(const bk_gui_scrollbar_t *bar,
                                  bk_gui_scrollbar_drag_t *drag,
                                  const bk_gui_event_t *event,
                                  uint32_t wheel_step,
                                  uint32_t *new_value) {
    bk_gui_rect_t thumb;
    uint32_t maximum;
    uint32_t value;
    int track_y;
    int movable;
    int thumb_top;
    int wheel_value;
    if (!bar || !drag || !event || !new_value) return false;
    maximum = bar->total > bar->visible ? bar->total - bar->visible : 0U;
    if (event->type == BK_GUI_EVENT_MOUSE_WHEEL) {
        if (!wheel_step) wheel_step = 1U;
        wheel_value = (int)bar->value - event->dy * (int)wheel_step;
        if (wheel_value < 0) wheel_value = 0;
        if ((uint32_t)wheel_value > maximum) wheel_value = (int)maximum;
        *new_value = (uint32_t)wheel_value;
        return true;
    }
    if (event->type == BK_GUI_EVENT_MOUSE_DOWN && event->button == 1 &&
        local_rect_contains(bar->bounds, event->x, event->y)) {
        thumb = local_scrollbar_thumb(bar);
        if (local_rect_contains(thumb, event->x, event->y) && maximum) {
            drag->active = true;
            drag->grab_offset = event->y - thumb.y;
            *new_value = bar->value;
            return true;
        }
        value = bar->value;
        if (bar->visible >= bar->total) value = 0U;
        else if (event->y < bar->bounds.y + NSBK_SCROLLBAR_W) {
            value = value > 0U ? value - 1U : 0U;
        } else if (event->y >= bar->bounds.y + bar->bounds.h - NSBK_SCROLLBAR_W) {
            if (value < maximum) value++;
        } else if (event->y < thumb.y) {
            value = value > bar->visible ? value - bar->visible : 0U;
        } else if (event->y >= thumb.y + thumb.h) {
            value += bar->visible;
            if (value > maximum) value = maximum;
        }
        *new_value = value;
        return true;
    }
    if (event->type == BK_GUI_EVENT_MOUSE_MOVE && drag->active) {
        if (!(event->buttons & 1U)) {
            drag->active = false;
            return false;
        }
        thumb = local_scrollbar_thumb(bar);
        track_y = bar->bounds.y + NSBK_SCROLLBAR_W;
        movable = bar->bounds.h - NSBK_SCROLLBAR_W * 2 - thumb.h;
        if (movable <= 0 || maximum == 0U) {
            *new_value = 0U;
            return true;
        }
        thumb_top = event->y - drag->grab_offset - track_y;
        if (thumb_top < 0) thumb_top = 0;
        if (thumb_top > movable) thumb_top = movable;
        *new_value = (uint32_t)thumb_top * maximum / (uint32_t)movable;
        return true;
    }
    if (event->type == BK_GUI_EVENT_MOUSE_UP && event->button == 1 &&
        drag->active) {
        drag->active = false;
        *new_value = bar->value;
        return true;
    }
    return false;
}

static void page_scrollbar(netsurf_state_t *state, bk_gui_rect_t content,
                           bk_gui_scrollbar_t *bar) {
    bk_gui_rect_t page;
    bk_gui_rect_t bounds;
    uint32_t total;
    if (!state || !bar) return;
    page = page_rect(state, content);
    bounds = page_scrollbar_rect(state, content);
    total = page_content_height(state, page);
    if (total < (uint32_t)page.h) total = (uint32_t)page.h;
    local_scrollbar_init(bar, bounds, state->scroll_line,
                         (uint32_t)page.h, total);
}

static bool handle_scrollbar_event(netsurf_state_t *state,
                                   const bk_gui_event_t *event,
                                   bk_gui_rect_t content) {
    bk_gui_scrollbar_t bar;
    uint32_t new_scroll;
    if (!state || !event) return false;
    page_scrollbar(state, content, &bar);
    new_scroll = state->scroll_line;
    if (!local_scrollbar_event(&bar, &state->scrollbar_drag,
                                event, NSBK_SCROLL_STEP, &new_scroll))
        return false;
    state->scroll_line = new_scroll;
    clamp_scroll(state);
    position_page_textbox(state);
    bk_gui_window_invalidate(state->window);
    return true;
}

static int32_t link_at_point(netsurf_state_t *state, bk_gui_rect_t page,
                             int x, int y) {
    uint32_t i;
    nsbk_layout_t *layout;
    if (!state || !state->document.layout.valid || !point_in(x, y, page))
        return -1;
    layout = &state->document.layout;
    for (i = 0U; i < layout->item_count; i++) {
        nsbk_layout_item_t *item = &layout->items[i];
        bk_gui_rect_t rect;
        int32_t screen_y;
        if (!(item->flags & (NSBK_LAYOUT_TEXT | NSBK_LAYOUT_IMAGE)) ||
            item->link_index < 0) continue;
        screen_y = layout_screen_y(item, page.y, state->scroll_line);
        rect = (bk_gui_rect_t){page.x + item->x, screen_y,
                              item->width, item->height};
        if (point_in(x, y, rect)) return item->link_index;
    }
    return -1;
}

static int32_t control_at_point(netsurf_state_t *state, bk_gui_rect_t page,
                                int x, int y) {
    uint32_t i;
    nsbk_layout_t *layout;
    if (!state || !state->document.layout.valid || !point_in(x, y, page))
        return -1;
    layout = &state->document.layout;
    for (i = 0U; i < layout->item_count; i++) {
        nsbk_layout_item_t *item = &layout->items[i];
        bk_gui_rect_t rect;
        int32_t screen_y;
        if (!(item->flags & NSBK_LAYOUT_CONTROL) || item->control_index < 0)
            continue;
        screen_y = layout_screen_y(item, page.y, state->scroll_line);
        rect = (bk_gui_rect_t){page.x + item->x, screen_y,
                              item->width, item->height};
        if (point_in(x, y, rect)) return item->control_index;
    }
    return -1;
}

static nsbk_layout_item_t *control_layout_item(netsurf_state_t *state,
                                                uint32_t control_index) {
    uint32_t i;
    if (!state || !state->document.layout.valid) return NULL;
    for (i = 0U; i < state->document.layout.item_count; i++) {
        nsbk_layout_item_t *item = &state->document.layout.items[i];
        if ((item->flags & NSBK_LAYOUT_CONTROL) && item->control_index >= 0 &&
            (uint32_t)item->control_index == control_index) return item;
    }
    return NULL;
}

static void hide_page_textbox(netsurf_state_t *state, bool commit) {
    char value[NSBK_HTML_FORM_VALUE_MAX];
    if (!state || !state->page_textbox) return;
    if (commit && state->active_control < state->document.control_count &&
        bk_gui_widget_get_text(state->page_textbox, value, sizeof(value)))
        nsbk_form_set_value(&state->document, state->active_control, value);
    bk_gui_widget_set_focus(state->window, state->page_textbox, false);
    bk_gui_widget_set_visible(state->window, state->page_textbox, false);
    state->active_control = NSBK_NO_CONTROL;
}

static void position_page_textbox(netsurf_state_t *state) {
    bk_gui_rect_t content, page, bounds;
    nsbk_layout_item_t *item;
    if (!state || !state->page_textbox ||
        state->active_control >= state->document.control_count ||
        !bk_gui_window_content_rect(state->window, &content)) return;
    item = control_layout_item(state, state->active_control);
    if (!item) { hide_page_textbox(state, true); return; }
    page = page_rect(state, content);
    bounds.x = page.x - content.x + item->x;
    bounds.y = layout_screen_y(item, page.y - content.y, state->scroll_line);
    bounds.w = item->width;
    bounds.h = item->height;
    if (bounds.y + bounds.h <= page.y - content.y ||
        bounds.y >= page.y - content.y + page.h) {
        bk_gui_widget_set_visible(state->window, state->page_textbox, false);
        return;
    }
    bk_gui_widget_set_bounds(state->window, state->page_textbox, bounds);
    bk_gui_widget_set_visible(state->window, state->page_textbox, true);
}

static void activate_control(netsurf_state_t *state, uint32_t control_index) {
    nsbk_html_control_t *control;
    if (!state || !state->page_textbox ||
        control_index >= state->document.control_count) return;
    control = &state->document.controls[control_index];
    if (control->disabled ||
        (control->type != NSBK_CONTROL_TEXT &&
         control->type != NSBK_CONTROL_SEARCH &&
         control->type != NSBK_CONTROL_PASSWORD &&
         control->type != NSBK_CONTROL_TEXTAREA)) return;
    if (state->active_control != control_index)
        hide_page_textbox(state, true);
    state->active_control = control_index;
    bk_gui_widget_set_text(state->page_textbox, control->value);
    position_page_textbox(state);
    bk_gui_widget_set_focus(state->window, state->page_textbox, true);
    bk_gui_window_invalidate(state->window);
}

static void submit_form(netsurf_state_t *state, uint32_t form_index,
                        uint32_t submit_control) {
    char url[NSBK_URL_MAX];
    char method[8];
    char body[NSBK_FORM_BODY_MAX];
    uint32_t body_length = 0U;
    if (!state || state->loading || form_index >= state->document.form_count)
        return;
    if (state->page_textbox &&
        state->active_control < state->document.control_count) {
        char value[NSBK_HTML_FORM_VALUE_MAX];
        if (bk_gui_widget_get_text(state->page_textbox, value, sizeof(value)))
            nsbk_form_set_value(&state->document, state->active_control, value);
    }
    if (!nsbk_form_build_submission(&state->document, form_index,
            submit_control, url, sizeof(url), method, sizeof(method),
            body, sizeof(body), &body_length)) {
        set_status(state, "@H34D9756C");
        return;
    }
    hide_page_textbox(state, false);
    if (!queue_request(state, url, method, body_length ? body : NULL,
                       body_length, true))
        set_status(state, "@H6AA1D86D");
}

static void scroll_selected_link_into_view(netsurf_state_t *state,
                                           bk_gui_rect_t page) {
    uint32_t i;
    nsbk_layout_t *layout;
    if (!state || state->selected_link >= state->document.link_count ||
        !state->document.layout.valid) return;
    layout = &state->document.layout;
    for (i = 0U; i < layout->item_count; i++) {
        nsbk_layout_item_t *item = &layout->items[i];
        uint32_t top;
        uint32_t bottom;
        if (item->link_index < 0 ||
            (uint32_t)item->link_index != state->selected_link) continue;
        if (item->flags & NSBK_LAYOUT_FIXED) return;
        top = item->y > 0 ? (uint32_t)item->y : 0U;
        bottom = top + (item->height > 0 ? (uint32_t)item->height : 1U);
        if (top < state->scroll_line)
            state->scroll_line = top;
        else if (bottom > state->scroll_line + (uint32_t)page.h)
            state->scroll_line = bottom - (uint32_t)page.h;
        return;
    }
}

static uint32_t css_opacity_color(uint32_t color, uint8_t opacity) {
    uint32_t r, g, b;
    if (opacity >= 255U) return color;
    r = (color >> 16U) & 0xffU;
    g = (color >> 8U) & 0xffU;
    b = color & 0xffU;
    r = (r * opacity + 255U * (255U - opacity)) / 255U;
    g = (g * opacity + 255U * (255U - opacity)) / 255U;
    b = (b * opacity + 255U * (255U - opacity)) / 255U;
    return (r << 16U) | (g << 8U) | b;
}

static void draw_spaced_text(bk_gui_surface_t *surface, bk_gui_rect_t clip,
                             int x, int y, const char *text, uint32_t length,
                             uint32_t foreground, uint16_t font_px,
                             bool bold, bool italic, bool monospace,
                             int8_t letter_spacing, int8_t word_spacing) {
    uint32_t i;
    int cursor = x;
    if (!surface || !text || length == 0U) return;
    if (letter_spacing == 0 && word_spacing == 0) {
        bk_gui_surface_draw_text_px(surface, x, y, text, length, foreground,
                                    font_px, bold, italic, monospace, clip);
        return;
    }
    for (i = 0U; i < length; i++) {
        uint16_t width = bk_gui_text_width_px(text + i, 1U, font_px,
                                              monospace, bold);
        bk_gui_surface_draw_text_px(surface, cursor, y, text + i, 1U,
                                    foreground, font_px, bold, italic,
                                    monospace, clip);
        cursor += (int)width + letter_spacing;
        if (text[i] == ' ') cursor += word_spacing;
        if (cursor >= clip.x + clip.w) break;
    }
}

static int32_t rounded_inset(int32_t radius, int32_t row) {
    int32_t dy = radius - 1 - row;
    int32_t inset = 0;
    while (inset < radius) {
        int32_t dx = radius - 1 - inset;
        if (dx * dx + dy * dy < radius * radius) break;
        inset++;
    }
    return inset;
}

static void fill_rounded_rect(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                              int32_t radius, uint32_t color) {
    int32_t row;
    if (!surface || rect.w <= 0 || rect.h <= 0) return;
    if (radius * 2 > rect.w) radius = rect.w / 2;
    if (radius * 2 > rect.h) radius = rect.h / 2;
    if (radius <= 0) {
        bk_gui_surface_fill_rect(surface, rect, color);
        return;
    }
    for (row = 0; row < rect.h; row++) {
        int32_t inset = 0;
        if (row < radius) inset = rounded_inset(radius, row);
        else if (row >= rect.h - radius)
            inset = rounded_inset(radius, rect.h - 1 - row);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + inset, rect.y + row,
                            rect.w - inset * 2, 1}, color);
    }
}

static void draw_control_box(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                             int32_t radius, uint32_t background,
                             uint32_t border) {
    fill_rounded_rect(surface, rect, radius, border);
    if (rect.w > 2 && rect.h > 2)
        fill_rounded_rect(surface,
            (bk_gui_rect_t){rect.x + 1, rect.y + 1,
                            rect.w - 2, rect.h - 2},
            radius > 1 ? radius - 1 : 0, background);
}

static void draw_control_item(netsurf_state_t *state,
                              bk_gui_surface_t *surface,
                              nsbk_layout_item_t *item,
                              bk_gui_rect_t rect) {
    nsbk_html_control_t *control;
    char display[NSBK_HTML_FORM_VALUE_MAX];
    uint32_t length, i;
    uint32_t foreground;
    uint32_t background, border;
    uint16_t font_px;
    int32_t radius;
    bool button;
    if (!state || !surface || !item || item->control_index < 0 ||
        (uint32_t)item->control_index >= state->document.control_count) return;
    if ((uint32_t)item->control_index == state->active_control) return;
    control = &state->document.controls[item->control_index];
    foreground = item->foreground;
    if (control->disabled) foreground = 0x00808080U;
    foreground = css_opacity_color(foreground, item->opacity);
    font_px = item->font_px ? item->font_px : 12U;
    if (font_px < 8U) font_px = 8U;
    if (font_px > 28U) font_px = 28U;
    button = control->type == NSBK_CONTROL_SUBMIT ||
             control->type == NSBK_CONTROL_BUTTON;
    if (control->type == NSBK_CONTROL_CHECKBOX ||
        control->type == NSBK_CONTROL_RADIO) {
        bk_gui_surface_fill_rect(surface, rect, 0x00FFFFFFU);
        bk_gui_surface_draw_rect(surface, rect,
            (uint32_t)item->control_index == state->hovered_control ?
            0x000000C0U : 0x00606060U);
        if (control->checked) {
            bk_gui_rect_t mark = {rect.x + 3, rect.y + 3,
                                  rect.w - 6, rect.h - 6};
            if (control->type == NSBK_CONTROL_RADIO)
                bk_gui_surface_fill_rect(surface, mark, 0x00000000U);
            else
                bk_gui_surface_draw_text(surface, rect.x + 2, rect.y,
                                         "x", 0x00000000U,
                                         0x00FFFFFFU, false);
        }
        return;
    }
    background = css_opacity_color(item->background, item->opacity);
    border = (uint32_t)item->control_index == state->hovered_control ?
             0x000000C0U : css_opacity_color(item->border, item->opacity);
    /* HTML controls need a usable fallback when border-radius is not exposed
       by this libcss version. Search/text fields use the familiar pill shape;
       buttons keep a restrained radius. This also removes the harsh square
       corners seen on current Google pages. */
    radius = button ? 4 : (control->type == NSBK_CONTROL_SEARCH ?
                           rect.h / 2 : 3);
    draw_control_box(surface, rect, radius, background, border);
    display[0] = '\0';
    if (button || control->type == NSBK_CONTROL_SELECT) {
        text_copy(display, sizeof(display), control->label);
    } else if (control->type == NSBK_CONTROL_PASSWORD) {
        length = text_length(control->value);
        if (length >= sizeof(display)) length = sizeof(display) - 1U;
        for (i = 0U; i < length; i++) display[i] = '*';
        display[length] = '\0';
    } else if (control->value[0]) {
        text_copy(display, sizeof(display), control->value);
    } else {
        text_copy(display, sizeof(display), control->placeholder);
        foreground = 0x00808080U;
    }
    while (display[0] && bk_gui_text_width_px(display, text_length(display),
            font_px, false, false) > (uint16_t)(rect.w > 12 ? rect.w - 12 : 1))
        display[text_length(display) - 1U] = '\0';
    if (button) {
        uint16_t text_w = bk_gui_text_width_px(display, text_length(display),
                                               font_px, false, false);
        bk_gui_surface_draw_text_px(surface,
            rect.x + (rect.w - (int)text_w) / 2,
            rect.y + (rect.h - (int)font_px) / 2,
            display, text_length(display), foreground, font_px,
            (item->flags & NSBK_LAYOUT_BOLD) != 0,
            (item->font_flags & NSBK_FONT_ITALIC) != 0,
            (item->font_flags & NSBK_FONT_MONOSPACE) != 0, rect);
    } else {
        bk_gui_surface_draw_text_px(surface, rect.x + 5,
            rect.y + (rect.h - (int)font_px) / 2,
            display, text_length(display), foreground, font_px,
            (item->flags & NSBK_LAYOUT_BOLD) != 0,
            (item->font_flags & NSBK_FONT_ITALIC) != 0,
            (item->font_flags & NSBK_FONT_MONOSPACE) != 0, rect);
    }
    if (control->type == NSBK_CONTROL_SELECT) {
        bk_gui_rect_t arrow = {rect.x + rect.w - 18, rect.y + 2, 16, rect.h - 4};
        bk_gui_surface_fill_rect(surface, arrow, 0x00E4E4E4U);
        bk_gui_surface_draw_rect(surface, arrow, 0x00808080U);
        bk_gui_surface_draw_text(surface, arrow.x + 5, arrow.y + 3,
                                 "v", 0x00000000U, 0x00E4E4E4U, false);
    }
}

static void draw_background_image(netsurf_state_t *state,
                                  bk_gui_surface_t *surface,
                                  bk_gui_rect_t rect,
                                  bk_gui_rect_t clip,
                                  const nsbk_layout_item_t *item) {
    const bk_gui_image_t *image;
    int32_t x, y;
    bool repeat_x, repeat_y;
    if (!state || !surface || !item || item->image_index < 0 ||
        (uint32_t)item->image_index >= state->document.image_count) return;
    image = &state->document.images[item->image_index].image;
    if (!image->pixels || !image->width || !image->height) return;
    repeat_x = item->background_repeat == 1U || item->background_repeat == 3U;
    repeat_y = item->background_repeat == 2U || item->background_repeat == 3U;
    x = rect.x + ((item->background_position_flags & NSBK_BG_POS_X_PERCENT) ?
        (rect.w - image->width) * item->background_x / 100 : item->background_x);
    y = rect.y + ((item->background_position_flags & NSBK_BG_POS_Y_PERCENT) ?
        (rect.h - image->height) * item->background_y / 100 : item->background_y);
    if (repeat_x) {
        while (x > rect.x) x -= image->width;
        while (x + image->width <= rect.x) x += image->width;
    }
    if (repeat_y) {
        while (y > rect.y) y -= image->height;
        while (y + image->height <= rect.y) y += image->height;
    }
    {
        int32_t start_x = x;
        for (; y < rect.y + rect.h; y += image->height) {
            for (x = start_x; x < rect.x + rect.w; x += image->width) {
            int32_t w = image->width;
            int32_t h = image->height;
            if (x + w > rect.x + rect.w) w = rect.x + rect.w - x;
            if (y + h > rect.y + rect.h) h = rect.y + rect.h - y;
            if (w > 0 && h > 0) {
                bk_gui_surface_draw_image(surface,
                    (bk_gui_rect_t){x, y, w, h}, clip, image);
            }
            if (!repeat_x) break;
            }
            if (!repeat_y) break;
        }
    }
}

static void draw_styled_document(netsurf_state_t *state,
                                 bk_gui_surface_t *surface,
                                 bk_gui_rect_t area) {
    nsbk_layout_t *layout = &state->document.layout;
    nsbk_plotter_t plotter;
    uint32_t i;
    nsbk_plotter_init(&plotter, surface, area);
    if (!layout->valid) return;
    for (i = 0U; i < layout->item_count; i++) {
        nsbk_layout_item_t *item = &layout->items[i];
        int32_t y = layout_screen_y(item, area.y, state->scroll_line);
        bk_gui_rect_t rect;
        if (y + item->height < area.y || y >= area.y + area.h) continue;
        rect = (bk_gui_rect_t){area.x + item->x, y,
                              item->width, item->height};
        if (item->flags & NSBK_LAYOUT_CONTROL) {
            draw_control_item(state, surface, item, rect);
            continue;
        }
        if ((item->flags & NSBK_LAYOUT_IMAGE) && item->image_index >= 0 &&
            (uint32_t)item->image_index < state->document.image_count) {
            bk_gui_surface_draw_image(surface, rect, area,
                &state->document.images[item->image_index].image);
            if (item->link_index >= 0 &&
                ((uint32_t)item->link_index == state->selected_link ||
                 (uint32_t)item->link_index == state->hovered_link))
                bk_gui_surface_draw_rect(surface, rect, 0x00000080U);
            continue;
        }
        if (item->flags & NSBK_LAYOUT_BOX) {
            if (item->flags & NSBK_LAYOUT_BACKGROUND)
                bk_gui_surface_fill_rect(surface, rect,
                    css_opacity_color(item->background, item->opacity));
            if (item->flags & NSBK_LAYOUT_BACKGROUND_IMAGE)
                draw_background_image(state, surface, rect, area, item);
            if (item->flags & NSBK_LAYOUT_BORDER)
                bk_gui_surface_draw_rect(surface, rect,
                    css_opacity_color(item->border, item->opacity));
            continue;
        }
        if (item->flags & NSBK_LAYOUT_RULE) {
            bk_gui_surface_fill_rect(surface, rect,
                                     css_opacity_color(item->border, item->opacity));
            continue;
        }
        if ((item->flags & NSBK_LAYOUT_TEXT) &&
            item->text_offset < layout->text_length) {
            const char *text = layout->text + item->text_offset;
            uint32_t foreground = css_opacity_color(item->foreground,
                                                     item->opacity);
            if (item->link_index >= 0 &&
                ((uint32_t)item->link_index == state->selected_link ||
                 (uint32_t)item->link_index == state->hovered_link)) {
                bk_gui_surface_fill_rect(surface,
                    (bk_gui_rect_t){rect.x - 1, rect.y - 1,
                                    rect.w + 2, rect.h}, 0x00000080U);
                foreground = 0x00FFFFFFU;
            }
            if (item->letter_spacing || item->word_spacing)
                draw_spaced_text(surface, area, rect.x, rect.y, text,
                    item->text_length, foreground,
                    item->font_px ? item->font_px : 12,
                    (item->flags & NSBK_LAYOUT_BOLD) != 0,
                    (item->font_flags & NSBK_FONT_ITALIC) != 0,
                    (item->font_flags & NSBK_FONT_MONOSPACE) != 0,
                    item->letter_spacing, item->word_spacing);
            else
                nsbk_plot_text(&plotter, rect.x, rect.y, text,
                               item->text_length, foreground,
                               item->font_px ? item->font_px : 12,
                               (item->flags & NSBK_LAYOUT_BOLD) != 0,
                               (item->font_flags & NSBK_FONT_ITALIC) != 0,
                               (item->font_flags & NSBK_FONT_MONOSPACE) != 0);
            if (item->flags & NSBK_LAYOUT_UNDERLINE)
                bk_gui_surface_fill_rect(surface,
                    (bk_gui_rect_t){rect.x, rect.y + rect.h - 3,
                                    rect.w, 1}, foreground);
        }
    }
}

static void draw_document(netsurf_state_t *state, bk_gui_surface_t *surface,
                          bk_gui_rect_t area) {
    char line[96];
    uint32_t columns;
    uint32_t visible_lines;
    uint32_t source = 0;
    uint32_t logical_line = 0;
    uint32_t row = 0;
    uint32_t line_used = 0;
    const char *text;
    uint32_t length;
    uint32_t scroll_line;

    if (!state || !surface || area.w <= 16 || area.h <= 8) return;
    if (state->document.layout.valid) {
        draw_styled_document(state, surface, area);
        return;
    }
    columns = (uint32_t)((area.w - 16) / NSBK_CHAR_W);
    if (columns >= sizeof(line)) columns = sizeof(line) - 1U;
    if (columns < 8U) columns = 8U;
    visible_lines = (uint32_t)((area.h - 8) / NSBK_LINE_H);
    scroll_line = state->scroll_line / NSBK_LINE_H;
    text = state->document.text_data;
    length = state->document.text_length;

    if (!text || length == 0U) {
        bk_gui_surface_draw_text(surface, area.x + 8, area.y + 8,
            "@HC6AE59AD", 0x00202020U,
            0x00FFFFFFU, false);
        bk_gui_surface_draw_text(surface, area.x + 8, area.y + 24,
            "@H4108ECD1",
            0x00404040U, 0x00FFFFFFU, false);
        bk_gui_surface_draw_text(surface, area.x + 8, area.y + 40,
            "@HEC2026CA",
            0x00404040U, 0x00FFFFFFU, false);
        return;
    }

    while (source <= length && row < visible_lines) {
        char character = source < length ? text[source++] : '\n';
        if (character == '\n' || line_used >= columns) {
            line[line_used] = '\0';
            if (logical_line >= scroll_line) {
                bk_gui_surface_draw_text(surface, area.x + 8,
                    area.y + 6 + (int)row * NSBK_LINE_H, line,
                    0x00101010U, 0x00FFFFFFU, false);
                row++;
            }
            logical_line++;
            line_used = 0;
            if (character != '\n' && source > 0U) source--;
            continue;
        }
        line[line_used++] = character;
    }
}

static void set_widget_bounds_safe(netsurf_state_t *state,
                                   bk_gui_widget_t *widget,
                                   bk_gui_rect_t bounds) {
    if (state && state->window && widget)
        bk_gui_widget_set_bounds(state->window, widget, bounds);
}

static void layout_toolbar_widgets(netsurf_state_t *state, int content_width) {
    int address_x = 214;
    int go_width = 42;
    int address_width = content_width - address_x - go_width - 12;
    if (!state || !state->window) return;
    if (address_width < 80) address_width = 80;
    set_widget_bounds_safe(state, state->back_button,
        (bk_gui_rect_t){6, 9, 30, 26});
    set_widget_bounds_safe(state, state->forward_button,
        (bk_gui_rect_t){38, 9, 30, 26});
    set_widget_bounds_safe(state, state->reload_button,
        (bk_gui_rect_t){70, 9, 48, 26});
    set_widget_bounds_safe(state, state->stop_button,
        (bk_gui_rect_t){120, 9, 42, 26});
    set_widget_bounds_safe(state, state->home_button,
        (bk_gui_rect_t){164, 9, 46, 26});
    set_widget_bounds_safe(state, state->address_box,
        (bk_gui_rect_t){address_x, 9, address_width, 26});
    set_widget_bounds_safe(state, state->go_button,
        (bk_gui_rect_t){address_x + address_width + 4, 9, go_width, 26});
}

static void draw_toolbar_background(bk_gui_surface_t *surface,
                                    bk_gui_rect_t content) {
    bk_gui_rect_t toolbar;
    if (!surface || content.w <= 0) return;
    toolbar = (bk_gui_rect_t){content.x, content.y,
                              content.w, NSBK_TOOLBAR_H};
    /* Paint browser chrome after the document.  This prevents a page repaint
       during scrolling from visually replacing the toolbar with page white. */
    bk_gui_surface_fill_rect(surface, toolbar, 0x00C0C0C0U);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){toolbar.x, toolbar.y, toolbar.w, 1}, 0x00FFFFFFU);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){toolbar.x, toolbar.y + toolbar.h - 3,
                        toolbar.w, 1}, 0x00606060U);
    bk_gui_surface_fill_rect(surface,
        (bk_gui_rect_t){toolbar.x, toolbar.y + toolbar.h - 2,
                        toolbar.w, 1}, 0x00FFFFFFU);
}

static void netsurf_paint(bk_gui_window_t *window UNUSED,
                          bk_gui_surface_t *surface, void *context) {
    netsurf_state_t *state = (netsurf_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t page_frame;
    bk_gui_rect_t page;
    bk_gui_scrollbar_t scrollbar;
    bk_gui_rect_t status;
    if (!state || !surface ||
        !bk_gui_window_content_rect(state->window, &content)) return;

    layout_toolbar_widgets(state, content.w);
    page_frame = page_frame_rect(content);
    page = page_rect(state, content);
    page_scrollbar(state, content, &scrollbar);
    status = (bk_gui_rect_t){content.x, content.y + content.h - NSBK_STATUS_H,
                             content.w, NSBK_STATUS_H};

    bk_gui_surface_clear(surface, 0x00C0C0C0U);

    bk_gui_surface_fill_rect(surface, page_frame, 0x00FFFFFFU);
    bk_gui_surface_draw_rect(surface, page_frame, 0x00808080U);
    if (state->document.title[0]) {
        bk_gui_rect_t title = (bk_gui_rect_t){page_frame.x + 1, page_frame.y + 1,
                                             page_frame.w - 2, 18};
        bk_gui_surface_fill_rect(surface, title, 0x00E8E8E8U);
        bk_gui_surface_draw_text(surface, title.x + 6, title.y + 5,
                                 state->document.title, 0x00000080U,
                                 0x00E8E8E8U, false);
    }
    draw_document(state, surface, page);
    local_scrollbar_paint(surface, &scrollbar);

    bk_gui_surface_fill_rect(surface, status, 0x00D4D0C8U);
    bk_gui_surface_draw_rect(surface, status, 0x00808080U);
    bk_gui_surface_draw_text(surface, status.x + 5, status.y + 5,
                             state->status, 0x00202020U, 0x00D4D0C8U, false);
    if (state->loading || (state->progress_visible_until &&
        bk_sys_uptime_ms() < state->progress_visible_until)) {
        bk_gui_rect_t frame = {status.x + status.w - 116, status.y + 3, 110, 12};
        uint32_t blocks = (state->loading_progress + 9U) / 10U;
        uint32_t color = state->progress_failed ? 0x00A02020U : 0x000000A0U;
        uint32_t i;
        bk_gui_surface_fill_rect(surface, frame, 0x00FFFFFFU);
        bk_gui_surface_draw_rect(surface, frame, 0x00808080U);
        for (i = 0U; i < blocks && i < 10U; i++) {
            bk_gui_surface_fill_rect(surface,
                (bk_gui_rect_t){frame.x + 3 + (int)i * 10, frame.y + 3, 7, 6},
                color);
        }
    }
    /* Keep the browser chrome opaque and stable across scroll repaints.
       The GUI compositor paints buttons/textboxes after this callback. */
    draw_toolbar_background(surface, content);
}

static void clamp_scroll(netsurf_state_t *state) {
    bk_gui_rect_t content;
    bk_gui_rect_t page;
    uint32_t maximum;
    if (!state || !bk_gui_window_content_rect(state->window, &content)) return;
    page = page_rect(state, content);
    maximum = maximum_scroll(state, page);
    if (state->scroll_line > maximum) state->scroll_line = maximum;
}

static void netsurf_go_callback(bk_gui_window_t *window UNUSED,
                                uint32_t widget_id UNUSED) {
    netsurf_state_t *state = g_netsurf_state;
    if (!state) return;
    bk_console_write("[NETSURF] boton Ir\n");
    (void)load_from_address_box(state);
}

static void netsurf_home_callback(bk_gui_window_t *window UNUSED,
                                  uint32_t widget_id UNUSED) {
    netsurf_state_t *state = g_netsurf_state;
    if (!state) return;
    bk_console_write("[NETSURF] boton Inicio\n");
    if (state->loading || state->pending_navigation) return;
    if (!queue_url(state, state->home_url, true)) {
        set_status(state, "No se pudo abrir la pagina de inicio");
        bk_gui_window_invalidate(state->window);
    }
}

static void netsurf_widget_callback(bk_gui_window_t *window UNUSED,
                                    uint32_t widget_id) {
    netsurf_state_t *state = g_netsurf_state;
    if (!state) return;
    if (state->stop_id && widget_id == state->stop_id) {
        if (state->loading) {
            state->cancel_requested = true;
            set_status(state, "Deteniendo carga...");
            bk_gui_window_invalidate(state->window);
        }
        return;
    }
    if (state->loading || state->pending_navigation) return;
    if (state->page_textbox_id && widget_id == state->page_textbox_id &&
        state->active_control < state->document.control_count) {
        nsbk_html_control_t *control =
            &state->document.controls[state->active_control];
        submit_form(state, control->form_index, NSBK_NO_CONTROL);
    } else if (state->back_id && widget_id == state->back_id)
        navigate_history(state, -1);
    else if (state->forward_id && widget_id == state->forward_id)
        navigate_history(state, 1);
    else if (state->reload_id && widget_id == state->reload_id)
        (void)queue_url(state, state->address, false);
    else if (state->home_id && widget_id == state->home_id)
        netsurf_home_callback(state->window, widget_id);
    else if (state->go_id && widget_id == state->go_id)
        netsurf_go_callback(state->window, widget_id);
    else if (state->address_id && widget_id == state->address_id)
        (void)load_from_address_box(state);
}

static bool netsurf_event(bk_gui_window_t *window UNUSED,
                          const bk_gui_event_t *event, void *context) {
    netsurf_state_t *state = (netsurf_state_t *)context;
    bk_gui_rect_t content;
    bk_gui_rect_t page;
    uint8_t key;
    if (!state || !event ||
        !bk_gui_window_content_rect(state->window, &content)) return false;
    page = page_rect(state, content);

    if (!state->loading && handle_scrollbar_event(state, event, content))
        return true;

    /* The widget layer queues Ring 3 callbacks asynchronously.  Keep a
       window-level fallback for the two essential navigation buttons so a
       dropped/full upcall queue can never leave them visually pressed but
       inactive.  If the normal callback is delivered afterwards it sees the
       pending request and becomes a harmless no-op. */
    if (event->type == BK_GUI_EVENT_MOUSE_UP) {
        int address_x = 214;
        int go_width = 42;
        int address_width = content.w - address_x - go_width - 12;
        bk_gui_rect_t home_rect;
        bk_gui_rect_t go_rect;
        if (address_width < 80) address_width = 80;
        home_rect = (bk_gui_rect_t){content.x + 164, content.y + 9, 46, 26};
        go_rect = (bk_gui_rect_t){content.x + address_x + address_width + 4,
                                  content.y + 9, go_width, 26};
        if (local_rect_contains(go_rect, event->x, event->y)) {
            bk_console_write("[NETSURF] clic directo Ir\n");
            netsurf_go_callback(state->window, state->go_id);
            return true;
        }
        if (local_rect_contains(home_rect, event->x, event->y)) {
            bk_console_write("[NETSURF] clic directo Inicio\n");
            netsurf_home_callback(state->window, state->home_id);
            return true;
        }
    }

    if (event->type == BK_GUI_EVENT_MOUSE_MOVE && !state->loading) {
        int32_t control = control_at_point(state, page, event->x, event->y);
        int32_t link = control < 0 ?
            link_at_point(state, page, event->x, event->y) : -1;
        uint32_t next_link = link >= 0 ? (uint32_t)link : NSBK_NO_LINK;
        uint32_t next_control = control >= 0 ?
            (uint32_t)control : NSBK_NO_CONTROL;
        if (next_link != state->hovered_link ||
            next_control != state->hovered_control) {
            state->hovered_link = next_link;
            state->hovered_control = next_control;
            if (next_control < state->document.control_count) {
                nsbk_html_control_t *field =
                    &state->document.controls[next_control];
                text_copy(state->status, sizeof(state->status), "Formulario: ");
                append_text(state->status, sizeof(state->status),
                            field->name[0] ? field->name : field->label);
            } else if (next_link < state->document.link_count) {
                show_link_status(state, next_link);
            } else {
                update_loaded_status(state, true);
            }
            bk_gui_window_invalidate(state->window);
        }
        return next_link != NSBK_NO_LINK || next_control != NSBK_NO_CONTROL;
    }

    if (event->type == BK_GUI_EVENT_MOUSE_UP && !state->loading) {
        int32_t control_index = control_at_point(state, page, event->x, event->y);
        if (control_index >= 0 &&
            (uint32_t)control_index < state->document.control_count) {
            nsbk_html_control_t *control =
                &state->document.controls[control_index];
            if (control->disabled) return true;
            if (control->type == NSBK_CONTROL_TEXT ||
                control->type == NSBK_CONTROL_SEARCH ||
                control->type == NSBK_CONTROL_PASSWORD ||
                control->type == NSBK_CONTROL_TEXTAREA) {
                activate_control(state, (uint32_t)control_index);
            } else if (control->type == NSBK_CONTROL_CHECKBOX ||
                       control->type == NSBK_CONTROL_RADIO) {
                hide_page_textbox(state, true);
                nsbk_form_toggle(&state->document, (uint32_t)control_index);
                bk_gui_window_invalidate(state->window);
            } else if (control->type == NSBK_CONTROL_SELECT) {
                hide_page_textbox(state, true);
                nsbk_form_select_next(&state->document,
                                      (uint32_t)control_index, 1);
                bk_gui_window_invalidate(state->window);
            } else if (control->type == NSBK_CONTROL_SUBMIT) {
                submit_form(state, control->form_index,
                            (uint32_t)control_index);
            }
            return true;
        }
        {
            int32_t link = link_at_point(state, page, event->x, event->y);
            if (link >= 0) {
                hide_page_textbox(state, true);
                state->selected_link = (uint32_t)link;
                open_link(state, (uint32_t)link);
                return true;
            }
        }
    }

    if (event->type == BK_GUI_EVENT_MOUSE_WHEEL) {
        if (event->dy < 0) state->scroll_line += NSBK_SCROLL_STEP;
        else if (state->scroll_line >= NSBK_SCROLL_STEP)
            state->scroll_line -= NSBK_SCROLL_STEP;
        else state->scroll_line = 0U;
        clamp_scroll(state);
        position_page_textbox(state);
        bk_gui_window_invalidate(state->window);
        return true;
    }

    if (event->type != BK_GUI_EVENT_KEY) return false;
    key = (uint8_t)event->key;

    if (state->loading) {
        if (key == BK_KEY_ESCAPE) {
            state->cancel_requested = true;
            set_status(state, "Deteniendo carga...");
            bk_gui_window_invalidate(state->window);
            return true;
        }
        return false;
    }

    if (event->ctrl && (key == 'l' || key == 'L' ||
                        key == 'k' || key == 'K')) {
        hide_page_textbox(state, true);
        if (state->address_box)
        if (state->address_box) bk_gui_widget_set_focus(state->window, state->address_box, true);
        bk_gui_window_invalidate(state->window);
        return true;
    }
    if (event->alt && key == BK_KEY_LEFT) {
        navigate_history(state, -1);
        return true;
    }
    if (event->alt && key == BK_KEY_RIGHT) {
        navigate_history(state, 1);
        return true;
    }
    if (key == BK_KEY_F5 || (event->ctrl && (key == 'r' || key == 'R'))) {
        (void)queue_url(state, state->address, false);
        return true;
    }
    if ((state->address_box && bk_gui_widget_is_focused(state->window, state->address_box)) ||
        (state->page_textbox &&
         bk_gui_widget_is_focused(state->window, state->page_textbox)))
        return false;
    if (key == BK_KEY_BACKSPACE) {
        navigate_history(state, event->shift ? 1 : -1);
        return true;
    }

    if (key == BK_KEY_TAB && state->document.link_count > 0U) {
        if (state->selected_link >= state->document.link_count)
            state->selected_link = 0U;
        else
            state->selected_link =
                (state->selected_link + 1U) % state->document.link_count;
        show_link_status(state, state->selected_link);
        scroll_selected_link_into_view(state, page);
        clamp_scroll(state);
        bk_gui_window_invalidate(state->window);
        return true;
    }
    if (key == BK_KEY_ENTER &&
        state->selected_link < state->document.link_count) {
        open_link(state, state->selected_link);
        return true;
    }
    if (key == BK_KEY_UP) {
        state->scroll_line = state->scroll_line >= 18U ?
                             state->scroll_line - 18U : 0U;
    } else if (key == BK_KEY_DOWN) {
        state->scroll_line += 18U;
    } else if (key == BK_KEY_PGUP) {
        state->scroll_line = state->scroll_line >= (uint32_t)page.h ?
                             state->scroll_line - (uint32_t)page.h : 0U;
    } else if (key == BK_KEY_PGDN) {
        state->scroll_line += (uint32_t)page.h;
    } else if (key == BK_KEY_HOME) {
        state->scroll_line = 0U;
    } else if (key == BK_KEY_END) {
        state->scroll_line = state->document.layout.valid &&
                             state->document.layout.height > 0 ?
                             (uint32_t)state->document.layout.height :
                             0U;
    } else {
        return false;
    }
    clamp_scroll(state);
    position_page_textbox(state);
    bk_gui_window_invalidate(state->window);
    return true;
}

static void netsurf_menu_callback(bk_gui_window_t *window UNUSED,
                                   uint32_t item_id, void *context) {
    netsurf_state_t *state = (netsurf_state_t *)context;
    if (!state) return;
    switch (item_id) {
    case NSBK_MENU_FILE_OPEN:
        if (state->address_box) bk_gui_widget_set_focus(state->window, state->address_box, true);
        break;
    case NSBK_MENU_FILE_CLOSE:
        bk_gui_close_window(state->window);
        return;
    case NSBK_MENU_VIEW_RELOAD:
        if (!state->loading) (void)queue_url(state, state->address, false);
        break;
    case NSBK_MENU_VIEW_STOP:
        if (state->loading) {
            state->cancel_requested = true;
            set_status(state, "Deteniendo carga...");
        }
        break;
    case NSBK_MENU_VIEW_IMAGES:
        state->allow_images = !state->allow_images;
        nsbk_document_set_preferences(&state->document, state->allow_stylesheets,
                                      state->allow_images, state->allow_cookies);
        set_status(state, state->allow_images ? "Imagenes activadas" :
                                               "Imagenes desactivadas");
        break;
    case NSBK_MENU_VIEW_CSS:
        state->allow_stylesheets = !state->allow_stylesheets;
        nsbk_document_set_preferences(&state->document, state->allow_stylesheets,
                                      state->allow_images, state->allow_cookies);
        set_status(state, state->allow_stylesheets ? "CSS externo activado" :
                                                    "CSS externo desactivado");
        break;
    case NSBK_MENU_GO_BACK:
        if (!state->loading) navigate_history(state, -1);
        break;
    case NSBK_MENU_GO_FORWARD:
        if (!state->loading) navigate_history(state, 1);
        break;
    case NSBK_MENU_GO_HOME:
        if (!state->loading) (void)queue_url(state, state->home_url, true);
        break;
    case NSBK_MENU_HELP_ABOUT:
        set_status(state, "NetSurf 3.11 - Hubbub, libcss y BlesKernOS");
        break;
    default:
        break;
    }
    update_navigation_buttons(state);
    bk_gui_window_invalidate(state->window);
}

static bool create_menus(netsurf_state_t *state) {
    int file, view, go, help;
    if (!state || !state->window) return false;
    file = bk_gui_add_menu(state->window, "Archivo");
    view = bk_gui_add_menu(state->window, "Ver");
    go = bk_gui_add_menu(state->window, "Ir");
    help = bk_gui_add_menu(state->window, "Ayuda");
    if (file < 0 || view < 0 || go < 0 || help < 0) return false;
    return
        bk_gui_add_menu_item(state->window, file, NSBK_MENU_FILE_OPEN,
                             "Abrir direccion", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, file, NSBK_MENU_FILE_CLOSE,
                             "Cerrar", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, view, NSBK_MENU_VIEW_RELOAD,
                             "Recargar", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, view, NSBK_MENU_VIEW_STOP,
                             "Detener", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, view, NSBK_MENU_VIEW_IMAGES,
                             "Alternar imagenes", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, view, NSBK_MENU_VIEW_CSS,
                             "Alternar CSS", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, go, NSBK_MENU_GO_BACK,
                             "Atras", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, go, NSBK_MENU_GO_FORWARD,
                             "Adelante", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, go, NSBK_MENU_GO_HOME,
                             "Inicio", netsurf_menu_callback, state) &&
        bk_gui_add_menu_item(state->window, help, NSBK_MENU_HELP_ABOUT,
                             "Acerca de NetSurf", netsurf_menu_callback, state);
}

static bool create_toolbar(netsurf_state_t *state) {
    if (!state || !state->desktop || !state->window) return false;
    state->back_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){6, 6, 30, 24}, "<", netsurf_widget_callback);
    state->forward_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){38, 6, 30, 24}, ">", netsurf_widget_callback);
    state->reload_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){70, 9, 48, 26}, "Rec", netsurf_widget_callback);
    state->stop_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){120, 9, 42, 26}, "X", netsurf_widget_callback);
    state->home_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){164, 9, 46, 26}, "@H799FFCDA", netsurf_widget_callback);
    state->address_box = bk_gui_create_textbox(state->desktop, state->window,
        (bk_gui_rect_t){214, 9, 392, 26}, state->address,
        NSBK_URL_MAX - 1U, netsurf_widget_callback);
    state->go_button = bk_gui_create_button(state->desktop, state->window,
        (bk_gui_rect_t){610, 9, 42, 26}, "@H4CE8DBA2", netsurf_widget_callback);
    state->page_textbox = bk_gui_create_textbox(state->desktop, state->window,
        (bk_gui_rect_t){0, 0, 8, 8}, "",
        NSBK_HTML_FORM_VALUE_MAX - 1U, netsurf_widget_callback);
    if (!state->back_button || !state->forward_button ||
        !state->reload_button || !state->stop_button || !state->home_button ||
        !state->address_box || !state->go_button ||
        !state->page_textbox) return false;
    (void)bk_gui_widget_set_icon(state->back_button, "ArrowLeft");
    (void)bk_gui_widget_set_icon(state->forward_button, "ArrowRight");
    (void)bk_gui_widget_set_icon(state->reload_button, "Refresh");
    (void)bk_gui_widget_set_icon(state->stop_button, "Close");
    (void)bk_gui_widget_set_icon(state->home_button, "WebOpen");
    (void)bk_gui_widget_set_icon(state->go_button, "ArrowRight");
    state->back_id = bk_gui_widget_id(state->back_button);
    state->forward_id = bk_gui_widget_id(state->forward_button);
    state->reload_id = bk_gui_widget_id(state->reload_button);
    state->stop_id = bk_gui_widget_id(state->stop_button);
    state->home_id = bk_gui_widget_id(state->home_button);
    state->address_id = bk_gui_widget_id(state->address_box);
    state->go_id = bk_gui_widget_id(state->go_button);
    state->page_textbox_id = bk_gui_widget_id(state->page_textbox);
    bk_gui_widget_set_visible(state->window, state->page_textbox, false);
    update_navigation_buttons(state);
    return true;
}

void bleskernos_program_main(bk_gui_desktop_t *desktop) {
    netsurf_state_t *state;
    char launch_url[NSBK_URL_MAX];

    if (bk_sys_api_version() < NSBK_MIN_API_VERSION) {
        bk_console_write("[NETSURF] API del sistema incompatible\n");
        return;
    }
    bk_console_write("[NETSURF] inicio\n");
    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) {
        bk_console_write("[NETSURF] no hay escritorio\n");
        return;
    }

    state = (netsurf_state_t *)bk_sys_alloc(sizeof(*state));
    if (!state) {
        bk_console_write("[NETSURF] sin memoria para estado\n");
        return;
    }
    state->desktop = desktop;
    state->window = NULL;
    state->back_button = NULL;
    state->forward_button = NULL;
    state->reload_button = NULL;
    state->stop_button = NULL;
    state->home_button = NULL;
    state->address_box = NULL;
    state->go_button = NULL;
    state->page_textbox = NULL;
    state->address[0] = '\0';
    state->home_url[0] = '\0';
    state->status[0] = '\0';
    state->pending_url[0] = '\0';
    state->pending_method[0] = '\0';
    state->pending_body[0] = '\0';
    state->worker_url[0] = '\0';
    state->worker_method[0] = '\0';
    state->worker_body[0] = '\0';
    state->worker_fragment[0] = '\0';
    state->history_count = 0U;
    state->history_index = 0U;
    state->scroll_line = 0U;
    state->scrollbar_drag.active = false;
    state->scrollbar_drag.grab_offset = 0;
    state->layout_width = 0;
    state->request_timeout_ms = 10000U;
    state->progress_visible_until = 0U;
    state->pending_body_length = 0U;
    state->worker_body_length = 0U;
    state->worker_viewport = 640;
    state->loading_progress = 0U;
    state->worker_complete = false;
    state->worker_success = false;
    state->loading = false;
    state->pending_navigation = false;
    state->pending_add_history = false;
    state->progress_failed = false;
    state->cancel_requested = false;
    state->allow_stylesheets = true;
    state->allow_images = true;
    state->allow_cookies = true;
    state->selected_link = NSBK_NO_LINK;
    state->hovered_link = NSBK_NO_LINK;
    state->active_control = NSBK_NO_CONTROL;
    state->hovered_control = NSBK_NO_CONTROL;
    state->document.network_data = NULL;
    state->document.resource_data = NULL;
    state->document.decode_data = NULL;
    state->document.text_data = NULL;

    internet_load_settings(state);
    if (!nsbk_document_init(&state->document)) {
        bk_console_write("[NETSURF] fallo al iniciar documento\n");
        bk_sys_free(state);
        return;
    }
    bk_console_write("[NETSURF] documento listo\n");
    nsbk_document_set_progress(&state->document, netsurf_progress, state);
    nsbk_document_set_cancel_flag(&state->document,
                                  &state->cancel_requested);
    nsbk_document_set_preferences(&state->document,
        state->allow_stylesheets, state->allow_images, state->allow_cookies);
    text_copy(state->address, sizeof(state->address), state->home_url);
    set_status(state, "@H10E97821");
    launch_url[0] = '\0';
    if (bk_proc_launch_arg_copy(launch_url, sizeof(launch_url)) && launch_url[0])
        text_copy(state->address, sizeof(state->address), launch_url);

    state->window = bk_gui_create_window(desktop, 40, 30,
                                          NSBK_WINDOW_W, NSBK_WINDOW_H,
                                          "@HE4ACFE1B");
    if (!state->window) {
        bk_console_write("[NETSURF] fallo al crear ventana\n");
        nsbk_document_destroy(&state->document);
        bk_sys_free(state);
        return;
    }
    bk_console_write("[NETSURF] ventana creada\n");
    g_netsurf_state = state;
    bk_gui_set_window_content(state->window, netsurf_paint, state);
    bk_gui_set_window_event_handler(state->window, netsurf_event, state);
    bk_gui_set_window_min_size(state->window, 500, 280);
    bk_gui_window_set_owner(state->window, bk_sys_getpid());
    bk_proc_bind_window(state->window);
    (void)create_toolbar(state);
    bk_console_write("[NETSURF] barra procesada stage18.2\n");
    if (!create_menus(state))
        bk_console_write("[NETSURF] aviso: menus no disponibles\n");
    else
        bk_console_write("[NETSURF] menus listos\n");
    if (state->address_box) bk_gui_widget_set_focus(state->window, state->address_box, true);
    bk_gui_window_invalidate(state->window);
    (void)queue_url(state, state->address, true);
    bk_console_write("[NETSURF] bucle principal\n");

    while (bk_gui_window_is_open(state->window) && !bk_proc_exit_requested()) {
        bk_gui_rect_t content;
        if (state->loading && state->worker_complete)
            finish_load_request(state);
        if (state->pending_navigation && !state->loading) {
            state->pending_navigation = false;
            load_request(state, state->pending_url, state->pending_method,
                         state->pending_body_length ? state->pending_body : NULL,
                         state->pending_body_length,
                         state->pending_add_history);
            state->pending_url[0] = '\0';
            state->pending_method[0] = '\0';
            state->pending_body[0] = '\0';
            state->pending_body_length = 0U;
            state->pending_add_history = false;
        }
        if (!state->loading && state->progress_visible_until &&
            bk_sys_uptime_ms() >= state->progress_visible_until) {
            state->progress_visible_until = 0U;
            bk_gui_window_invalidate(state->window);
        }
        if (!state->loading && state->document.body_length > 0U &&
            bk_gui_window_content_rect(state->window, &content)) {
            int32_t width = page_rect(state, content).w;
            if (width >= 160 && width != state->layout_width &&
                nsbk_document_reflow(&state->document, width)) {
                state->layout_width = width;
                clamp_scroll(state);
                position_page_textbox(state);
                bk_gui_window_invalidate(state->window);
            }
        }
        if (state->active_control < state->document.control_count)
            position_page_textbox(state);
        bk_sys_sleep_ms(20U);
    }

    /* The worker owns document buffers until it reports completion. */
    while (state->loading && !state->worker_complete)
        bk_sys_sleep_ms(10U);
    if (state->loading) finish_load_request(state);
    bk_proc_bind_window(NULL);
    g_netsurf_state = NULL;
    if (state->window) bk_gui_destroy_window(state->desktop, state->window);
    nsbk_document_destroy(&state->document);
    bk_sys_free(state);
}
