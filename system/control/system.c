#include "control_common.h"
#include "kernel/include/api.h"

#define SYSTEM_WINDOW_W        560
#define SYSTEM_WINDOW_H        430
#define SYSTEM_TAB_LEFT         12
#define SYSTEM_TAB_TOP           8
#define SYSTEM_TAB_COUNT         2
#define SYSTEM_DEVICE_ROW_H     13
#define SYSTEM_REPORT_PATH      "/SYSTEM/USER/CONFIG/DEVICES.TXT"

typedef enum {
    SYSTEM_TAB_GENERAL = 0,
    SYSTEM_TAB_DEVICES = 1,
} system_tab_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint8_t current_tab;

    system_memory_info_t memory;
    gfx_info_t gfx;
    mouse_state_t mouse;
    rtc_datetime_t now;
    bool memory_ok;
    bool gfx_ok;
    bool rtc_ok;
    bool sb16_present;
    bool pcm_available;
    const char *pcm_name;
    uint32_t uptime_ms;
    uint32_t processes;
    uint32_t api_version;
    uint32_t last_refresh_ms;

    uint32_t *icon;
    gui_gif_animation_t logo;
    uint16_t logo_frame;
    uint32_t logo_next_tick;

    gui_widget_t *refresh_button;
    gui_widget_t *beep_button;
    gui_widget_t *report_button;
    bool tone_ran;
    bool tone_ok;
    bool report_ran;
    bool report_ok;
    char status[96];
} system_cpl_state_t;

static const cpl_tab_spec_t g_system_tabs[SYSTEM_TAB_COUNT] = {
    {"@H5561FD0B", 82},
    {"@H619D37EF", 108},
};

static void system_set_status(system_cpl_state_t *st, const char *text) {
    if (!st) return;
    snprintf(st->status, sizeof(st->status), "%s", text ? text : "");
}

static gui_rect_t system_client_rect(const system_cpl_state_t *st) {
    return st && st->window
        ? bk_gui_window_content_rect_raw(st->window)
        : (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t system_page_rect(const system_cpl_state_t *st) {
    gui_rect_t client = system_client_rect(st);
    return (gui_rect_t){client.x + 8, client.y + 30,
                        client.w - 16, client.h - 50};
}

static gui_rect_t system_status_rect(const system_cpl_state_t *st) {
    gui_rect_t client = system_client_rect(st);
    return (gui_rect_t){client.x + 8, client.y + client.h - 18,
                        client.w - 16, 15};
}

static uint32_t system_logo_delay_ticks(const system_cpl_state_t *st,
                                        uint16_t frame) {
    uint32_t hz;
    uint32_t delay_cs;
    uint32_t ticks;

    if (!st || !st->logo.delays_cs || frame >= st->logo.frame_count) return 1;
    hz = bk_sys_tick_frequency();
    if (!hz) hz = 100;
    delay_cs = st->logo.delays_cs[frame];
    if (!delay_cs) delay_cs = 10;
    ticks = (delay_cs * hz + 99U) / 100U;
    return ticks ? ticks : 1U;
}

static bool system_advance_logo(system_cpl_state_t *st, uint32_t now) {
    bool advanced = false;

    if (!st || st->logo.frame_count <= 1) return false;
    if (!st->logo_next_tick)
        st->logo_next_tick = now + system_logo_delay_ticks(st, st->logo_frame);

    while ((int32_t)(now - st->logo_next_tick) >= 0) {
        st->logo_frame =
            (uint16_t)((st->logo_frame + 1U) % st->logo.frame_count);
        st->logo_next_tick += system_logo_delay_ticks(st, st->logo_frame);
        advanced = true;
    }
    return advanced;
}

static const gui_image_t *system_current_logo(const system_cpl_state_t *st) {
    if (!st || !st->logo.frames || !st->logo.frame_count) return NULL;
    if (st->logo_frame >= st->logo.frame_count) return &st->logo.frames[0];
    return &st->logo.frames[st->logo_frame];
}

static void system_refresh(system_cpl_state_t *st) {
    if (!st) return;
    st->memory_ok = bk_sys_memory_info(&st->memory);
    st->gfx_ok = bk_gfx_info(&st->gfx);
    (void)bk_input_mouse(&st->mouse);
    st->rtc_ok = bk_time_datetime(&st->now);
    st->sb16_present = bk_sound_has_sb16();
    st->pcm_available = bk_sound_pcm_available();
    st->pcm_name = bk_sound_pcm_name();
    st->uptime_ms = bk_sys_uptime_ms();
    st->processes = bk_proc_count();
    st->api_version = bk_sys_api_version();
    st->last_refresh_ms = st->uptime_ms;
    if (st->window) st->window->dirty = true;
}

static void system_sync_widgets(system_cpl_state_t *st) {
    bool devices;

    if (!st) return;
    devices = st->current_tab == SYSTEM_TAB_DEVICES;
    if (st->refresh_button) st->refresh_button->visible = devices;
    if (st->beep_button) st->beep_button->visible = devices;
    if (st->report_button) st->report_button->visible = devices;
}

static void system_switch_tab(system_cpl_state_t *st, uint8_t tab) {
    if (!st || tab >= SYSTEM_TAB_COUNT || st->current_tab == tab) return;
    st->current_tab = tab;
    system_sync_widgets(st);
    system_set_status(st, tab == SYSTEM_TAB_GENERAL
        ? "@H48A09BB3"
        : "@HC9E12E30");
    if (st->window) st->window->dirty = true;
}

static void system_draw_logo(gui_surface_t *s, system_cpl_state_t *st,
                             int x, int y, int w) {
    const gui_image_t *logo = system_current_logo(st);
    int logo_w = logo ? (int)logo->width : 48;
    int logo_x = x + (w - logo_w) / 2;

    if (logo_x < x) logo_x = x;
    if (logo && logo->pixels) {
        bk_app_draw_icon(s, logo_x, y, logo->pixels,
                                 logo->width, logo->height);
    } else if (st && st->icon) {
        bk_app_draw_icon(s, logo_x, y, st->icon, 48, 48);
    } else {
        cpl_draw_bevel(s, (gui_rect_t){logo_x, y, 48, 48}, 0x00204070, false);
        bk_gui_font_draw_string_scaled(s, logo_x + 14, y + 13, "B",
                                    CPL_WHITE, 3);
    }
}

static void system_draw_label(gui_surface_t *s, int x, int y,
                              const char *label, const char *value,
                              gui_rect_t clip) {
    bk_gui_font_draw_string(s, x, y, label, 0x00203040, 0, false);
    bk_gui_font_draw_string_clipped(s, x + 92, y, value ? value : "",
                                 0x00203040, clip);
}

static void system_paint_general(system_cpl_state_t *st, gui_surface_t *s) {
    gui_rect_t page = system_page_rect(st);
    gui_rect_t box;
    gui_rect_t clip;
    char line[96];
    int x = page.x + 18;
    int y = page.y + 14;
    int w = page.w - 36;
    int logo_h;
    const gui_image_t *logo = system_current_logo(st);

    logo_h = logo ? (int)logo->height : 48;
    if (logo_h > 128) logo_h = 128;
    system_draw_logo(s, st, x, y, w);
    y += logo_h + 12;

    box = (gui_rect_t){x + 8, y, w - 16, 106};
    cpl_draw_group(s, box, "@HAF21B997");
    clip = (gui_rect_t){box.x + 16, box.y + 16, box.w - 32, box.h - 24};

    snprintf(line, sizeof(line), "v%u", st->api_version);
    system_draw_label(s, box.x + 16, box.y + 20, "@H95A9FAAB", line, clip);
    snprintf(line, sizeof(line), "@H8507053D", st->uptime_ms / 1000U);
    system_draw_label(s, box.x + 16, box.y + 39, "Uptime:", line, clip);
    snprintf(line, sizeof(line), "@H2631FC47", st->processes);
    system_draw_label(s, box.x + 16, box.y + 58, "Procesos:", line, clip);
    snprintf(line, sizeof(line), "@H295943D3",
             (uint32_t)(st->memory.total_bytes / 1024U),
             (uint32_t)(st->memory.free_bytes / 1024U));
    system_draw_label(s, box.x + 16, box.y + 77, "Memoria:", line, clip);

    y += 122;
    box = (gui_rect_t){x + 8, y, w - 16, 56};
    cpl_draw_group(s, box, "Multimedia");
    snprintf(line, sizeof(line), "%ux%ux%u",
             st->gfx.width, st->gfx.height, st->gfx.bpp);
    system_draw_label(s, box.x + 16, box.y + 20, "Video:", line,
                      (gui_rect_t){box.x + 16, box.y + 16, box.w - 32, 44});
    snprintf(line, sizeof(line), "%s", st->pcm_name ? st->pcm_name : "@HE9405393");
    system_draw_label(s, box.x + 16, box.y + 38, "Audio:", line,
                      (gui_rect_t){box.x + 16, box.y + 16, box.w - 32, 44});

    bk_gui_font_draw_string_clipped(s, x + 8, page.y + page.h - 16,
        "@HFF8073CA",
        CPL_SHADOW, (gui_rect_t){x + 8, page.y + page.h - 18, w - 16, 16});
}

static uint32_t system_status_color(int status) {
    if (status == 1) return 0x0000A040;
    if (status == 2) return 0x00C08000;
    if (status == 3) return 0x00C02020;
    return 0x00909090;
}

static const char *system_status_text(int status) {
    if (status == 1) return "OK";
    if (status == 2) return "WARN";
    if (status == 3) return "FALLO";
    return "?";
}

static int system_draw_device_row(gui_surface_t *s, int x, int y, int w,
                                  const char *name, const char *detail,
                                  int status) {
    char line[128];
    uint32_t color = system_status_color(status);

    bk_gui_gfx_fill_rect(s, (gui_rect_t){x, y + 4, 7, 7}, color);
    bk_gui_gfx_draw_rect(s, (gui_rect_t){x, y + 4, 7, 7}, CPL_DARK);
    snprintf(line, sizeof(line), "@H32906DE7",
             system_status_text(status), name, detail ? detail : "");
    bk_gui_font_draw_string_clipped(s, x + 14, y + 1, line, CPL_TEXT,
                                 (gui_rect_t){x + 14, y, w - 14,
                                              SYSTEM_DEVICE_ROW_H});
    return y + SYSTEM_DEVICE_ROW_H;
}

static int system_draw_device_header(gui_surface_t *s, int x, int y, int w,
                                     const char *title) {
    bk_gui_gfx_fill_rect(s, (gui_rect_t){x, y, w, 16}, 0x00E4EEF7);
    bk_gui_gfx_draw_rect(s, (gui_rect_t){x, y, w, 16}, 0x0090A0A8);
    bk_gui_font_draw_string_clipped(s, x + 6, y + 4, title, 0x00002060,
                                 (gui_rect_t){x + 4, y, w - 8, 16});
    return y + 19;
}

static void system_paint_devices(system_cpl_state_t *st, gui_surface_t *s) {
    gui_rect_t page = system_page_rect(st);
    char detail[96];
    int x = page.x + 18;
    int y = page.y + 12;
    int w = page.w - 36;
    int list_h = page.h - 62;
    uint32_t count;

    bk_gui_gfx_fill_rect(s, (gui_rect_t){x - 4, y - 4, w + 8, list_h},
                      0x00FFFFFF);
    bk_gui_gfx_draw_rect(s, (gui_rect_t){x - 4, y - 4, w + 8, list_h},
                      0x008090A0);

    y = system_draw_device_header(s, x, y, w, "Almacenamiento");
    count = bk_device_block_count();
    snprintf(detail, sizeof(detail), "@HD948B502", count);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H43E10BCF", detail,
                               count ? 1 : 2);
    for (uint32_t i = 0; i < count && i < 3; i++) {
        bk_block_info_t dev;
        if (!bk_device_block_info(i, &dev)) continue;
        snprintf(detail, sizeof(detail), "@H2D6C35CF",
                 dev.type_name, dev.sector_count,
                 dev.read_only ? "@H95E5221C" : "");
        y = system_draw_device_row(s, x + 10, y, w - 20,
                                   dev.name, detail, 1);
    }

    y += 3;
    y = system_draw_device_header(s, x, y, w, "@H9EE6DF3B");
    snprintf(detail, sizeof(detail), "@H7EDA3267",
             st->gfx.width, st->gfx.height, st->gfx.bpp);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H48894648", detail,
                               st->gfx_ok ? 1 : 3);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H17912F88",
                               st->sb16_present ? "detectado" : "@HDC0AAE01",
                               st->sb16_present ? 1 : 2);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H4691F881",
                               st->pcm_available ? st->pcm_name : "@HE9405393",
                               st->pcm_available ? 1 : 3);

    y += 3;
    y = system_draw_device_header(s, x, y, w, "@HBB52E289");
    snprintf(detail, sizeof(detail), "@H92E4222F",
             (uint32_t)st->mouse.device_id,
             (uint32_t)st->mouse.packet_size,
             (int)st->mouse.x, (int)st->mouse.y,
             st->mouse.irq_count);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H9DC4AFC4", detail,
                               st->mouse.present ? 1 : 3);
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H544D0602", "@H5497557B",
                               1);
    if (st->rtc_ok) {
        snprintf(detail, sizeof(detail), "@H84B357B3",
                 st->now.date.year, st->now.date.month, st->now.date.day,
                 st->now.time.hour, st->now.time.minute, st->now.time.second);
    } else {
        snprintf(detail, sizeof(detail), "@HE9405393");
    }
    y = system_draw_device_row(s, x + 10, y, w - 20, "RTC", detail,
                               st->rtc_ok ? 1 : 3);

    y += 3;
    y = system_draw_device_header(s, x, y, w, "@H4D54EE26");
    snprintf(detail, sizeof(detail), "@H4579E6DC",
             bk_device_pci_count());
    y = system_draw_device_row(s, x + 10, y, w - 20,
                               "@H6532D26D", detail,
                               bk_device_pci_count() ? 1 : 0);
    for (uint32_t i = 0; i < bk_device_pci_count() && i < 1; i++) {
        bk_pci_info_t dev;
        if (!bk_device_pci_info(i, &dev)) continue;
        snprintf(detail, sizeof(detail), "@H7FCB885D",
                 dev.vendor_id, dev.device_id, dev.bus, dev.slot,
                 dev.interrupt_line);
        y = system_draw_device_row(s, x + 10, y, w - 20,
                                   dev.class_name, detail, 1);
    }
    snprintf(detail, sizeof(detail), "@H27DE078E",
             st->last_refresh_ms);
    y = system_draw_device_row(s, x + 10, y, w - 20, "Panel", detail, 1);

    bk_gui_gfx_fill_rect(s,
                      (gui_rect_t){page.x + 2, page.y + page.h - 44,
                                   page.w - 4, 28},
                      CPL_FACE);
    bk_gui_gfx_draw_line(s, page.x + 4, page.y + page.h - 46,
                      page.x + page.w - 5, page.y + page.h - 46,
                      CPL_SHADOW);
    (void)y;
}

static void system_paint(gui_window_t *window UNUSED, gui_surface_t *s,
                         void *context) {
    system_cpl_state_t *st = (system_cpl_state_t *)context;
    gui_rect_t client;
    gui_rect_t page;
    gui_rect_t status;

    if (!st || !st->window || !s) return;
    client = system_client_rect(st);
    page = system_page_rect(st);
    status = system_status_rect(st);

    cpl_draw_tabs(s, client, g_system_tabs, SYSTEM_TAB_COUNT,
                  st->current_tab, page, SYSTEM_TAB_LEFT, SYSTEM_TAB_TOP);
    if (st->current_tab == SYSTEM_TAB_GENERAL)
        system_paint_general(st, s);
    else
        system_paint_devices(st, s);

    bk_gui_gfx_fill_rect(s, status, 0x00E8E8E0);
    bk_gui_font_draw_string_clipped(s, status.x + 4, status.y + 3,
                                 st->status[0] ? st->status : "Listo.",
                                 CPL_SHADOW, status);
}

static void system_write_report(system_cpl_state_t *st) {
    char report[768];
    uint32_t len = 0;

    if (!st) return;
    system_refresh(st);
    snprintf(report, sizeof(report),
             "BlesKernOS System Report\r\n"
             "api=%u\r\n"
             "tasks=%u\r\n"
             "uptime_ms=%u\r\n"
             "memory_total_kb=%u\r\n"
             "memory_used_kb=%u\r\n"
             "memory_free_kb=%u\r\n"
             "screen=%ux%ux%u\r\n"
             "sb16=%u\r\n"
             "pcm=%u\r\n"
             "mouse_present=%u\r\n"
             "mouse_packets=%u\r\n"
             "rtc_ok=%u\r\n"
             "block_devices=%u\r\n"
             "pci_devices=%u\r\n",
             st->api_version,
             st->processes,
             st->uptime_ms,
             (uint32_t)(st->memory.total_bytes / 1024U),
             (uint32_t)(st->memory.used_bytes / 1024U),
             (uint32_t)(st->memory.free_bytes / 1024U),
             st->gfx.width, st->gfx.height, st->gfx.bpp,
             st->sb16_present ? 1U : 0U,
             st->pcm_available ? 1U : 0U,
             st->mouse.present ? 1U : 0U,
             st->mouse.packets,
             st->rtc_ok ? 1U : 0U,
             bk_device_block_count(),
             bk_device_pci_count());
    while (len < sizeof(report) && report[len]) len++;
    st->report_ran = true;
    st->report_ok = bk_file_write_all(SYSTEM_REPORT_PATH, report, len);
    system_set_status(st, st->report_ok
        ? "Reporte escrito en " SYSTEM_REPORT_PATH
        : "@H0CB0F2AF");
}

static void system_button(gui_window_t *window, uint32_t id) {
    system_cpl_state_t *st = window
        ? (system_cpl_state_t *)window->content_context : NULL;

    if (!st) return;
    if (st->refresh_button && id == st->refresh_button->id) {
        system_refresh(st);
        system_set_status(st, "@HCCDFC20C");
    } else if (st->beep_button && id == st->beep_button->id) {
        st->tone_ran = true;
        st->tone_ok = bk_sound_tone(880, 120);
        system_set_status(st, st->tone_ok
            ? "@H2CD60457"
            : "@HA6F92402");
    } else if (st->report_button && id == st->report_button->id) {
        system_write_report(st);
    }

    if (st->window) st->window->dirty = true;
    bk_gui_request_paint();
}

static bool system_event(gui_window_t *window UNUSED,
                         const gui_event_t *event,
                         void *context) {
    system_cpl_state_t *st = (system_cpl_state_t *)context;
    int tab_hit;

    if (!st || !event) return false;
    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        tab_hit = cpl_hit_tab(system_client_rect(st), g_system_tabs,
                              SYSTEM_TAB_COUNT, SYSTEM_TAB_LEFT,
                              SYSTEM_TAB_TOP, event->x, event->y);
        if (tab_hit >= 0) {
            system_switch_tab(st, (uint8_t)tab_hit);
            return true;
        }
    }
    if (event->type == GUI_EVENT_KEY && event->key == '\t') {
        system_switch_tab(st,
            (uint8_t)((st->current_tab + 1U) % SYSTEM_TAB_COUNT));
        return true;
    }
    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    system_cpl_state_t *st;
    uint32_t next_refresh;

    if (!desktop) return;
    st = (system_cpl_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->current_tab = SYSTEM_TAB_GENERAL;
    system_set_status(st, "@H48A09BB3");

    st->window = bk_gui_create_window(desktop, 88, 48,
                                           SYSTEM_WINDOW_W, SYSTEM_WINDOW_H,
                                           "@HAF21B997");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }

    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "@HAF21B997", "@H1C1E4EC2", "@HCCCF3973",
        "@H7A28E1E5", "/ICONS/SYSTEM.BMP"});

    bk_gui_set_window_min_size(st->window, SYSTEM_WINDOW_W, SYSTEM_WINDOW_H);
    bk_gui_set_window_content(st->window, system_paint, st);
    bk_gui_set_window_event_handler(st->window, system_event, st);
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    st->icon = bk_app_load_icon("/ICONS/SYSTEM.BMP", 48, 48);
    (void)bk_gui_gif_load_animation(&st->logo, "/ABOUNT.GIF");

    st->refresh_button = bk_gui_widget_create_button(
        desktop, st->window, (gui_rect_t){316, 366, 82, 22},
        "@H7CC1E2B3", system_button);
    st->beep_button = bk_gui_widget_create_button(
        desktop, st->window, (gui_rect_t){406, 366, 52, 22},
        "Beep", system_button);
    st->report_button = bk_gui_widget_create_button(
        desktop, st->window, (gui_rect_t){466, 366, 70, 22},
        "Reporte", system_button);
    (void)bk_gui_widget_set_icon(st->refresh_button, "Refresh");
    (void)bk_gui_widget_set_icon(st->beep_button, "Mmsys101");
    (void)bk_gui_widget_set_icon(st->report_button, "FileText");
    system_sync_widgets(st);

    system_refresh(st);
    next_refresh = bk_sys_uptime_ms() + 1000U;
    bk_gui_request_paint();

    while (!bk_proc_exit_requested() && st->window->listed) {
        uint32_t now = bk_sys_uptime_ms();
        bool dirty = false;

        if (system_advance_logo(st, bk_sys_ticks())) dirty = true;
        if ((int32_t)(now - next_refresh) >= 0) {
            system_refresh(st);
            next_refresh = now + 1000U;
            dirty = true;
        }
        if (dirty && st->window) {
            st->window->dirty = true;
            bk_gui_request_paint();
        }
        bk_sys_sleep_ticks(4);
    }

    cpl_destroy_window(st->desktop, st->window);
    if (st->icon) bk_sys_free(st->icon);
    bk_gui_gif_animation_free(&st->logo);
    bk_sys_free(st);
    bk_proc_exit();
}
