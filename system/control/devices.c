#include "kernel/include/api.h"
#include <stdio.h>

#define DEVMGR_CATEGORY_COUNT 6
#define DEVMGR_BUTTON_COUNT   6
#define DEVMGR_HEADER_H       16
#define DEVMGR_ROW_H          14
#define DEVMGR_STATUS_OK       1
#define DEVMGR_STATUS_WARN     2
#define DEVMGR_STATUS_FAIL     3
#define DEVMGR_STATUS_UNKNOWN  4

#define DEVMGR_BTN_REFRESH     1
#define DEVMGR_BTN_EXPAND      2
#define DEVMGR_BTN_COLLAPSE    3
#define DEVMGR_BTN_PROBE_DISK  4
#define DEVMGR_BTN_BEEP        5
#define DEVMGR_BTN_REPORT      6

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    uint32_t pid;

    bool cat_expanded[DEVMGR_CATEGORY_COUNT];
    int cat_header_y[DEVMGR_CATEGORY_COUNT];
    int content_x;
    int content_w;

    uint32_t button_ids[DEVMGR_BUTTON_COUNT];

    bool gfx_ok;
    gfx_info_t gfx;

    bool sb16_present;
    bool pcm_available;
    const char *pcm_name;
    bool tone_ran;
    bool tone_ok;

    mouse_state_t mouse;
    bool mouse_prev_down;
    uint32_t key_events;

    bool rtc_ok;
    rtc_datetime_t now;

    bool mem_ok;
    uint32_t total_kb;
    uint32_t used_kb;
    uint32_t free_kb;
    uint32_t reserved_kb;

    uint32_t task_count;
    uint32_t api_version;
    uint32_t uptime_ms;
    uint32_t last_refresh_ms;

    bool disk_probe_ran;
    bool disk_probe_ok;
    bool report_ran;
    bool report_ok;
    char status[80];
} devmgr_state_t;

static devmgr_state_t *g_devmgr;

static const char *g_cat_names[DEVMGR_CATEGORY_COUNT] = {
    "Almacenamiento",
    "Pantalla",
    "Sonido",
    "Entrada",
    "Sistema",
    "Diagnostico"
};

static uint32_t devmgr_status_color(int status) {
    if (status == DEVMGR_STATUS_OK) return 0x0000A040;
    if (status == DEVMGR_STATUS_WARN) return 0x00C08000;
    if (status == DEVMGR_STATUS_FAIL) return 0x00C02020;
    return 0x00909090;
}

static const char *devmgr_status_text(int status) {
    if (status == DEVMGR_STATUS_OK) return "OK";
    if (status == DEVMGR_STATUS_WARN) return "WARN";
    if (status == DEVMGR_STATUS_FAIL) return "FALLO";
    return "?";
}

static void devmgr_set_status(devmgr_state_t *st, const char *text) {
    if (!st) return;
    if (!text) text = "";
    snprintf(st->status, sizeof(st->status), "%s", text);
}

static void devmgr_refresh(devmgr_state_t *st) {
    system_memory_info_t mem;

    if (!st) return;

    st->pid = bk_sys_getpid();
    st->api_version = bk_sys_api_version();
    st->uptime_ms = bk_sys_uptime_ms();
    st->task_count = bk_proc_count();
    st->rtc_ok = bk_time_datetime(&st->now);
    st->gfx_ok = bk_gfx_info(&st->gfx);

    st->sb16_present = bk_sound_has_sb16();
    st->pcm_available = bk_sound_pcm_available();
    st->pcm_name = bk_sound_pcm_name();

    (void)bk_input_mouse(&st->mouse);

    st->mem_ok = bk_sys_memory_info(&mem);
    if (st->mem_ok) {
        st->total_kb = (uint32_t)(mem.total_bytes / 1024U);
        st->used_kb = (uint32_t)(mem.used_bytes / 1024U);
        st->free_kb = (uint32_t)(mem.free_bytes / 1024U);
        st->reserved_kb = (uint32_t)(mem.reserved_bytes / 1024U);
    }

    st->last_refresh_ms = st->uptime_ms;
}

static void devmgr_probe_disk(devmgr_state_t *st) {
    if (!st) return;
    st->disk_probe_ran = true;
    st->disk_probe_ok = bk_device_block_count() > 0;
    devmgr_set_status(st, st->disk_probe_ok ?
                      "Deteccion completada: hay dispositivos de bloque" :
                      "Deteccion completada: no hay dispositivos de bloque");
    bk_sys_log(st->disk_probe_ok ?
               "[DEVMGR] disk probe OK" :
               "[DEVMGR] disk probe FAIL");
}

static void devmgr_write_report(devmgr_state_t *st) {
    char report[768];
    uint32_t len;

    if (!st) return;
    devmgr_refresh(st);

    snprintf(report, sizeof(report),
             "BlesKernOS Device Manager Report\r\n"
             "api=%u\r\n"
             "pid=%u\r\n"
             "tasks=%u\r\n"
             "uptime_ms=%u\r\n"
             "memory_total_kb=%u\r\n"
             "memory_used_kb=%u\r\n"
             "memory_free_kb=%u\r\n"
             "memory_reserved_kb=%u\r\n"
             "gfx_ok=%u\r\n"
             "screen=%ux%ux%u\r\n"
             "sb16=%u\r\n"
             "pcm=%u\r\n"
             "mouse_present=%u\r\n"
             "mouse_id=%u\r\n"
             "mouse_packets=%u\r\n"
             "mouse_irq=%u\r\n"
             "rtc_ok=%u\r\n"
             "disk_probe_ran=%u\r\n"
             "disk_probe_ok=%u\r\n",
             st->api_version,
             st->pid,
             st->task_count,
             st->uptime_ms,
             st->total_kb,
             st->used_kb,
             st->free_kb,
             st->reserved_kb,
             st->gfx_ok ? 1U : 0U,
             st->gfx.width, st->gfx.height, st->gfx.bpp,
             st->sb16_present ? 1U : 0U,
             st->pcm_available ? 1U : 0U,
             st->mouse.present ? 1U : 0U,
             (uint32_t)st->mouse.device_id,
             st->mouse.packets,
             st->mouse.irq_count,
             st->rtc_ok ? 1U : 0U,
             st->disk_probe_ran ? 1U : 0U,
             st->disk_probe_ok ? 1U : 0U);

    for (len = 0; len < sizeof(report) && report[len]; len++) {}
    st->report_ran = true;
    st->report_ok = bk_file_write_all("/DEVICES.TXT", report, len);
    devmgr_set_status(st, st->report_ok ?
                      "Reporte escrito en /DEVICES.TXT" :
                      "No se pudo escribir /DEVICES.TXT");
}

static void devmgr_expand_all(devmgr_state_t *st, bool expanded) {
    int i;

    if (!st) return;
    for (i = 0; i < DEVMGR_CATEGORY_COUNT; i++) st->cat_expanded[i] = expanded;
    devmgr_set_status(st, expanded ? "Todas las categorias expandidas" :
                                   "Todas las categorias contraidas");
}

static void devmgr_button(gui_window_t *window, uint32_t id) {
    devmgr_state_t *st = window
        ? (devmgr_state_t *)window->content_context : g_devmgr;

    if (!st) return;

    if (id == st->button_ids[0] || id == DEVMGR_BTN_REFRESH) {
        devmgr_refresh(st);
        devmgr_set_status(st, "Datos actualizados");
    } else if (id == st->button_ids[1] || id == DEVMGR_BTN_EXPAND) {
        devmgr_expand_all(st, true);
    } else if (id == st->button_ids[2] || id == DEVMGR_BTN_COLLAPSE) {
        devmgr_expand_all(st, false);
    } else if (id == st->button_ids[3] || id == DEVMGR_BTN_PROBE_DISK) {
        devmgr_probe_disk(st);
    } else if (id == st->button_ids[4] || id == DEVMGR_BTN_BEEP) {
        st->tone_ran = true;
        st->tone_ok = bk_sound_tone(880, 120);
        devmgr_set_status(st, st->tone_ok ? "Tono de prueba enviado" :
                                        "Tono enviado, audio no confirmado");
    } else if (id == st->button_ids[5] || id == DEVMGR_BTN_REPORT) {
        devmgr_write_report(st);
    }

    if (st->window) st->window->dirty = true;
    bk_gui_request_paint();
}

static int devmgr_draw_header(gui_surface_t *surface, devmgr_state_t *st,
                              int index, int x, int y, int w) {
    char line[64];
    bool expanded;

    if (!st) return y;
    expanded = st->cat_expanded[index];
    st->cat_header_y[index] = y;

    bk_gui_gfx_fill_rect(surface,
                      (gui_rect_t){x, y - 2, w, DEVMGR_HEADER_H},
                      expanded ? 0x00E4EEF7 : 0x00F0F0F0);
    bk_gui_gfx_draw_rect(surface, (gui_rect_t){x, y - 2, w, DEVMGR_HEADER_H},
                      0x0090A0A8);

    snprintf(line, sizeof(line), "%c %d. %s",
             expanded ? '-' : '+', index + 1, g_cat_names[index]);
    bk_gui_font_draw_string_clipped(surface, x + 6, y + 2, line,
                                 0x00002060,
                                 (gui_rect_t){x + 4, y - 1, w - 8,
                                              DEVMGR_HEADER_H});
    return y + DEVMGR_HEADER_H;
}

static int devmgr_draw_device(gui_surface_t *surface, int x, int y,
                              const char *name, const char *detail,
                              int status) {
    char line[128];
    uint32_t color;

    color = devmgr_status_color(status);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 10, y + 3, 7, 7}, color);
    bk_gui_gfx_draw_rect(surface, (gui_rect_t){x + 10, y + 3, 7, 7}, 0x00404040);

    snprintf(line, sizeof(line), "[%s] %s  %s",
             devmgr_status_text(status), name, detail ? detail : "");
    bk_gui_font_draw_string_clipped(surface, x + 24, y, line, 0x00202020,
                                 (gui_rect_t){x + 22, y, 420, DEVMGR_ROW_H});
    return y + DEVMGR_ROW_H;
}

static int devmgr_draw_storage(gui_surface_t *surface, devmgr_state_t *st,
                               int x, int y) {
    char detail[96];
    int disk_status;

    if (!st->disk_probe_ran) {
        disk_status = DEVMGR_STATUS_UNKNOWN;
        snprintf(detail, sizeof(detail), "sin detectar; boton Detectar");
    } else if (st->disk_probe_ok) {
        disk_status = DEVMGR_STATUS_OK;
        snprintf(detail, sizeof(detail), "%u dispositivos de bloque", bk_device_block_count());
    } else {
        disk_status = DEVMGR_STATUS_FAIL;
        snprintf(detail, sizeof(detail), "no se detectaron dispositivos");
    }

    y = devmgr_draw_device(surface, x, y, "Dispositivos de bloque", detail, disk_status);
    for (uint32_t i = 0; i < bk_device_block_count() && i < 5; i++) {
        block_device_t *dev = bk_device_block_at(i);
        if (!dev) continue;
        snprintf(detail, sizeof(detail), "%s, %u sectores de %u bytes%s",
                 bk_device_block_type_name(dev->type), dev->sector_count, dev->sector_size,
                 dev->read_only ? ", solo lectura" : "");
        y = devmgr_draw_device(surface, x, y, dev->name, detail, DEVMGR_STATUS_OK);
    }
    return y;
}

static int devmgr_draw_display(gui_surface_t *surface, devmgr_state_t *st,
                               int x, int y) {
    char detail[96];

    if (st->gfx_ok) {
        snprintf(detail, sizeof(detail), "%ux%u, %u bpp",
                 st->gfx.width, st->gfx.height, st->gfx.bpp);
    } else {
        snprintf(detail, sizeof(detail), "no disponible");
    }
    y = devmgr_draw_device(surface, x, y, "Adaptador grafico", detail,
                           st->gfx_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);
    y = devmgr_draw_device(surface, x, y, "GUI compositor",
                           st->window ? "ventana y repaint activos" : "sin ventana",
                           st->window ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);
    if (st->gfx_ok && st->gfx.bpp == 8) {
        y = devmgr_draw_device(surface, x, y, "Profundidad de color",
                               "VESA 256 colores activo",
                               DEVMGR_STATUS_OK);
    } else if (st->gfx_ok && st->gfx.bpp < 8) {
        y = devmgr_draw_device(surface, x, y, "Profundidad de color",
                               "modo bajo; algunas apps pueden verse limitadas",
                               DEVMGR_STATUS_WARN);
    }
    return y;
}

static int devmgr_draw_sound(gui_surface_t *surface, devmgr_state_t *st,
                             int x, int y) {
    char detail[96];

    y = devmgr_draw_device(surface, x, y, "Sound Blaster 16",
                           st->sb16_present ? "detectado" : "no detectado",
                           st->sb16_present ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);
    snprintf(detail, sizeof(detail), "%s",
             st->pcm_available ? (st->pcm_name ? st->pcm_name : "disponible") :
                                 "no disponible");
    y = devmgr_draw_device(surface, x, y, "Audio PCM", detail,
                           st->pcm_available ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);
    y = devmgr_draw_device(surface, x, y, "Tono de prueba",
                           st->tone_ran ? (st->tone_ok ? "ultimo tono OK" : "fallo") :
                                          "sin probar; boton Beep",
                           !st->tone_ran ? DEVMGR_STATUS_UNKNOWN :
                           (st->tone_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL));
    return y;
}

static int devmgr_draw_input(gui_surface_t *surface, devmgr_state_t *st,
                             int x, int y) {
    char detail[96];

    snprintf(detail, sizeof(detail), "%u eventos recibidos", st->key_events);
    y = devmgr_draw_device(surface, x, y, "Teclado PS/2 / GUI", detail,
                           DEVMGR_STATUS_OK);

    if (st->mouse.present) {
        snprintf(detail, sizeof(detail), "id=%u pkt=%u pos=%i,%i irq=%u",
                 (uint32_t)st->mouse.device_id,
                 (uint32_t)st->mouse.packet_size,
                 (int)st->mouse.x, (int)st->mouse.y,
                 st->mouse.irq_count);
    } else {
        snprintf(detail, sizeof(detail), "no detectado");
    }
    y = devmgr_draw_device(surface, x, y, "Mouse PS/2", detail,
                           st->mouse.present ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);

    snprintf(detail, sizeof(detail), "packets=%u buttons=0x%02x wheel=%i",
             st->mouse.packets, (uint32_t)st->mouse.buttons,
             (int)st->mouse.wheel);
    y = devmgr_draw_device(surface, x, y, "Estado mouse", detail,
                           st->mouse.present ? DEVMGR_STATUS_OK : DEVMGR_STATUS_UNKNOWN);
    return y;
}

static int devmgr_draw_system(gui_surface_t *surface, devmgr_state_t *st,
                              int x, int y) {
    char detail[96];

    snprintf(detail, sizeof(detail), "API v%u, PID %u, uptime %u ms",
             st->api_version, st->pid, st->uptime_ms);
    y = devmgr_draw_device(surface, x, y, "Kernel/BleskAPI", detail,
                           DEVMGR_STATUS_OK);

    snprintf(detail, sizeof(detail), "%u tareas activas", st->task_count);
    y = devmgr_draw_device(surface, x, y, "Procesos", detail,
                           DEVMGR_STATUS_OK);

    if (st->mem_ok) {
        snprintf(detail, sizeof(detail), "%u total / %u usado / %u libre KB",
                 st->total_kb, st->used_kb, st->free_kb);
    } else {
        snprintf(detail, sizeof(detail), "no disponible");
    }
    y = devmgr_draw_device(surface, x, y, "Memoria", detail,
                           st->mem_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);

    if (st->rtc_ok) {
        snprintf(detail, sizeof(detail), "%u-%02u-%02u %02u:%02u:%02u",
                 st->now.date.year, st->now.date.month, st->now.date.day,
                 st->now.time.hour, st->now.time.minute, st->now.time.second);
    } else {
        snprintf(detail, sizeof(detail), "no disponible");
    }
    y = devmgr_draw_device(surface, x, y, "RTC", detail,
                           st->rtc_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL);

    snprintf(detail, sizeof(detail), "%u dispositivos enumerados", bk_device_pci_count());
    y = devmgr_draw_device(surface, x, y, "Bus PCI", detail,
                           bk_device_pci_count() ? DEVMGR_STATUS_OK : DEVMGR_STATUS_UNKNOWN);
    for (uint32_t i = 0; i < bk_device_pci_count() && i < 3; i++) {
        const pci_device_t *dev = bk_device_pci_at(i);
        if (!dev) continue;
        snprintf(detail, sizeof(detail), "%04x:%04x bus %u slot %u irq %u",
                 dev->vendor_id, dev->device_id, dev->bus, dev->slot,
                 dev->interrupt_line);
        y = devmgr_draw_device(surface, x, y,
            bk_device_pci_class_name(dev->class_code, dev->subclass), detail, DEVMGR_STATUS_OK);
    }
    return y;
}

static int devmgr_draw_diag(gui_surface_t *surface, devmgr_state_t *st,
                            int x, int y) {
    char detail[96];

    snprintf(detail, sizeof(detail), "ultimo refresh %u ms", st->last_refresh_ms);
    y = devmgr_draw_device(surface, x, y, "Refresh", detail, DEVMGR_STATUS_OK);

    y = devmgr_draw_device(surface, x, y, "Probe disco",
                           !st->disk_probe_ran ? "sin ejecutar" :
                           (st->disk_probe_ok ? "OK" : "fallo"),
                           !st->disk_probe_ran ? DEVMGR_STATUS_UNKNOWN :
                           (st->disk_probe_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL));

    y = devmgr_draw_device(surface, x, y, "Reporte",
                           !st->report_ran ? "sin generar" :
                           (st->report_ok ? "/DEVICES.TXT escrito" : "fallo"),
                           !st->report_ran ? DEVMGR_STATUS_UNKNOWN :
                           (st->report_ok ? DEVMGR_STATUS_OK : DEVMGR_STATUS_FAIL));
    return y;
}

static void devmgr_paint(gui_window_t *window UNUSED,
                         gui_surface_t *surface,
                         void *context) {
    devmgr_state_t *st = (devmgr_state_t *)context;
    int x;
    int y;
    int w;
    int body_h;
    int body_top;
    char title[96];

    if (!st || !st->window || !surface) return;

    x = st->window->bounds.x + 12;
    body_top = st->window->bounds.y + GUI_TITLEBAR_HEIGHT + 34;
    y = body_top + 8;
    w = st->window->bounds.w - 24;
    body_h = st->window->bounds.h - GUI_TITLEBAR_HEIGHT - 44;
    st->content_x = x;
    st->content_w = w;

    bk_gui_gfx_fill_rect(surface,
                      (gui_rect_t){x - 6, body_top, w + 12, body_h},
                      0x00FFFFFF);
    bk_gui_gfx_draw_rect(surface,
                      (gui_rect_t){x - 6, body_top, w + 12, body_h},
                      0x008090A0);

    snprintf(title, sizeof(title), "Device Manager - %u dispositivos/logicas",
             (uint32_t)DEVMGR_CATEGORY_COUNT);
    bk_gui_font_draw_string(surface, x, y, title, 0x00002060, 0, false);
    y += 17;

    y = devmgr_draw_header(surface, st, 0, x, y, w);
    if (st->cat_expanded[0]) y = devmgr_draw_storage(surface, st, x, y);

    y = devmgr_draw_header(surface, st, 1, x, y, w);
    if (st->cat_expanded[1]) y = devmgr_draw_display(surface, st, x, y);

    y = devmgr_draw_header(surface, st, 2, x, y, w);
    if (st->cat_expanded[2]) y = devmgr_draw_sound(surface, st, x, y);

    y = devmgr_draw_header(surface, st, 3, x, y, w);
    if (st->cat_expanded[3]) y = devmgr_draw_input(surface, st, x, y);

    y = devmgr_draw_header(surface, st, 4, x, y, w);
    if (st->cat_expanded[4]) y = devmgr_draw_system(surface, st, x, y);

    y = devmgr_draw_header(surface, st, 5, x, y, w);
    if (st->cat_expanded[5]) y = devmgr_draw_diag(surface, st, x, y);

    bk_gui_gfx_fill_rect(surface,
                      (gui_rect_t){x - 6, st->window->bounds.y + st->window->bounds.h - 18,
                                   w + 12, 14},
                      0x00E8E8E0);
    bk_gui_font_draw_string_clipped(surface, x, st->window->bounds.y + st->window->bounds.h - 16,
                                 st->status[0] ? st->status :
                                                 "Listo. Botones: Actualizar, Expandir, Contraer, Probar disco, Beep, Reporte.",
                                 0x00404040,
                                 (gui_rect_t){x, st->window->bounds.y + st->window->bounds.h - 16,
                                              w, 12});
}

static void devmgr_toggle(devmgr_state_t *st, int index) {
    if (!st || index < 0 || index >= DEVMGR_CATEGORY_COUNT) return;
    st->cat_expanded[index] = !st->cat_expanded[index];
    snprintf(st->status, sizeof(st->status), "%s %s",
             g_cat_names[index], st->cat_expanded[index] ? "expandido" : "contraido");
    if (st->window) st->window->dirty = true;
    bk_gui_request_paint();
}

static bool devmgr_event(gui_window_t *window UNUSED,
                         const gui_event_t *event,
                         void *context) {
    devmgr_state_t *st = (devmgr_state_t *)context;

    if (!st || !event) return false;

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        for (int i = 0; i < DEVMGR_CATEGORY_COUNT; i++) {
            int top = st->cat_header_y[i] - 2;
            if (event->x >= st->content_x &&
                event->x <= st->content_x + st->content_w &&
                event->y >= top && event->y < top + DEVMGR_HEADER_H) {
                devmgr_toggle(st, i);
                return true;
            }
        }
    }

    if (event->type == GUI_EVENT_KEY) {
        st->key_events++;
        if (event->key >= '1' && event->key <= '6') {
            devmgr_toggle(st, event->key - '1');
            return true;
        }
        if (event->key == 'r' || event->key == 'R') {
            devmgr_refresh(st);
            devmgr_set_status(st, "Datos actualizados");
            if (st->window) st->window->dirty = true;
            bk_gui_request_paint();
            return true;
        }
        if (event->key == 'b' || event->key == 'B') {
            devmgr_button(st->window, st->button_ids[4]);
            return true;
        }
        if (event->key == 'p' || event->key == 'P') {
            devmgr_button(st->window, st->button_ids[3]);
            return true;
        }
    }

    return false;
}

static void devmgr_create_buttons(devmgr_state_t *st) {
    gui_widget_t *button;
    static const char *labels[DEVMGR_BUTTON_COUNT] = {
        "Actualizar", "Expandir", "Contraer", "Detectar", "Beep", "Reporte"
    };
    int widths[DEVMGR_BUTTON_COUNT] = {72, 64, 68, 58, 46, 62};
    int x = 8;

    if (!st || !st->desktop || !st->window) return;

    for (int i = 0; i < DEVMGR_BUTTON_COUNT; i++) {
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
                                   (gui_rect_t){x, 6, widths[i], 22},
                                   labels[i], devmgr_button);
        if (button) st->button_ids[i] = button->id;
        x += widths[i] + 6;
    }
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    devmgr_state_t *st;
    uint32_t next_refresh;
    int i;

    if (!desktop) desktop = bk_gui_desktop();
    if (!desktop) return;

    st = (devmgr_state_t *)bk_sys_alloc(sizeof(*st));
    if (!st) return;
    bk_runtime_memset(st, 0, sizeof(*st));
    st->desktop = desktop;
    g_devmgr = st;

    for (i = 0; i < DEVMGR_CATEGORY_COUNT; i++) st->cat_expanded[i] = false;
    st->cat_expanded[0] = true;
    devmgr_set_status(st, "Administrador de dispositivos iniciado");

    st->window = bk_gui_create_window(desktop, 86, 52, 560, 420,
                                      "Administrador de Dispositivos");
    if (!st->window) {
        if (g_devmgr == st) g_devmgr = NULL;
        bk_sys_free(st);
        return;
    }

    bk_gui_set_window_content(st->window, devmgr_paint, st);
    bk_gui_set_window_event_handler(st->window, devmgr_event, st);
    bk_gui_set_window_min_size(st->window, 470, 300);
    st->window->owner_pid = bk_sys_getpid();
    devmgr_create_buttons(st);

    devmgr_refresh(st);
    next_refresh = bk_sys_uptime_ms() + 500U;
    bk_gui_request_paint();

    while (st->window && st->window->listed) {
        uint32_t now = bk_sys_uptime_ms();

        if ((int32_t)(now - next_refresh) >= 0) {
            devmgr_refresh(st);
            st->window->dirty = true;
            bk_gui_request_paint();
            next_refresh = now + 500U;
        }
        bk_sys_sleep_ticks(1);
    }

    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
    }
    if (g_devmgr == st) g_devmgr = NULL;
    bk_sys_free(st);
}
