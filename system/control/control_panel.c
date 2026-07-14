#include "control_common.h"
#include "kernel/include/api.h"

#define CONTROL_ITEM_COUNT 7
#define CONTROL_COLS       4
#define CONTROL_CELL_W     118
#define CONTROL_CELL_H     112
#define CONTROL_ICON_SIZE   48

typedef struct {
    const char *name;
    const char *description;
    const char *path;
    const char *icon_path;
} control_item_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    int selected;
    int hover;
    int pressed;
    int last_clicked;
    uint32_t last_click_tick;
    char status[96];
    uint32_t *icons[CONTROL_ITEM_COUNT];
} control_state_t;

static const control_item_t g_control_items[CONTROL_ITEM_COUNT] = {
    {"Pantalla", "Fondo, protector y resolucion", "/SYSTEM/CONTROL/DISPLAY.CPL",
     "/ICONS/DISPLAY.BMP"},
    {"Sonido", "Dispositivos de audio", "/SYSTEM/CONTROL/SOUND.CPL",
     "/ICONS/SOUND.BMP"},
    {"Fecha y hora", "Reloj del sistema", "/SYSTEM/CONTROL/DATETIME.CPL",
     "/ICONS/DATETIME.BMP"},
    {"Mouse", "Estado y sensibilidad", "/SYSTEM/CONTROL/MOUSE.CPL",
     "/ICONS/MOUSE.BMP"},
    {"Teclado", "Distribucion y prueba", "/SYSTEM/CONTROL/KEYBOARD.CPL",
     "/ICONS/KEYBOARD.BMP"},
    {"Sistema", "Informacion del equipo", "/SYSTEM/CONTROL/SYSTEM.CPL",
     "/ICONS/SYSTEM.BMP"},
    {"Dispositivos", "Administrador de hardware", "/SYSTEM/CONTROL/DEVMGR.CPL",
     "/ICONS/DEVICES.BMP"},
};

static gui_rect_t control_client_rect(const control_state_t *st) {
    return st && st->window
        ? bk_gui_window_content_rect_raw(st->window)
        : (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t control_item_rect(const control_state_t *st, int index) {
    gui_rect_t client = control_client_rect(st);
    int grid_w = CONTROL_COLS * CONTROL_CELL_W - 4;
    int left = client.x + (client.w - grid_w) / 2;
    int top = client.y + 22;

    if (left < client.x + 8) left = client.x + 8;
    return (gui_rect_t){left + (index % CONTROL_COLS) * CONTROL_CELL_W,
                        top + (index / CONTROL_COLS) * CONTROL_CELL_H,
                        CONTROL_CELL_W - 4, CONTROL_CELL_H - 4};
}

static void control_draw_fallback_icon(gui_surface_t *surface, int x, int y) {
    int inner_x = x + 9;
    int inner_y = y + 8;
    int inner_w = CONTROL_ICON_SIZE - 18;
    int inner_h = CONTROL_ICON_SIZE - 20;

    cpl_draw_bevel(surface, (gui_rect_t){x, y, CONTROL_ICON_SIZE, CONTROL_ICON_SIZE},
                   CPL_FACE, false);
    bk_gui_gfx_fill_rect(surface,
        (gui_rect_t){inner_x, inner_y, inner_w, inner_h},
        0x00E8E8E8);
    bk_gui_gfx_draw_rect(surface,
        (gui_rect_t){inner_x, inner_y, inner_w, inner_h},
        CPL_DARK);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 14, y + 14, 18, 4}, CPL_BLUE);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 14, y + 22, 18, 4}, CPL_SHADOW);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){x + 14, y + 30, 18, 4}, CPL_SHADOW);
}

static void control_draw_icon(gui_surface_t *surface, int x, int y,
                              const uint32_t *pixels) {
    if (pixels) {
        bk_app_draw_icon(surface, x, y, pixels,
                                 CONTROL_ICON_SIZE, CONTROL_ICON_SIZE);
        return;
    }
    control_draw_fallback_icon(surface, x, y);
}

static void control_load_icons(control_state_t *st) {
    if (!st) return;
    for (int i = 0; i < CONTROL_ITEM_COUNT; i++) {
        if (st->icons[i]) continue;
        st->icons[i] = bk_app_load_icon(g_control_items[i].icon_path,
                                                    CONTROL_ICON_SIZE,
                                                    CONTROL_ICON_SIZE);
    }
}

static void control_free_icons(control_state_t *st) {
    if (!st) return;
    for (int i = 0; i < CONTROL_ITEM_COUNT; i++) {
        if (!st->icons[i]) continue;
        bk_sys_free(st->icons[i]);
        st->icons[i] = NULL;
    }
}

static int control_hit(const control_state_t *st, int x, int y) {
    for (int i = 0; i < CONTROL_ITEM_COUNT; i++)
        if (bk_gui_rect_contains(control_item_rect(st, i), x, y)) return i;
    return -1;
}

static void control_open(control_state_t *st, int index) {
    if (!st || index < 0 || index >= CONTROL_ITEM_COUNT) return;
    if (bk_app_execute_path(st->desktop, g_control_items[index].path)) {
        snprintf(st->status, sizeof(st->status), "Abriendo %s...",
                 g_control_items[index].name);
    } else {
        snprintf(st->status, sizeof(st->status), "No se pudo abrir %s.",
                 g_control_items[index].name);
    }
    st->window->dirty = true;
}

enum {
    CONTROL_CONTEXT_OPEN = 1,
    CONTROL_CONTEXT_INFO,
};

static void control_context_callback(gui_window_t *window, uint32_t item_id,
                                     void *context) {
    control_state_t *st = (control_state_t *)context;
    if (!st || st->selected < 0 || st->selected >= CONTROL_ITEM_COUNT) return;
    if (item_id == CONTROL_CONTEXT_OPEN) {
        control_open(st, st->selected);
    } else if (item_id == CONTROL_CONTEXT_INFO) {
        snprintf(st->status, sizeof(st->status), "%s: %s",
                 g_control_items[st->selected].name,
                 g_control_items[st->selected].description);
        if (window) window->dirty = true;
    }
}

static void control_open_context(control_state_t *st, int hit, int x, int y) {
    if (!st || !st->window || hit < 0 || hit >= CONTROL_ITEM_COUNT) return;
    st->selected = hit;
    bk_gui_window_context_clear(st->window);
    (void)bk_gui_window_context_add_item(st->window, CONTROL_CONTEXT_OPEN,
        "Abrir configuracion", true, control_context_callback, st);
    (void)bk_gui_window_context_add_item(st->window, CONTROL_CONTEXT_INFO,
        "Informacion", true, control_context_callback, st);
    bk_gui_window_context_open(st->window, x, y);
}

static void control_paint(gui_window_t *window, gui_surface_t *surface,
                          void *context) {
    control_state_t *st = (control_state_t *)context;
    (void)window;
    gui_rect_t client = control_client_rect(st);
    gui_rect_t status = {client.x + 6, client.y + client.h - 18,
                         client.w - 12, 16};

    bk_gui_font_draw_string(surface, client.x + 12, client.y + 7,
        "Seleccione un icono para configurar BlesKernOS.",
        CPL_TEXT, 0, false);

    for (int i = 0; i < CONTROL_ITEM_COUNT; i++) {
        gui_rect_t cell = control_item_rect(st, i);
        bool active = i == st->selected || i == st->hover || i == st->pressed;
        uint32_t fill = i == st->selected ? 0x00D8E8F8 :
                        (i == st->hover ? 0x00E8E8E0 : 0x00D7D7D0);
        int label_x;

        if (active)
            cpl_draw_bevel(surface, cell, fill, i == st->pressed);

        control_draw_icon(surface,
            cell.x + (cell.w - CONTROL_ICON_SIZE) / 2, cell.y + 6, st->icons[i]);

        label_x = cell.x + (cell.w - (int)bk_gui_font_text_width(g_control_items[i].name)) / 2;
        bk_gui_font_draw_string(surface, label_x, cell.y + 66,
                             g_control_items[i].name, CPL_TEXT, 0, false);
    }

    cpl_draw_bevel(surface, status, CPL_FACE, true);
    bk_gui_font_draw_string_clipped(surface, status.x + 4,
        status.y + 4,
        st->status[0] ? st->status :
        (st->selected >= 0
            ? g_control_items[st->selected].description
            : "Listo"),
        CPL_TEXT, (gui_rect_t){status.x + 4,
        status.y + 2,
        status.w - 8, status.h - 4});
}

static bool control_event(gui_window_t *window UNUSED, const gui_event_t *event,
                          void *context) {
    control_state_t *st = (control_state_t *)context;
    int hit = control_hit(st, event->x, event->y);

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        if (st->hover != hit) {
            st->hover = hit;
            st->window->dirty = true;
        }
        return hit >= 0;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (event->button == MOUSE_RIGHT_BUTTON && hit >= 0) {
            control_open_context(st, hit, event->x, event->y);
            st->window->dirty = true;
            return true;
        }
        st->pressed = hit;
        st->selected = hit;
        st->window->dirty = true;
        return hit >= 0;
    }

    if (event->type == GUI_EVENT_MOUSE_UP && hit >= 0 && hit == st->pressed) {
        uint32_t now = bk_sys_ticks();
        if (st->last_clicked == hit && now - st->last_click_tick <= 500)
            control_open(st, hit);
        st->last_clicked = hit;
        st->last_click_tick = now;
        st->pressed = -1;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP) {
        if (st->pressed >= 0) st->window->dirty = true;
        st->pressed = -1;
    }

    if (event->type == GUI_EVENT_KEY && event->key == KEY_ENTER &&
        st->selected >= 0) {
        control_open(st, st->selected);
        return true;
    }

    return false;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    control_state_t *st;

    if (!desktop) return;
    st = (control_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    st->selected = st->hover = st->pressed = st->last_clicked = -1;
    st->window = bk_gui_create_window(desktop, 62, 38, 500, 292,
                                           "Panel de control");
    if (!st->window) {
        bk_sys_free(st);
        return;
    }

    (void)bk_about_attach(st->window, desktop, &(bk_about_info_t){
        "Panel de control", "Version 1.0", "Configuracion de BlesKernOS.",
        "Bles.INC (C) 2026", "/ICONS/CONTROL.BMP"});

    bk_gui_set_window_min_size(st->window, 500, 292);
    bk_gui_set_window_content(st->window, control_paint, st);
    bk_gui_set_window_event_handler(st->window, control_event, st);
    st->window->bg_color = 0x00CFCFCF;
    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);

    control_load_icons(st);

    while (!bk_proc_exit_requested() && st->window->listed) bk_sys_sleep_ticks(2);

    control_free_icons(st);
    cpl_destroy_window(st->desktop, st->window);
    bk_sys_free(st);
    bk_proc_exit();
}
