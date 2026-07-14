#include "kernel/include/api.h"

#define SCREENSAVER_DEFAULT_PATH "/SYSTEM/SCREENS/SSLOGO.SCV"
#define SCREENSAVER_MAX_PATH 64

typedef struct {
    gui_desktop_t *desktop;
} screensaverd_state_t;

static gui_desktop_t *g_screensaver_desktop;
static bool g_screensaver_enabled = true;
static uint32_t g_screensaver_timeout_seconds = 300;
static char g_screensaver_path[SCREENSAVER_MAX_PATH] = SCREENSAVER_DEFAULT_PATH;
static bool g_screensaverd_started;

static bool text_starts_with(const char *text, const char *prefix) {
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

static const char *find_ini_value(const char *text, const char *key) {
    size_t key_len;

    if (!text || !key) return NULL;
    key_len = bk_runtime_strlen(key);
    for (uint32_t i = 0; text[i]; i++) {
        if (i != 0 && text[i - 1] != '\n' && text[i - 1] != '\r')
            continue;
        if (!text_starts_with(text + i, key)) continue;
        if (text[i + key_len] == '=') return text + i + key_len + 1;
    }
    return NULL;
}

static uint32_t parse_uint_default(const char *text, uint32_t fallback) {
    uint32_t value = 0;
    bool any = false;

    if (!text) return fallback;
    while (*text >= '0' && *text <= '9') {
        any = true;
        value = value * 10U + (uint32_t)(*text - '0');
        text++;
    }
    return any ? value : fallback;
}

static void copy_ini_string(char *dst, size_t dst_len, const char *src) {
    size_t i = 0;

    if (!dst || !dst_len) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] && src[i] != '\n' && src[i] != '\r' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void append_char(char *text, size_t capacity, size_t *length, char c) {
    if (!text || !length || *length + 1 >= capacity) return;
    text[*length] = c;
    (*length)++;
    text[*length] = '\0';
}

static void append_string(char *text, size_t capacity, size_t *length,
                          const char *src) {
    while (src && *src) append_char(text, capacity, length, *src++);
}

static void append_uint(char *text, size_t capacity, size_t *length,
                        uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (value == 0) {
        append_char(text, capacity, length, '0');
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count) append_char(text, capacity, length, digits[--count]);
}

static void screensaver_save_config(void) {
    char text[160];
    size_t len = 0;

    text[0] = '\0';
    append_string(text, sizeof(text), &len, "enabled=");
    append_char(text, sizeof(text), &len, g_screensaver_enabled ? '1' : '0');
    append_char(text, sizeof(text), &len, '\n');
    append_string(text, sizeof(text), &len, "timeout=");
    append_uint(text, sizeof(text), &len, g_screensaver_timeout_seconds);
    append_char(text, sizeof(text), &len, '\n');
    append_string(text, sizeof(text), &len, "path=");
    append_string(text, sizeof(text), &len, g_screensaver_path);
    append_char(text, sizeof(text), &len, '\n');

    (void)bk_user_config_write_text(BK_SCREENSAVER_CONFIG_PATH, text);
}

static void screensaver_normalize_path(void) {
    if (bk_runtime_strcmp(g_screensaver_path, "/SYSTEM/SCREENS/SSLOGO.O") == 0)
        bk_runtime_strncpy(g_screensaver_path, "/SYSTEM/SCREENS/SSLOGO.SCV",
                 sizeof(g_screensaver_path) - 1);
    else if (bk_runtime_strcmp(g_screensaver_path, "/SYSTEM/SCREENS/SSPIPES.O") == 0)
        bk_runtime_strncpy(g_screensaver_path, "/SYSTEM/SCREENS/SSPIPES.SCV",
                 sizeof(g_screensaver_path) - 1);
    g_screensaver_path[sizeof(g_screensaver_path) - 1] = '\0';
}

static void screensaver_load_config(void) {
    void *data = NULL;
    uint32_t length = 0;
    char *text;
    const char *value;

    if (!bk_user_config_read_all(BK_SCREENSAVER_CONFIG_PATH,
                                 BK_SCREENSAVER_CONFIG_LEGACY_PATH,
                                 &data, &length) || !data)
        return;

    text = (char *)bk_sys_alloc((size_t)length + 1U);
    if (!text) {
        bk_sys_free(data);
        return;
    }
    bk_runtime_memcpy(text, data, length);
    text[length] = '\0';
    bk_sys_free(data);

    value = find_ini_value(text, "enabled");
    if (value) g_screensaver_enabled = parse_uint_default(value, 1) != 0;

    value = find_ini_value(text, "timeout");
    if (value) {
        uint32_t seconds = parse_uint_default(value, g_screensaver_timeout_seconds);
        if (seconds < 10) seconds = 10;
        g_screensaver_timeout_seconds = seconds;
    }

    value = find_ini_value(text, "path");
    if (value)
        copy_ini_string(g_screensaver_path, sizeof(g_screensaver_path), value);
    if (!g_screensaver_path[0])
        bk_runtime_strncpy(g_screensaver_path, SCREENSAVER_DEFAULT_PATH,
                 sizeof(g_screensaver_path) - 1);
    screensaver_normalize_path();

    bk_sys_free(text);
}

static bool screensaver_any_active(void) {
    return false;
}

static bool screensaver_launch(gui_desktop_t *desktop) {
    if (!desktop) desktop = g_screensaver_desktop;
    if (!desktop) return false;

    /* Los protectores son componentes externos en /SYSTEM/SCREENS. */
    return program_execute_path(desktop, g_screensaver_path);
}

bool screensaver_preview(gui_desktop_t *desktop) {
    return screensaver_launch(desktop);
}

bool screensaver_is_enabled(void) {
    return g_screensaver_enabled;
}

uint32_t screensaver_get_timeout_seconds(void) {
    return g_screensaver_timeout_seconds;
}

void screensaver_set_enabled(bool enabled) {
    g_screensaver_enabled = enabled;
    screensaver_save_config();
}

void screensaver_set_timeout_seconds(uint32_t seconds) {
    if (seconds < 10) seconds = 10;
    g_screensaver_timeout_seconds = seconds;
    screensaver_save_config();
}

const char *screensaver_get_path(void) {
    return g_screensaver_path;
}

void screensaver_set_path(const char *path) {
    if (!path || !path[0]) return;
    bk_runtime_strncpy(g_screensaver_path, path, sizeof(g_screensaver_path) - 1);
    g_screensaver_path[sizeof(g_screensaver_path) - 1] = '\0';
    screensaver_normalize_path();
    screensaver_save_config();
}

static void screensaverd_main(void *arg) {
    screensaverd_state_t *st = (screensaverd_state_t *)arg;
    uint32_t last_launch_activity;

    if (!st) bk_proc_exit();
    g_screensaver_desktop = st->desktop;
    screensaver_load_config();
    screensaver_save_config();
    last_launch_activity = 0xFFFFFFFFU;

    while (!bk_proc_exit_requested()) {
        uint32_t now = bk_sys_ticks();
        uint32_t freq = bk_sys_tick_frequency();
        uint32_t last_input = bk_gui_get_last_input_tick();
        uint32_t idle_ticks = now - last_input;
        uint32_t timeout_ticks = g_screensaver_timeout_seconds * freq;

        if (g_screensaver_enabled && !screensaver_any_active() &&
            last_input != last_launch_activity && idle_ticks >= timeout_ticks) {
            last_launch_activity = last_input;
            (void)screensaver_launch(g_screensaver_desktop);
        }

        bk_sys_sleep_ticks(freq / 2U ? freq / 2U : 1U);
    }

    bk_sys_free(st);
    g_screensaverd_started = false;
    bk_proc_exit();
}

void screensaverd_install(gui_desktop_t *desktop) {
    screensaverd_state_t *st;

    if (!desktop || g_screensaverd_started) return;
    st = (screensaverd_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    g_screensaverd_started = true;
    if (bk_proc_spawn_thread("screensaverd", screensaverd_main, st) < 0) {
        g_screensaverd_started = false;
        bk_sys_free(st);
    }
}
