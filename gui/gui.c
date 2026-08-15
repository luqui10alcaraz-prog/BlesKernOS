#include "gui.h"
#include "../programs/programs.h"
#include "../kernel/include/gfx.h"
#include "../kernel/include/pic.h"
#include "../kernel/include/vga.h"
#include "../kernel/include/pit.h"
#include "../kernel/include/task.h"
#include "../kernel/include/kernel_domains.h"
#include "../kernel/include/bootsplash.h"
#include "../kernel/include/user_config.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/perfmon.h"
#include "../kernel/include/compat_mode.h"
#include "../kernel/string.h"
#include "../kernel/stdio.h"

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
/* Counts GUI loop turns before reloading the wallpaper after a mode change.
   A Ring-3 display callback sets it while still returning to the scheduler;
   delaying one extra turn keeps VFS/bitmap work out of that transaction. */
static volatile uint32_t g_mode_layout_refresh_pending;
static bool g_setup_mode;

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

static void gui_load_display_preferences(void) {
    void *config = NULL;
    uint32_t config_size = 0;
    bool outline_enabled = true;

    if (bk_user_config_read_all(BK_DISPLAY_CONFIG_PATH,
                                BK_DISPLAY_CONFIG_LEGACY_PATH,
                                &config, &config_size) && config) {
        char *line = (char *)config;
        (void)config_size;

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
                *eq++ = '\0';
                if (kstrcmp(line, "drag_outline") == 0)
                    outline_enabled = gui_parse_int_value(eq, 1) != 0;
            }
            *end = saved;
            line = end;
            while (*line == '\r' || *line == '\n') line++;
        }
        kfree(config);
    }

    gui_desktop_set_drag_outline(&g_desktop, outline_enabled);
}

void gui_set_setup_mode(bool enabled) {
    g_setup_mode = enabled;
}

bool gui_setup_mode(void) {
    return g_setup_mode;
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
    gui_desktop_t *real_desktop = gui_get_desktop();
    uint16_t old_width;
    uint16_t old_height;
    uint8_t old_hardware_bpp;
    bool old_16_colors;
    bool use_16_colors;
    uint8_t hardware_bpp;

    /* Ring 3 receives a geometry-only desktop proxy.  It must never become
       the compositor target: modifying that copy switches the hardware while
       the real desktop keeps its old surface, leaving only the clear color.
       BlesKernOS has one active desktop, so resolve every public request to
       that authoritative instance. */
    if (real_desktop) desktop = real_desktop;
    if (!desktop || !info || !gfx_can_change_mode()) return false;
    if (compat_mode_is_tiny() &&
        (width != 320U || height != 200U || (bpp != 0U && bpp != 8U)))
        return false;

    /* Serialize hardware/backbuffer replacement with the compositor. Do not
       render synchronously here: this API is normally entered from a Ring-3
       widget callback. Painting from inside that callback queued more Ring-3
       paints to the same owner while preemption was blocked, leaving only the
       newly cleared desktop background visible. */
    gui_desktop_paint_lock();
    old_width = info->width;
    old_height = info->height;
    old_hardware_bpp = info->bpp;
    old_16_colors = gui_gfx_16_color_mode();
    if (!bpp) bpp = gui_display_color_depth();
    use_16_colors = bpp == 4;
    /* VGA 12h is genuinely planar 4 bpp. VESA/SVGA backends emulate the
       same palette through indexed 8 bpp when they do not expose 4 bpp. */
    hardware_bpp = use_16_colors &&
        (info->mode == GFX_MODE_VGA_13H || info->mode == GFX_MODE_VGA_12H)
        ? 4 : (use_16_colors ? 8 : bpp);
    kprintf("[GUI:MODE] solicitud %ux%ux%u desde %ux%ux%u pid=%u\n",
            width, height, hardware_bpp, old_width, old_height,
            old_hardware_bpp, task_current_pid());

    /* GPU compositor surfaces are tied to the old transport/mode generation.
       Drop them before the driver tears that generation down. */
    gui_gpu_compositor_shutdown();
    if (!gfx_set_display_mode(width, height, hardware_bpp)) {
        kprintf("[GUI:MODE] FAIL hardware\n");
        gui_desktop_paint_unlock();
        return false;
    }
    info = gfx_get_info();
    if (!info || !info->width || !info->height || !info->bpp || !info->pitch) {
        kprintf("[GUI:MODE] FAIL geometria driver got=%ux%ux%u pitch=%u "
                "wanted=%ux%ux%u pitch_min=%u\n",
                info ? info->width : 0U, info ? info->height : 0U,
                info ? info->bpp : 0U, info ? info->pitch : 0U,
                width, height, hardware_bpp,
                (uint32_t)width * ((uint32_t)hardware_bpp + 7U) / 8U);
        (void)gfx_set_display_mode(old_width, old_height, old_hardware_bpp);
        gui_desktop_paint_unlock();
        return false;
    }
    /* El backend ya leyó los registros del adaptador y confirmó la operación.
       Algunos SVGA ajustan pitch o profundidad internamente; rechazar esa
       geometría publicada convertía un cambio exitoso en una vuelta forzada
       a 640x480. Reconfigurar el escritorio con los valores reales. */
    if (info->width != width || info->height != height ||
        info->bpp != hardware_bpp) {
        kprintf("[GUI:MODE] geometria ajustada por driver: %ux%ux%u\n",
                info->width, info->height, info->bpp);
        width = info->width;
        height = info->height;
        hardware_bpp = info->bpp;
    }

    kprintf("[GUI:MODE] hardware OK; reservando backbuffer\n");
    gui_gfx_set_16_color_mode(use_16_colors);
    if (!gui_gfx_reconfigure(&desktop->surface)) {
        (void)gfx_set_display_mode(old_width, old_height, old_hardware_bpp);
        gui_gfx_set_16_color_mode(old_16_colors);
        /* The constrained-memory reconfigure may have released the previous
           backbuffer before retrying. Recreate it after restoring hardware. */
        if (!desktop->surface.pixels)
            (void)gui_gfx_reconfigure(&desktop->surface);
        kprintf("[GUI:MODE] FAIL backbuffer; modo anterior restaurado\n");
        gui_desktop_paint_unlock();
        return false;
    }

    kprintf("[GUI:MODE] backbuffer OK; reflow e invalidacion diferida\n");
    /* Start from a deterministic surface even though the normal GUI loop owns
       the first full repaint after this Ring-3 callback returns.  SVGA limpia
       o reutiliza su scanout al escribir WIDTH/HEIGHT; dejar esta imagen solo
       en RAM hasta el próximo frame hacía que se vieran filas de la VRAM del
       modo anterior (o negro) durante la transición. Publicar el fondo entero
       ahora establece una base válida con el pitch nuevo. */
    gui_gfx_clear(&desktop->surface, deskmanager_get_background());
    gui_gfx_present(&desktop->surface);
    gui_desktop_reflow(desktop);
    gui_desktop_reset_after_mode_change(desktop);
    /* No recargue wallpaper ni enumere discos aqui. Esta funcion puede ser
       invocada desde un callback Ring 3 y esas rutas toman VFS/heap mientras
       el cambio grafico sigue en curso. El siguiente compositor reutiliza el
       fondo ya residente (o el color de escritorio) y por eso siempre puede
       presentar un frame completo sin depender del filesystem. */
    gui_gfx_invalidate_front();
    /* Rebase the input sampler after mouse_set_bounds() clamped coordinates.
       Otherwise the old button/position state can synthesize a click or a huge
       movement immediately after the mode switch. */
    gui_event_init(&g_events);
    gui_desktop_paint_unlock();
    __sync_lock_test_and_set(&g_mode_layout_refresh_pending, 2U);
    /* Nunca componga desde este callback: desktop_paint puede encolar la
       pintura de la misma aplicacion Ring 3 que aun no regreso. Eso deja al
       task actual esperando su propio upcall y congela tanto eventos como el
       escritorio en el color base. reset_after_mode_change ya invalida toda
       la pantalla; el bucle GUI hara el primer frame al recuperar control. */
    gui_request_paint();
    kprintf("[GUI:MODE] OK %ux%ux%u; repaint pendiente\n",
            width, height, hardware_bpp);
    return true;
}

uint8_t gui_display_color_depth(void) {
    const gfx_info_t *info = gfx_get_info();
    if (gui_gfx_16_color_mode()) return 4;
    return info ? info->bpp : 0;
}

void gui_init(void) {
    gui_surface_t surface;

    if (!gui_gfx_init(&surface)) {
        kprintf("[GUI] No se pudo iniciar modo grafico.\n");
        return;
    }

    bootsplash_show("@H31F338DB", 74);
    gui_desktop_init(&g_desktop, surface);
    gui_event_init(&g_events);
    gui_load_mouse_preferences();
    gui_load_display_preferences();
    if (compat_mode_is_low_memory()) {
        gui_desktop_set_cursor_trail(&g_desktop, false);
        gui_desktop_set_drag_outline(&g_desktop, true);
    }
    g_last_input_tick = pit_get_ticks();
    gui_request_paint();

    bootsplash_show("@HD53E5F05", 78);
    if (g_setup_mode) {
        /* El primer arranque usa el gestor de ventanas, pero no instala el
           escritorio normal, sus iconos, deskbar ni screensaver. */
        deskmanager_install_setup(&g_desktop,
                                  "/SYSTEM/WALLPAPR/NOCHE.BMP");
    } else {
        deskmanager_install(&g_desktop);
        bootsplash_show("@H42CC64D2", 84);
        bootsplash_show("@HA841B7CD", 94);
        deskbar_install(&g_desktop);
        if (compat_mode_allow_screensaver())
            screensaverd_install(&g_desktop);
    }
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
    uint32_t target_fps = compat_mode_gui_target_fps();
    uint32_t idle_hz = compat_mode_gui_idle_hz();
    uint32_t paint_interval = timer_hz / target_fps;
    uint32_t clock_interval = timer_hz;

    if (!paint_interval) paint_interval = 1U;
    if (!clock_interval) clock_interval = 1U;

    while (true) {
        uint64_t perf_events_started;
        uint32_t mode_refresh =
            __sync_lock_test_and_set(&g_mode_layout_refresh_pending, 0U);

        if (mode_refresh > 1U) {
            __sync_lock_test_and_set(&g_mode_layout_refresh_pending,
                                     mode_refresh - 1U);
        } else if (mode_refresh == 1U) {
            /* The new desktop geometry is now authoritative. Rebuild the
               cached/stretch wallpaper for it, then repaint the whole scene.
               Failure is harmless: deskmanager falls back to its solid color. */
            deskmanager_refresh_layout();
            gui_desktop_invalidate_all(&g_desktop);
            gui_request_paint();
        }
        /* Reap at most one task per frame before taking GUI/GFX domains. This
         * keeps destructor, Wine and heap work out of the IRQ scheduler lock. */
        task_reap_deferred();
        perfmon_gui_loop();
        /* GUI state and hardware presentation are separate domains from VFS,
         * networking and the scheduler. Ring-3 windows may run on APs, but a
         * frame observes one coherent desktop transaction. */
        kernel_domains_enter(KDOMAIN_GUI | KDOMAIN_GFX);
        perf_events_started = perfmon_scope_begin();
        gui_event_poll(&g_events);
        while (gui_event_next(&g_events, &event)) {
            perfmon_gui_event(event.type == GUI_EVENT_MOUSE_MOVE);
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
        perfmon_scope_end(PERF_SCOPE_GUI_EVENTS, perf_events_started);
        uint32_t now = pit_get_ticks();
        if (g_paint_generation != handled_paint_generation) {
            needs_paint = true;
            /* gui_request_paint() es también la API histórica para programas
               que no conocen dirty rectangles. Si sólo cambió la generación
               y no hay una región marcada, un repaint limitado al cursor no
               puede mostrar su contenido nuevo. Conviértalo en invalidación
               completa una sola vez; las ventanas modernas conservan sus
               regiones parciales mediante gui_desktop_has_dirty(). */
            if (!gui_desktop_has_dirty(&g_desktop))
                gui_desktop_invalidate_all(&g_desktop);
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
            uint64_t perf_frame_started = perfmon_scope_begin();
            gui_desktop_paint(&g_desktop);
            perfmon_scope_end(PERF_SCOPE_GUI_FRAME, perf_frame_started);
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
        kernel_domains_exit(KDOMAIN_GUI | KDOMAIN_GFX);
        perfmon_poll();
        /* En reposo no hace falta despertar el GUI en cada uno de los 300 Hz
         * del PIT. 100 sondeos/s mantienen menos de 10 ms de latencia PS/2 y
         * dejan intervalos reales de idle al Pentium III. Durante animacion o
         * dirty pendiente se conserva la granularidad de un tick. */
        if (needs_paint || gui_desktop_has_dirty(&g_desktop)) {
            task_sleep(1U);
        } else {
            uint32_t idle_ticks = timer_hz / idle_hz;
            if (!idle_ticks) idle_ticks = 1U;
            task_sleep(idle_ticks);
        }
    }
}
