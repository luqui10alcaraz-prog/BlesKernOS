#include "gui.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/task.h"
#include "../programs/programs.h"

static void copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    kstrncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
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

    program->id = desktop->next_program_id++;
    program->state = state;
    program->paint = paint;
    program->handle_event = handle_event;
    program->destroy = destroy;
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

    if (program->destroy) program->destroy(program);
    kfree(program);
}

gui_window_t *gui_desktop_create_window(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title) {
    gui_window_t *window = gui_window_create(desktop, x, y, w, h, title);
    if (!window) return NULL;
    gui_desktop_add_window(desktop, window);
    return window;
}

void gui_desktop_focus_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!desktop) return;
    task_preempt_disable();
    if (desktop->focused_window) desktop->focused_window->focused = false;
    desktop->focused_window = window;
    if (window) window->focused = true;
    task_preempt_enable();
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
    gui_desktop_focus_window(desktop, window);
}

void gui_desktop_raise_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!desktop || !window || desktop->last_window == window) return;
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
    task_preempt_enable();
}

void gui_desktop_remove_window(gui_desktop_t *desktop, gui_window_t *window) {
    if (!desktop || !window) return;
    task_preempt_disable();

    if (window->prev) window->prev->next = window->next;
    if (window->next) window->next->prev = window->prev;
    if (desktop->first_window == window) desktop->first_window = window->next;
    if (desktop->last_window == window) desktop->last_window = window->prev;
    if (desktop->focused_window == window) desktop->focused_window = NULL;
    if (desktop->drag_window == window) desktop->drag_window = NULL;
    if (desktop->resize_window == window) desktop->resize_window = NULL;

    window->prev = NULL;
    window->next = NULL;
    task_preempt_enable();
}

gui_window_t *gui_desktop_window_at(gui_desktop_t *desktop, int x, int y) {
    gui_window_t *window;
    if (!desktop) return NULL;
    task_preempt_disable();
    window = desktop->last_window;
    while (window) {
        if (gui_window_contains(window, x, y)) {
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

    if (!desktop || !event) return;

    if (event->type == GUI_EVENT_MOUSE_MOVE || event->type == GUI_EVENT_MOUSE_DOWN || event->type == GUI_EVENT_MOUSE_UP) {
        desktop->mouse_x = event->x;
        desktop->mouse_y = event->y;
        desktop->mouse_buttons = event->buttons;
    }

    /* Screensavers must close before normal windows/apps consume the input.
       Without this, SSLOGO can remain active behind the desktop and leave
       black regions while other apps keep receiving events. */
program = desktop->last_program;
    while (program) {
        if (program->handle_event && program->handle_event(program, desktop, event)) {
            return;
        }
        program = program->prev;
    }
}

void gui_desktop_paint_programs(gui_desktop_t *desktop) {
    gui_program_t *program;

    if (!desktop) return;
    program = desktop->first_program;
    while (program) {
        if (program->paint) program->paint(program, desktop, &desktop->surface);
        program = program->next;
    }
}

void gui_desktop_paint(gui_desktop_t *desktop) {
    gui_compositor_paint(desktop);
}

bool gui_desktop_has_dirty(const gui_desktop_t *desktop) {
    const gui_window_t *window;

    if (!desktop) return false;
    window = desktop->first_window;
    while (window) {
        if (window->visible && window->dirty) return true;
        window = window->next;
    }
    return false;
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
