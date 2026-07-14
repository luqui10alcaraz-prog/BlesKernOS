#ifndef GUI_H
#define GUI_H

#include "../kernel/include/types.h"

#define GUI_TITLEBAR_HEIGHT 20
#define GUI_BORDER_SIZE      2
#define GUI_MAX_EVENTS      32
#define GUI_MAX_MENUS        5
#define GUI_MAX_MENU_ITEMS   8
#define GUI_MAX_CONTEXT_ITEMS 10
#define GUI_MENU_HEIGHT     18
#define GUI_CURSOR_TRAIL_MAX 6
#define GUI_SCROLLBAR_SIZE  16

typedef enum {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_MOUSE_MOVE,
    GUI_EVENT_MOUSE_DOWN,
    GUI_EVENT_MOUSE_UP,
    GUI_EVENT_MOUSE_WHEEL,
    GUI_EVENT_KEY,
} gui_event_type_t;

typedef enum {
    GUI_WIDGET_LABEL = 1,
    GUI_WIDGET_BUTTON = 2,
} gui_widget_type_t;

typedef enum {
    GUI_WIDGET_STYLE_BUTTON = 0,
    GUI_WIDGET_STYLE_SELECTABLE = 1,
    GUI_WIDGET_STYLE_LISTBOX = 2,
    GUI_WIDGET_STYLE_DROPDOWN = 3,
} gui_widget_style_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} gui_rect_t;

typedef struct {
    gui_rect_t bounds;
    uint32_t value;
    uint32_t visible;
    uint32_t total;
} gui_scrollbar_t;

typedef struct {
    bool active;
    int grab_offset;
} gui_scrollbar_drag_t;

typedef struct {
    uint32_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    gui_rect_t clip;
} gui_surface_t;

typedef enum {
    GUI_WINDOW_BUTTON_NONE = 0,
    GUI_WINDOW_BUTTON_MINIMIZE,
    GUI_WINDOW_BUTTON_CLOSE,
} gui_window_button_t;

typedef struct {
    gui_event_type_t type;
    int x;
    int y;
    int dx;
    int dy;
    uint8_t buttons;
    uint8_t button;
    char key;
    bool shift;
    bool ctrl;
    bool alt;
} gui_event_t;

struct gui_desktop;
struct gui_program;
struct gui_window;

typedef struct gui_desktop gui_desktop_t;
typedef struct gui_program gui_program_t;
typedef void (*gui_window_content_paint_t)(struct gui_window *window,
                                           gui_surface_t *surface,
                                           void *context);
typedef bool (*gui_window_event_t)(struct gui_window *window,
                                   const gui_event_t *event,
                                   void *context);
typedef void (*gui_widget_callback_t)(struct gui_window *window, uint32_t widget_id);
typedef void (*gui_program_paint_t)(gui_program_t *program, gui_desktop_t *desktop, gui_surface_t *surface);
typedef bool (*gui_program_event_t)(gui_program_t *program, gui_desktop_t *desktop, const gui_event_t *event);
typedef void (*gui_program_destroy_t)(gui_program_t *program);
typedef void (*gui_menu_callback_t)(struct gui_window *window,
                                    uint32_t item_id, void *context);

typedef struct {
    uint32_t id;
    char label[24];
    bool enabled;
    gui_menu_callback_t callback;
    void *context;
    uint32_t callback_pid;
} gui_menu_item_t;

typedef struct {
    char label[16];
    gui_menu_item_t items[GUI_MAX_MENU_ITEMS];
    uint8_t item_count;
} gui_menu_t;

typedef struct {
    gui_menu_item_t items[GUI_MAX_CONTEXT_ITEMS];
    uint8_t item_count;
    bool open;
    int x;
    int y;
    int width;
    int8_t pressed_item;
} gui_context_menu_t;

typedef struct gui_widget {
    uint32_t id;
    gui_widget_type_t type;
    gui_widget_style_t style;
    gui_rect_t bounds;
    char text[48];
    bool hovered;
    bool pressed;
    bool selected;
    bool enabled;
    bool visible;
    gui_widget_callback_t callback;
    uint32_t callback_pid;
    void *payload;
    struct gui_widget *prev;
    struct gui_widget *next;
} gui_widget_t;

typedef struct gui_window {
    uint32_t id;
    gui_rect_t bounds;
    int min_w;
    int min_h;
    char title[48];
    uint32_t bg_color;
    uint32_t title_color;
    uint32_t border_color;
    bool visible;
    bool listed;
    bool minimized;
    bool focused;
    bool dirty;
    bool borderless;
    bool resizable;
    uint8_t drag_height;
    gui_widget_t *widgets;
    gui_window_content_paint_t content_paint;
    void *content_context;
    uint32_t content_pid;
    bool content_pending;
    bool content_ready;
    bool content_repaint;
    uint32_t *content_cache;
    uint16_t content_cache_width;
    uint16_t content_cache_height;
    gui_surface_t content_staging;
    bool content_staging_active;
    gui_rect_t content_staging_rect;
    gui_window_event_t event_handler;
    void *event_context;
    uint32_t event_pid;
    uint32_t owner_pid;
    gui_menu_t menus[GUI_MAX_MENUS];
    uint8_t menu_count;
    int8_t open_menu;
    int8_t pressed_menu_item;
    gui_context_menu_t context_menu;
    struct gui_window *prev;
    struct gui_window *next;
    bool paint_bounds_valid;
    gui_rect_t paint_bounds;
} gui_window_t;

struct gui_program {
    uint32_t id;
    char name[32];
    void *state;
    gui_program_paint_t paint;
    gui_program_event_t handle_event;
    gui_program_destroy_t destroy;
    uint32_t callback_pid;
    bool paint_pending;
    bool paint_ready;
    gui_surface_t paint_cache;
    gui_surface_t paint_staging;
    gui_program_t *prev;
    gui_program_t *next;
};

struct gui_desktop {
    gui_surface_t surface;
    gui_program_t *first_program;
    gui_program_t *last_program;
    gui_window_t *first_window;
    gui_window_t *last_window;
    gui_window_t *focused_window;
    gui_window_t *drag_window;
    gui_window_t *resize_window;
    gui_rect_t resize_start_bounds;
    uint8_t resize_edges;
    int resize_start_x;
    int resize_start_y;
    int drag_off_x;
    int drag_off_y;
    int mouse_x;
    int mouse_y;
    uint8_t mouse_buttons;
    uint32_t next_program_id;
    uint32_t next_window_id;
    uint32_t next_widget_id;
    bool dirty_valid;
    gui_rect_t dirty_rect;
    uint32_t dirty_generation;
    bool cursor_valid;
    gui_rect_t cursor_rect;
    bool cursor_trail_enabled;
    uint8_t cursor_trail_count;
    int cursor_trail_x[GUI_CURSOR_TRAIL_MAX];
    int cursor_trail_y[GUI_CURSOR_TRAIL_MAX];
    uint8_t cursor_paint_count;
    gui_rect_t cursor_paint_rects[GUI_CURSOR_TRAIL_MAX + 1];
    bool paint_valid;
};

typedef struct {
    gui_event_t queue[GUI_MAX_EVENTS];
    uint8_t head;
    uint8_t tail;
    uint8_t last_mouse_buttons;
    int last_mouse_x;
    int last_mouse_y;
    int32_t last_mouse_wheel;
} gui_event_queue_t;

void gui_init(void);
void gui_run(void);
uint8_t gui_get_cpu_usage(void);
gui_desktop_t *gui_get_desktop(void);
uint32_t gui_get_last_input_tick(void);
void gui_request_paint(void);

bool gui_gfx_init(gui_surface_t *surface);
bool gui_gfx_reconfigure(gui_surface_t *surface);
void gui_gfx_shutdown(gui_surface_t *surface);
void gui_gfx_present(const gui_surface_t *surface);
void gui_gfx_present_rect(const gui_surface_t *surface, gui_rect_t rect);
void gui_gfx_invalidate_front(void);
void gui_gfx_clear(gui_surface_t *surface, uint32_t color);
void gui_gfx_set_clip(gui_surface_t *surface, gui_rect_t clip);
void gui_gfx_reset_clip(gui_surface_t *surface);
gui_rect_t gui_gfx_get_clip(const gui_surface_t *surface);
bool gui_gfx_point_visible(const gui_surface_t *surface, int x, int y);
void gui_gfx_putpixel(gui_surface_t *surface, int x, int y, uint32_t color);
void gui_gfx_fill_rect(gui_surface_t *surface, gui_rect_t rect, uint32_t color);
void gui_gfx_draw_rect(gui_surface_t *surface, gui_rect_t rect, uint32_t color);
void gui_gfx_draw_line(gui_surface_t *surface, int x0, int y0, int x1, int y1, uint32_t color);
void gui_gfx_fill_gradient(gui_surface_t *surface, gui_rect_t rect, uint32_t top, uint32_t bottom);
void gui_gfx_fill_rounded_rect(gui_surface_t *surface, gui_rect_t rect, int radius, uint32_t color);
uint32_t gui_color_blend(uint32_t base, uint32_t top, uint8_t alpha);
uint32_t gui_color_lerp(uint32_t a, uint32_t b, uint8_t t);

void gui_font_draw_char(gui_surface_t *surface, int x, int y, char c, uint32_t fg, uint32_t bg, bool fill_bg);
void gui_font_draw_string(gui_surface_t *surface, int x, int y, const char *text, uint32_t fg, uint32_t bg, bool fill_bg);
void gui_font_draw_string_scaled(gui_surface_t *surface, int x, int y,
                                 const char *text, uint32_t fg, int scale);
void gui_font_draw_string_clipped(gui_surface_t *surface, int x, int y,
                                  const char *text, uint32_t fg,
                                  gui_rect_t clip);
void gui_font_draw_string_scaled_clipped(gui_surface_t *surface, int x, int y,
                                         const char *text, uint32_t fg,
                                         int scale, gui_rect_t clip);
uint16_t gui_font_text_width(const char *text);

void gui_desktop_init(gui_desktop_t *desktop, gui_surface_t surface);
gui_program_t *gui_desktop_register_program(gui_desktop_t *desktop, const char *name, void *state, gui_program_paint_t paint, gui_program_event_t handle_event, gui_program_destroy_t destroy);
void gui_desktop_focus_window(gui_desktop_t *desktop, gui_window_t *window);
gui_window_t *gui_desktop_create_window(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title);
void gui_desktop_add_window(gui_desktop_t *desktop, gui_window_t *window);
void gui_desktop_raise_window(gui_desktop_t *desktop, gui_window_t *window);
void gui_desktop_remove_window(gui_desktop_t *desktop, gui_window_t *window);
void gui_desktop_unregister_program(gui_desktop_t *desktop, gui_program_t *program);
gui_window_t *gui_desktop_window_at(gui_desktop_t *desktop, int x, int y);
void gui_desktop_handle_event(gui_desktop_t *desktop, const gui_event_t *event);
void gui_desktop_paint(gui_desktop_t *desktop);
void gui_desktop_paint_programs(gui_desktop_t *desktop);
bool gui_program_prepare_paint(gui_program_t *program,
                               const gui_surface_t *source,
                               gui_surface_t **staging_out);
void gui_program_composite_paint(const gui_program_t *program,
                                 gui_surface_t *destination);
void gui_program_finish_paint(gui_program_t *program);
void gui_program_release_paint(gui_program_t *program);
bool gui_desktop_has_dirty(const gui_desktop_t *desktop);
void gui_desktop_invalidate_rect(gui_desktop_t *desktop, gui_rect_t rect);
void gui_desktop_invalidate_all(gui_desktop_t *desktop);
void gui_desktop_reflow(gui_desktop_t *desktop);
void gui_desktop_set_cursor_trail(gui_desktop_t *desktop, bool enabled);
bool gui_desktop_cursor_trail_enabled(const gui_desktop_t *desktop);

bool gui_change_resolution(gui_desktop_t *desktop, uint16_t width,
                           uint16_t height, uint8_t bpp);

gui_window_t *gui_window_create(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title);
void gui_window_destroy(gui_window_t *window);
void gui_window_paint(gui_surface_t *surface, gui_window_t *window, gui_rect_t clip);
void gui_window_paint_widgets(gui_surface_t *surface, gui_window_t *window,
                              gui_rect_t clip);
bool gui_window_contains(gui_window_t *window, int x, int y);
bool gui_window_titlebar_contains(gui_window_t *window, int x, int y);
gui_rect_t gui_window_minimize_button_rect(gui_window_t *window);
gui_rect_t gui_window_close_button_rect(gui_window_t *window);
gui_window_button_t gui_window_titlebar_button_at(gui_window_t *window, int x, int y);
void gui_window_minimize(gui_window_t *window);
void gui_window_close(gui_window_t *window);
void gui_window_restore(gui_window_t *window);
void gui_window_set_min_size(gui_window_t *window, int min_w, int min_h);
void gui_window_set_borderless(gui_window_t *window, bool borderless,
                               uint8_t drag_height);
void gui_window_set_content(gui_window_t *window,
                            gui_window_content_paint_t paint,
                            void *context);
bool gui_window_capture_content(gui_window_t *window,
                                const gui_surface_t *surface);
bool gui_window_begin_content_paint(gui_window_t *window,
                                    const gui_surface_t *source,
                                    gui_surface_t **staging_out);
void gui_window_end_content_paint(gui_window_t *window);
void gui_window_paint_cached_content(gui_surface_t *surface,
                                     const gui_window_t *window,
                                     gui_rect_t clip);
void gui_window_set_event_handler(gui_window_t *window,
                                  gui_window_event_t handler,
                                  void *context);
bool gui_window_dispatch_event(gui_window_t *window,
                               const gui_event_t *event);
int gui_window_add_menu(gui_window_t *window, const char *label);
bool gui_window_add_menu_item(gui_window_t *window, int menu,
                              uint32_t id, const char *label,
                              gui_menu_callback_t callback, void *context);
void gui_window_paint_menus(gui_surface_t *surface, gui_window_t *window);
bool gui_window_handle_menu_event(gui_window_t *window,
                                  const gui_event_t *event);
void gui_window_context_clear(gui_window_t *window);
bool gui_window_context_add_item(gui_window_t *window, uint32_t id,
                                 const char *label, bool enabled,
                                 gui_menu_callback_t callback, void *context);
void gui_window_context_open(gui_window_t *window, int x, int y);
void gui_window_context_close(gui_window_t *window);
void gui_context_menu_clear(gui_context_menu_t *menu);
bool gui_context_menu_add_item(gui_context_menu_t *menu, uint32_t id,
                               const char *label, bool enabled,
                               gui_menu_callback_t callback, void *context);
void gui_context_menu_open(gui_context_menu_t *menu, int x, int y,
                           gui_rect_t limits);
void gui_context_menu_close(gui_context_menu_t *menu);
void gui_context_menu_paint(gui_surface_t *surface,
                            const gui_context_menu_t *menu);
bool gui_context_menu_handle_event(gui_context_menu_t *menu,
                                   gui_window_t *callback_window,
                                   const gui_event_t *event);
int gui_window_content_top(const gui_window_t *window);
gui_rect_t gui_window_content_rect(const gui_window_t *window);
gui_rect_t gui_window_content_rect_inset(const gui_window_t *window, int inset);
gui_rect_t gui_window_clamp_local_rect(const gui_window_t *window,
                                       gui_rect_t rect);

gui_widget_t *gui_widget_create(gui_desktop_t *desktop, gui_window_t *window, gui_widget_type_t type, gui_rect_t bounds, const char *text, gui_widget_callback_t callback);
gui_widget_t *gui_widget_create_button(gui_desktop_t *desktop,
                                       gui_window_t *window,
                                       gui_rect_t bounds,
                                       const char *text,
                                       gui_widget_callback_t callback);
gui_widget_t *gui_widget_create_selectable_button(gui_desktop_t *desktop,
                                                  gui_window_t *window,
                                                  gui_rect_t bounds,
                                                  const char *text,
                                                  gui_widget_callback_t callback);
gui_widget_t *gui_widget_create_listbox(gui_desktop_t *desktop,
                                        gui_window_t *window,
                                        gui_rect_t bounds,
                                        const char *text);
gui_widget_t *gui_widget_create_dropdown(gui_desktop_t *desktop,
                                         gui_window_t *window,
                                         gui_rect_t bounds,
                                         gui_widget_callback_t callback);
gui_rect_t gui_widget_screen_bounds(const gui_window_t *window,
                                    const gui_widget_t *widget);
void gui_widget_destroy(gui_widget_t *widget);
void gui_widget_set_style(gui_widget_t *widget, gui_widget_style_t style);
void gui_widget_set_selected(gui_widget_t *widget, bool selected);
void gui_widget_set_enabled(gui_widget_t *widget, bool enabled);
void gui_widget_dropdown_clear(gui_widget_t *widget);
bool gui_widget_dropdown_add_item(gui_widget_t *widget,
                                  const char *label,
                                  const char *value);
int gui_widget_dropdown_get_selected(const gui_widget_t *widget);
void gui_widget_dropdown_set_selected(gui_widget_t *widget, int index);
bool gui_widget_dropdown_set_selected_by_value(gui_widget_t *widget,
                                               const char *value);
const char *gui_widget_dropdown_get_selected_label(const gui_widget_t *widget);
const char *gui_widget_dropdown_get_selected_value(const gui_widget_t *widget);
const char *gui_widget_dropdown_get_item_label(const gui_widget_t *widget,
                                               int index);
const char *gui_widget_dropdown_get_item_value(const gui_widget_t *widget,
                                               int index);
bool gui_widget_is_dropdown_expanded(const gui_widget_t *widget);
void gui_widget_paint(gui_surface_t *surface, gui_window_t *window, gui_widget_t *widget, gui_rect_t clip);
bool gui_widget_handle_event(gui_window_t *window, gui_widget_t *widget, const gui_event_t *event);

void gui_compositor_paint(gui_desktop_t *desktop);

void gui_event_init(gui_event_queue_t *queue);
void gui_event_poll(gui_event_queue_t *queue);
bool gui_event_next(gui_event_queue_t *queue, gui_event_t *event);
void gui_event_queue_reset(gui_event_queue_t *queue);
bool gui_event_queue_push(gui_event_queue_t *queue, const gui_event_t *event);
bool gui_event_queue_pop(gui_event_queue_t *queue, gui_event_t *event);

bool gui_rect_intersect(gui_rect_t a, gui_rect_t b, gui_rect_t *out);
bool gui_rect_contains(gui_rect_t rect, int x, int y);
gui_rect_t gui_rect_union(gui_rect_t a, gui_rect_t b);

void gui_scrollbar_init_vertical(gui_scrollbar_t *bar, gui_rect_t bounds,
                                 uint32_t value, uint32_t visible,
                                 uint32_t total);
gui_rect_t gui_scrollbar_thumb_rect(const gui_scrollbar_t *bar);
void gui_scrollbar_paint_vertical(gui_surface_t *surface,
                                  const gui_scrollbar_t *bar);
bool gui_scrollbar_handle_click_vertical(const gui_scrollbar_t *bar,
                                         int x, int y,
                                         uint32_t *new_value);
bool gui_scrollbar_handle_event_vertical(const gui_scrollbar_t *bar,
                                         gui_scrollbar_drag_t *drag,
                                         const gui_event_t *event,
                                         uint32_t wheel_step,
                                         uint32_t *new_value);

#endif
