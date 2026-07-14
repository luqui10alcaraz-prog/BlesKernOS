#include "../kernel/include/api.h"
#include <stdio.h>

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t started_ms;
    uint32_t pid;
    uint32_t task_count;
    uint32_t total_kb;
    uint32_t free_kb;
    uint16_t screen_w;
    uint16_t screen_h;
    bool file_ok;
    bool rtc_ok;
    rtc_datetime_t now;
} apitest_state_t;

static uint32_t apitest_strlen(const char *text) {
    uint32_t len = 0;
    while (text && text[len]) len++;
    return len;
}

static void apitest_refresh(apitest_state_t *st) {
    system_memory_info_t mem;
    gfx_info_t gfx;

    if (!st) return;
    st->pid = bk_sys_getpid();
    st->task_count = bk_proc_count();
    st->rtc_ok = bk_time_datetime(&st->now);

    if (bk_sys_memory_info(&mem)) {
        st->total_kb = (uint32_t)(mem.total_bytes / 1024U);
        st->free_kb = (uint32_t)(mem.free_bytes / 1024U);
    }

    if (bk_gfx_info(&gfx)) {
        st->screen_w = gfx.width;
        st->screen_h = gfx.height;
    }
}

static void apitest_write_report(apitest_state_t *st) {
    char text[256];

    if (!st) return;
    snprintf(text, sizeof(text),
             "BlesKernOS API test\r\n"
             "version=%u\r\n"
             "pid=%u\r\n"
             "tasks=%u\r\n"
             "uptime_ms=%u\r\n"
             "screen=%ux%u\r\n",
             bk_sys_api_version(),
             st->pid,
             st->task_count,
             bk_sys_uptime_ms(),
             st->screen_w,
             st->screen_h);
    st->file_ok = bk_file_write_all("/API_TEST.TXT", text,
                                    apitest_strlen(text));
}

static void apitest_paint(gui_window_t *window UNUSED,
                          gui_surface_t *surface,
                          void *context) {
    apitest_state_t *st = (apitest_state_t *)context;
    gui_rect_t bounds;
    gui_rect_t content;
    char line[96];
    int x;
    int y;
    int row;

    if (!st || !st->window || !surface ||
        !bk_gui_window_bounds(st->window, &bounds) ||
        !bk_gui_window_content_rect(st->window, &content)) return;
    x = content.x + 16;
    y = content.y + 14;

    bk_gui_surface_fill_rect(surface,
                             (gui_rect_t){x - 8, y - 8,
                                          bounds.w - 32, content.h - 20},
                             0x00FFFFFF);
    bk_gui_surface_draw_rect(surface,
                             (gui_rect_t){x - 8, y - 8,
                                          bounds.w - 32, content.h - 20},
                             0x008090A0);

    bk_gui_surface_draw_text(surface, x, y, "BlesKernOS API Test",
                             0x00002060, 0, false);
    row = y + 18;

    snprintf(line, sizeof(line), "Sistema: API v%u, PID %u, uptime %u ms",
             bk_sys_api_version(), st->pid, bk_sys_uptime_ms());
    bk_gui_surface_draw_text(surface, x, row, line, 0x00202020, 0, false);
    row += 14;

    snprintf(line, sizeof(line), "Memoria: %u KB total / %u KB libres",
             st->total_kb, st->free_kb);
    bk_gui_surface_draw_text(surface, x, row, line, 0x00202020, 0, false);
    row += 14;

    snprintf(line, sizeof(line), "Archivos: /API_TEST.TXT %s",
             st->file_ok ? "creado" : "fallo");
    bk_gui_surface_draw_text(surface, x, row, line,
                             st->file_ok ? 0x00008020 : 0x00800000,
                             0, false);
    row += 14;

    snprintf(line, sizeof(line), "GUI/GFX: ventana OK, pantalla %ux%u",
             st->screen_w, st->screen_h);
    bk_gui_surface_draw_text(surface, x, row, line, 0x00202020, 0, false);
    row += 14;

    snprintf(line, sizeof(line), "Procesos: %u tasks activos",
             st->task_count);
    bk_gui_surface_draw_text(surface, x, row, line, 0x00202020, 0, false);
    row += 14;

    if (st->rtc_ok) {
        snprintf(line, sizeof(line), "RTC: %u-%02u-%02u %02u:%02u:%02u",
                 st->now.date.year, st->now.date.month, st->now.date.day,
                 st->now.time.hour, st->now.time.minute, st->now.time.second);
    } else {
        snprintf(line, sizeof(line), "RTC: no disponible");
    }
    bk_gui_surface_draw_text(surface, x, row, line, 0x00202020, 0, false);
    row += 18;

    bk_gui_surface_draw_text(
        surface, x, row,
        "Se probo: bk_sys, bk_file, bk_gui, bk_gfx, bk_sound/time/proc",
        0x00505050, 0, false);
}

static bool apitest_event(gui_window_t *window UNUSED,
                          const gui_event_t *event,
                          void *context) {
    apitest_state_t *st = (apitest_state_t *)context;

    if (!st || !event) return false;
    if (event->type == GUI_EVENT_KEY && (event->key == 'b' || event->key == 'B')) {
        (void)bk_sound_tone(880, 120);
        return true;
    }
    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    apitest_state_t *st;
    uint32_t next_refresh;

    if (bk_sys_api_version() < 3U ||
        (bk_sys_capabilities() &
         (BK_API_CAP_SYSTEM | BK_API_CAP_FILES | BK_API_CAP_GUI)) !=
        (BK_API_CAP_SYSTEM | BK_API_CAP_FILES | BK_API_CAP_GUI)) return;

    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;

    st = (apitest_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->started_ms = bk_sys_uptime_ms();

    st->window = bk_gui_create_window(desktop, 92, 76, 460, 210, "API Test");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }

    bk_gui_set_window_content(st->window, apitest_paint, st);
    bk_gui_set_window_event_handler(st->window, apitest_event, st);
    bk_gui_set_window_min_size(st->window, 360, 170);
    bk_gui_window_set_owner(st->window, bk_sys_getpid());
    bk_proc_bind_window(st->window);

    apitest_refresh(st);
    apitest_write_report(st);
    (void)bk_sound_tone(660, 80);
    next_refresh = bk_sys_uptime_ms() + 500U;
    bk_gui_request_paint();

    while (bk_gui_window_is_open(st->window) && !bk_proc_exit_requested()) {
        uint32_t now = bk_sys_uptime_ms();
        if ((int32_t)(now - next_refresh) >= 0) {
            apitest_refresh(st);
            bk_gui_window_invalidate(st->window);
            next_refresh = now + 500U;
        }
        bk_sys_sleep_ms(10);
    }

    bk_proc_bind_window(NULL);
    if (st->window) bk_gui_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
}
