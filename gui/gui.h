#ifndef GUI_H
#define GUI_H

#include "../kernel/include/types.h"

#define GUI_TITLEBAR_HEIGHT 20
#define GUI_BORDER_SIZE      2
#define GUI_MAX_EVENTS      32
#define GUI_MAX_MENUS        5
#define GUI_MAX_MENU_ITEMS   8
#define GUI_MENU_HEIGHT     18

typedef enum {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_MOUSE_MOVE,
    GUI_EVENT_MOUSE_DOWN,
    GUI_EVENT_MOUSE_UP,
    GUI_EVENT_KEY,
} gui_event_type_t;

typedef enum {
    GUI_WIDGET_LABEL = 1,
    GUI_WIDGET_BUTTON = 2,
} gui_widget_type_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} gui_rect_t;

typedef struct {
    uint32_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
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
    gui_menu_callback_t callback;
    void *context;
} gui_menu_item_t;

typedef struct {
    char label[16];
    gui_menu_item_t items[GUI_MAX_MENU_ITEMS];
    uint8_t item_count;
} gui_menu_t;

typedef struct gui_widget {
    uint32_t id;
    gui_widget_type_t type;
    gui_rect_t bounds;
    char text[48];
    bool hovered;
    bool pressed;
    bool visible;
    gui_widget_callback_t callback;
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
    gui_widget_t *widgets;
    gui_window_content_paint_t content_paint;
    void *content_context;
    gui_window_event_t event_handler;
    void *event_context;
    uint32_t owner_pid;
    gui_menu_t menus[GUI_MAX_MENUS];
    uint8_t menu_count;
    int8_t open_menu;
    int8_t pressed_menu_item;
    struct gui_window *prev;
    struct gui_window *next;
} gui_window_t;

struct gui_program {
    uint32_t id;
    char name[32];
    void *state;
    gui_program_paint_t paint;
    gui_program_event_t handle_event;
    gui_program_destroy_t destroy;
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
};

typedef struct {
    gui_event_t queue[GUI_MAX_EVENTS];
    uint8_t head;
    uint8_t tail;
    uint8_t last_mouse_buttons;
    int last_mouse_x;
    int last_mouse_y;
} gui_event_queue_t;

void gui_init(void);
void gui_run(void);
uint8_t gui_get_cpu_usage(void);

bool gui_gfx_init(gui_surface_t *surface);
bool gui_gfx_reconfigure(gui_surface_t *surface);
void gui_gfx_shutdown(gui_surface_t *surface);
void gui_gfx_present(const gui_surface_t *surface);
void gui_gfx_clear(gui_surface_t *surface, uint32_t color);
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
gui_window_t *gui_desktop_window_at(gui_desktop_t *desktop, int x, int y);
void gui_desktop_handle_event(gui_desktop_t *desktop, const gui_event_t *event);
void gui_desktop_paint(gui_desktop_t *desktop);
void gui_desktop_paint_programs(gui_desktop_t *desktop);
bool gui_desktop_has_dirty(const gui_desktop_t *desktop);
void gui_desktop_reflow(gui_desktop_t *desktop);

bool gui_change_resolution(gui_desktop_t *desktop, uint16_t width,
                           uint16_t height);

gui_window_t *gui_window_create(gui_desktop_t *desktop, int x, int y, int w, int h, const char *title);
void gui_window_destroy(gui_window_t *window);
void gui_window_paint(gui_surface_t *surface, gui_window_t *window, gui_rect_t clip);
bool gui_window_contains(gui_window_t *window, int x, int y);
bool gui_window_titlebar_contains(gui_window_t *window, int x, int y);
gui_rect_t gui_window_minimize_button_rect(gui_window_t *window);
gui_rect_t gui_window_close_button_rect(gui_window_t *window);
gui_window_button_t gui_window_titlebar_button_at(gui_window_t *window, int x, int y);
void gui_window_minimize(gui_window_t *window);
void gui_window_close(gui_window_t *window);
void gui_window_restore(gui_window_t *window);
void gui_window_set_min_size(gui_window_t *window, int min_w, int min_h);
void gui_window_set_content(gui_window_t *window,
                            gui_window_content_paint_t paint,
                            void *context);
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
int gui_window_content_top(const gui_window_t *window);

gui_widget_t *gui_widget_create(gui_desktop_t *desktop, gui_window_t *window, gui_widget_type_t type, gui_rect_t bounds, const char *text, gui_widget_callback_t callback);
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

#endif
