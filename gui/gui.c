#include "gui.h"
#include "../programs/programs.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/pic.h"
#include "../kernel/include/vga.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/task.h"

static gui_desktop_t g_desktop;
static gui_event_queue_t g_events;
static uint8_t g_cpu_usage;

uint8_t gui_get_cpu_usage(void) {
    return g_cpu_usage;
}

bool gui_change_resolution(gui_desktop_t *desktop, uint16_t width,
                           uint16_t height) {
    const gfx_info_t *info = gfx_get_info();

    if (!desktop || !info || info->mode != GFX_MODE_VESA_LFB) return false;
    if (!gfx_set_display_mode(width, height, info->bpp)) return false;
    if (!gui_gfx_reconfigure(&desktop->surface)) return false;

    gui_desktop_reflow(desktop);
    deskmanager_refresh_layout();
    gui_desktop_paint(desktop);
    return true;
}

void gui_init(void) {
    gui_surface_t surface;

    if (!gui_gfx_init(&surface)) {
        kprintf("[GUI] No se pudo iniciar modo grafico.\n");
        return;
    }

    gui_desktop_init(&g_desktop, surface);
    gui_event_init(&g_events);
    deskmanager_install(&g_desktop);
    about_install(&g_desktop);
    filebrowser_install(&g_desktop);
    shelllauncher_install(&g_desktop);
    texteditor_install(&g_desktop);
    calculator_install(&g_desktop);
    processmanager_install(&g_desktop);
    midamp_install(&g_desktop);
    imageviewer_install(&g_desktop);
    games_install(&g_desktop);
    settings_install(&g_desktop);
    deskbar_install(&g_desktop);
}

void gui_run(void) {
    gui_event_t event;
    uint32_t last_clock_tick = pit_get_ticks();
    uint32_t last_paint_tick = last_clock_tick;
    uint32_t cpu_sample_tick = last_clock_tick;
    uint32_t activity = 0;
    bool needs_paint = true;
    bool urgent_paint = true;

    while (true) {
        gui_event_poll(&g_events);
        while (gui_event_next(&g_events, &event)) {
            gui_desktop_handle_event(&g_desktop, &event);
            activity += 4;
            needs_paint = true;
            /*
             * El movimiento PS/2 puede llegar mucho más rápido que lo que
             * cuesta presentar 800x600 por software. Los clics y teclas sí
             * deben verse inmediatamente; el movimiento se agrupa al ritmo
             * máximo de pintado para impedir una cola infinita de cuadros.
             */
            if (event.type != GUI_EVENT_MOUSE_MOVE)
                urgent_paint = true;
        }
        uint32_t now = pit_get_ticks();
        if (!needs_paint && gui_desktop_has_dirty(&g_desktop))
            needs_paint = true;
        if (now - last_clock_tick >= 100) {
            last_clock_tick = now;
            needs_paint = true;
        }
        if (needs_paint &&
            (urgent_paint || now - last_paint_tick >= 3U)) {
            gui_desktop_paint(&g_desktop);
            activity += 2;
            needs_paint = false;
            urgent_paint = false;
            last_paint_tick = pit_get_ticks();
        }
        if (now - cpu_sample_tick >= 1000) {
            g_cpu_usage = activity > 100 ? 100 : (uint8_t)activity;
            activity = 0;
            cpu_sample_tick = now;
        }
        task_sleep(1);
    }
}
