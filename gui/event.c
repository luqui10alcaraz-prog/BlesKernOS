#include "gui.h"
#include "../kernel/include/keyboard.h"
#include "../kernel/include/mouse.h"
#include "../kernel/include/task.h"

static bool queue_full(gui_event_queue_t *queue) {
    return ((queue->head + 1) % GUI_MAX_EVENTS) == queue->tail;
}

static bool queue_empty(gui_event_queue_t *queue) {
    return queue->head == queue->tail;
}

static void push_event(gui_event_queue_t *queue, gui_event_t event) {
    if (!queue || queue_full(queue)) return;
    queue->queue[queue->head] = event;
    queue->head = (queue->head + 1) % GUI_MAX_EVENTS;
}

void gui_event_queue_reset(gui_event_queue_t *queue) {
    if (!queue) return;
    task_preempt_disable();
    queue->head = 0;
    queue->tail = 0;
    task_preempt_enable();
}

bool gui_event_queue_push(gui_event_queue_t *queue, const gui_event_t *event) {
    bool pushed = false;
    if (!queue || !event) return false;
    task_preempt_disable();
    if (!queue_full(queue)) {
        queue->queue[queue->head] = *event;
        queue->head = (queue->head + 1) % GUI_MAX_EVENTS;
        pushed = true;
    }
    task_preempt_enable();
    return pushed;
}

bool gui_event_queue_pop(gui_event_queue_t *queue, gui_event_t *event) {
    bool ok = false;
    if (!queue || !event) return false;
    task_preempt_disable();
    if (!queue_empty(queue)) {
        *event = queue->queue[queue->tail];
        queue->tail = (queue->tail + 1) % GUI_MAX_EVENTS;
        ok = true;
    }
    task_preempt_enable();
    return ok;
}

void gui_event_init(gui_event_queue_t *queue) {
    mouse_state_t mouse;
    if (!queue) return;
    queue->head = 0;
    queue->tail = 0;
    mouse_get_state(&mouse);
    queue->last_mouse_buttons = mouse.buttons;
    queue->last_mouse_x = mouse.x;
    queue->last_mouse_y = mouse.y;
    queue->last_mouse_wheel = mouse.wheel;
}

void gui_event_poll(gui_event_queue_t *queue) {
    mouse_state_t mouse;
    kbd_modifiers_t mods;
    kbd_key_event_t key_event;
    uint8_t changed;

    if (!queue) return;
    mouse_get_state(&mouse);
    changed = (uint8_t)(mouse.buttons ^ queue->last_mouse_buttons);

    if (mouse.x != queue->last_mouse_x || mouse.y != queue->last_mouse_y) {
        push_event(queue, (gui_event_t){
            .type = GUI_EVENT_MOUSE_MOVE, .x = mouse.x, .y = mouse.y,
            .dx = mouse.x - queue->last_mouse_x,
            .dy = mouse.y - queue->last_mouse_y, .buttons = mouse.buttons
        });
    }

    if (mouse.wheel != queue->last_mouse_wheel) {
        push_event(queue, (gui_event_t){
            .type = GUI_EVENT_MOUSE_WHEEL, .x = mouse.x, .y = mouse.y,
            .dy = mouse.wheel - queue->last_mouse_wheel,
            .buttons = mouse.buttons
        });
    }

    if (changed & MOUSE_LEFT_BUTTON) {
        push_event(queue, (gui_event_t){
            .type = (mouse.buttons & MOUSE_LEFT_BUTTON)
                ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP,
            .x = mouse.x, .y = mouse.y, .buttons = mouse.buttons,
            .button = MOUSE_LEFT_BUTTON
        });
    }
    if (changed & MOUSE_RIGHT_BUTTON) {
        push_event(queue, (gui_event_t){
            .type = (mouse.buttons & MOUSE_RIGHT_BUTTON)
                ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP,
            .x = mouse.x, .y = mouse.y, .buttons = mouse.buttons,
            .button = MOUSE_RIGHT_BUTTON
        });
    }

    while (kbd_next_event(&key_event)) {
        if (!key_event.pressed) continue;
        kbd_get_modifiers(&mods);
        push_event(queue, (gui_event_t){GUI_EVENT_KEY, mouse.x, mouse.y, 0, 0,
                   mouse.buttons, 0, key_event.key,
                   mods.shift, mods.ctrl, mods.alt});
    }

    queue->last_mouse_buttons = mouse.buttons;
    queue->last_mouse_x = mouse.x;
    queue->last_mouse_y = mouse.y;
    queue->last_mouse_wheel = mouse.wheel;
}

bool gui_event_next(gui_event_queue_t *queue, gui_event_t *event) {
    return gui_event_queue_pop(queue, event);
}
