#include "gui.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"
#include "../kernel/include/klock.h"
#include "../programs/programs.h"

static kmutex_t g_desktop_paint_lock = KMUTEX_INITIALIZER;

void gui_desktop_paint_lock(void) {
    kmutex_lock(&g_desktop_paint_lock);
}

void gui_desktop_paint_unlock(void) {
    kmutex_unlock(&g_desktop_paint_lock);
}

static void copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void gui_desktop_clear_cursor_trail(gui_desktop_t *desktop) {
    if (!desktop) return;
    desktop->cursor_trail_count = 0;
    desktop->cursor_paint_count = 0;
}

static void gui_desktop_push_cursor_trail(gui_desktop_t *desktop, int x, int y) {
    if (!desktop || !desktop->cursor_trail_enabled) return;
    if (desktop->cursor_trail_count > 0) {
        uint8_t last = (uint8_t)(desktop->cursor_trail_count - 1);
        if (desktop->cursor_trail_x[last] == x &&
            desktop->cursor_trail_y[last] == y) return;
    }

    if (desktop->cursor_trail_count < GUI_CURSOR_TRAIL_MAX) {
        desktop->cursor_trail_x[desktop->cursor_trail_count] = x;
        desktop->cursor_trail_y[desktop->cursor_trail_count] = y;
        desktop->cursor_trail_count++;
        return;
    }

    for (uint8_t i = 1; i < GUI_CURSOR_TRAIL_MAX; i++) {
        desktop->cursor_trail_x[i - 1] = desktop->cursor_trail_x[i];
        desktop->cursor_trail_y[i - 1] = desktop->cursor_trail_y[i];
    }
    desktop->cursor_trail_x[GUI_CURSOR_TRAIL_MAX - 1] = x;
    desktop->cursor_trail_y[GUI_CURSOR_TRAIL_MAX - 1] = y;
}

static bool gui_program_surface_size(gui_surface_t *surface,
                                     const gui_surface_t *source) {
    uint32_t *pixels;
    uint32_t count;

    if (!surface || !source || !source->pixels) return false;
    if (surface->pixels && surface->width == source->width &&
        surface->height == source->height && surface->pitch == source->pitch)
        return true;
    count = (uint32_t)source->pitch * source->height;
    pixels = (uint32_t *)kmalloc(count * sizeof(uint32_t));
    if (!pixels) return false;
    /* El compositor conserva el buffer despues de devolver CPU a Ring 3. */
    if (!mm_set_allocation_owner(pixels, 0U)) {
        kfree(pixels);
        return false;
    }
    if (surface->pixels) kfree(surface->pixels);
    surface->pixels = pixels;
    surface->width = source->width;
    surface->height = source->height;
    surface->pitch = source->pitch;
    gui_gfx_reset_clip(surface);
    return true;
}

bool gui_program_prepare_paint(gui_program_t *program,
                               const gui_surface_t *source,
                               gui_surface_t **staging_out) {
    gui_rect_t screen;
    gui_rect_t copy_rect;

    if (staging_out) *staging_out = NULL;
    if (!program || !source || !source->pixels || !staging_out ||
        !gui_program_surface_size(&program->paint_cache, source) ||
        !gui_program_surface_size(&program->paint_staging, source))
        return false;

    screen = (gui_rect_t){0, 0, source->width, source->height};
    if (!program->paint_ready) {
        uint32_t count = (uint32_t)source->pitch * source->height;

        /* Inicializar ambos lados del doble buffer una sola vez. Después de
         * cada swap, el staging vuelve a contener una imagen completa válida
         * y sólo hace falta actualizar la región sucia. */
        kmemcpy(program->paint_cache.pixels, source->pixels,
                (size_t)count * sizeof(uint32_t));
        kmemcpy(program->paint_staging.pixels, source->pixels,
                (size_t)count * sizeof(uint32_t));
        copy_rect = screen;
    } else {
        gui_rect_t source_clip = gui_gfx_get_clip(source);
        if (!gui_rect_intersect(screen, source_clip, &copy_rect))
            return false;
        for (int y = copy_rect.y; y < copy_rect.y + copy_rect.h; y++) {
            kmemcpy(&program->paint_staging.pixels[
                        (uint32_t)y * program->paint_staging.pitch +
                        (uint32_t)copy_rect.x],
                    &source->pixels[(uint32_t)y * source->pitch +
                                    (uint32_t)copy_rect.x],
                    (size_t)copy_rect.w * sizeof(uint32_t));
        }
    }
    gui_gfx_set_clip(&program->paint_staging, copy_rect);
    *staging_out = &program->paint_staging;
    return true;
}

void gui_program_composite_paint(const gui_program_t *program,
                                 gui_surface_t *destination) {
    gui_rect_t visible;
    gui_rect_t screen;

    if (!program || !program->paint_ready || !program->paint_cache.pixels ||
        !destination || !destination->pixels) return;
    /* A display-mode change replaces the desktop surface immediately, while
       a Ring-3 full-screen painter may still own a cache produced for the old
       geometry. Never index that cache using the new width/height. The normal
       prepare path will resize it before the next callback. */
    if (program->paint_cache.width != destination->width ||
        program->paint_cache.height != destination->height ||
        program->paint_cache.pitch != destination->pitch) return;
    screen = (gui_rect_t){0, 0, destination->width, destination->height};
    if (!gui_rect_intersect(screen, gui_gfx_get_clip(destination), &visible))
        return;
    for (int y = visible.y; y < visible.y + visible.h; y++) {
        kmemcpy(&destination->pixels[(uint32_t)y * destination->pitch +
                                     (uint32_t)visible.x],
                &program->paint_cache.pixels[(uint32_t)y *
                    program->paint_cache.pitch + (uint32_t)visible.x],
                (size_t)visible.w * sizeof(uint32_t));
    }
}

void gui_program_finish_paint(gui_program_t *program) {
    gui_surface_t swap;
    if (!program || !program->paint_staging.pixels) return;
    swap = program->paint_cache;
    program->paint_cache = program->paint_staging;
    program->paint_staging = swap;
    program->paint_pending = false;
    program->paint_ready = true;
}

void gui_program_release_paint(gui_program_t *program) {
    if (!program) return;
    if (program->paint_cache.pixels) kfree(program->paint_cache.pixels);
    if (program->paint_staging.pixels) kfree(program->paint_staging.pixels);
    kmemset(&program->paint_cache, 0, sizeof(program->paint_cache));
    kmemset(&program->paint_staging, 0, sizeof(program->paint_staging));
}

void gui_desktop_init(gui_desktop_t *desktop, gui_surface_t surface) {
    if (!desktop) return;
    kmemset(desktop, 0, sizeof(*desktop));
    desktop->surface = surface;
    desktop->mouse_x = surface.width / 2;
    desktop->mouse_y = surface.height / 2;
    desktop->next_program_id = 1;
    desktop->next_window_id = 1;
    desktop->next_widget_id = 1;
}

gui_program_t *gui_desktop_register_program(gui_desktop_t *desktop, const char *name, void *state, gui_program_paint_t paint, gui_program_event_t handle_event, gui_program_destroy_t destroy) {
    gui_program_t *program;

    if (!desktop || !paint) return NULL;

    program = (gui_program_t *)kzalloc(sizeof(gui_program_t));
    if (!program) return NULL;
    if (!mm_set_allocation_owner(program, 0U)) {
        kfree(program);
        return NULL;
    }

    program->id = desktop->next_program_id++;
    program->state = state;
    program->paint = paint;
    program->handle_event = handle_event;
    program->destroy = destroy;
    program->paint_requested = true;
    if ((uint32_t)(uintptr_t)paint >= HEAP_START && task_current_is_user()) {
        program->callback_pid = task_current_pid();
        /* Native desktop paint/event callbacks still serialize with the CPU0
         * compositor.  Only this owner thread is pinned; process workers are
         * unaffected and may run on APs. */
        (void)task_set_affinity_mask(program->callback_pid, 1U);
    }
    copy_text(program->name, sizeof(program->name), name);

    program->prev = desktop->last_program;
    program->next = NULL;
    if (desktop->last_program) desktop->last_program->next = program;
    desktop->last_program = program;
    if (!desktop->first_program) desktop->first_program = program;
    return program;
}


void gui_desktop_unregister_program(gui_desktop_t *desktop, gui_program_t *program) {
    if (!desktop || !program) return;
    task_preempt_disable();

    if (program->prev) program->prev->next = program->next;
    if (program->next) program->next->prev = program->prev;
    if (desktop->first_program == program) desktop->first_program = program->next;
    if (desktop->last_program == program) desktop->last_program = program->prev;

    program->prev = NULL;
    program->next = NULL;
    task_preempt_enable();

    if (program->destroy && program->callback_pid) {
        uint32_t argument = (uint32_t)(uintptr_t)program;
        if (task_queue_user_upcall(program->callback_pid,
                (uint32_t)(uintptr_t)program->destroy, &argument, 1,
                NULL, 0, -4)) return;
    } else if (program->destroy) {
        program->destroy(program);
    }
    gui_program_release_paint(program);
    kfree(program);
}

gui_window_t *gui_desktop_create_window(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title) {
    gui_window_t *window = gui_window_create(desktop, x, y, w, h, title);
    if (!window) return NULL;
    gui_desktop_add_window(desktop, window);
    return window;
}

void gui_desktop_focus_window(gui_desktop_t *desktop, gui_window_t *window) {
    gui_window_t *previous;
    gui_rect_t previous_bounds = {0, 0, 0, 0};
    gui_rect_t next_bounds = {0, 0, 0, 0};
    bool previous_valid = false;
    bool next_valid = false;

    if (!desktop) return;
    task_preempt_disable();
    previous = desktop->focused_window;
    if (previous == window) {
        if (window) {
            window->focused = true;
            window->dirty = true;
            next_bounds = window->bounds;
            next_valid = true;
        }
        task_preempt_enable();
        if (next_valid) gui_desktop_invalidate_rect(desktop, next_bounds);
        gui_request_paint();
        return;
    }
    if (previous) {
        previous_bounds = previous->bounds;
        previous_valid = true;
        previous->focused = false;
        previous->dirty = true;
    }
    desktop->focused_window = window;
    if (window) {
        next_bounds = window->bounds;
        next_valid = true;
        window->focused = true;
        window->dirty = true;
    }
    task_preempt_enable();

    /* Focus changes affect title bars and the Deskbar representation. Make
       them explicit desktop invalidations instead of relying only on a
       per-window dirty bit that older compositor paths may consume early. */
    if (previous_valid) gui_desktop_invalidate_rect(desktop, previous_bounds);
    if (next_valid) gui_desktop_invalidate_rect(desktop, next_bounds);
    gui_request_paint();
}

void gui_desktop_add_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!desktop || !window) return;
    task_preempt_disable();
    window->prev = desktop->last_window;
    window->next = NULL;
    if (desktop->last_window) desktop->last_window->next = window;
    desktop->last_window = window;
    if (!desktop->first_window) desktop->first_window = window;
    task_preempt_enable();
    window->dirty = true;
    gui_desktop_focus_window(desktop, window);
}

void gui_desktop_raise_window(gui_desktop_t *desktop, gui_window_t *window) {
    gui_rect_t bounds;
    if (!desktop || !window) return;
    bounds = window->bounds;
    if (desktop->last_window == window) {
        window->dirty = true;
        gui_desktop_invalidate_rect(desktop, bounds);
        gui_request_paint();
        return;
    }
    task_preempt_disable();

    if (window->prev) window->prev->next = window->next;
    if (window->next) window->next->prev = window->prev;
    if (desktop->first_window == window) desktop->first_window = window->next;
    if (desktop->last_window == window) desktop->last_window = window->prev;

    window->prev = desktop->last_window;
    window->next = NULL;
    if (desktop->last_window) desktop->last_window->next = window;
    desktop->last_window = window;
    if (!desktop->first_window) desktop->first_window = window;
    window->dirty = true;
    task_preempt_enable();
    /* Reordering changes every overlapping pixel inside this rectangle even
       when neither window changed its own contents. */
    gui_desktop_invalidate_rect(desktop, bounds);
    gui_request_paint();
}

void gui_desktop_remove_window(gui_desktop_t *desktop, gui_window_t *window) {
    bool was_focused;
    bool focus_needs_repair;
    if (!desktop || !window) return;
    if (window->paint_bounds_valid)
        gui_desktop_invalidate_rect(desktop, window->paint_bounds);
    else
        gui_desktop_invalidate_rect(desktop, window->bounds);
    task_preempt_disable();
    was_focused = desktop->focused_window == window;
    focus_needs_repair = was_focused || !desktop->focused_window ||
        !desktop->focused_window->visible || !desktop->focused_window->listed ||
        !desktop->focused_window->input_enabled ||
        desktop->focused_window->destroy_state != 0U;

    if (window->prev) window->prev->next = window->next;
    if (window->next) window->next->prev = window->prev;
    if (desktop->first_window == window) desktop->first_window = window->next;
    if (desktop->last_window == window) desktop->last_window = window->prev;
    if (focus_needs_repair) {
        gui_window_t *fallback = desktop->last_window;
        window->focused = false;
        while (fallback && (!fallback->visible || !fallback->listed ||
                            !fallback->input_enabled ||
                            fallback->destroy_state != 0U))
            fallback = fallback->prev;
        if (desktop->focused_window && desktop->focused_window != window)
            desktop->focused_window->focused = false;
        desktop->focused_window = fallback;
        if (fallback) {
            fallback->focused = true;
            fallback->dirty = true;
        }
    }
    if (desktop->drag_window == window) {
        desktop->drag_window = NULL;
        desktop->drag_outline_visible = false;
    }
    if (desktop->resize_window == window) {
        desktop->resize_window = NULL;
        desktop->resize_edges = 0U;
        desktop->drag_outline_visible = false;
    }

    window->prev = NULL;
    window->next = NULL;
    task_preempt_enable();
}

static void gui_desktop_unlink_dead_program(gui_desktop_t *desktop,
                                            gui_program_t *program) {
    if (!desktop || !program) return;
    task_preempt_disable();
    if (program->prev) program->prev->next = program->next;
    if (program->next) program->next->prev = program->prev;
    if (desktop->first_program == program)
        desktop->first_program = program->next;
    if (desktop->last_program == program)
        desktop->last_program = program->prev;
    task_preempt_enable();
    /* El callback destroy pertenece al hilo que ya terminó. No se lo puede
     * volver a invocar desde el reaper. */
    gui_program_release_paint(program);
    kfree(program);
}

void gui_desktop_cleanup_thread(gui_desktop_t *desktop, uint32_t tid) {
    gui_window_t *window;
    gui_program_t *program;

    if (!desktop || !tid) return;
    if (desktop->cursor_custom && desktop->cursor_owner_pid == tid)
        gui_desktop_reset_cursor(desktop, tid);

    /* owner_pid es deliberadamente un ID de proceso: las aplicaciones usan
     * bk_sys_getpid() al crear la ventana. No debe compararse con el TID del
     * hilo principal, porque ese TID coincide con el PID y suele terminar
     * apenas crea el worker real de la aplicación. Sólo los callbacks son
     * propiedad de un hilo concreto. */
    window = desktop->first_window;
    while (window) {
        gui_window_t *next = window->next;
        if (window->content_pid == tid || window->event_pid == tid) {
            gui_desktop_remove_window(desktop, window);
            gui_window_destroy(window);
        }
        window = next;
    }

    program = desktop->first_program;
    while (program) {
        gui_program_t *next = program->next;
        if (program->callback_pid == tid)
            gui_desktop_unlink_dead_program(desktop, program);
        program = next;
    }
    gui_request_paint();
}

void gui_desktop_cleanup_process(gui_desktop_t *desktop,
                                 uint32_t process_id) {
    gui_window_t *window;

    if (!desktop || !process_id) return;
    window = desktop->first_window;
    while (window) {
        gui_window_t *next = window->next;
        if (window->owner_pid == process_id) {
            gui_desktop_remove_window(desktop, window);
            gui_window_destroy(window);
        }
        window = next;
    }
    gui_request_paint();
}

void gui_desktop_cleanup_owner(gui_desktop_t *desktop, uint32_t owner_pid) {
    /* Compatibilidad con el nombre histórico: un owner sin contexto puede
     * representar tanto un TID de callback como un PID de ventana. El kernel
     * nuevo usa las dos funciones explícitas para no mezclar ambos conceptos. */
    gui_desktop_cleanup_thread(desktop, owner_pid);
    gui_desktop_cleanup_process(desktop, owner_pid);
}

gui_window_t *gui_desktop_window_at(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window;
    if (!desktop) return NULL;
    task_preempt_disable();
    window = desktop->last_window;
    while (window) {
        /* A Ring-3 close can leave a tombstoned object linked until its last
         * callback returns.  It must not form an invisible input shield over
         * the desktop or over another application during that interval. */
        if (window->destroy_state == 0U &&
            gui_window_contains(window, x, y)) {
            task_preempt_enable();
            return window;
        }
        window = window->prev;
    }
    task_preempt_enable();
    return NULL;
}

void gui_desktop_handle_event(gui_desktop_t *desktop, const gui_event_t *event) {
    gui_program_t *program;
    int old_x;
    int old_y;

    if (!desktop || !event) return;

    old_x = desktop->mouse_x;
    old_y = desktop->mouse_y;
    if (event->type == GUI_EVENT_MOUSE_MOVE || event->type == GUI_EVENT_MOUSE_DOWN ||
        event->type == GUI_EVENT_MOUSE_UP || event->type == GUI_EVENT_MOUSE_WHEEL) {
        desktop->mouse_x = event->x;
        desktop->mouse_y = event->y;
        desktop->mouse_buttons = event->buttons;
        if ((old_x != desktop->mouse_x || old_y != desktop->mouse_y) &&
            desktop->cursor_trail_enabled)
            gui_desktop_push_cursor_trail(desktop, old_x, old_y);
    }

    /* Screensavers must close before normal windows/apps consume the input.
       Without this, SSLOGO can remain active behind the desktop and leave
       black regions while other apps keep receiving events. */
    program = desktop->last_program;
    while (program) {
        if (program->handle_event) {
            if (program->callback_pid) {
                uint32_t arguments[3] = {
                    (uint32_t)(uintptr_t)program,
                    (uint32_t)(uintptr_t)desktop, 0U};
                if (task_queue_user_upcall(program->callback_pid,
                        (uint32_t)(uintptr_t)program->handle_event,
                        arguments, 3, event, sizeof(*event), 2)) return;
            } else if (program->handle_event(program, desktop, event)) {
                return;
            }
        }
        program = program->prev;
    }
}

bool gui_desktop_request_owner_paint(gui_desktop_t *desktop,
                                     uint32_t process_id,
                                     uint32_t thread_id) {
    gui_window_t *window;
    gui_program_t *program;
    bool found = false;
    bool full_desktop = false;

    if (!desktop || (!process_id && !thread_id)) return false;
    task_preempt_disable();
    window = desktop->first_window;
    while (window) {
        if ((process_id && window->owner_pid == process_id) ||
            (thread_id && (window->content_pid == thread_id ||
                           window->event_pid == thread_id))) {
            window->dirty = true;
            found = true;
        }
        window = window->next;
    }
    program = desktop->first_program;
    while (program) {
        if (thread_id && program->callback_pid == thread_id) {
            program->paint_requested = true;
            found = true;
            full_desktop = true;
        }
        program = program->next;
    }
    task_preempt_enable();

    /* Los gui_program históricos (principalmente screensavers) no poseen una
     * ventana que delimite su dibujo. Sólo ellos requieren invalidación total.
     * Las aplicaciones modernas quedan limitadas a sus propias ventanas. */
    if (full_desktop) gui_desktop_invalidate_all(desktop);
    return found;
}

void gui_desktop_paint_programs(gui_desktop_t *desktop) {
    gui_program_t *program;

    if (!desktop) return;
    program = desktop->first_program;
    while (program) {
        if (program->paint && program->callback_pid) {
            gui_surface_t *staging = NULL;
            gui_program_composite_paint(program, &desktop->surface);
            if (program->paint_requested && !program->paint_pending &&
                gui_program_prepare_paint(program, &desktop->surface,
                                          &staging)) {
                uint32_t arguments[3] = {
                    (uint32_t)(uintptr_t)program,
                    (uint32_t)(uintptr_t)desktop,
                    (uint32_t)(uintptr_t)staging};
                program->paint_requested = false;
                if (task_queue_user_upcall(program->callback_pid,
                        (uint32_t)(uintptr_t)program->paint, arguments, 3,
                        NULL, 0, -3)) {
                    program->paint_pending = true;
                } else {
                    program->paint_requested = true;
                }
            }
        } else if (program->paint) {
            program->paint(program, desktop, &desktop->surface);
        }
        program = program->next;
    }
}

void gui_desktop_paint(gui_desktop_t *desktop) {
    gui_desktop_paint_lock();
    gui_compositor_paint(desktop);
    gui_desktop_paint_unlock();
}

bool gui_desktop_has_dirty(const gui_desktop_t *desktop) {
    const gui_window_t *window;

    if (!desktop) return false;
    if (desktop->dirty_valid) return true;
    window = desktop->first_window;
    while (window) {
        if (window->dirty) return true;
        window = window->next;
    }
    return false;
}

void gui_desktop_invalidate_rect(gui_desktop_t *desktop, gui_rect_t rect) {
    gui_rect_t screen;
    gui_rect_t clipped;

    if (!desktop || rect.w <= 0 || rect.h <= 0) return;
    screen = (gui_rect_t){0, 0, desktop->surface.width,
                          desktop->surface.height};
    if (!gui_rect_intersect(screen, rect, &clipped)) return;
    desktop->dirty_generation++;
    if (desktop->dirty_valid)
        desktop->dirty_rect = gui_rect_union(desktop->dirty_rect, clipped);
    else {
        desktop->dirty_rect = clipped;
        desktop->dirty_valid = true;
    }
}

void gui_desktop_invalidate_all(gui_desktop_t *desktop) {
    if (!desktop) return;
    gui_desktop_invalidate_rect(desktop, (gui_rect_t){0, 0,
        desktop->surface.width, desktop->surface.height});
}

void gui_desktop_reflow(gui_desktop_t *desktop) {
    gui_window_t *window;
    int max_h;

    if (!desktop) return;

    if (desktop->mouse_x >= desktop->surface.width)
        desktop->mouse_x = desktop->surface.width > 0
                         ? desktop->surface.width - 1 : 0;
    if (desktop->mouse_y >= desktop->surface.height)
        desktop->mouse_y = desktop->surface.height > 0
                         ? desktop->surface.height - 1 : 0;

    max_h = desktop->surface.height - 28;
    if (max_h < 64) max_h = desktop->surface.height;

    task_preempt_disable();
    window = desktop->first_window;
    while (window) {
        if (window->bounds.w > desktop->surface.width)
            window->bounds.w = desktop->surface.width;
        if (window->bounds.h > max_h)
            window->bounds.h = max_h;
        if (window->bounds.w < window->min_w)
            window->bounds.w = window->min_w;
        if (window->bounds.h < window->min_h)
            window->bounds.h = window->min_h;
        if (window->bounds.w > desktop->surface.width)
            window->bounds.w = desktop->surface.width;
        if (window->bounds.h > max_h)
            window->bounds.h = max_h;
        if (window->bounds.x + window->bounds.w > desktop->surface.width)
            window->bounds.x = desktop->surface.width - window->bounds.w;
        if (window->bounds.y + window->bounds.h > max_h)
            window->bounds.y = max_h - window->bounds.h;
        if (window->bounds.x < 0) window->bounds.x = 0;
        if (window->bounds.y < 0) window->bounds.y = 0;
        window->dirty = true;
        window = window->next;
    }
    task_preempt_enable();
}

void gui_desktop_reset_after_mode_change(gui_desktop_t *desktop) {
    gui_window_t *window;
    gui_program_t *program;

    if (!desktop) return;
    /* The old cursor backing belongs to the previous surface dimensions. */
    desktop->cursor_valid = false;
    desktop->cursor_trail_count = 0U;
    desktop->cursor_paint_count = 0U;
    desktop->paint_valid = false;
    desktop->dirty_valid = false;
    desktop->drag_window = NULL;
    desktop->resize_window = NULL;
    desktop->drag_outline_visible = false;

    task_preempt_disable();
    window = desktop->first_window;
    while (window) {
        /* A callback may already be queued with a pointer to its borrowed
           staging view. Keep that buffer alive until the callback completes;
           finish_content_paint() rejects it if reflow changed the client rect.
           Mark a follow-up repaint so the new geometry is produced afterward. */
        window->content_repaint = true;
        window->paint_bounds_valid = false;
        window->dirty = true;
        window = window->next;
    }
    program = desktop->first_program;
    while (program) {
        program->paint_requested = true;
        /* Do not clear paint_pending: an already queued callback still owns
           its staging pointer. Existing caches are retained until the normal
           prepare path can resize them, but never composited across modes. */
        if (program->paint_cache.width != desktop->surface.width ||
            program->paint_cache.height != desktop->surface.height ||
            program->paint_cache.pitch != desktop->surface.pitch)
            program->paint_ready = false;
        program = program->next;
    }
    task_preempt_enable();

    gui_desktop_invalidate_all(desktop);
    gui_request_paint();
}

void gui_desktop_set_cursor_trail(gui_desktop_t *desktop, bool enabled) {
    if (!desktop) return;
    if (desktop->cursor_trail_enabled == enabled) return;
    desktop->cursor_trail_enabled = enabled;
    gui_desktop_clear_cursor_trail(desktop);
    gui_desktop_invalidate_all(desktop);
    gui_request_paint();
}

bool gui_desktop_cursor_trail_enabled(const gui_desktop_t *desktop) {
    return desktop && desktop->cursor_trail_enabled;
}


bool gui_desktop_set_cursor_image(gui_desktop_t *desktop,
                                  const uint32_t *pixels,
                                  uint16_t width, uint16_t height,
                                  int hotspot_x, int hotspot_y,
                                  uint32_t owner_pid) {
    uint32_t row;
    uint32_t col;
    if (!desktop || !pixels || !width || !height ||
        width > GUI_CURSOR_WIDTH || height > GUI_CURSOR_HEIGHT ||
        hotspot_x < 0 || hotspot_y < 0 || hotspot_x >= (int)width ||
        hotspot_y >= (int)height || !owner_pid) return false;
    for (row = 0U; row < GUI_CURSOR_HEIGHT; row++)
        for (col = 0U; col < GUI_CURSOR_WIDTH; col++)
            desktop->cursor_pixels[row * GUI_CURSOR_WIDTH + col] =
                row < height && col < width ? pixels[row * width + col] : 0U;
    desktop->cursor_width = width;
    desktop->cursor_height = height;
    desktop->cursor_hotspot_x = (int16_t)hotspot_x;
    desktop->cursor_hotspot_y = (int16_t)hotspot_y;
    desktop->cursor_owner_pid = owner_pid;
    desktop->cursor_custom = true;
    desktop->cursor_valid = false;
    gui_desktop_clear_cursor_trail(desktop);
    gui_desktop_invalidate_all(desktop);
    gui_request_paint();
    return true;
}

void gui_desktop_reset_cursor(gui_desktop_t *desktop, uint32_t owner_pid) {
    if (!desktop || !desktop->cursor_custom) return;
    if (owner_pid && desktop->cursor_owner_pid != owner_pid) return;
    desktop->cursor_custom = false;
    desktop->cursor_width = 0U;
    desktop->cursor_height = 0U;
    desktop->cursor_hotspot_x = 0;
    desktop->cursor_hotspot_y = 0;
    desktop->cursor_owner_pid = 0U;
    desktop->cursor_valid = false;
    gui_desktop_clear_cursor_trail(desktop);
    gui_desktop_invalidate_all(desktop);
    gui_request_paint();
}

void gui_desktop_set_drag_outline(gui_desktop_t *desktop, bool enabled) {
    if (!desktop) return;
    desktop->drag_outline_enabled = enabled;
    if (!enabled) desktop->drag_outline_visible = false;
}

bool gui_desktop_drag_outline_enabled(const gui_desktop_t *desktop) {
    return desktop && desktop->drag_outline_enabled;
}
