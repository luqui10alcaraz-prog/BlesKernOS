#include "../kernel/include/api.h"

#define VIEWER_TOOLBAR_H   30
#define VIEWER_STATUS_H    18
#define VIEWER_MARGIN       6
#define VIEWER_MIN_ZOOM    10
#define VIEWER_MAX_ZOOM   800
#define VIEWER_ZOOM_STEP   25

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_gif_animation_t gif;
    gui_image_t bmp;
    bool animated;
    bool loaded;
    bool playing;
    bool fit_to_window;
    bool dragging_pan;
    uint16_t frame;
    uint16_t zoom_pct;
    uint32_t next_frame;
    int pan_x;
    int pan_y;
    int drag_last_x;
    int drag_last_y;
    uint16_t image_count;
    int16_t image_index;
    uint32_t prev_id;
    uint32_t next_id;
    uint32_t zoom_out_id;
    uint32_t zoom_in_id;
    uint32_t fit_id;
    uint32_t actual_id;
    uint32_t play_id;
    char path[VFS_MAX_PATH];
    char directory[VFS_MAX_PATH];
    char image_names[VFS_MAX_DIR_ENTRIES][VFS_MAX_NAME];
} viewer_state_t;

static viewer_state_t *g_viewer;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool viewer_load_bmp(gui_image_t *image, const char *path) {
    void *raw = NULL;
    uint32_t size = 0, offset, stride;
    int width, raw_height, height;
    uint16_t bpp;
    uint8_t *data;

    if (!bk_file_read_all(path, &raw, &size) || !raw || size < 54) return false;
    data = (uint8_t *)raw;
    offset = rd32(data + 10);
    width = (int)rd32(data + 18);
    raw_height = (int)rd32(data + 22);
    height = raw_height < 0 ? -raw_height : raw_height;
    bpp = rd16(data + 28);
    if (data[0] != 'B' || data[1] != 'M' || width <= 0 || height <= 0 ||
        width > 1600 || height > 1200 || (bpp != 24 && bpp != 32) ||
        rd32(data + 30) != 0) {
        bk_sys_free(raw);
        return false;
    }
    stride = ((uint32_t)width * (bpp / 8) + 3U) & ~3U;
    if (offset >= size || offset + stride * (uint32_t)height > size) {
        bk_sys_free(raw);
        return false;
    }
    image->pixels = (uint32_t *)bk_sys_alloc((uint32_t)width * height * 4U);
    if (!image->pixels) {
        bk_sys_free(raw);
        return false;
    }
    image->width = (uint16_t)width;
    image->height = (uint16_t)height;
    for (int y = 0; y < height; y++) {
        int sy = raw_height > 0 ? height - 1 - y : y;
        for (int x = 0; x < width; x++) {
            uint8_t *p = data + offset + (uint32_t)sy * stride +
                         (uint32_t)x * (bpp / 8);
            image->pixels[(uint32_t)y * width + x] =
                0xFF000000U | ((uint32_t)p[2] << 16) |
                ((uint32_t)p[1] << 8) | p[0];
        }
    }
    bk_sys_free(raw);
    return true;
}

static bool ends_with(const char *s, const char *upper, const char *lower) {
    const char *dot = NULL;
    while (s && *s) {
        if (*s == '.') dot = s;
        s++;
    }
    return dot && (bk_runtime_strcmp(dot, upper) == 0 || bk_runtime_strcmp(dot, lower) == 0);
}

static bool viewer_is_image_name(const char *name) {
    return ends_with(name, ".BMP", ".bmp") || ends_with(name, ".GIF", ".gif");
}

static gui_image_t *viewer_current_image(viewer_state_t *st) {
    if (!st || !st->loaded) return NULL;
    return st->animated ? &st->gif.frames[st->frame] : &st->bmp;
}

static void viewer_append_char(char *text, size_t capacity, size_t *length,
                               char c) {
    if (!text || !length || *length + 1 >= capacity) return;
    text[*length] = c;
    (*length)++;
    text[*length] = '\0';
}

static void viewer_append_uint(char *text, size_t capacity, size_t *length,
                               uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (!text || !length) return;
    if (value == 0) {
        viewer_append_char(text, capacity, length, '0');
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        viewer_append_char(text, capacity, length, digits[--count]);
    }
}

static gui_widget_t *viewer_find_widget(viewer_state_t *st, uint32_t id) {
    gui_widget_t *widget;

    if (!st || !st->window || id == 0) return NULL;
    widget = st->window->widgets;
    while (widget) {
        if (widget->id == id) return widget;
        widget = widget->next;
    }
    return NULL;
}

static void viewer_split_path(const char *path, char *directory,
                              char *name) {
    const char *slash = NULL;
    const char *cursor = path;

    if (directory) {
        directory[0] = '/';
        directory[1] = '\0';
    }
    if (name) name[0] = '\0';
    if (!path || !*path) return;

    while (*cursor) {
        if (*cursor == '/') slash = cursor;
        cursor++;
    }
    if (!slash) {
        if (name) {
            bk_runtime_strncpy(name, path, VFS_MAX_NAME - 1);
            name[VFS_MAX_NAME - 1] = '\0';
        }
        return;
    }
    if (directory) {
        size_t dir_len = (size_t)(slash - path);
        if (dir_len == 0) {
            directory[0] = '/';
            directory[1] = '\0';
        } else {
            if (dir_len >= VFS_MAX_PATH) dir_len = VFS_MAX_PATH - 1;
            bk_runtime_memcpy(directory, path, dir_len);
            directory[dir_len] = '\0';
        }
    }
    if (name) {
        bk_runtime_strncpy(name, slash + 1, VFS_MAX_NAME - 1);
        name[VFS_MAX_NAME - 1] = '\0';
    }
}

static void viewer_join_path(char *out, const char *directory,
                             const char *name) {
    size_t len;

    if (!out) return;
    bk_runtime_strncpy(out, directory && *directory ? directory : "/", VFS_MAX_PATH - 1);
    out[VFS_MAX_PATH - 1] = '\0';
    len = bk_runtime_strlen(out);
    if (len > 1 && out[len - 1] != '/' && len + 1 < VFS_MAX_PATH) {
        out[len++] = '/';
        out[len] = '\0';
    }
    bk_runtime_strncpy(out + len, name ? name : "", VFS_MAX_PATH - len - 1);
}

static void viewer_update_title(viewer_state_t *st) {
    char name[VFS_MAX_NAME];
    size_t used;

    if (!st || !st->window) return;
    viewer_split_path(st->path, NULL, name);
    bk_runtime_strncpy(st->window->title, "Visor: ", sizeof(st->window->title) - 1);
    st->window->title[sizeof(st->window->title) - 1] = '\0';
    used = bk_runtime_strlen(st->window->title);
    bk_runtime_strncpy(st->window->title + used, name,
             sizeof(st->window->title) - used - 1);
    st->window->title[sizeof(st->window->title) - 1] = '\0';
    st->window->dirty = true;
}

static void viewer_refresh_gallery(viewer_state_t *st) {
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint32_t count = 0;
    char current_name[VFS_MAX_NAME];

    if (!st) return;
    st->image_count = 0;
    st->image_index = -1;
    viewer_split_path(st->path, st->directory, current_name);
    if (!bk_file_list_dir(st->directory, entries, VFS_MAX_DIR_ENTRIES, &count))
        return;

    for (uint32_t i = 0; i < count && st->image_count < VFS_MAX_DIR_ENTRIES;
         i++) {
        if (entries[i].type != VFS_NODE_FILE ||
            !viewer_is_image_name(entries[i].name))
            continue;
        bk_runtime_strncpy(st->image_names[st->image_count], entries[i].name,
                 VFS_MAX_NAME - 1);
        st->image_names[st->image_count][VFS_MAX_NAME - 1] = '\0';
        if (bk_runtime_strcmp(entries[i].name, current_name) == 0)
            st->image_index = (int16_t)st->image_count;
        st->image_count++;
    }
}

static void viewer_reset_view(viewer_state_t *st, bool fit_to_window) {
    if (!st) return;
    st->fit_to_window = fit_to_window;
    st->zoom_pct = 100;
    st->pan_x = 0;
    st->pan_y = 0;
    st->dragging_pan = false;
}

static gui_rect_t viewer_image_area(const viewer_state_t *st) {
    gui_rect_t content = bk_gui_window_content_rect_raw(st->window);
    int x = st->window->bounds.x + VIEWER_MARGIN;
    int y = content.y + VIEWER_TOOLBAR_H;
    int w = st->window->bounds.w - VIEWER_MARGIN * 2;
    int h = content.h - VIEWER_TOOLBAR_H - VIEWER_STATUS_H - VIEWER_MARGIN;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return (gui_rect_t){x, y, w, h};
}

static uint16_t viewer_effective_zoom(const viewer_state_t *st,
                                      const gui_image_t *image) {
    gui_rect_t area;
    uint32_t zoom_x;
    uint32_t zoom_y;
    uint32_t zoom;

    if (!st || !image || !image->width || !image->height) return 100;
    if (!st->fit_to_window) return st->zoom_pct;

    area = viewer_image_area(st);
    zoom_x = ((uint32_t)area.w * 100U) / image->width;
    zoom_y = ((uint32_t)area.h * 100U) / image->height;
    zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
    if (zoom == 0) zoom = 1;
    if (zoom > VIEWER_MAX_ZOOM) zoom = VIEWER_MAX_ZOOM;
    return (uint16_t)zoom;
}

static void viewer_clamp_pan(viewer_state_t *st) {
    gui_image_t *image;
    gui_rect_t area;
    int draw_w;
    int draw_h;
    int overflow_x;
    int overflow_y;
    int max_pan_x;
    int max_pan_y;

    if (!st) return;
    image = viewer_current_image(st);
    if (!image || st->fit_to_window) {
        st->pan_x = 0;
        st->pan_y = 0;
        return;
    }

    area = viewer_image_area(st);
    draw_w = ((int)image->width * st->zoom_pct) / 100;
    draw_h = ((int)image->height * st->zoom_pct) / 100;
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;

    overflow_x = draw_w - area.w;
    overflow_y = draw_h - area.h;
    max_pan_x = overflow_x > 0 ? (overflow_x + 1) / 2 : 0;
    max_pan_y = overflow_y > 0 ? (overflow_y + 1) / 2 : 0;

    if (st->pan_x > max_pan_x) st->pan_x = max_pan_x;
    if (st->pan_x < -max_pan_x) st->pan_x = -max_pan_x;
    if (st->pan_y > max_pan_y) st->pan_y = max_pan_y;
    if (st->pan_y < -max_pan_y) st->pan_y = -max_pan_y;
}

static void viewer_update_controls(viewer_state_t *st) {
    gui_widget_t *widget;

    if (!st || !st->window) return;

    widget = viewer_find_widget(st, st->prev_id);
    if (widget) widget->visible = st->image_count > 1;
    widget = viewer_find_widget(st, st->next_id);
    if (widget) widget->visible = st->image_count > 1;

    widget = viewer_find_widget(st, st->play_id);
    if (widget) {
        widget->visible = st->animated && st->gif.frame_count > 1;
        bk_runtime_strncpy(widget->text, st->playing ? "Pausa" : "Play",
                 sizeof(widget->text) - 1);
        widget->text[sizeof(widget->text) - 1] = '\0';
    }

    st->window->dirty = true;
}

static void viewer_free_media(viewer_state_t *st) {
    if (!st) return;
    bk_gui_image_free(&st->bmp);
    bk_gui_gif_animation_free(&st->gif);
    st->animated = false;
    st->loaded = false;
    st->playing = false;
    st->frame = 0;
    st->next_frame = 0;
}

static void viewer_load_current(viewer_state_t *st) {
    if (!st) return;

    viewer_free_media(st);
    if (ends_with(st->path, ".GIF", ".gif")) {
        st->loaded = bk_gui_gif_load_animation_limited(&st->gif, st->path, 64);
        st->animated = st->loaded && st->gif.frame_count > 0;
    } else {
        st->loaded = viewer_load_bmp(&st->bmp, st->path);
    }
    st->frame = 0;
    st->playing = st->animated && st->gif.frame_count > 1;
    st->next_frame = bk_sys_ticks() + 100;
    viewer_refresh_gallery(st);
    viewer_reset_view(st, true);
    viewer_update_title(st);
    viewer_update_controls(st);
}

static void viewer_open_index(viewer_state_t *st, int index) {
    char next_path[VFS_MAX_PATH];

    if (!st || st->image_count == 0 || index < 0 ||
        (uint32_t)index >= st->image_count)
        return;
    viewer_join_path(next_path, st->directory, st->image_names[index]);
    bk_runtime_strncpy(st->path, next_path, sizeof(st->path) - 1);
    st->path[sizeof(st->path) - 1] = '\0';
    viewer_load_current(st);
}

static void viewer_step_image(viewer_state_t *st, int delta) {
    int next;

    if (!st || st->image_count < 2 || st->image_index < 0) return;
    next = st->image_index + delta;
    if (next < 0) next = (int)st->image_count - 1;
    if ((uint32_t)next >= st->image_count) next = 0;
    viewer_open_index(st, next);
}

static void viewer_zoom_to(viewer_state_t *st, int zoom_pct) {
    if (!st) return;
    if (zoom_pct < VIEWER_MIN_ZOOM) zoom_pct = VIEWER_MIN_ZOOM;
    if (zoom_pct > VIEWER_MAX_ZOOM) zoom_pct = VIEWER_MAX_ZOOM;
    st->fit_to_window = false;
    st->zoom_pct = (uint16_t)zoom_pct;
    viewer_clamp_pan(st);
    st->window->dirty = true;
}

static void viewer_zoom_step(viewer_state_t *st, int delta) {
    int current_zoom;

    if (!st || !viewer_current_image(st)) return;
    current_zoom = viewer_effective_zoom(st, viewer_current_image(st));
    viewer_zoom_to(st, current_zoom + delta);
}

static void viewer_fit(viewer_state_t *st) {
    viewer_reset_view(st, true);
    if (st && st->window) st->window->dirty = true;
}

static void viewer_actual_size(viewer_state_t *st) {
    viewer_reset_view(st, false);
    if (st && st->window) st->window->dirty = true;
}

static void viewer_toggle_play(viewer_state_t *st) {
    if (!st || !st->animated || st->gif.frame_count < 2) return;
    st->playing = !st->playing;
    st->next_frame = bk_sys_ticks() + 100;
    viewer_update_controls(st);
}

static void viewer_prev_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_step_image(st, -1);
}

static void viewer_next_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_step_image(st, 1);
}

static void viewer_zoom_out_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_zoom_step(st, -VIEWER_ZOOM_STEP);
}

static void viewer_zoom_in_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_zoom_step(st, VIEWER_ZOOM_STEP);
}

static void viewer_fit_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_fit(st);
}

static void viewer_actual_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_actual_size(st);
}

static void viewer_play_cb(gui_window_t *window, uint32_t id UNUSED) {
    viewer_state_t *st = window ? (viewer_state_t *)window->content_context
                                : NULL;
    viewer_toggle_play(st);
}

static void viewer_draw_checker(gui_surface_t *surface, gui_rect_t rect) {
    const int tile = 10;

    for (int y = 0; y < rect.h; y++) {
        for (int x = 0; x < rect.w; x++) {
            bool dark = (((x / tile) + (y / tile)) & 1) != 0;
            bk_gui_gfx_putpixel(surface, rect.x + x, rect.y + y,
                             dark ? 0x00383838 : 0x00484848);
        }
    }
}

static void viewer_build_status(viewer_state_t *st, char *text,
                                size_t capacity) {
    gui_image_t *image = viewer_current_image(st);
    char name[VFS_MAX_NAME];
    size_t len = 0;

    if (!text || capacity == 0) return;
    text[0] = '\0';
    if (!st || !image) return;

    viewer_split_path(st->path, NULL, name);
    bk_runtime_strncpy(text, name, capacity - 1);
    text[capacity - 1] = '\0';
    len = bk_runtime_strlen(text);

    viewer_append_char(text, capacity, &len, ' ');
    viewer_append_uint(text, capacity, &len, image->width);
    viewer_append_char(text, capacity, &len, 'x');
    viewer_append_uint(text, capacity, &len, image->height);
    viewer_append_char(text, capacity, &len, ' ');
    viewer_append_uint(text, capacity, &len, viewer_effective_zoom(st, image));
    viewer_append_char(text, capacity, &len, '%');

    if (st->image_count > 1 && st->image_index >= 0) {
        viewer_append_char(text, capacity, &len, ' ');
        viewer_append_char(text, capacity, &len, '[');
        viewer_append_uint(text, capacity, &len,
                           (uint32_t)st->image_index + 1U);
        viewer_append_char(text, capacity, &len, '/');
        viewer_append_uint(text, capacity, &len, st->image_count);
        viewer_append_char(text, capacity, &len, ']');
    }
    if (st->animated && st->gif.frame_count > 1) {
        viewer_append_char(text, capacity, &len, ' ');
        viewer_append_char(text, capacity, &len, 'F');
        viewer_append_char(text, capacity, &len, ':');
        viewer_append_uint(text, capacity, &len, (uint32_t)st->frame + 1U);
        viewer_append_char(text, capacity, &len, '/');
        viewer_append_uint(text, capacity, &len, st->gif.frame_count);
    }
}

static void viewer_draw(gui_window_t *window UNUSED, gui_surface_t *surface,
                        void *context) {
    viewer_state_t *st = (viewer_state_t *)context;
    gui_image_t *image;
    gui_rect_t area;
    gui_rect_t status_rect;
    char status[96];
    int draw_w;
    int draw_h;
    int origin_x;
    int origin_y;

    if (!st || !st->window) return;

    area = viewer_image_area(st);
    status_rect = (gui_rect_t){
        st->window->bounds.x + VIEWER_MARGIN,
        st->window->bounds.y + st->window->bounds.h - VIEWER_STATUS_H - 4,
        st->window->bounds.w - VIEWER_MARGIN * 2,
        VIEWER_STATUS_H
    };

    bk_gui_gfx_fill_rect(surface,
        (gui_rect_t){st->window->bounds.x + 2,
                     bk_gui_window_content_rect_raw(st->window).y + VIEWER_TOOLBAR_H,
                     st->window->bounds.w - 4,
                     bk_gui_window_content_rect_raw(st->window).h -
                         VIEWER_TOOLBAR_H - 3},
        0x00282828);
    bk_gui_gfx_fill_rect(surface,
        (gui_rect_t){st->window->bounds.x + 2,
                     bk_gui_window_content_rect_raw(st->window).y + VIEWER_TOOLBAR_H - 1,
                     st->window->bounds.w - 4, 1},
        0x00484848);
    bk_gui_gfx_fill_rect(surface, status_rect, 0x001C1C1C);
    bk_gui_gfx_draw_rect(surface, status_rect, 0x00484848);

    if (!st->loaded) {
        bk_gui_font_draw_string(surface, area.x + 12, area.y + 12,
                             "No se pudo abrir la imagen",
                             0x00FFFFFF, 0, false);
        bk_gui_font_draw_string_clipped(surface, area.x + 12, area.y + 30,
                                     st->path, 0x00B0B0B0,
                                     (gui_rect_t){area.x + 8, area.y + 26,
                                                  area.w - 16, 18});
        return;
    }

    image = viewer_current_image(st);
    if (!image) return;

    viewer_clamp_pan(st);
    draw_w = ((int)image->width * viewer_effective_zoom(st, image)) / 100;
    draw_h = ((int)image->height * viewer_effective_zoom(st, image)) / 100;
    if (draw_w < 1) draw_w = 1;
    if (draw_h < 1) draw_h = 1;
    origin_x = area.x + (area.w - draw_w) / 2 + st->pan_x;
    origin_y = area.y + (area.h - draw_h) / 2 + st->pan_y;

    viewer_draw_checker(surface, area);
    bk_gui_gfx_draw_rect(surface, area, 0x00565656);

    {
        int clip_x0 = origin_x > area.x ? origin_x : area.x;
        int clip_y0 = origin_y > area.y ? origin_y : area.y;
        int clip_x1 = origin_x + draw_w < area.x + area.w
                    ? origin_x + draw_w : area.x + area.w;
        int clip_y1 = origin_y + draw_h < area.y + area.h
                    ? origin_y + draw_h : area.y + area.h;
        for (int y = clip_y0; y < clip_y1; y++) {
            int sy = ((y - origin_y) * image->height) / draw_h;
            for (int x = clip_x0; x < clip_x1; x++) {
                int sx = ((x - origin_x) * image->width) / draw_w;
                uint32_t color = image->pixels[(uint32_t)sy * image->width + sx];
                if ((color >> 24) != 0)
                    bk_gui_gfx_putpixel(surface, x, y, color & 0x00FFFFFF);
            }
        }
    }

    viewer_build_status(st, status, sizeof(status));
    bk_gui_font_draw_string_clipped(surface, status_rect.x + 6, status_rect.y + 5,
                                 status, 0x00E4E4E4, status_rect);
}

static bool viewer_event(gui_window_t *window UNUSED, const gui_event_t *event,
                         void *context) {
    viewer_state_t *st = (viewer_state_t *)context;
    gui_image_t *image;
    gui_rect_t area;
    int draw_w;
    int draw_h;

    if (!st || !event || !st->window) return false;
    image = viewer_current_image(st);
    area = viewer_image_area(st);
    draw_w = image ? ((int)image->width * viewer_effective_zoom(st, image)) / 100
                   : 0;
    draw_h = image ? ((int)image->height * viewer_effective_zoom(st, image)) / 100
                   : 0;

    if (event->type == GUI_EVENT_KEY) {
        if (event->key == '+' || event->key == '=') {
            viewer_zoom_step(st, VIEWER_ZOOM_STEP);
            return true;
        }
        if (event->key == '-') {
            viewer_zoom_step(st, -VIEWER_ZOOM_STEP);
            return true;
        }
        if (event->key == '0' || event->key == 'f' || event->key == 'F') {
            viewer_fit(st);
            return true;
        }
        if (event->key == '1') {
            viewer_actual_size(st);
            return true;
        }
        if (event->key == 'a' || event->key == 'A') {
            viewer_step_image(st, -1);
            return true;
        }
        if (event->key == 'd' || event->key == 'D') {
            viewer_step_image(st, 1);
            return true;
        }
        if (event->key == ' ') {
            viewer_toggle_play(st);
            return true;
        }
        return false;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN &&
        image && bk_gui_rect_contains(area, event->x, event->y) &&
        (draw_w > area.w || draw_h > area.h)) {
        st->dragging_pan = true;
        st->drag_last_x = event->x;
        st->drag_last_y = event->y;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE && st->dragging_pan) {
        st->pan_x += event->x - st->drag_last_x;
        st->pan_y += event->y - st->drag_last_y;
        st->drag_last_x = event->x;
        st->drag_last_y = event->y;
        viewer_clamp_pan(st);
        st->window->dirty = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_UP && st->dragging_pan) {
        st->dragging_pan = false;
        return true;
    }

    return false;
}

static void viewer_cleanup(viewer_state_t *st) {
    if (!st) return;
    viewer_free_media(st);
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (g_viewer == st) g_viewer = NULL;
    bk_sys_free(st);
}

static void viewer_main(void *argument) {
    viewer_state_t *st = (viewer_state_t *)argument;

    if (!st) {
        bk_proc_exit();
        return;
    }

    viewer_load_current(st);
    st->window = bk_gui_create_window(st->desktop, 82, 36, 440, 320,
                                           "Visor de imagenes");
    if (st->window) {
        (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
            "Visor de imagenes", "Version 1.0",
            "Visor BMP y GIF de BlesKernOS.", "Bles.INC (C) 2026",
            "/ICONS/IMAGE.BMP"});
        gui_widget_t *button;
        bk_gui_set_window_content(st->window, viewer_draw, st);
        bk_gui_set_window_event_handler(st->window, viewer_event, st);
        bk_gui_set_window_min_size(st->window, 280, 190);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);

        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){6, 6, 24, 18}, "<", viewer_prev_cb);
        if (button) st->prev_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){34, 6, 24, 18}, ">", viewer_next_cb);
        if (button) st->next_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){78, 6, 24, 18}, "-", viewer_zoom_out_cb);
        if (button) st->zoom_out_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){106, 6, 24, 18}, "+", viewer_zoom_in_cb);
        if (button) st->zoom_in_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){142, 6, 56, 18}, "Ajustar", viewer_fit_cb);
        if (button) st->fit_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){202, 6, 46, 18}, "100%", viewer_actual_cb);
        if (button) st->actual_id = button->id;
        button = bk_gui_widget_create(st->desktop, st->window, GUI_WIDGET_BUTTON,
            (gui_rect_t){252, 6, 54, 18}, "Pausa", viewer_play_cb);
        if (button) st->play_id = button->id;
        viewer_update_title(st);
        viewer_update_controls(st);
    }

    while (!bk_proc_exit_requested() && st->window && st->window->listed) {
        if (st->animated && st->playing && st->gif.frame_count > 1 &&
            bk_sys_ticks() >= st->next_frame) {
            uint16_t delay = st->gif.delays_cs[st->frame];
            st->frame = (uint16_t)((st->frame + 1) % st->gif.frame_count);
            st->next_frame = bk_sys_ticks() +
                             (uint32_t)(delay ? delay : 10) * 10U;
            st->window->dirty = true;
        }
        bk_sys_sleep_ticks(4);
    }
    viewer_cleanup(st);
    bk_proc_exit();
}

void imageviewer_open(gui_desktop_t *desktop, const char *path) {
    viewer_state_t *st;

    if (!desktop || !path) return;
    st = (viewer_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->zoom_pct = 100;
    bk_runtime_strncpy(st->path, path, sizeof(st->path) - 1);
    st->path[sizeof(st->path) - 1] = '\0';
    g_viewer = st;
    if (bk_proc_spawn_thread("imageviewer", viewer_main, st) < 0) viewer_cleanup(st);
}

void imageviewer_open_from_desktop(gui_desktop_t *desktop) {
    imageviewer_open(desktop, "/ABOUNT.GIF");
}

void imageviewer_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    const char *path = bk_app_launch_argument();

    imageviewer_open(desktop, (path && path[0]) ? path : "/ABOUNT.GIF");
}
