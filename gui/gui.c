#include "gui.h"
#include "../programs/programs.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/pic.h"
#include "../kernel/include/vga.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/task.h"
#include "../kernel/include/bootsplash.h"
#include "../kernel/include/user_config.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/memory.h"
#include "../kernel/string.h"

/* El LFB sincroniza la presentación al retrazado. El reloj interno debe ir
 * por encima de 60 Hz para no perder cada segundo retrazado por cuantización
 * del PIT (300 Hz); 120 Hz da una latencia máxima de dos ticks. */
#define GUI_TARGET_FPS 120U

static gui_desktop_t g_desktop;
static gui_event_queue_t g_events;
static uint8_t g_cpu_usage;
static volatile uint32_t g_last_input_tick;
/*
 * Un booleano pierde solicitudes si una app termina de pintar mientras el
 * compositor esta consumiendo/limpiando la solicitud anterior. Un contador
 * permite distinguir cada invalidacion, incluso si llega durante un frame.
 */
static volatile uint32_t g_paint_generation;

static int gui_parse_int_value(const char *text, int fallback) {
    int value = 0;
    bool seen_digit = false;

    if (!text) return fallback;
    if (*text == '+') text++;
    while (*text >= '0' && *text <= '9') {
        seen_digit = true;
        value = value * 10 + (*text - '0');
        text++;
    }
    return seen_digit ? value : fallback;
}

static void gui_load_mouse_preferences(void) {
    void *config = NULL;
    uint32_t config_size = 0;
    bool trail_enabled = gui_desktop_cursor_trail_enabled(&g_desktop);

    if (!bk_user_config_read_all(BK_MOUSE_CONFIG_PATH,
                                 BK_MOUSE_CONFIG_LEGACY_PATH,
                                 &config, &config_size) || !config)
        return;
    (void)config_size;

    {
        char *line = (char *)config;

        while (*line) {
            char *end = line;
            char *eq = NULL;
            char saved;

            while (*end && *end != '\r' && *end != '\n') {
                if (*end == '=' && !eq) eq = end;
                end++;
            }
            saved = *end;
            *end = '\0';

            if (eq) {
                int value;

                *eq++ = '\0';
                value = gui_parse_int_value(eq, 0);
                if (kstrcmp(line, "sensitivity") == 0) {
                    mouse_set_sensitivity((uint8_t)((value >= 1 && value <= 5)
                        ? value : mouse_get_sensitivity()));
                } else if (kstrcmp(line, "trail") == 0) {
                    trail_enabled = value != 0;
                }
            }

            *end = saved;
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
    }

    kfree(config);
    gui_desktop_set_cursor_trail(&g_desktop, trail_enabled);
}

uint8_t gui_get_cpu_usage(void) {
    return g_cpu_usage;
}

gui_desktop_t *gui_get_desktop(void) {
    return &g_desktop;
}

uint32_t gui_get_last_input_tick(void) {
    return g_last_input_tick;
}

void gui_request_paint(void) {
    __asm__ volatile ("lock; incl %0"
                      : "+m"(g_paint_generation) : : "memory", "cc");
}

bool gui_change_resolution(gui_desktop_t *desktop, uint16_t width,
                           uint16_t height, uint8_t bpp) {
    const gfx_info_t *info = gfx_get_info();

    if (!desktop || !info || info->mode != GFX_MODE_VESA_LFB) return false;
    if (!bpp) bpp = info->bpp;
    if (!gfx_set_display_mode(width, height, bpp)) return false;
    if (!gui_gfx_reconfigure(&desktop->surface)) return false;

    gui_desktop_reflow(desktop);
    deskmanager_refresh_layout();
    gui_gfx_invalidate_front();
    gui_desktop_invalidate_all(desktop);
    gui_desktop_paint(desktop);
    return true;
}

void gui_init(void) {
    gui_surface_t surface;

    if (!gui_gfx_init(&surface)) {
        kprintf("[GUI] No se pudo iniciar modo grafico.\n");
        return;
    }

    bootsplash_show("PREPARING DESKTOP", 74);
    bootsplash_show("PREPARING DESKTOP", 74);
    gui_desktop_init(&g_desktop, surface);
    gui_event_init(&g_events);
    gui_load_mouse_preferences();
    g_last_input_tick = pit_get_ticks();
    gui_request_paint();

    bootsplash_show("LOADING DESKTOP ICONS", 78);
    deskmanager_install(&g_desktop);
    bootsplash_show("LOADING APPLICATIONS", 84);
    bootsplash_show("LOADING DESKBAR", 94);
    deskbar_install(&g_desktop);
    screensaverd_install(&g_desktop);
    bootsplash_show("READY", 100);
}

void gui_run(void) {
    bootsplash_debug("GUI_RUN entered");
    bootsplash_disable();
    gui_event_t event;
    uint32_t last_clock_tick = pit_get_ticks();
    uint32_t last_paint_tick = last_clock_tick;
    uint32_t cpu_sample_tick = last_clock_tick;
    uint32_t handled_paint_generation = 0;
    uint32_t activity = 0;
    bool needs_paint = true;
    bool urgent_paint = true;
    uint32_t timer_hz = pit_get_frequency_hz();
    uint32_t paint_interval = timer_hz / GUI_TARGET_FPS;
    uint32_t clock_interval = timer_hz;

    if (!paint_interval) paint_interval = 1U;
    if (!clock_interval) clock_interval = 1U;

    while (true) {
        gui_event_poll(&g_events);
        while (gui_event_next(&g_events, &event)) {
            g_last_input_tick = pit_get_ticks();
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
            /* El handler ya marca la ventana/menu afectado. Invalidar toda
             * la pantalla por cada click o tecla producia barridos visibles,
             * tearing y cursores fantasma en framebuffers lentos. */
        }
        uint32_t now = pit_get_ticks();
        if (g_paint_generation != handled_paint_generation) {
            needs_paint = true;
        }
        if (!needs_paint && gui_desktop_has_dirty(&g_desktop))
            needs_paint = true;
        if (now - last_clock_tick >= clock_interval) {
            last_clock_tick = now;
            gui_desktop_invalidate_rect(&g_desktop,
                (gui_rect_t){0, g_desktop.surface.height > 24
                                ? g_desktop.surface.height - 24 : 0,
                             g_desktop.surface.width,
                             g_desktop.surface.height > 24
                                ? 24 : g_desktop.surface.height});
            needs_paint = true;
        }

        /*
         * Los eventos urgentes se pintan ya. El mouse move se limita al ritmo
         * máximo de paint para evitar que el escritorio entero se redibuje a
         * velocidad absurda durante movimiento continuo.
         */
        if (needs_paint &&
            (urgent_paint || now - last_paint_tick >= paint_interval)) {
            uint32_t frame_generation = g_paint_generation;
            gui_desktop_paint(&g_desktop);
            activity += 2;
            handled_paint_generation = frame_generation;

            /*
             * Una superficie Ring 3 puede quedar lista mientras se compone
             * este mismo frame. En ese caso el compositor pudo limpiar su
             * rectangulo sucio despues de la invalidacion. Conservamos el
             * pedido y restauramos el dirty para presentar el frame nuevo sin
             * depender de un movimiento posterior del mouse.
             */
            if (g_paint_generation != frame_generation) {
                needs_paint = true;
                urgent_paint = true;
            } else {
                needs_paint = false;
                urgent_paint = false;
            }
            last_paint_tick = pit_get_ticks();
        }
        if (now - cpu_sample_tick >= timer_hz) {
            g_cpu_usage = activity > 100 ? 100 : (uint8_t)activity;
            activity = 0;
            cpu_sample_tick = now;
        }
        task_sleep(1);
    }
}
