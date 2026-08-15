#include "image.h"
#include "../kernel/include/memory.h"
#include "../kernel/string.h"

#define SVG_SCALE 1024
#define SVG_POINT_MAX 768

typedef struct { int32_t x, y; } svg_point_t;
typedef struct {
    int32_t view_x, view_y, view_w, view_h;
    uint32_t width, height;
    uint32_t *pixels;
} svg_canvas_t;

static bool svg_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',';
}

static int32_t svg_number(const char **cursor, const char *end, bool *valid) {
    const char *p = *cursor;
    int32_t integer = 0, fraction = 0, divisor = 1;
    bool negative = false, found = false;
    while (p < end && svg_space(*p)) p++;
    if (p < end && (*p == '+' || *p == '-')) negative = *p++ == '-';
    while (p < end && *p >= '0' && *p <= '9') {
        found = true;
        if (integer < 1000000) integer = integer * 10 + (*p - '0');
        p++;
    }
    if (p < end && *p == '.') {
        p++;
        while (p < end && *p >= '0' && *p <= '9') {
            found = true;
            if (divisor < 1000000) { fraction = fraction * 10 + (*p - '0'); divisor *= 10; }
            p++;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *exponent = p++;
        bool exponent_negative = false;
        int power = 0;
        if (p < end && (*p == '+' || *p == '-')) exponent_negative = *p++ == '-';
        if (p >= end || *p < '0' || *p > '9') p = exponent;
        else {
            while (p < end && *p >= '0' && *p <= '9') { if (power < 7) power = power * 10 + (*p - '0'); p++; }
            while (power-- > 0) {
                if (exponent_negative) divisor *= 10;
                else integer *= 10, fraction *= 10;
            }
        }
    }
    *cursor = p;
    *valid = found;
    integer = integer * SVG_SCALE + fraction * SVG_SCALE / divisor;
    return negative ? -integer : integer;
}

static bool svg_attr(const char *tag, const char *end, const char *name,
                     const char **value, const char **value_end) {
    uint32_t length = (uint32_t)kstrlen(name);
    const char *p = tag;
    while (p + length < end) {
        if ((p == tag || svg_space(p[-1]) || p[-1] == '<') &&
            kmemcmp(p, name, length) == 0) {
            const char *q = p + length;
            char quote;
            while (q < end && svg_space(*q)) q++;
            if (q >= end || *q++ != '=') { p++; continue; }
            while (q < end && svg_space(*q)) q++;
            if (q >= end) return false;
            quote = (*q == '\'' || *q == '"') ? *q++ : 0;
            *value = q;
            while (q < end && ((quote && *q != quote) ||
                   (!quote && !svg_space(*q) && *q != '>'))) q++;
            *value_end = q;
            return q > *value;
        }
        p++;
    }
    return false;
}

static int32_t svg_attr_number(const char *tag, const char *end,
                               const char *name, int32_t fallback) {
    const char *value, *value_end;
    bool valid;
    int32_t number;
    if (!svg_attr(tag, end, name, &value, &value_end)) return fallback;
    number = svg_number(&value, value_end, &valid);
    return valid ? number : fallback;
}

static uint8_t svg_hex(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0;
}

static uint32_t svg_color(const char *tag, const char *end) {
    const char *value, *value_end;
    uint32_t color = 0xff000000U;
    if (!svg_attr(tag, end, "fill", &value, &value_end)) return color;
    if (value_end - value == 4 && kmemcmp(value, "none", 4) == 0) return 0U;
    if (*value == '#') {
        uint32_t length = (uint32_t)(value_end - ++value);
        if (length == 3U)
            color |= (uint32_t)svg_hex(value[0]) * 17U << 16 |
                     (uint32_t)svg_hex(value[1]) * 17U << 8 |
                     (uint32_t)svg_hex(value[2]) * 17U;
        else if (length >= 6U)
            color |= (uint32_t)(svg_hex(value[0]) * 16U + svg_hex(value[1])) << 16 |
                     (uint32_t)(svg_hex(value[2]) * 16U + svg_hex(value[3])) << 8 |
                     (uint32_t)(svg_hex(value[4]) * 16U + svg_hex(value[5]));
    } else if (value_end - value == 5 && kmemcmp(value, "white", 5) == 0)
        color = 0xffffffffU;
    return color;
}

static int32_t svg_x(const svg_canvas_t *c, int32_t value) {
    return (int32_t)(((int64_t)(value - c->view_x) * c->width) / c->view_w);
}
static int32_t svg_y(const svg_canvas_t *c, int32_t value) {
    return (int32_t)(((int64_t)(value - c->view_y) * c->height) / c->view_h);
}

static void svg_pixel(svg_canvas_t *canvas, int x, int y, uint32_t color) {
    if (color && x >= 0 && y >= 0 && x < (int)canvas->width && y < (int)canvas->height)
        canvas->pixels[(uint32_t)y * canvas->width + (uint32_t)x] = color;
}

static void svg_fill_polygon(svg_canvas_t *canvas, const svg_point_t *points,
                             uint32_t count, uint32_t color) {
    int32_t intersections[SVG_POINT_MAX];
    int min_y, max_y;
    if (!canvas || !points || count < 3U || !color) return;
    min_y = max_y = points[0].y;
    for (uint32_t i = 1U; i < count; i++) {
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    if (min_y < 0) min_y = 0;
    if (max_y >= (int)canvas->height) max_y = canvas->height - 1;
    for (int y = min_y; y <= max_y; y++) {
        uint32_t used = 0U;
        for (uint32_t i = 0U, j = count - 1U; i < count; j = i++) {
            int yi = points[i].y, yj = points[j].y;
            if (((yi <= y && yj > y) || (yj <= y && yi > y)) && used < SVG_POINT_MAX)
                intersections[used++] = points[i].x +
                    (int32_t)(((int64_t)(y - yi) * (points[j].x - points[i].x)) /
                              (yj - yi));
        }
        for (uint32_t i = 1U; i < used; i++) {
            int32_t value = intersections[i]; uint32_t j = i;
            while (j && intersections[j - 1U] > value) { intersections[j] = intersections[j - 1U]; j--; }
            intersections[j] = value;
        }
        for (uint32_t i = 0U; i + 1U < used; i += 2U) {
            int left = intersections[i], right = intersections[i + 1U];
            if (left < 0) left = 0;
            if (right >= (int)canvas->width) right = canvas->width - 1;
            for (int x = left; x <= right; x++) svg_pixel(canvas, x, y, color);
        }
    }
}

static void svg_rect(svg_canvas_t *canvas, const char *tag, const char *end,
                     uint32_t color) {
    int x0 = svg_x(canvas, svg_attr_number(tag, end, "x", 0));
    int y0 = svg_y(canvas, svg_attr_number(tag, end, "y", 0));
    int x1 = svg_x(canvas, svg_attr_number(tag, end, "x", 0) + svg_attr_number(tag, end, "width", 0));
    int y1 = svg_y(canvas, svg_attr_number(tag, end, "y", 0) + svg_attr_number(tag, end, "height", 0));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)canvas->width) x1 = canvas->width;
    if (y1 > (int)canvas->height) y1 = canvas->height;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) svg_pixel(canvas, x, y, color);
}

static void svg_ellipse(svg_canvas_t *canvas, const char *tag, const char *end,
                        uint32_t color, bool circle) {
    int cx = svg_x(canvas, svg_attr_number(tag, end, "cx", 0));
    int cy = svg_y(canvas, svg_attr_number(tag, end, "cy", 0));
    int32_t rxv = svg_attr_number(tag, end, circle ? "r" : "rx", 0);
    int32_t ryv = circle ? rxv : svg_attr_number(tag, end, "ry", 0);
    int rx = svg_x(canvas, canvas->view_x + rxv) - svg_x(canvas, canvas->view_x);
    int ry = svg_y(canvas, canvas->view_y + ryv) - svg_y(canvas, canvas->view_y);
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; y++) for (int x = -rx; x <= rx; x++)
        if ((int64_t)x*x*ry*ry + (int64_t)y*y*rx*rx <= (int64_t)rx*rx*ry*ry)
            svg_pixel(canvas, cx + x, cy + y, color);
}

static void svg_path(svg_canvas_t *canvas, const char *tag, const char *end,
                     uint32_t color) {
    const char *path, *path_end, *p;
    svg_point_t points[SVG_POINT_MAX];
    uint32_t count = 0U;
    int32_t x = 0, y = 0, start_x = 0, start_y = 0;
    char command = 0;
    if (!svg_attr(tag, end, "d", &path, &path_end)) return;
    p = path;
    while (p < path_end && count < SVG_POINT_MAX) {
        bool relative, valid;
        int32_t a, b;
        while (p < path_end && svg_space(*p)) p++;
        if (p >= path_end) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) command = *p++;
        if (!command) break;
        relative = command >= 'a' && command <= 'z';
        if (command == 'Z' || command == 'z') {
            x = start_x; y = start_y;
            if (count < SVG_POINT_MAX) points[count++] = (svg_point_t){svg_x(canvas,x),svg_y(canvas,y)};
            command = 0; continue;
        }
        if (command == 'H' || command == 'h') {
            a = svg_number(&p, path_end, &valid); if (!valid) break;
            x = relative ? x + a : a;
        } else if (command == 'V' || command == 'v') {
            a = svg_number(&p, path_end, &valid); if (!valid) break;
            y = relative ? y + a : a;
        } else if (command == 'M' || command == 'm' || command == 'L' || command == 'l') {
            a = svg_number(&p, path_end, &valid); if (!valid) break;
            b = svg_number(&p, path_end, &valid); if (!valid) break;
            x = relative ? x + a : a; y = relative ? y + b : b;
            if (command == 'M' || command == 'm') { start_x = x; start_y = y; command = relative ? 'l' : 'L'; }
        } else if (command == 'C' || command == 'c') {
            int32_t x1 = svg_number(&p,path_end,&valid); if(!valid) break;
            int32_t y1 = svg_number(&p,path_end,&valid); if(!valid) break;
            int32_t x2 = svg_number(&p,path_end,&valid); if(!valid) break;
            int32_t y2 = svg_number(&p,path_end,&valid); if(!valid) break;
            int32_t x3 = svg_number(&p,path_end,&valid); if(!valid) break;
            int32_t y3 = svg_number(&p,path_end,&valid); if(!valid) break;
            if (relative) { x1+=x; y1+=y; x2+=x; y2+=y; x3+=x; y3+=y; }
            for (int step=1; step<=12 && count<SVG_POINT_MAX; step++) {
                int32_t t=step*SVG_SCALE/12, u=SVG_SCALE-t;
                int64_t denominator=(int64_t)SVG_SCALE*SVG_SCALE*SVG_SCALE;
                int32_t px=(int32_t)(((int64_t)u*u*u*x+3LL*u*u*t*x1+3LL*u*t*t*x2+(int64_t)t*t*t*x3)/denominator);
                int32_t py=(int32_t)(((int64_t)u*u*u*y+3LL*u*u*t*y1+3LL*u*t*t*y2+(int64_t)t*t*t*y3)/denominator);
                points[count++]=(svg_point_t){svg_x(canvas,px),svg_y(canvas,py)};
            }
            x=x3; y=y3; continue;
        } else { command = 0; continue; }
        points[count++] = (svg_point_t){svg_x(canvas, x), svg_y(canvas, y)};
    }
    svg_fill_polygon(canvas, points, count, color);
}

bool gui_svg_decode(gui_image_t *image, const uint8_t *data, uint32_t length) {
    char *xml;
    const char *svg, *svg_end, *value, *value_end, *p, *end;
    svg_canvas_t canvas;
    int32_t width, height;
    if (!image || !data || length < 5U || length > 1024U * 1024U) return false;
    image->pixels = NULL; image->width = image->height = 0U;
    xml = (char *)kmalloc(length + 1U); if (!xml) return false;
    kmemcpy(xml, data, length); xml[length] = '\0'; end = xml + length;
    svg = strstr(xml, "<svg");
    if (!svg || svg >= end) { kfree(xml); return false; }
    svg_end = strchr(svg, '>'); if (!svg_end || svg_end > end) { kfree(xml); return false; }
    width = svg_attr_number(svg, svg_end, "width", 0);
    height = svg_attr_number(svg, svg_end, "height", 0);
    canvas.view_x = canvas.view_y = 0; canvas.view_w = width; canvas.view_h = height;
    if (svg_attr(svg, svg_end, "viewBox", &value, &value_end)) {
        bool valid;
        canvas.view_x=svg_number(&value,value_end,&valid); canvas.view_y=svg_number(&value,value_end,&valid);
        canvas.view_w=svg_number(&value,value_end,&valid); canvas.view_h=svg_number(&value,value_end,&valid);
    }
    if (width <= 0) width = canvas.view_w;
    if (height <= 0) height = canvas.view_h;
    canvas.width = (uint32_t)(width / SVG_SCALE); canvas.height = (uint32_t)(height / SVG_SCALE);
    if (!canvas.width) canvas.width = 128U;
    if (!canvas.height) canvas.height = 128U;
    if (!canvas.view_w) canvas.view_w = (int32_t)canvas.width * SVG_SCALE;
    if (!canvas.view_h) canvas.view_h = (int32_t)canvas.height * SVG_SCALE;
    if (canvas.width > 1024U || canvas.height > 768U || canvas.width * canvas.height > 600000U) { kfree(xml); return false; }
    canvas.pixels = (uint32_t *)kmalloc(canvas.width * canvas.height * 4U);
    if (!canvas.pixels) { kfree(xml); return false; }
    kmemset(canvas.pixels, 0, canvas.width * canvas.height * 4U);
    p = svg_end + 1;
    while (p < end && (p = strchr(p, '<')) != NULL && p < end) {
        const char *tag_end = strchr(p, '>');
        uint32_t color;
        if (!tag_end || tag_end > end) break;
        color = svg_color(p, tag_end);
        if (p + 5 < tag_end && kmemcmp(p, "<rect", 5) == 0) svg_rect(&canvas,p,tag_end,color);
        else if (p + 7 < tag_end && kmemcmp(p,"<circle",7)==0) svg_ellipse(&canvas,p,tag_end,color,true);
        else if (p + 8 < tag_end && kmemcmp(p,"<ellipse",8)==0) svg_ellipse(&canvas,p,tag_end,color,false);
        else if (p + 5 < tag_end && kmemcmp(p,"<path",5)==0) svg_path(&canvas,p,tag_end,color);
        p = tag_end + 1;
    }
    kfree(xml);
    image->pixels=canvas.pixels; image->width=(uint16_t)canvas.width; image->height=(uint16_t)canvas.height;
    return true;
}
