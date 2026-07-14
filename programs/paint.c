#include "../kernel/include/api.h"
/*
 * BlesPaint - simple bitmap paint program for BlesKernOS
 *
 * v5:
 *  - palette moved to the right side under the help/status text
 *  - live preview for line and rectangle tools
 *  - editable BMP filename in the bottom box
 *  - Save writes /<typed-name>.BMP
 *  - title is simply "Paint"
 *  - fixed bucket fill after v4 border guard regression
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PAINT_CANVAS_W 320
#define PAINT_CANVAS_H 200

#define PAINT_WINDOW_W 520
#define PAINT_WINDOW_H 330

#define PAINT_TOOL_PENCIL 0
#define PAINT_TOOL_ERASER 1
#define PAINT_TOOL_FILL   2
#define PAINT_TOOL_LINE   3
#define PAINT_TOOL_RECT   4

#define PAINT_COLOR_WHITE 0x00FFFFFF
#define PAINT_COLOR_BLACK 0x00000000

#define PAINT_FILENAME_MAX 24
#define PAINT_PATH_MAX     40

typedef struct {
    uint16_t x;
    uint16_t y;
} paint_point_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;

    uint32_t *canvas;
    uint32_t color;
    uint8_t tool;
    uint8_t brush_size;

    bool mouse_down;
    int last_x;
    int last_y;
    int start_x;
    int start_y;
    int preview_x;
    int preview_y;

    bool editing_name;
    char filename[PAINT_FILENAME_MAX];
    uint8_t filename_len;

    char status[72];
    uint32_t saves;
} paint_state_t;

static paint_state_t *g_paint;

static const uint32_t g_palette[] = {
    0x00000000, 0x00FFFFFF, 0x00C0C0C0, 0x00808080,
    0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00,
    0x00FF00FF, 0x0000FFFF, 0x00800000, 0x00008000,
    0x00000080, 0x00808000, 0x00800080, 0x00008080,
};

static const uint8_t g_brush_sizes[] = {1, 2, 4, 8, 12};

static void paint_status(paint_state_t *st, const char *text)
{
    if (!st || !text) return;
    strncpy(st->status, text, sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
}

static void paint_status2(paint_state_t *st, const char *a, const char *b)
{
    uint32_t pos = 0;

    if (!st) return;
    st->status[0] = '\0';

    if (a) {
        while (*a && pos + 1 < sizeof(st->status)) {
            st->status[pos++] = *a++;
        }
    }

    if (b) {
        while (*b && pos + 1 < sizeof(st->status)) {
            st->status[pos++] = *b++;
        }
    }

    st->status[pos] = '\0';
}

static int paint_abs(int value)
{
    return value < 0 ? -value : value;
}

static void paint_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void paint_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static char paint_upper(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static bool paint_is_filename_char(char c)
{
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '_' || c == '-' || c == '.') return true;
    return false;
}

static void paint_set_default_filename(paint_state_t *st)
{
    if (!st) return;
    strncpy(st->filename, "DRAWING", sizeof(st->filename) - 1);
    st->filename[sizeof(st->filename) - 1] = '\0';
    st->filename_len = (uint8_t)strlen(st->filename);
}

static bool paint_filename_has_bmp(const char *name)
{
    uint32_t len;

    if (!name) return false;
    len = (uint32_t)strlen(name);
    if (len < 4) return false;

    return name[len - 4] == '.' &&
           paint_upper(name[len - 3]) == 'B' &&
           paint_upper(name[len - 2]) == 'M' &&
           paint_upper(name[len - 1]) == 'P';
}

static void paint_make_save_path(paint_state_t *st, char *out, uint32_t out_size)
{
    uint32_t pos = 0;
    const char *name;

    if (!out || out_size == 0) return;
    out[0] = '\0';

    name = (st && st->filename[0]) ? st->filename : "DRAWING.BMP";

    if (pos + 1 < out_size) out[pos++] = '/';

    while (*name && pos + 1 < out_size) {
        out[pos++] = paint_upper(*name++);
    }

    if (!paint_filename_has_bmp(out) && pos + 4 < out_size) {
        out[pos++] = '.';
        out[pos++] = 'B';
        out[pos++] = 'M';
        out[pos++] = 'P';
    }

    out[pos] = '\0';
}

static const char *paint_tool_name(uint8_t tool)
{
    if (tool == PAINT_TOOL_PENCIL) return "Tool: Pencil";
    if (tool == PAINT_TOOL_ERASER) return "Tool: Eraser";
    if (tool == PAINT_TOOL_FILL) return "Tool: Fill";
    if (tool == PAINT_TOOL_LINE) return "Tool: Line";
    if (tool == PAINT_TOOL_RECT) return "Tool: Rect";
    return "Tool";
}

static void paint_canvas_clear(paint_state_t *st, uint32_t color)
{
    if (!st || !st->canvas) return;
    for (uint32_t i = 0; i < (uint32_t)(PAINT_CANVAS_W * PAINT_CANVAS_H); i++) {
        st->canvas[i] = color;
    }
    if (st->window) st->window->dirty = true;
}

static void paint_draw_dot(paint_state_t *st, int x, int y)
{
    uint32_t color;
    int radius;

    if (!st || !st->canvas) return;

    color = (st->tool == PAINT_TOOL_ERASER) ? PAINT_COLOR_WHITE : st->color;
    radius = st->brush_size <= 1 ? 0 : (int)(st->brush_size / 2);

    for (int yy = y - radius; yy <= y + radius; yy++) {
        if (yy < 0 || yy >= PAINT_CANVAS_H) continue;
        for (int xx = x - radius; xx <= x + radius; xx++) {
            if (xx < 0 || xx >= PAINT_CANVAS_W) continue;
            st->canvas[(uint32_t)yy * PAINT_CANVAS_W + (uint32_t)xx] = color;
        }
    }
}

static void paint_draw_line_canvas(paint_state_t *st, int x0, int y0, int x1, int y1)
{
    int dx;
    int sx;
    int dy;
    int sy;
    int err;

    if (!st || !st->canvas) return;

    dx = paint_abs(x1 - x0);
    sx = x0 < x1 ? 1 : -1;
    dy = -paint_abs(y1 - y0);
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;

    while (true) {
        int e2;
        paint_draw_dot(st, x0, y0);
        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }

    if (st->window) st->window->dirty = true;
}

static void paint_draw_rect_canvas(paint_state_t *st, int x0, int y0, int x1, int y1)
{
    int left = x0 < x1 ? x0 : x1;
    int right = x0 < x1 ? x1 : x0;
    int top = y0 < y1 ? y0 : y1;
    int bottom = y0 < y1 ? y1 : y0;

    paint_draw_line_canvas(st, left, top, right, top);
    paint_draw_line_canvas(st, right, top, right, bottom);
    paint_draw_line_canvas(st, right, bottom, left, bottom);
    paint_draw_line_canvas(st, left, bottom, left, top);
}

/*
 * Flood fill guard:
 * A pure exact-color flood fill can slip through 1px diagonal borders or tiny
 * gaps. For this paint app we use a conservative fill: pixels directly next to
 * a different color are treated as boundary-adjacent and are not entered.
 * This leaves a tiny safety margin near outlines, but it respects hand-drawn
 * lines much better.
 */
static bool paint_fill_near_boundary(paint_state_t *st, int x, int y,
                                     uint32_t target, uint32_t replacement)
{
    if (!st || !st->canvas) return true;

    for (int dy = -1; dy <= 1; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= PAINT_CANVAS_H) continue;

        for (int dx = -1; dx <= 1; dx++) {
            int xx = x + dx;
            uint32_t color;

            if (dx == 0 && dy == 0) continue;
            if (xx < 0 || xx >= PAINT_CANVAS_W) continue;

            color = st->canvas[(uint32_t)yy * PAINT_CANVAS_W + (uint32_t)xx];

            /*
             * Important:
             * The fill writes replacement pixels while it expands.
             * Those already-filled pixels are NOT borders. v4 treated them as
             * borders, so after coloring the first pixel every neighbor got
             * rejected and the bucket looked broken.
             */
            if (color != target && color != replacement) return true;
        }
    }

    return false;
}

static void paint_fill_try_push(paint_state_t *st, paint_point_t *queue,
                                uint32_t *tail, uint32_t max_points,
                                int x, int y, uint32_t target,
                                uint32_t replacement)
{
    uint32_t index;

    if (!st || !st->canvas || !queue || !tail) return;
    if (x < 0 || y < 0 || x >= PAINT_CANVAS_W || y >= PAINT_CANVAS_H) return;
    if (*tail >= max_points) return;

    index = (uint32_t)y * PAINT_CANVAS_W + (uint32_t)x;
    if (st->canvas[index] != target) return;

    /*
     * Allow the first clicked pixel, but avoid expanding into pixels touching
     * a border color. This prevents the bucket from eating through outlines.
     */
    if (*tail > 0 && paint_fill_near_boundary(st, x, y, target, replacement)) return;

    st->canvas[index] = replacement;
    queue[*tail].x = (uint16_t)x;
    queue[*tail].y = (uint16_t)y;
    (*tail)++;
}

static void paint_flood_fill(paint_state_t *st, int x, int y)
{
    uint32_t target;
    uint32_t replacement;
    uint32_t max_points = (uint32_t)(PAINT_CANVAS_W * PAINT_CANVAS_H);
    paint_point_t *queue;
    uint32_t head = 0;
    uint32_t tail = 0;

    if (!st || !st->canvas) return;
    if (x < 0 || y < 0 || x >= PAINT_CANVAS_W || y >= PAINT_CANVAS_H) return;

    target = st->canvas[(uint32_t)y * PAINT_CANVAS_W + (uint32_t)x];
    replacement = (st->tool == PAINT_TOOL_ERASER) ? PAINT_COLOR_WHITE : st->color;

    if (target == replacement) {
        paint_status(st, "Nada que rellenar");
        return;
    }

    queue = (paint_point_t *)bk_sys_alloc_zero((uint32_t)(sizeof(paint_point_t) * max_points));
    if (!queue) {
        paint_status(st, "Sin memoria para cubeta");
        return;
    }

    paint_fill_try_push(st, queue, &tail, max_points, x, y, target, replacement);

    while (head < tail) {
        paint_point_t p = queue[head++];
        paint_fill_try_push(st, queue, &tail, max_points,
                            (int)p.x + 1, (int)p.y, target, replacement);
        paint_fill_try_push(st, queue, &tail, max_points,
                            (int)p.x - 1, (int)p.y, target, replacement);
        paint_fill_try_push(st, queue, &tail, max_points,
                            (int)p.x, (int)p.y + 1, target, replacement);
        paint_fill_try_push(st, queue, &tail, max_points,
                            (int)p.x, (int)p.y - 1, target, replacement);
    }

    bk_sys_free(queue);
    paint_status(st, "Relleno aplicado");
    if (st->window) st->window->dirty = true;
}

static bool paint_build_bmp(paint_state_t *st, uint8_t **out_data, uint32_t *out_size)
{
    uint32_t row_stride;
    uint32_t pixel_bytes;
    uint32_t file_size;
    uint8_t *bmp;

    if (!st || !st->canvas || !out_data || !out_size) return false;

    *out_data = NULL;
    *out_size = 0;

    row_stride = (uint32_t)((PAINT_CANVAS_W * 3 + 3) & ~3);
    pixel_bytes = row_stride * PAINT_CANVAS_H;
    file_size = 54 + pixel_bytes;

    bmp = (uint8_t *)bk_sys_alloc_zero(file_size);
    if (!bmp) return false;

    bmp[0] = 'B';
    bmp[1] = 'M';
    paint_put_le32(bmp + 2, file_size);
    paint_put_le32(bmp + 10, 54);

    paint_put_le32(bmp + 14, 40);
    paint_put_le32(bmp + 18, PAINT_CANVAS_W);
    paint_put_le32(bmp + 22, PAINT_CANVAS_H);
    paint_put_le16(bmp + 26, 1);
    paint_put_le16(bmp + 28, 24);
    paint_put_le32(bmp + 30, 0);
    paint_put_le32(bmp + 34, pixel_bytes);
    paint_put_le32(bmp + 38, 2835);
    paint_put_le32(bmp + 42, 2835);

    for (int y = 0; y < PAINT_CANVAS_H; y++) {
        uint8_t *dst = bmp + 54 + (uint32_t)(PAINT_CANVAS_H - 1 - y) * row_stride;
        for (int x = 0; x < PAINT_CANVAS_W; x++) {
            uint32_t rgb = st->canvas[(uint32_t)y * PAINT_CANVAS_W + (uint32_t)x];
            dst[x * 3 + 0] = (uint8_t)(rgb & 0xFF);
            dst[x * 3 + 1] = (uint8_t)((rgb >> 8) & 0xFF);
            dst[x * 3 + 2] = (uint8_t)((rgb >> 16) & 0xFF);
        }
    }

    *out_data = bmp;
    *out_size = file_size;
    return true;
}

static bool paint_save_bmp(paint_state_t *st)
{
    uint8_t *bmp = NULL;
    uint32_t bmp_size = 0;
    char path[PAINT_PATH_MAX];
    FILE *file;
    size_t wrote;

    if (!st) return false;

    paint_make_save_path(st, path, sizeof(path));

    if (!paint_build_bmp(st, &bmp, &bmp_size)) {
        paint_status(st, "No se pudo crear BMP en memoria");
        return false;
    }

    file = fopen(path, "wb");
    if (!file) {
        bk_sys_free(bmp);
        paint_status(st, "Guardar fallo: fopen/VFS write no disponible");
        return false;
    }

    wrote = fwrite(bmp, 1, bmp_size, file);
    fclose(file);
    bk_sys_free(bmp);

    if ((uint32_t)wrote != bmp_size) {
        paint_status(st, "Guardar fallo: escritura incompleta");
        return false;
    }

    st->saves++;
    paint_status2(st, "Guardado como ", path);
    return true;
}

static gui_rect_t paint_canvas_rect(const gui_window_t *window)
{
    int top = bk_gui_window_content_top(window);
    return (gui_rect_t){
        window->bounds.x + 12,
        window->bounds.y + top + 64,
        PAINT_CANVAS_W,
        PAINT_CANVAS_H
    };
}

static gui_rect_t paint_name_rect(const gui_window_t *window)
{
    return (gui_rect_t){
        window->bounds.x + 12,
        window->bounds.y + window->bounds.h - GUI_BORDER_SIZE - 22,
        window->bounds.w - 24,
        18
    };
}

static gui_rect_t paint_button_rect(const gui_window_t *window, int index)
{
    int top = bk_gui_window_content_top(window);
    return (gui_rect_t){
        window->bounds.x + 12 + index * 60,
        window->bounds.y + top + 8,
        54,
        24
    };
}

static gui_rect_t paint_palette_rect(const gui_window_t *window, int index)
{
    gui_rect_t canvas = paint_canvas_rect(window);
    int side_x = canvas.x + canvas.w + 18;

    return (gui_rect_t){
        side_x + (index % 4) * 24,
        canvas.y + 94 + (index / 4) * 20,
        18,
        16
    };
}

static gui_rect_t paint_brush_rect(const gui_window_t *window, int index)
{
    int top = bk_gui_window_content_top(window);
    return (gui_rect_t){
        window->bounds.x + 332 + index * 30,
        window->bounds.y + top + 9,
        24,
        22
    };
}

static void paint_draw_relief(gui_surface_t *surface, gui_rect_t rect, bool pressed)
{
    uint32_t light = pressed ? 0x00606060 : 0x00FFFFFF;
    uint32_t dark = pressed ? 0x00FFFFFF : 0x00606060;
    uint32_t mid = 0x00A0A0A0;

    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, rect.w, 1}, light);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y, 1, rect.h}, light);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x, rect.y + rect.h - 1, rect.w, 1}, dark);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 1, rect.y, 1, rect.h}, dark);

    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + 1, rect.y + rect.h - 2, rect.w - 2, 1}, mid);
    bk_gui_gfx_fill_rect(surface, (gui_rect_t){rect.x + rect.w - 2, rect.y + 1, 1, rect.h - 2}, mid);
}

static void paint_draw_button(gui_surface_t *surface, gui_rect_t rect,
                              const char *text, bool active)
{
    uint32_t fill = active ? 0x00BFE7FF : 0x00E8E8E8;
    int text_x = rect.x + 6;
    int text_y = rect.y + 8;

    bk_gui_gfx_fill_rect(surface, rect, fill);
    paint_draw_relief(surface, rect, active);

    if (active) {
        text_x++;
        text_y++;
    }

    bk_gui_font_draw_string_clipped(surface, text_x, text_y,
                                 text, 0x00000000, rect);
}

static void paint_draw_brush_button(gui_surface_t *surface, gui_rect_t rect,
                                    const char *text, bool active)
{
    uint32_t fill = active ? 0x00BFE7FF : 0x00E8E8E8;

    bk_gui_gfx_fill_rect(surface, rect, fill);
    paint_draw_relief(surface, rect, active);
    bk_gui_font_draw_string_clipped(surface, rect.x + 7, rect.y + 7,
                                 text, 0x00000000, rect);
}

static void paint_draw_preview(paint_state_t *st, gui_surface_t *surface,
                               gui_rect_t canvas_rect)
{
    uint32_t preview_color;
    int x0;
    int y0;
    int x1;
    int y1;
    gui_rect_t r;

    if (!st || !surface || !st->mouse_down) return;
    if (st->tool != PAINT_TOOL_LINE && st->tool != PAINT_TOOL_RECT) return;

    preview_color = st->color == PAINT_COLOR_WHITE ? PAINT_COLOR_BLACK : st->color;

    x0 = canvas_rect.x + st->start_x;
    y0 = canvas_rect.y + st->start_y;
    x1 = canvas_rect.x + st->preview_x;
    y1 = canvas_rect.y + st->preview_y;

    if (st->tool == PAINT_TOOL_LINE) {
        bk_gui_gfx_draw_line(surface, x0, y0, x1, y1, preview_color);
        bk_gui_gfx_draw_line(surface, x0 + 1, y0, x1 + 1, y1, preview_color);
    } else {
        int left = x0 < x1 ? x0 : x1;
        int right = x0 < x1 ? x1 : x0;
        int top = y0 < y1 ? y0 : y1;
        int bottom = y0 < y1 ? y1 : y0;

        r = (gui_rect_t){left, top, right - left + 1, bottom - top + 1};
        bk_gui_gfx_draw_rect(surface, r, preview_color);
        bk_gui_gfx_draw_rect(surface, (gui_rect_t){r.x + 1, r.y + 1, r.w - 2, r.h - 2},
                          preview_color);
    }
}

static void paint_draw_name_box(paint_state_t *st, gui_window_t *window,
                                gui_surface_t *surface)
{
    gui_rect_t box;
    char path[PAINT_PATH_MAX];
    char line[64];
    uint32_t pos = 0;
    const char *prefix = "Save as: ";

    if (!st || !window || !surface) return;

    box = paint_name_rect(window);
    paint_make_save_path(st, path, sizeof(path));

    while (*prefix && pos + 1 < sizeof(line)) line[pos++] = *prefix++;
    for (uint32_t i = 0; path[i] && pos + 1 < sizeof(line); i++) {
        line[pos++] = path[i];
    }
    if (st->editing_name && pos + 1 < sizeof(line)) {
        line[pos++] = '_';
    }
    line[pos] = '\0';

    bk_gui_gfx_fill_rect(surface, box, st->editing_name ? 0x00FFFFFF : 0x00EFEFEF);
    bk_gui_gfx_draw_rect(surface, box, st->editing_name ? 0x00000000 : 0x00808080);
    bk_gui_font_draw_string_clipped(surface, box.x + 5, box.y + 5,
                                 line, 0x00000000, box);
}

static void paint_content(gui_window_t *window, gui_surface_t *surface, void *context)
{
    paint_state_t *st = (paint_state_t *)context;
    gui_rect_t clip;
    gui_rect_t canvas_rect;
    gui_rect_t r;
    gui_rect_t label_rect;
    static const char *brush_labels[] = {"1", "2", "4", "8", "12"};

    if (!st || !window || !surface || !window->visible) return;

    clip = (gui_rect_t){
        window->bounds.x + GUI_BORDER_SIZE,
        window->bounds.y + bk_gui_window_content_top(window),
        window->bounds.w - GUI_BORDER_SIZE * 2,
        window->bounds.h - bk_gui_window_content_top(window) - GUI_BORDER_SIZE
    };

    bk_gui_gfx_fill_rect(surface, clip, 0x00DCDCDC);

    paint_draw_button(surface, paint_button_rect(window, 0), "Pencil",
                      st->tool == PAINT_TOOL_PENCIL);
    paint_draw_button(surface, paint_button_rect(window, 1), "Eraser",
                      st->tool == PAINT_TOOL_ERASER);
    paint_draw_button(surface, paint_button_rect(window, 2), "Fill",
                      st->tool == PAINT_TOOL_FILL);
    paint_draw_button(surface, paint_button_rect(window, 3), "Clean", false);
    paint_draw_button(surface, paint_button_rect(window, 4), "Save", false);

    label_rect = (gui_rect_t){window->bounds.x + 332,
                              window->bounds.y + bk_gui_window_content_top(window) + 36,
                              150, 18};
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 "Brush", 0x00000000, label_rect);

    for (int i = 0; i < 5; i++) {
        paint_draw_brush_button(surface, paint_brush_rect(window, i),
                                brush_labels[i],
                                st->brush_size == g_brush_sizes[i]);
    }

    canvas_rect = paint_canvas_rect(window);
    bk_gui_gfx_fill_rect(surface,
                      (gui_rect_t){canvas_rect.x - 2, canvas_rect.y - 2,
                                   canvas_rect.w + 4, canvas_rect.h + 4},
                      0x00606060);

    if (st->canvas) {
        for (int y = 0; y < PAINT_CANVAS_H; y++) {
            for (int x = 0; x < PAINT_CANVAS_W; x++) {
                bk_gui_gfx_putpixel(surface, canvas_rect.x + x, canvas_rect.y + y,
                                 st->canvas[(uint32_t)y * PAINT_CANVAS_W + (uint32_t)x]);
            }
        }
    } else {
        bk_gui_gfx_fill_rect(surface, canvas_rect, PAINT_COLOR_WHITE);
        bk_gui_font_draw_string_clipped(surface, canvas_rect.x + 8, canvas_rect.y + 8,
                                     "Sin memoria para canvas", 0x00000000,
                                     canvas_rect);
    }

    paint_draw_preview(st, surface, canvas_rect);

    label_rect = (gui_rect_t){canvas_rect.x + canvas_rect.w + 18,
                              canvas_rect.y,
                              150,
                              18};
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 paint_tool_name(st->tool), 0x00000000,
                                 label_rect);
    label_rect.y += 16;
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 st->status, 0x00000000,
                                 label_rect);
    label_rect.y += 18;
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 "Keys: S save", 0x00000000,
                                 label_rect);
    label_rect.y += 14;
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 "C clean", 0x00000000,
                                 label_rect);
    label_rect.y += 14;
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 "P/E/F/L/R", 0x00000000,
                                 label_rect);
    label_rect.y += 22;
    bk_gui_font_draw_string_clipped(surface, label_rect.x, label_rect.y,
                                 "Colors", 0x00000000,
                                 label_rect);

    for (int i = 0; i < (int)(sizeof(g_palette) / sizeof(g_palette[0])); i++) {
        r = paint_palette_rect(window, i);
        bk_gui_gfx_fill_rect(surface, r, g_palette[i]);
        bk_gui_gfx_draw_rect(surface, r,
                          st->color == g_palette[i] ? 0x00FFFFFF : 0x00404040);
        if (st->color == g_palette[i]) {
            bk_gui_gfx_draw_rect(surface,
                              (gui_rect_t){r.x - 1, r.y - 1, r.w + 2, r.h + 2},
                              0x00000000);
        }
    }

    paint_draw_name_box(st, window, surface);
}

static bool paint_inside(gui_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void paint_event_to_canvas_clamped(paint_state_t *st,
                                          const gui_event_t *event,
                                          int *cx, int *cy)
{
    gui_rect_t canvas_rect;
    int x;
    int y;

    if (!st || !st->window || !event || !cx || !cy) return;

    canvas_rect = paint_canvas_rect(st->window);
    x = event->x - canvas_rect.x;
    y = event->y - canvas_rect.y;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PAINT_CANVAS_W) x = PAINT_CANVAS_W - 1;
    if (y >= PAINT_CANVAS_H) y = PAINT_CANVAS_H - 1;

    *cx = x;
    *cy = y;
}

static bool paint_canvas_event(paint_state_t *st, const gui_event_t *event)
{
    gui_rect_t canvas_rect;
    int cx;
    int cy;

    if (!st || !st->window || !event) return false;

    canvas_rect = paint_canvas_rect(st->window);
    if (!paint_inside(canvas_rect, event->x, event->y)) return false;

    cx = event->x - canvas_rect.x;
    cy = event->y - canvas_rect.y;

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        st->editing_name = false;
        st->mouse_down = true;
        st->last_x = cx;
        st->last_y = cy;
        st->start_x = cx;
        st->start_y = cy;
        st->preview_x = cx;
        st->preview_y = cy;

        if (st->tool == PAINT_TOOL_FILL) {
            paint_flood_fill(st, cx, cy);
            st->mouse_down = false;
        } else if (st->tool == PAINT_TOOL_PENCIL ||
                   st->tool == PAINT_TOOL_ERASER) {
            paint_draw_dot(st, cx, cy);
            paint_status(st, "Dibujando");
        } else if (st->tool == PAINT_TOOL_LINE) {
            paint_status(st, "Preview de linea");
        } else if (st->tool == PAINT_TOOL_RECT) {
            paint_status(st, "Preview de rectangulo");
        }

        if (st->window) st->window->dirty = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE && st->mouse_down) {
        st->preview_x = cx;
        st->preview_y = cy;

        if (st->tool == PAINT_TOOL_PENCIL ||
            st->tool == PAINT_TOOL_ERASER) {
            paint_draw_line_canvas(st, st->last_x, st->last_y, cx, cy);
            st->last_x = cx;
            st->last_y = cy;
        }

        if (st->window) st->window->dirty = true;
        return true;
    }

    return false;
}

static void paint_set_brush(paint_state_t *st, uint8_t size)
{
    if (!st) return;

    st->brush_size = size;

    if (size == 1) paint_status(st, "Brocha 1 px");
    else if (size == 2) paint_status(st, "Brocha 2 px");
    else if (size == 4) paint_status(st, "Brocha 4 px");
    else if (size == 8) paint_status(st, "Brocha 8 px");
    else paint_status(st, "Brocha 12 px");
}

static void paint_handle_toolbar(paint_state_t *st, const gui_event_t *event)
{
    if (!st || !st->window || !event) return;

    if (paint_inside(paint_name_rect(st->window), event->x, event->y)) {
        st->editing_name = true;
        paint_status(st, "Escribi nombre y Enter para guardar");
    } else {
        st->editing_name = false;
    }

    if (paint_inside(paint_button_rect(st->window, 0), event->x, event->y)) {
        st->tool = PAINT_TOOL_PENCIL;
        paint_status(st, "Herramienta: lapiz");
    } else if (paint_inside(paint_button_rect(st->window, 1), event->x, event->y)) {
        st->tool = PAINT_TOOL_ERASER;
        paint_status(st, "Herramienta: goma");
    } else if (paint_inside(paint_button_rect(st->window, 2), event->x, event->y)) {
        st->tool = PAINT_TOOL_FILL;
        paint_status(st, "Herramienta: cubeta");
    } else if (paint_inside(paint_button_rect(st->window, 3), event->x, event->y)) {
        paint_canvas_clear(st, PAINT_COLOR_WHITE);
        paint_status(st, "Canvas limpio");
    } else if (paint_inside(paint_button_rect(st->window, 4), event->x, event->y)) {
        (void)paint_save_bmp(st);
    } else {
        for (int i = 0; i < (int)(sizeof(g_brush_sizes) / sizeof(g_brush_sizes[0])); i++) {
            if (paint_inside(paint_brush_rect(st->window, i), event->x, event->y)) {
                paint_set_brush(st, g_brush_sizes[i]);
                if (st->window) st->window->dirty = true;
                return;
            }
        }

        for (int i = 0; i < (int)(sizeof(g_palette) / sizeof(g_palette[0])); i++) {
            if (paint_inside(paint_palette_rect(st->window, i), event->x, event->y)) {
                st->color = g_palette[i];
                if (st->tool == PAINT_TOOL_ERASER) {
                    st->tool = PAINT_TOOL_PENCIL;
                }
                paint_status(st, "Color seleccionado");
                break;
            }
        }
    }

    if (st->window) st->window->dirty = true;
}

static bool paint_handle_filename_key(paint_state_t *st, char key)
{
    if (!st || !st->editing_name) return false;

    if (key == '\r' || key == '\n') {
        st->editing_name = false;
        (void)paint_save_bmp(st);
        if (st->window) st->window->dirty = true;
        return true;
    }

    if (key == 27) {
        st->editing_name = false;
        paint_status(st, "Edicion de nombre cancelada");
        if (st->window) st->window->dirty = true;
        return true;
    }

    if (key == '\b' || key == 8 || key == 127) {
        if (st->filename_len > 0) {
            st->filename_len--;
            st->filename[st->filename_len] = '\0';
        }
        if (st->window) st->window->dirty = true;
        return true;
    }

    if (paint_is_filename_char(key) && st->filename_len + 1 < sizeof(st->filename)) {
        st->filename[st->filename_len++] = paint_upper(key);
        st->filename[st->filename_len] = '\0';
        if (st->window) st->window->dirty = true;
        return true;
    }

    return true;
}

static bool paint_event(gui_window_t *window, const gui_event_t *event, void *context)
{
    paint_state_t *st = (paint_state_t *)context;

    (void)window;

    if (!st || !event) return false;

    if (event->type == GUI_EVENT_MOUSE_UP) {
        if (st->mouse_down) {
            int cx;
            int cy;
            paint_event_to_canvas_clamped(st, event, &cx, &cy);
            st->preview_x = cx;
            st->preview_y = cy;

            if (st->tool == PAINT_TOOL_LINE) {
                paint_draw_line_canvas(st, st->start_x, st->start_y, cx, cy);
                paint_status(st, "Linea dibujada");
            } else if (st->tool == PAINT_TOOL_RECT) {
                paint_draw_rect_canvas(st, st->start_x, st->start_y, cx, cy);
                paint_status(st, "Rectangulo dibujado");
            }
        }
        st->mouse_down = false;
        if (st->window) st->window->dirty = true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        paint_handle_toolbar(st, event);
        if (paint_canvas_event(st, event)) return true;
        return true;
    }

    if (event->type == GUI_EVENT_MOUSE_MOVE) {
        if (paint_canvas_event(st, event)) return true;
        return false;
    }

    if (event->type == GUI_EVENT_KEY) {
        if (paint_handle_filename_key(st, event->key)) {
            return true;
        }

        if (event->key == 's' || event->key == 'S') {
            (void)paint_save_bmp(st);
        } else if (event->key == 'c' || event->key == 'C') {
            paint_canvas_clear(st, PAINT_COLOR_WHITE);
            paint_status(st, "Canvas limpio");
        } else if (event->key == 'e' || event->key == 'E') {
            st->tool = PAINT_TOOL_ERASER;
            paint_status(st, "Herramienta: goma");
        } else if (event->key == 'p' || event->key == 'P') {
            st->tool = PAINT_TOOL_PENCIL;
            paint_status(st, "Herramienta: lapiz");
        } else if (event->key == 'f' || event->key == 'F') {
            st->tool = PAINT_TOOL_FILL;
            paint_status(st, "Herramienta: cubeta");
        } else if (event->key == 'l' || event->key == 'L') {
            st->tool = PAINT_TOOL_LINE;
            paint_status(st, "Herramienta: linea");
        } else if (event->key == 'r' || event->key == 'R') {
            st->tool = PAINT_TOOL_RECT;
            paint_status(st, "Herramienta: rectangulo");
        } else if (event->key == '1') {
            paint_set_brush(st, 1);
        } else if (event->key == '2') {
            paint_set_brush(st, 2);
        } else if (event->key == '4') {
            paint_set_brush(st, 4);
        } else if (event->key == '8') {
            paint_set_brush(st, 8);
        } else {
            return false;
        }

        if (st->window) st->window->dirty = true;
        return true;
    }

    return false;
}

static void paint_menu(gui_window_t *window, uint32_t item_id, void *context)
{
    paint_state_t *st = (paint_state_t *)context;

    (void)window;

    if (!st) return;

    if (item_id == 1) {
        paint_canvas_clear(st, PAINT_COLOR_WHITE);
        paint_status(st, "Nuevo dibujo");
    } else if (item_id == 2) {
        (void)paint_save_bmp(st);
    } else if (item_id == 3) {
        st->tool = PAINT_TOOL_PENCIL;
        paint_status(st, "Herramienta: lapiz");
    } else if (item_id == 4) {
        st->tool = PAINT_TOOL_ERASER;
        paint_status(st, "Herramienta: goma");
    } else if (item_id == 5) {
        st->tool = PAINT_TOOL_FILL;
        paint_status(st, "Herramienta: cubeta");
    } else if (item_id == 6) {
        st->tool = PAINT_TOOL_LINE;
        paint_status(st, "Herramienta: linea");
    } else if (item_id == 7) {
        st->tool = PAINT_TOOL_RECT;
        paint_status(st, "Herramienta: rectangulo");
    } else if (item_id >= 10 && item_id <= 14) {
        paint_set_brush(st, g_brush_sizes[item_id - 10]);
    }

    if (st->window) st->window->dirty = true;
}

static void paint_cleanup(paint_state_t *st)
{
    if (!st) return;

    if (st->canvas) {
        bk_sys_free(st->canvas);
        st->canvas = NULL;
    }

    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
        st->window = NULL;
    }

    if (g_paint == st) g_paint = NULL;
    bk_sys_free(st);
}

bool paint_get_runtime_info(program_runtime_info_t *info)
{
    if (!info || !g_paint) return false;
    info->window = g_paint->window;
    info->memory_bytes = (uint32_t)sizeof(*g_paint);
    info->memory_bytes += (uint32_t)(PAINT_CANVAS_W * PAINT_CANVAS_H * sizeof(uint32_t));
    return true;
}

static void paint_main(void *argument)
{
    paint_state_t *st = (paint_state_t *)argument;

    if (!st || !st->desktop) {
        paint_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint((uint32_t)sizeof(*st) +
                         (uint32_t)(PAINT_CANVAS_W * PAINT_CANVAS_H * sizeof(uint32_t)));

    st->canvas = (uint32_t *)bk_sys_alloc_zero((uint32_t)(PAINT_CANVAS_W * PAINT_CANVAS_H * sizeof(uint32_t)));
    if (st->canvas) {
        paint_canvas_clear(st, PAINT_COLOR_WHITE);
    }

    paint_set_default_filename(st);
    st->color = PAINT_COLOR_BLACK;
    st->tool = PAINT_TOOL_PENCIL;
    st->brush_size = 2;
    st->preview_x = 0;
    st->preview_y = 0;
    paint_status(st, "Click abajo para nombrar BMP");

    st->window = bk_gui_create_window(st->desktop, 85, 45,
                                           PAINT_WINDOW_W, PAINT_WINDOW_H,
                                           "Paint");
    if (!st->window) {
        paint_cleanup(st);
        bk_proc_exit();
    }

    bk_gui_set_window_min_size(st->window, PAINT_WINDOW_W, PAINT_WINDOW_H);
    bk_gui_set_window_content(st->window, paint_content, st);
    bk_gui_set_window_event_handler(st->window, paint_event, st);

    if (!st->window->menu_count) {
        int file_menu = bk_gui_add_menu(st->window, "File");
        bk_gui_add_menu_item(st->window, file_menu, 1, "Nuevo",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, file_menu, 2, "Guardar BMP",
                                 paint_menu, st);

        int tool_menu = bk_gui_add_menu(st->window, "Tool");
        bk_gui_add_menu_item(st->window, tool_menu, 3, "Lapiz",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, tool_menu, 4, "Goma",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, tool_menu, 5, "Cubeta",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, tool_menu, 6, "Linea",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, tool_menu, 7, "Rectangulo",
                                 paint_menu, st);

        int brush_menu = bk_gui_add_menu(st->window, "Brush");
        bk_gui_add_menu_item(st->window, brush_menu, 10, "1 px",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, brush_menu, 11, "2 px",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, brush_menu, 12, "4 px",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, brush_menu, 13, "8 px",
                                 paint_menu, st);
        bk_gui_add_menu_item(st->window, brush_menu, 14, "12 px",
                                 paint_menu, st);
    }
    (void)bk_about_attach(st->window, st->desktop, &(bk_about_info_t){
        "Paint", "Version 1.0", "Editor de imagenes BMP de BlesKernOS.",
        "Bles.INC (C) 2026", "/ICONS/IMAGE.BMP"});

    st->window->owner_pid = bk_sys_getpid();
    bk_proc_bind_window(st->window);
    st->window->dirty = true;

    while (!bk_proc_exit_requested()) {
        if (!st->window || !st->window->listed) break;
        bk_sys_sleep_ticks(5);
    }

    paint_cleanup(st);
    bk_proc_exit();
}

void paint_open_from_desktop(gui_desktop_t *desktop)
{
    paint_state_t *st;

    if (!desktop) return;

    if (g_paint && g_paint->window) {
        bk_gui_window_restore(g_paint->window);
        bk_gui_desktop_raise_window(desktop, g_paint->window);
        bk_gui_focus_window(desktop, g_paint->window);
        return;
    }

    st = (paint_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;

    st->desktop = desktop;
    g_paint = st;

    if (bk_proc_spawn_thread("paint", paint_main, st) < 0) {
        paint_cleanup(st);
    }
}

void paint_install(gui_desktop_t *desktop)
{
    (void)desktop;
}

void bleskernos_program_main(gui_desktop_t *desktop) {
    paint_open_from_desktop(desktop);
}
