#include "css_engine.h"
#include "css_compat.h"
#include "layout.h"
#include "platform.h"

#include <libcss/libcss.h>
#include <libcss/unit.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NSBK_CSS_SHEET_MAX 64U
#define NSBK_CHAR_W 8
#define NSBK_BASE_LINE_H 13
#define NSBK_PAGE_PAD 8
#define NSBK_LAYOUT_BUDGET_MS 12000U

static const char nsbk_ua_css[] =
    "html,body{display:block;color:#000;background:#fff;font-size:12px;margin:0;padding:0}"
    "head,title,base,meta,link,style,script,template,source{display:none}"
    "noscript{display:block} picture{display:inline}"
    "address,article,aside,blockquote,div,dl,fieldset,figure,figcaption,footer,form,"
    "h1,h2,h3,h4,h5,h6,header,hr,main,nav,ol,p,pre,section,table,ul{display:block}"
    "center{display:block;text-align:center}"
    "li{display:list-item;margin-left:16px}"
    "a:link{color:#0000cc;text-decoration:underline}"
    "h1{font-size:24px;font-weight:bold;margin:10px 0 6px}"
    "h2{font-size:20px;font-weight:bold;margin:9px 0 5px}"
    "h3{font-size:17px;font-weight:bold;margin:8px 0 4px}"
    "h4,h5,h6{font-size:14px;font-weight:bold;margin:6px 0 3px}"
    "p{margin:5px 0} blockquote{margin:6px 20px}"
    "pre,code,kbd,samp{font-family:monospace;white-space:pre}"
    "strong,b{font-weight:bold} em,i{font-style:italic}"
    "ul,ol{margin:5px 0;padding-left:18px}"
    "table{display:table;border-spacing:2px} tbody{display:table-row-group}"
    "thead{display:table-header-group} tfoot{display:table-footer-group}"
    "tr{display:table-row} td,th{display:table-cell;padding:2px}"
    "th{font-weight:bold;background:#e8e8e8}"
    "input,button,textarea,select{display:inline-block;margin:2px;padding:3px;border:1px solid #808080;background:#fff;box-sizing:border-box}"
    "button,input[type=submit],input[type=button]{background:#e4e4e4;min-height:22px}"
    "input[type=hidden]{display:none}"
    "select{display:inline-block} img{display:inline-block;max-width:100%} br{display:inline} hr{border:1px solid #808080;margin:6px 0}";

typedef struct {
    css_select_ctx *select;
    css_stylesheet *sheets[NSBK_CSS_SHEET_MAX];
    bool sheet_owned[NSBK_CSS_SHEET_MAX];
    uint32_t sheet_count;
    css_unit_ctx units;
    css_media media;
    nsbk_layout_t *layout;
    nsbk_html_result_t *result;
    int32_t viewport_width;
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t line_start_x;
    int32_t line_right;
    int32_t base_line_start;
    int32_t base_line_right;
    int32_t line_height;
    int32_t float_left_width;
    int32_t float_right_width;
    int32_t float_bottom;
    uint32_t line_item_start;
    uint32_t work_counter;
    uint32_t deadline_ms;
    uint8_t text_align;
    bool pending_space;
    bool preformatted;
    bool line_positioned;
    bool aborted;
    int32_t active_link;
    nsbk_dom_node_t *force_inline_node;
    nsbk_css_compat_t compatibility;
} css_context_t;

static bool layout_should_abort(css_context_t *ctx) {
    if (!ctx) return true;
    if (ctx->aborted) return true;
    if (ctx->deadline_ms != 0U &&
        (int32_t)(bk_sys_uptime_ms() - ctx->deadline_ms) >= 0) {
        ctx->aborted = true;
        if (ctx->layout) ctx->layout->truncated = true;
        return true;
    }
    return false;
}

typedef struct {
    css_stylesheet *sheets[NSBK_CSS_SHEET_MAX];
    uint32_t sheet_count;
    css_stylesheet *root;
} nsbk_compiled_css_t;

typedef struct {
    uint32_t color;
    uint32_t background;
    uint32_t border;
    int32_t margin_top, margin_right, margin_bottom, margin_left;
    int32_t padding_top, padding_right, padding_bottom, padding_left;
    int32_t border_top, border_right, border_bottom, border_left;
    int32_t font_px;
    int32_t line_height;
    int32_t width;
    int32_t height;
    int32_t min_width, max_width, min_height, max_height;
    int32_t top, right, bottom, left;
    int32_t flex_basis;
    int32_t text_indent;
    int32_t flex_gap;
    int32_t order;
    int16_t letter_spacing;
    int16_t word_spacing;
    int16_t background_x;
    int16_t background_y;
    char background_image[256];
    uint8_t background_repeat;
    uint8_t text_align;
    uint8_t display;
    uint8_t float_mode;
    uint8_t clear_mode;
    uint8_t position;
    uint8_t vertical_align;
    uint8_t align_items;
    uint8_t justify_content;
    uint8_t flex_direction;
    uint8_t flex_wrap;
    uint8_t align_self;
    uint8_t align_content;
    uint8_t overflow_x;
    uint8_t overflow_y;
    uint8_t text_transform;
    uint8_t opacity;
    bool visible;
    bool bold;
    bool italic;
    bool monospace;
    bool underline;
    bool has_background;
    bool has_border;
    bool preformatted;
    bool no_wrap;
    bool border_box;
    bool width_set;
    bool height_set;
    bool min_width_set, max_width_set, min_height_set, max_height_set;
    bool top_set, right_set, bottom_set, left_set;
    bool flex_basis_set;
    bool background_x_percent, background_y_percent;
    bool margin_left_auto, margin_right_auto;
    bool has_background_image;
} paint_style_t;

typedef struct {
    int32_t cursor_x, cursor_y;
    int32_t line_start_x, line_right;
    int32_t base_line_start, base_line_right;
    int32_t line_height;
    int32_t float_left_width, float_right_width, float_bottom;
    uint32_t line_item_start;
    uint8_t text_align;
    bool pending_space, preformatted, line_positioned;
    int32_t active_link;
    nsbk_dom_node_t *force_inline_node;
} flow_state_t;

#define NSBK_FLEX_GROUP_MAX 48U

typedef struct {
    uint32_t item_start;
    uint32_t item_end;
    int32_t min_x, min_y, max_x, max_y;
    int32_t order;
    uint8_t align_self;
} flex_group_t;

static void save_flow(const css_context_t *ctx, flow_state_t *state) {
    if (!ctx || !state) return;
    state->cursor_x = ctx->cursor_x;
    state->cursor_y = ctx->cursor_y;
    state->line_start_x = ctx->line_start_x;
    state->line_right = ctx->line_right;
    state->base_line_start = ctx->base_line_start;
    state->base_line_right = ctx->base_line_right;
    state->line_height = ctx->line_height;
    state->float_left_width = ctx->float_left_width;
    state->float_right_width = ctx->float_right_width;
    state->float_bottom = ctx->float_bottom;
    state->line_item_start = ctx->line_item_start;
    state->text_align = ctx->text_align;
    state->pending_space = ctx->pending_space;
    state->preformatted = ctx->preformatted;
    state->line_positioned = ctx->line_positioned;
    state->active_link = ctx->active_link;
    state->force_inline_node = ctx->force_inline_node;
}

static void restore_flow(css_context_t *ctx, const flow_state_t *state) {
    if (!ctx || !state) return;
    ctx->cursor_x = state->cursor_x;
    ctx->cursor_y = state->cursor_y;
    ctx->line_start_x = state->line_start_x;
    ctx->line_right = state->line_right;
    ctx->base_line_start = state->base_line_start;
    ctx->base_line_right = state->base_line_right;
    ctx->line_height = state->line_height;
    ctx->float_left_width = state->float_left_width;
    ctx->float_right_width = state->float_right_width;
    ctx->float_bottom = state->float_bottom;
    ctx->line_item_start = state->line_item_start;
    ctx->text_align = state->text_align;
    ctx->pending_space = state->pending_space;
    ctx->preformatted = state->preformatted;
    ctx->line_positioned = state->line_positioned;
    ctx->active_link = state->active_link;
    ctx->force_inline_node = state->force_inline_node;
}

static void shift_item_range(nsbk_layout_t *layout, uint32_t start,
                             uint32_t end, int32_t dx, int32_t dy) {
    uint32_t i;
    if (!layout || (!dx && !dy)) return;
    if (end > layout->item_count) end = layout->item_count;
    for (i = start; i < end; i++) {
        layout->items[i].x += dx;
        layout->items[i].y += dy;
    }
}

static void shift_anchor_range(nsbk_layout_t *layout, uint32_t start,
                               int32_t dy) {
    uint32_t i;
    if (!layout || !dy) return;
    for (i = start; i < layout->anchor_count; i++)
        layout->anchors[i].y += dy;
}

static bool item_range_bounds(const nsbk_layout_t *layout, uint32_t start,
                              uint32_t end, flex_group_t *group) {
    uint32_t i;
    bool have = false;
    if (!layout || !group) return false;
    if (end > layout->item_count) end = layout->item_count;
    group->item_start = start;
    group->item_end = end;
    for (i = start; i < end; i++) {
        const nsbk_layout_item_t *item = &layout->items[i];
        int32_t right = item->x + (item->width > 0 ? item->width : 1);
        int32_t bottom = item->y + (item->height > 0 ? item->height : 1);
        if (!have) {
            group->min_x = item->x;
            group->min_y = item->y;
            group->max_x = right;
            group->max_y = bottom;
            have = true;
        } else {
            if (item->x < group->min_x) group->min_x = item->x;
            if (item->y < group->min_y) group->min_y = item->y;
            if (right > group->max_x) group->max_x = right;
            if (bottom > group->max_y) group->max_y = bottom;
        }
    }
    return have;
}

static void refresh_float_bounds(css_context_t *ctx) {
    if (!ctx) return;
    if (ctx->float_bottom > 0 && ctx->cursor_y >= ctx->float_bottom) {
        ctx->float_left_width = 0;
        ctx->float_right_width = 0;
        ctx->float_bottom = 0;
    }
    ctx->line_start_x = ctx->base_line_start + ctx->float_left_width;
    ctx->line_right = ctx->base_line_right - ctx->float_right_width;
    if (ctx->line_right <= ctx->line_start_x)
        ctx->line_right = ctx->line_start_x + 1;
    if (ctx->cursor_x < ctx->line_start_x || ctx->cursor_x > ctx->line_right)
        ctx->cursor_x = ctx->line_start_x;
}

static uint32_t positive_attribute(const nsbk_dom_node_t *node,
                                   const char *name, uint32_t fallback) {
    const char *value = nsbk_dom_attribute(node, name);
    uint32_t number = 0U;
    if (!value || *value < '0' || *value > '9') return fallback;
    while (*value >= '0' && *value <= '9') {
        number = number * 10U + (uint32_t)(*value++ - '0');
        if (number > 4096U) return 4096U;
    }
    return number ? number : fallback;
}

static bool text_equal_nocase(const char *a, const char *b);

static int32_t intrinsic_node_width(const nsbk_dom_node_t *node,
                                    int32_t available) {
    const nsbk_dom_node_t *child;
    int32_t sum = 0;
    int32_t maximum = 0;
    if (!node) return 16;
    if (node->type == NSBK_DOM_TEXT) {
        uint32_t i, run = 0U, best = 0U;
        for (i = 0U; i < node->text_length; i++) {
            unsigned char c = (unsigned char)node->text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') {
                if (run > best) best = run;
                run = 0U;
            } else run++;
        }
        if (run > best) best = run;
        if (best > 64U) best = 64U;
        return (int32_t)best * NSBK_CHAR_W;
    }
    if (node->type != NSBK_DOM_ELEMENT) return 16;
    if (nsbk_dom_name_is(node, "img")) {
        uint32_t width = positive_attribute(node, "width", 0U);
        return width ? (int32_t)width : 96;
    }
    if (nsbk_dom_name_is(node, "input")) {
        const char *type = nsbk_dom_attribute(node, "type");
        const char *size = nsbk_dom_attribute(node, "size");
        if (type && text_equal_nocase(type, "hidden")) return 0;
        if (size && size[0] >= '0' && size[0] <= '9') {
            uint32_t columns = positive_attribute(node, "size", 20U);
            if (columns > 80U) columns = 80U;
            return (int32_t)columns * NSBK_CHAR_W + 16;
        }
        return 190;
    }
    if (nsbk_dom_name_is(node, "textarea")) return 240;
    if (nsbk_dom_name_is(node, "select")) return 110;
    if (nsbk_dom_name_is(node, "button")) {
        int32_t width = 0;
        for (child = node->first_child; child; child = child->next)
            width += intrinsic_node_width(child, available);
        if (width < 40) width = 40;
        return width + 18;
    }
    for (child = node->first_child; child; child = child->next) {
        int32_t width = intrinsic_node_width(child, available);
        if (child->type == NSBK_DOM_ELEMENT &&
            (nsbk_dom_name_is(child, "div") || nsbk_dom_name_is(child, "p") ||
             nsbk_dom_name_is(child, "section") || nsbk_dom_name_is(child, "ul") ||
             nsbk_dom_name_is(child, "ol") || nsbk_dom_name_is(child, "table"))) {
            if (width > maximum) maximum = width;
        } else {
            sum += width;
            if (sum > maximum) maximum = sum;
        }
    }
    if (maximum < 16) maximum = 16;
    maximum += 8;
    if (available > 0 && maximum > available) maximum = available;
    return maximum;
}

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool text_equal_nocase(const char *a, const char *b) {
    uint32_t i = 0U;
    if (!a || !b) return false;
    while (a[i] && b[i]) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
        i++;
    }
    return a[i] == b[i];
}

static bool token_contains_nocase(const char *value, const char *wanted) {
    uint32_t i = 0U;
    uint32_t wanted_length = (uint32_t)strlen(wanted);
    if (!value || !wanted_length) return false;
    while (value[i]) {
        uint32_t start, length, j;
        while (value[i] == ' ' || value[i] == '\t' || value[i] == '\r' ||
               value[i] == '\n' || value[i] == '\f') i++;
        start = i;
        while (value[i] && value[i] != ' ' && value[i] != '\t' &&
               value[i] != '\r' && value[i] != '\n' && value[i] != '\f') i++;
        length = i - start;
        if (length != wanted_length) continue;
        for (j = 0U; j < length; j++)
            if (ascii_lower(value[start + j]) != ascii_lower(wanted[j])) break;
        if (j == length) return true;
    }
    return false;
}

static bool bytes_equal_nocase(const char *a, uint32_t alen,
                               const char *b, uint32_t blen) {
    uint32_t i;
    if (!a || !b || alen != blen) return false;
    for (i = 0; i < alen; i++)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}

static nsbk_dom_node_t *element_parent(nsbk_dom_node_t *node) {
    node = node ? node->parent : NULL;
    while (node && node->type != NSBK_DOM_ELEMENT) node = node->parent;
    return node;
}

static nsbk_dom_node_t *previous_element(nsbk_dom_node_t *node) {
    node = node ? node->previous : NULL;
    while (node && node->type != NSBK_DOM_ELEMENT) node = node->previous;
    return node;
}

static bool qname_matches(nsbk_dom_node_t *node, const css_qname *qname) {
    bool match = false;
    if (!node || node->type != NSBK_DOM_ELEMENT || !node->name || !qname ||
        !qname->name) return false;
    if (lwc_string_length(qname->name) == 1U &&
        lwc_string_data(qname->name)[0] == '*') return true;
    if (lwc_string_caseless_isequal(node->name, qname->name, &match) !=
        lwc_error_ok) return false;
    return match;
}

static css_error selector_node_name(void *pw, void *raw, css_qname *qname) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    (void)pw;
    if (!node || !qname || !node->name) return CSS_BADPARM;
    qname->ns = NULL;
    qname->name = lwc_string_ref(node->name);
    return CSS_OK;
}

static bool ensure_classes(nsbk_dom_node_t *node) {
    const char *value;
    uint32_t count = 0U, i = 0U, start;
    lwc_string **classes;
    if (!node || node->type != NSBK_DOM_ELEMENT) return true;
    if (node->css_classes_ready) return true;
    node->css_classes_ready = true;
    value = nsbk_dom_attribute(node, "class");
    if (!value || !value[0]) return true;
    while (value[i]) {
        while (value[i] == ' ' || value[i] == '\t' || value[i] == '\r' ||
               value[i] == '\n' || value[i] == '\f') i++;
        if (!value[i]) break;
        count++;
        while (value[i] && value[i] != ' ' && value[i] != '\t' &&
               value[i] != '\r' && value[i] != '\n' && value[i] != '\f') i++;
    }
    if (count == 0U) return true;
    classes = (lwc_string **)calloc(count, sizeof(*classes));
    if (!classes) return false;
    i = 0U; count = 0U;
    while (value[i]) {
        while (value[i] == ' ' || value[i] == '\t' || value[i] == '\r' ||
               value[i] == '\n' || value[i] == '\f') i++;
        start = i;
        while (value[i] && value[i] != ' ' && value[i] != '\t' &&
               value[i] != '\r' && value[i] != '\n' && value[i] != '\f') i++;
        if (i > start && lwc_intern_string(value + start, i - start,
                                           &classes[count]) == lwc_error_ok)
            count++;
    }
    node->css_classes = classes;
    node->css_class_count = count;
    return true;
}

static css_error selector_node_classes(void *pw, void *raw,
        lwc_string ***classes, uint32_t *count) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    uint32_t i;
    (void)pw;
    if (!node || !classes || !count) return CSS_BADPARM;
    if (!ensure_classes(node)) return CSS_NOMEM;
    *classes = node->css_classes;
    *count = node->css_class_count;
    for (i = 0U; i < *count; i++) lwc_string_ref((*classes)[i]);
    return CSS_OK;
}

static css_error selector_node_id(void *pw, void *raw, lwc_string **id) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    const char *value;
    (void)pw;
    if (!node || !id) return CSS_BADPARM;
    *id = NULL;
    if (!node->css_id_ready) {
        node->css_id_ready = true;
        value = nsbk_dom_attribute(node, "id");
        if (value && value[0] && lwc_intern_string(value, strlen(value),
                &node->css_id) != lwc_error_ok) return CSS_NOMEM;
    }
    if (node->css_id) *id = lwc_string_ref(node->css_id);
    return CSS_OK;
}

static css_error named_ancestor(void *pw, void *raw,
        const css_qname *qname, void **result) {
    nsbk_dom_node_t *node = element_parent((nsbk_dom_node_t *)raw);
    (void)pw;
    while (node && !qname_matches(node, qname)) node = element_parent(node);
    *result = node;
    return CSS_OK;
}

static css_error named_parent(void *pw, void *raw,
        const css_qname *qname, void **result) {
    nsbk_dom_node_t *node = element_parent((nsbk_dom_node_t *)raw);
    (void)pw;
    *result = node && qname_matches(node, qname) ? node : NULL;
    return CSS_OK;
}

static css_error named_sibling(void *pw, void *raw,
        const css_qname *qname, void **result) {
    nsbk_dom_node_t *node = previous_element((nsbk_dom_node_t *)raw);
    (void)pw;
    *result = node && qname_matches(node, qname) ? node : NULL;
    return CSS_OK;
}

static css_error named_generic_sibling(void *pw, void *raw,
        const css_qname *qname, void **result) {
    nsbk_dom_node_t *node = previous_element((nsbk_dom_node_t *)raw);
    (void)pw;
    while (node && !qname_matches(node, qname)) node = previous_element(node);
    *result = node;
    return CSS_OK;
}

static css_error parent_node(void *pw, void *raw, void **result) {
    (void)pw;
    *result = element_parent((nsbk_dom_node_t *)raw);
    return CSS_OK;
}

static css_error sibling_node(void *pw, void *raw, void **result) {
    (void)pw;
    *result = previous_element((nsbk_dom_node_t *)raw);
    return CSS_OK;
}

static css_error node_has_name(void *pw, void *raw,
        const css_qname *qname, bool *match) {
    (void)pw;
    *match = qname_matches((nsbk_dom_node_t *)raw, qname);
    return CSS_OK;
}

static css_error node_has_class(void *pw, void *raw,
        lwc_string *name, bool *match) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    uint32_t i;
    (void)pw;
    *match = false;
    if (!ensure_classes(node)) return CSS_NOMEM;
    for (i = 0U; i < node->css_class_count; i++) {
        bool equal = false;
        if (lwc_string_caseless_isequal(node->css_classes[i], name, &equal) ==
            lwc_error_ok && equal) { *match = true; break; }
    }
    return CSS_OK;
}

static css_error node_has_id(void *pw, void *raw,
        lwc_string *name, bool *match) {
    lwc_string *id = NULL;
    bool equal = false;
    css_error error = selector_node_id(pw, raw, &id);
    if (error != CSS_OK) return error;
    if (id) {
        lwc_string_caseless_isequal(id, name, &equal);
        lwc_string_unref(id);
    }
    *match = equal;
    return CSS_OK;
}

static const char *attribute_value(nsbk_dom_node_t *node,
                                   const css_qname *qname) {
    uint32_t i;
    if (!node || !qname || !qname->name) return NULL;
    for (i = 0U; i < node->attribute_count; i++) {
        bool equal = false;
        if (node->attributes[i].name &&
            lwc_string_caseless_isequal(node->attributes[i].name,
                                        qname->name, &equal) == lwc_error_ok &&
            equal) return node->attributes[i].value;
    }
    return NULL;
}

static css_error node_has_attribute(void *pw, void *raw,
        const css_qname *qname, bool *match) {
    (void)pw;
    *match = attribute_value((nsbk_dom_node_t *)raw, qname) != NULL;
    return CSS_OK;
}

static css_error attribute_compare(void *pw, void *raw,
        const css_qname *qname, lwc_string *wanted, bool *match, int mode) {
    const char *value = attribute_value((nsbk_dom_node_t *)raw, qname);
    const char *needle = wanted ? lwc_string_data(wanted) : NULL;
    uint32_t vlen = value ? (uint32_t)strlen(value) : 0U;
    uint32_t nlen = wanted ? (uint32_t)lwc_string_length(wanted) : 0U;
    uint32_t i, start;
    (void)pw;
    *match = false;
    if (!value || !needle) return CSS_OK;
    if (mode == 0) *match = bytes_equal_nocase(value, vlen, needle, nlen);
    else if (mode == 1) {
        *match = vlen >= nlen && bytes_equal_nocase(value, nlen, needle, nlen) &&
                 (vlen == nlen || value[nlen] == '-');
    } else if (mode == 2) {
        for (i = 0U; i < vlen;) {
            while (i < vlen && (value[i] == ' ' || value[i] == '\t' ||
                   value[i] == '\r' || value[i] == '\n' || value[i] == '\f')) i++;
            start = i;
            while (i < vlen && value[i] != ' ' && value[i] != '\t' &&
                   value[i] != '\r' && value[i] != '\n' && value[i] != '\f') i++;
            if (bytes_equal_nocase(value + start, i - start, needle, nlen)) {
                *match = true; break;
            }
        }
    } else if (mode == 3) *match = vlen >= nlen &&
        bytes_equal_nocase(value, nlen, needle, nlen);
    else if (mode == 4) *match = vlen >= nlen &&
        bytes_equal_nocase(value + vlen - nlen, nlen, needle, nlen);
    else if (mode == 5 && nlen > 0U && vlen >= nlen) {
        for (i = 0U; i + nlen <= vlen; i++)
            if (bytes_equal_nocase(value + i, nlen, needle, nlen)) {
                *match = true; break;
            }
    }
    return CSS_OK;
}

#define ATTR_HANDLER(name, mode) \
static css_error name(void *pw, void *node, const css_qname *qname, \
        lwc_string *value, bool *match) { \
    return attribute_compare(pw, node, qname, value, match, mode); \
}
ATTR_HANDLER(node_attr_equal, 0)
ATTR_HANDLER(node_attr_dash, 1)
ATTR_HANDLER(node_attr_includes, 2)
ATTR_HANDLER(node_attr_prefix, 3)
ATTR_HANDLER(node_attr_suffix, 4)
ATTR_HANDLER(node_attr_substring, 5)

static css_error node_is_root(void *pw, void *raw, bool *match) {
    (void)pw;
    *match = element_parent((nsbk_dom_node_t *)raw) == NULL;
    return CSS_OK;
}

static css_error node_count_siblings(void *pw, void *raw, bool same_name,
        bool after, int32_t *count) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    nsbk_dom_node_t *current = after ? node->next : node->previous;
    (void)pw;
    *count = 0;
    while (current) {
        if (current->type == NSBK_DOM_ELEMENT &&
            (!same_name || (node->name && current->name &&
             bytes_equal_nocase(lwc_string_data(node->name),
                (uint32_t)lwc_string_length(node->name),
                lwc_string_data(current->name),
                (uint32_t)lwc_string_length(current->name))))) (*count)++;
        current = after ? current->next : current->previous;
    }
    return CSS_OK;
}

static css_error node_is_empty(void *pw, void *raw, bool *match) {
    nsbk_dom_node_t *child = ((nsbk_dom_node_t *)raw)->first_child;
    (void)pw;
    *match = true;
    while (child) {
        if (child->type == NSBK_DOM_ELEMENT ||
            (child->type == NSBK_DOM_TEXT && child->text_length > 0U)) {
            *match = false; break;
        }
        child = child->next;
    }
    return CSS_OK;
}

static css_error node_is_link(void *pw, void *raw, bool *match) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    (void)pw;
    *match = nsbk_dom_name_is(node, "a") &&
             nsbk_dom_attribute(node, "href") != NULL;
    return CSS_OK;
}

static css_error pseudo_false(void *pw, void *node, bool *match) {
    (void)pw; (void)node; *match = false; return CSS_OK;
}
static css_error node_is_enabled(void *pw, void *raw, bool *match) {
    (void)pw;
    *match = nsbk_dom_attribute((nsbk_dom_node_t *)raw, "disabled") == NULL;
    return CSS_OK;
}
static css_error node_is_disabled(void *pw, void *raw, bool *match) {
    (void)pw;
    *match = nsbk_dom_attribute((nsbk_dom_node_t *)raw, "disabled") != NULL;
    return CSS_OK;
}
static css_error node_is_checked(void *pw, void *raw, bool *match) {
    (void)pw;
    *match = nsbk_dom_attribute((nsbk_dom_node_t *)raw, "checked") != NULL ||
             nsbk_dom_attribute((nsbk_dom_node_t *)raw, "selected") != NULL;
    return CSS_OK;
}

static css_error node_is_lang(void *pw, void *raw,
        lwc_string *language, bool *match) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    const char *wanted = lwc_string_data(language);
    uint32_t wanted_len = (uint32_t)lwc_string_length(language);
    (void)pw;
    *match = false;
    while (node) {
        const char *value = nsbk_dom_attribute(node, "lang");
        if (value) {
            uint32_t len = (uint32_t)strlen(value);
            *match = len >= wanted_len &&
                bytes_equal_nocase(value, wanted_len, wanted, wanted_len) &&
                (len == wanted_len || value[wanted_len] == '-');
            break;
        }
        node = element_parent(node);
    }
    return CSS_OK;
}

static css_error no_hints(void *pw, void *node, uint32_t *count,
                          css_hint **hints) {
    (void)pw; (void)node; *count = 0U; *hints = NULL; return CSS_OK;
}

static css_error ua_default(void *pw, uint32_t property, css_hint *hint) {
    (void)pw;
    if (property == CSS_PROP_COLOR) {
        hint->data.color = 0xff000000U;
        hint->status = CSS_COLOR_COLOR;
    } else if (property == CSS_PROP_FONT_FAMILY) {
        hint->data.strings = NULL;
        hint->status = CSS_FONT_FAMILY_SANS_SERIF;
    } else if (property == CSS_PROP_QUOTES) {
        hint->data.strings = NULL;
        hint->status = CSS_QUOTES_NONE;
    } else if (property == CSS_PROP_VOICE_FAMILY) {
        hint->data.strings = NULL;
        hint->status = 0;
    } else return CSS_INVALID;
    return CSS_OK;
}

static css_error set_node_data(void *pw, void *raw, void *data) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    (void)pw;
    if (!node) return CSS_BADPARM;
    node->libcss_node_data = data;
    return CSS_OK;
}
static css_error get_node_data(void *pw, void *raw, void **data) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw;
    (void)pw;
    if (!node || !data) return CSS_BADPARM;
    *data = node->libcss_node_data;
    return CSS_OK;
}

static css_select_handler selector_handler = {
    CSS_SELECT_HANDLER_VERSION_1,
    selector_node_name, selector_node_classes, selector_node_id,
    named_ancestor, named_parent, named_sibling, named_generic_sibling,
    parent_node, sibling_node, node_has_name, node_has_class, node_has_id,
    node_has_attribute, node_attr_equal, node_attr_dash, node_attr_includes,
    node_attr_prefix, node_attr_suffix, node_attr_substring,
    node_is_root, node_count_siblings, node_is_empty, node_is_link,
    pseudo_false, pseudo_false, pseudo_false, pseudo_false,
    node_is_enabled, node_is_disabled, node_is_checked, pseudo_false,
    node_is_lang, no_hints, ua_default, set_node_data, get_node_data
};

static css_error resolve_url(void *pw, const char *base, lwc_string *relative,
                             lwc_string **absolute) {
    char resolved[NSBK_URL_MAX];
    const char *reference;
    const char *fallback = (const char *)pw;
    lwc_error error;
    if (!relative || !absolute) return CSS_BADPARM;
    reference = lwc_string_data(relative);
    if (!reference || !reference[0]) {
        *absolute = lwc_string_ref(relative);
        return CSS_OK;
    }
    if (strncmp(reference, "data:", 5U) == 0 || reference[0] == '#') {
        *absolute = lwc_string_ref(relative);
        return CSS_OK;
    }
    nsbk_url_resolve(resolved, sizeof(resolved),
                     base && base[0] ? base : fallback, reference);
    if (!resolved[0]) {
        *absolute = lwc_string_ref(relative);
        return CSS_OK;
    }
    error = lwc_intern_string(resolved, strlen(resolved), absolute);
    return error == lwc_error_ok ? CSS_OK : CSS_NOMEM;
}

static css_error ignore_import(void *pw, css_stylesheet *parent,
                               lwc_string *url) {
    (void)pw;
    (void)parent;
    (void)url;
    /* Imports are fetched by platform.c and registered below, using
       css_stylesheet_next_pending_import(), like the NetSurf content layer. */
    return CSS_OK;
}

static css_error system_color(void *pw, lwc_string *name, css_color *color) {
    const char *n = lwc_string_data(name);
    (void)pw;
    if (text_equal_nocase(n, "window")) *color = 0xffffffffU;
    else if (text_equal_nocase(n, "windowtext")) *color = 0xff000000U;
    else if (text_equal_nocase(n, "highlight")) *color = 0xff000080U;
    else if (text_equal_nocase(n, "highlighttext")) *color = 0xffffffffU;
    else return CSS_INVALID;
    return CSS_OK;
}

static css_stylesheet *create_sheet(nsbk_css_compat_t *compatibility,
                                    const uint8_t *data, uint32_t length,
                                    bool inline_style, bool quirks,
                                    const char *url) {
    css_stylesheet_params params;
    css_stylesheet *sheet = NULL;
    css_error error;
    static const uint8_t empty_css[] = "";
    uint8_t *compatible_data = NULL;
    uint32_t compatible_length = 0U;
    if (!data) data = empty_css;
    if (compatibility && length &&
        nsbk_css_compat_process(compatibility, data, length,
                                &compatible_data, &compatible_length)) {
        data = compatible_data;
        length = compatible_length;
    }
    memset(&params, 0, sizeof(params));
    params.params_version = CSS_STYLESHEET_PARAMS_VERSION_1;
    params.level = CSS_LEVEL_DEFAULT;
    /* External sheets may declare @charset or carry a HTTP charset. Let
       libparserutils perform detection instead of forcing every sheet UTF-8. */
    params.charset = inline_style ? "UTF-8" : NULL;
    params.url = url && url[0] ? url : "about:bleskernos";
    params.allow_quirks = quirks;
    params.inline_style = inline_style;
    params.resolve = resolve_url;
    params.resolve_pw = (void *)params.url;
    params.import = ignore_import;
    params.import_pw = NULL;
    params.color = system_color;
    error = css_stylesheet_create(&params, &sheet);
    if (error != CSS_OK) { free(compatible_data); return NULL; }
    if (length) {
        error = css_stylesheet_append_data(sheet, data, length);
        if (error != CSS_OK && error != CSS_NEEDDATA) {
            css_stylesheet_destroy(sheet);
            free(compatible_data);
            return NULL;
        }
    }
    error = css_stylesheet_data_done(sheet);
    if (error != CSS_OK && error != CSS_IMPORTS_PENDING) {
        css_stylesheet_destroy(sheet);
        free(compatible_data);
        return NULL;
    }
    free(compatible_data);
    return sheet;
}

static void collect_text(nsbk_dom_node_t *node, char *buffer,
                         uint32_t capacity, uint32_t *used) {
    nsbk_dom_node_t *child;
    uint32_t copy;
    if (!node || !buffer || !used || *used >= capacity) return;
    if (node->type == NSBK_DOM_TEXT && node->text) {
        copy = node->text_length;
        if (copy > capacity - *used - 1U) copy = capacity - *used - 1U;
        memcpy(buffer + *used, node->text, copy);
        *used += copy;
        buffer[*used] = '\0';
    }
    for (child = node->first_child; child; child = child->next)
        collect_text(child, buffer, capacity, used);
}

static const nsbk_css_resource_t *find_stylesheet_resource(
        const css_context_t *ctx, const char *reference) {
    uint32_t i;
    char absolute[NSBK_URL_MAX];
    const nsbk_resource_environment_t *resources;
    if (!ctx || !ctx->result || !reference || !ctx->result->resources)
        return NULL;
    resources = ctx->result->resources;
    for (i = 0U; i < resources->stylesheet_count; i++) {
        if (resources->stylesheets[i].reference &&
            strcmp(resources->stylesheets[i].reference, reference) == 0)
            return &resources->stylesheets[i];
    }
    nsbk_url_resolve(absolute, sizeof(absolute), resources->document_url,
                     reference);
    if (!absolute[0]) return NULL;
    for (i = 0U; i < resources->stylesheet_count; i++) {
        if (resources->stylesheets[i].url &&
            strcmp(resources->stylesheets[i].url, absolute) == 0)
            return &resources->stylesheets[i];
    }
    return NULL;
}

static const nsbk_css_resource_t *find_stylesheet_url(
        const css_context_t *ctx, const char *url) {
    uint32_t i;
    const nsbk_resource_environment_t *resources;
    if (!ctx || !url || !ctx->result || !ctx->result->resources) return NULL;
    resources = ctx->result->resources;
    for (i = 0U; i < resources->stylesheet_count; i++) {
        if (resources->stylesheets[i].url &&
            strcmp(resources->stylesheets[i].url, url) == 0)
            return &resources->stylesheets[i];
    }
    return NULL;
}

static bool remember_sheet(css_context_t *ctx, css_stylesheet *sheet,
                           bool owned) {
    if (!ctx || !sheet || ctx->sheet_count >= NSBK_CSS_SHEET_MAX) return false;
    ctx->sheets[ctx->sheet_count] = sheet;
    ctx->sheet_owned[ctx->sheet_count++] = owned;
    return true;
}

static bool register_pending_imports(css_context_t *ctx,
                                     css_stylesheet *parent,
                                     uint32_t depth) {
    lwc_string *import_url = NULL;
    if (!ctx || !parent) return false;
    if (depth >= 8U) return true;
    while (css_stylesheet_next_pending_import(parent, &import_url) == CSS_OK) {
        const char *url = lwc_string_data(import_url);
        const nsbk_css_resource_t *resource = find_stylesheet_url(ctx, url);
        css_stylesheet *child = NULL;
        if (resource && resource->data && resource->length) {
            child = create_sheet(&ctx->compatibility, resource->data, resource->length, false,
                                 ctx->result->quirks_mode != 0U,
                                 resource->url);
        }
        if (!child) {
            /* libcss requires an empty sheet when a fetch failed. This keeps
               later rules in the parent active instead of discarding it. */
            child = create_sheet(&ctx->compatibility, NULL, 0U, false,
                                 ctx->result->quirks_mode != 0U,
                                 url && url[0] ? url : "about:blank");
        }
        if (!child || !remember_sheet(ctx, child, true)) {
            if (child) css_stylesheet_destroy(child);
            lwc_string_unref(import_url);
            return false;
        }
        (void)register_pending_imports(ctx, child, depth + 1U);
        if (css_stylesheet_register_import(parent, child) != CSS_OK) {
            lwc_string_unref(import_url);
            return false;
        }
        lwc_string_unref(import_url);
        import_url = NULL;
    }
    return true;
}

static void append_author_sheet(css_context_t *ctx, const uint8_t *data,
                                uint32_t length, const char *url,
                                const char *media) {
    css_stylesheet *sheet;
    /* Zero bytes are a complete, valid stylesheet.  Keep it in the selection
       context just like upstream NetSurf instead of turning a successful
       empty HTTP response into a loader failure. */
    if (!ctx || !data || ctx->sheet_count >= NSBK_CSS_SHEET_MAX)
        return;
    sheet = create_sheet(&ctx->compatibility, data, length, false,
                         ctx->result->quirks_mode != 0U, url);
    if (!sheet) return;
    if (!remember_sheet(ctx, sheet, true)) {
        css_stylesheet_destroy(sheet);
        return;
    }
    (void)register_pending_imports(ctx, sheet, 0U);
    if (css_select_ctx_append_sheet(ctx->select, sheet, CSS_ORIGIN_AUTHOR,
            media && media[0] ? media : "all") != CSS_OK) {
        /* The sheet remains owned by ctx and will be destroyed at teardown. */
        return;
    }
}

static void append_compiled_resource(css_context_t *ctx,
                                     const nsbk_css_resource_t *constant,
                                     const char *media) {
    nsbk_css_resource_t *resource = (nsbk_css_resource_t *)constant;
    nsbk_compiled_css_t *compiled;
    css_stylesheet *sheet;
    uint32_t start, i;
    if (!ctx || !resource || !resource->data ||
        ctx->sheet_count >= NSBK_CSS_SHEET_MAX) return;
    compiled = (nsbk_compiled_css_t *)resource->compiled;
    if (compiled) {
        uint8_t *discard = NULL;
        uint32_t discard_length = 0U;
        /* Recollect custom properties on relayout: the parsed libcss sheet is
           cached, while the compatibility environment is per document pass. */
        if (nsbk_css_compat_process(&ctx->compatibility, resource->data,
                                    resource->length, &discard,
                                    &discard_length)) free(discard);
        for (i = 0U; i < compiled->sheet_count; i++)
            if (!remember_sheet(ctx, compiled->sheets[i], false)) return;
        (void)css_select_ctx_append_sheet(ctx->select, compiled->root,
            CSS_ORIGIN_AUTHOR, media && media[0] ? media : "all");
        return;
    }
    compiled = (nsbk_compiled_css_t *)calloc(1U, sizeof(*compiled));
    if (!compiled) {
        append_author_sheet(ctx, resource->data, resource->length,
                            resource->url, media);
        return;
    }
    sheet = create_sheet(&ctx->compatibility, resource->data, resource->length, false,
                         ctx->result->quirks_mode != 0U, resource->url);
    if (!sheet) { free(compiled); return; }
    start = ctx->sheet_count;
    if (!remember_sheet(ctx, sheet, true)) {
        css_stylesheet_destroy(sheet);
        free(compiled);
        return;
    }
    (void)register_pending_imports(ctx, sheet, 0U);
    if (css_select_ctx_append_sheet(ctx->select, sheet, CSS_ORIGIN_AUTHOR,
            media && media[0] ? media : "all") != CSS_OK) {
        free(compiled);
        return;
    }
    compiled->root = sheet;
    for (i = start; i < ctx->sheet_count; i++) {
        compiled->sheets[compiled->sheet_count++] = ctx->sheets[i];
        ctx->sheet_owned[i] = false;
    }
    resource->compiled = compiled;
}

static void collect_author_sheets(css_context_t *ctx, nsbk_dom_node_t *node) {
    nsbk_dom_node_t *child;
    if (!ctx || !node || layout_should_abort(ctx)) return;
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "link")) {
        const char *rel = nsbk_dom_attribute(node, "rel");
        const char *href = nsbk_dom_attribute(node, "href");
        const char *media = nsbk_dom_attribute(node, "media");
        const nsbk_css_resource_t *resource = NULL;
        if (rel && href && token_contains_nocase(rel, "stylesheet") &&
            !token_contains_nocase(rel, "alternate"))
            resource = find_stylesheet_resource(ctx, href);
        if (resource && !resource->imported)
            append_compiled_resource(ctx, resource,
                media && media[0] ? media : resource->media);
    } else if (node->type == NSBK_DOM_ELEMENT &&
               nsbk_dom_name_is(node, "style")) {
        char *css = (char *)malloc(65536U);
        uint32_t used = 0U;
        const char *media = nsbk_dom_attribute(node, "media");
        if (css) {
            css[0] = '\0';
            collect_text(node, css, 65536U, &used);
            append_author_sheet(ctx, (const uint8_t *)css, used,
                ctx->result->resources ?
                ctx->result->resources->document_url : "about:bleskernos",
                media && media[0] ? media : "all");
            free(css);
        }
    }
    for (child = node->first_child; child && !ctx->aborted;
         child = child->next)
        collect_author_sheets(ctx, child);
}

static int32_t css_length_px_relative(const css_computed_style *style,
        const css_unit_ctx *units, uint8_t status, uint8_t set_value,
        css_fixed length, css_unit unit, int32_t relative) {
    css_fixed px;
    if (status != set_value) return 0;
    if (unit == CSS_UNIT_PCT) {
        int32_t percentage = FIXTOINT(length);
        return relative > 0 ? relative * percentage / 100 : 0;
    }
    px = css_unit_len2css_px(style, units, length, unit);
    return FIXTOINT(px);
}

static int32_t css_length_px(const css_computed_style *style,
        const css_unit_ctx *units, uint8_t status, uint8_t set_value,
        css_fixed length, css_unit unit) {
    return css_length_px_relative(style, units, status, set_value,
                                  length, unit, 0);
}

static uint32_t color_to_rgb(css_color color) {
    return color & 0x00ffffffU;
}

static void extract_style(css_context_t *ctx, css_computed_style *computed,
                          paint_style_t *style, bool root) {
    css_fixed length;
    css_unit unit;
    css_color color;
    uint8_t value;
    memset(style, 0, sizeof(*style));
    style->color = 0x00000000U;
    style->background = 0x00ffffffU;
    style->border = 0x00808080U;
    style->display = css_computed_display(computed, root);
    style->visible = css_computed_visibility(computed) == CSS_VISIBILITY_VISIBLE;
    style->float_mode = css_computed_float(computed);
    style->clear_mode = css_computed_clear(computed);
    style->position = css_computed_position(computed);
    style->vertical_align = css_computed_vertical_align(computed, &length, &unit);
    style->align_items = css_computed_align_items(computed);
    style->align_self = css_computed_align_self(computed);
    style->align_content = css_computed_align_content(computed);
    style->justify_content = css_computed_justify_content(computed);
    style->flex_direction = css_computed_flex_direction(computed);
    style->flex_wrap = css_computed_flex_wrap(computed);
    style->overflow_x = css_computed_overflow_x(computed);
    style->overflow_y = css_computed_overflow_y(computed);
    style->text_transform = css_computed_text_transform(computed);
    style->opacity = 255U;
    style->border_box = css_computed_box_sizing(computed) == CSS_BOX_SIZING_BORDER_BOX;
    if (css_computed_color(computed, &color) == CSS_COLOR_COLOR)
        style->color = color_to_rgb(color);
    if (css_computed_background_color(computed, &color) ==
        CSS_BACKGROUND_COLOR_COLOR && (color >> 24) != 0U) {
        style->background = color_to_rgb(color);
        style->has_background = true;
    }
    {
        lwc_string *image_url = NULL;
        if (css_computed_background_image(computed, &image_url) ==
            CSS_BACKGROUND_IMAGE_IMAGE && image_url) {
            uint32_t copy = (uint32_t)lwc_string_length(image_url);
            if (copy >= sizeof(style->background_image))
                copy = sizeof(style->background_image) - 1U;
            memcpy(style->background_image, lwc_string_data(image_url), copy);
            style->background_image[copy] = '\0';
            style->has_background_image = copy > 0U;
        }
        style->background_repeat = css_computed_background_repeat(computed);
        {
            css_fixed hx = 0, vy = 0;
            css_unit hu = CSS_UNIT_PX, vu = CSS_UNIT_PX;
            if (css_computed_background_position(computed, &hx, &hu, &vy, &vu) ==
                CSS_BACKGROUND_POSITION_SET) {
                if (hu == CSS_UNIT_PCT) {
                    style->background_x = (int16_t)FIXTOINT(hx);
                    style->background_x_percent = true;
                } else {
                    int32_t px = css_length_px(computed, &ctx->units,
                        CSS_BACKGROUND_POSITION_SET,
                        CSS_BACKGROUND_POSITION_SET, hx, hu);
                    if (px < -32768) px = -32768;
                    if (px > 32767) px = 32767;
                    style->background_x = (int16_t)px;
                }
                if (vu == CSS_UNIT_PCT) {
                    style->background_y = (int16_t)FIXTOINT(vy);
                    style->background_y_percent = true;
                } else {
                    int32_t px = css_length_px(computed, &ctx->units,
                        CSS_BACKGROUND_POSITION_SET,
                        CSS_BACKGROUND_POSITION_SET, vy, vu);
                    if (px < -32768) px = -32768;
                    if (px > 32767) px = 32767;
                    style->background_y = (int16_t)px;
                }
            }
        }
    }
    style->text_align = css_computed_text_align(computed);
    value = css_computed_font_weight(computed);
    style->bold = value == CSS_FONT_WEIGHT_BOLD ||
        value == CSS_FONT_WEIGHT_BOLDER || value >= CSS_FONT_WEIGHT_600;
    value = css_computed_font_style(computed);
    style->italic = value == CSS_FONT_STYLE_ITALIC ||
                    value == CSS_FONT_STYLE_OBLIQUE;
    value = css_computed_font_family(computed, NULL);
    style->monospace = value == CSS_FONT_FAMILY_MONOSPACE;
    value = css_computed_text_decoration(computed);
    style->underline = (value & CSS_TEXT_DECORATION_UNDERLINE) != 0U;
    value = css_computed_white_space(computed);
    style->preformatted = value == CSS_WHITE_SPACE_PRE ||
                         value == CSS_WHITE_SPACE_PRE_WRAP;
    style->no_wrap = value == CSS_WHITE_SPACE_NOWRAP ||
                     value == CSS_WHITE_SPACE_PRE;
    value = css_computed_font_size(computed, &length, &unit);
    style->font_px = value == CSS_FONT_SIZE_DIMENSION ?
        css_length_px(computed, &ctx->units, value, CSS_FONT_SIZE_DIMENSION,
                      length, unit) : 12;
    if (style->font_px < 8) style->font_px = 8;
    if (style->font_px > 28) style->font_px = 28;
    style->line_height = style->font_px + 3;
    value = css_computed_line_height(computed, &length, &unit);
    if (value == CSS_LINE_HEIGHT_NUMBER) {
        int32_t hundredths = FIXTOINT(FMUL(length, INTTOFIX(100)));
        style->line_height = style->font_px * hundredths / 100;
    } else if (value == CSS_LINE_HEIGHT_DIMENSION) {
        style->line_height = css_length_px(computed, &ctx->units, value,
                                            CSS_LINE_HEIGHT_DIMENSION,
                                            length, unit);
    }
    if (style->line_height < style->font_px) style->line_height = style->font_px;
    if (style->line_height > 64) style->line_height = 64;
    value = css_computed_letter_spacing(computed, &length, &unit);
    if (value == CSS_LETTER_SPACING_SET) {
        int32_t spacing = css_length_px(computed, &ctx->units, value,
                                        CSS_LETTER_SPACING_SET, length, unit);
        if (spacing < -8) spacing = -8;
        if (spacing > 16) spacing = 16;
        style->letter_spacing = (int16_t)spacing;
    }
    value = css_computed_word_spacing(computed, &length, &unit);
    if (value == CSS_WORD_SPACING_SET) {
        int32_t spacing = css_length_px(computed, &ctx->units, value,
                                        CSS_WORD_SPACING_SET, length, unit);
        if (spacing < -8) spacing = -8;
        if (spacing > 24) spacing = 24;
        style->word_spacing = (int16_t)spacing;
    }
    value = css_computed_text_indent(computed, &length, &unit);
    if (value == CSS_TEXT_INDENT_SET)
        style->text_indent = css_length_px_relative(computed, &ctx->units,
            value, CSS_TEXT_INDENT_SET, length, unit,
            ctx->line_right - ctx->line_start_x);
    {
        css_fixed opacity;
        if (css_computed_opacity(computed, &opacity) == CSS_OPACITY_SET) {
            int32_t alpha = FIXTOINT(FMUL(opacity, INTTOFIX(255)));
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            style->opacity = (uint8_t)alpha;
        }
    }
    value = css_computed_column_gap(computed, &length, &unit);
    if (value == CSS_COLUMN_GAP_SET) {
        style->flex_gap = css_length_px(computed, &ctx->units, value,
                                        CSS_COLUMN_GAP_SET, length, unit);
        if (style->flex_gap < 0) style->flex_gap = 0;
        if (style->flex_gap > 128) style->flex_gap = 128;
    }
    value = css_computed_order(computed, &style->order);
    if (value != CSS_ORDER_SET) style->order = 0;
    value = css_computed_top(computed, &length, &unit);
    if (value == CSS_TOP_SET) {
        style->top = css_length_px_relative(computed, &ctx->units, value,
            CSS_TOP_SET, length, unit, ctx->viewport_width);
        style->top_set = true;
    }
    value = css_computed_right(computed, &length, &unit);
    if (value == CSS_RIGHT_SET) {
        style->right = css_length_px_relative(computed, &ctx->units, value,
            CSS_RIGHT_SET, length, unit, ctx->viewport_width);
        style->right_set = true;
    }
    value = css_computed_bottom(computed, &length, &unit);
    if (value == CSS_BOTTOM_SET) {
        style->bottom = css_length_px_relative(computed, &ctx->units, value,
            CSS_BOTTOM_SET, length, unit, 380);
        style->bottom_set = true;
    }
    value = css_computed_left(computed, &length, &unit);
    if (value == CSS_LEFT_SET) {
        style->left = css_length_px_relative(computed, &ctx->units, value,
            CSS_LEFT_SET, length, unit, ctx->viewport_width);
        style->left_set = true;
    }
    value = css_computed_flex_basis(computed, &length, &unit);
    if (value == CSS_FLEX_BASIS_SET) {
        style->flex_basis = css_length_px_relative(computed, &ctx->units, value,
            CSS_FLEX_BASIS_SET, length, unit,
            ctx->line_right - ctx->line_start_x);
        style->flex_basis_set = style->flex_basis > 0;
    }
    value = css_computed_width(computed, &length, &unit);
    if (value == CSS_WIDTH_SET) {
        style->width = css_length_px_relative(computed, &ctx->units, value,
                                     CSS_WIDTH_SET, length, unit,
                                     ctx->line_right - ctx->line_start_x);
        style->width_set = style->width > 0;
    }
    value = css_computed_height(computed, &length, &unit);
    if (value == CSS_HEIGHT_SET) {
        style->height = css_length_px(computed, &ctx->units, value,
                                      CSS_HEIGHT_SET, length, unit);
        style->height_set = style->height > 0;
    }
    value = css_computed_min_width(computed, &length, &unit);
    if (value == CSS_MIN_WIDTH_SET) {
        style->min_width = css_length_px_relative(computed, &ctx->units, value,
            CSS_MIN_WIDTH_SET, length, unit, ctx->line_right - ctx->line_start_x);
        style->min_width_set = style->min_width > 0;
    }
    value = css_computed_max_width(computed, &length, &unit);
    if (value == CSS_MAX_WIDTH_SET) {
        style->max_width = css_length_px_relative(computed, &ctx->units, value,
            CSS_MAX_WIDTH_SET, length, unit, ctx->line_right - ctx->line_start_x);
        style->max_width_set = style->max_width > 0;
    }
    value = css_computed_min_height(computed, &length, &unit);
    if (value == CSS_MIN_HEIGHT_SET) {
        style->min_height = css_length_px(computed, &ctx->units, value,
            CSS_MIN_HEIGHT_SET, length, unit);
        style->min_height_set = style->min_height > 0;
    }
    value = css_computed_max_height(computed, &length, &unit);
    if (value == CSS_MAX_HEIGHT_SET) {
        style->max_height = css_length_px(computed, &ctx->units, value,
            CSS_MAX_HEIGHT_SET, length, unit);
        style->max_height_set = style->max_height > 0;
    }

#define GET_LEN(field, function, setcode) do { \
    value = function(computed, &length, &unit); \
    style->field = css_length_px_relative(computed, &ctx->units, value, setcode, \
        length, unit, ctx->line_right - ctx->line_start_x); \
    if (style->field < 0) style->field = 0; \
} while (0)
    GET_LEN(margin_top, css_computed_margin_top, CSS_MARGIN_SET);
    GET_LEN(margin_right, css_computed_margin_right, CSS_MARGIN_SET);
    GET_LEN(margin_bottom, css_computed_margin_bottom, CSS_MARGIN_SET);
    GET_LEN(margin_left, css_computed_margin_left, CSS_MARGIN_SET);
    value = css_computed_margin_left(computed, &length, &unit);
    style->margin_left_auto = value == CSS_MARGIN_AUTO;
    value = css_computed_margin_right(computed, &length, &unit);
    style->margin_right_auto = value == CSS_MARGIN_AUTO;
    GET_LEN(padding_top, css_computed_padding_top, CSS_PADDING_SET);
    GET_LEN(padding_right, css_computed_padding_right, CSS_PADDING_SET);
    GET_LEN(padding_bottom, css_computed_padding_bottom, CSS_PADDING_SET);
    GET_LEN(padding_left, css_computed_padding_left, CSS_PADDING_SET);
#undef GET_LEN
#define GET_BORDER(side, function_width, function_style, function_color) do { \
    value = function_width(computed, &length, &unit); \
    if (function_style(computed) != CSS_BORDER_STYLE_NONE && \
        function_style(computed) != CSS_BORDER_STYLE_HIDDEN) { \
        style->side = value == CSS_BORDER_WIDTH_WIDTH ? \
            css_length_px(computed, &ctx->units, value, CSS_BORDER_WIDTH_WIDTH, length, unit) : 1; \
        if (style->side < 1) style->side = 1; \
        style->has_border = true; \
        if (function_color(computed, &color) == CSS_BORDER_COLOR_COLOR) \
            style->border = color_to_rgb(color); \
    } \
} while (0)
    GET_BORDER(border_top, css_computed_border_top_width,
               css_computed_border_top_style, css_computed_border_top_color);
    GET_BORDER(border_right, css_computed_border_right_width,
               css_computed_border_right_style, css_computed_border_right_color);
    GET_BORDER(border_bottom, css_computed_border_bottom_width,
               css_computed_border_bottom_style, css_computed_border_bottom_color);
    GET_BORDER(border_left, css_computed_border_left_width,
               css_computed_border_left_style, css_computed_border_left_color);
#undef GET_BORDER
}

static void finish_line(css_context_t *ctx) {
    if (!ctx) return;
    if (ctx->cursor_x != ctx->line_start_x || ctx->pending_space) {
        int32_t used = ctx->cursor_x - ctx->line_start_x;
        int32_t available = ctx->line_right - ctx->line_start_x;
        int32_t shift = 0;
        uint32_t i;
        if (!ctx->line_positioned &&
            (ctx->text_align == CSS_TEXT_ALIGN_CENTER ||
             ctx->text_align == CSS_TEXT_ALIGN_LIBCSS_CENTER))
            shift = (available - used) / 2;
        else if (!ctx->line_positioned &&
                 (ctx->text_align == CSS_TEXT_ALIGN_RIGHT ||
                  ctx->text_align == CSS_TEXT_ALIGN_LIBCSS_RIGHT))
            shift = available - used;
        if (shift > 0) for (i = ctx->line_item_start;
             i < ctx->layout->item_count; i++)
            ctx->layout->items[i].x += shift;
        ctx->cursor_y += ctx->line_height;
        ctx->line_height = NSBK_BASE_LINE_H;
        ctx->line_item_start = ctx->layout->item_count;
        ctx->line_positioned = false;
        refresh_float_bounds(ctx);
        ctx->cursor_x = ctx->line_start_x;
    }
    ctx->pending_space = false;
}

static uint32_t transformed_text(const char *text, uint32_t length,
                                 uint8_t transform, char *output) {
    uint32_t i;
    bool capitalize = true;
    if (!text || !output) return 0U;
    for (i = 0U; i < length; i++) {
        char c = text[i];
        if (transform == CSS_TEXT_TRANSFORM_UPPERCASE ||
            (transform == CSS_TEXT_TRANSFORM_CAPITALIZE && capitalize)) {
            if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
        } else if (transform == CSS_TEXT_TRANSFORM_LOWERCASE) {
            if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        }
        output[i] = c;
        capitalize = c == ' ' || c == '\t' || c == '-' || c == '/' || c == '.';
    }
    return length;
}

static int32_t text_spacing_width(const char *text, uint32_t length,
                                  const paint_style_t *style) {
    uint32_t i;
    int32_t extra = 0;
    if (!text || !style || length == 0U) return 0;
    if (length > 1U) extra += (int32_t)(length - 1U) * style->letter_spacing;
    for (i = 0U; i < length; i++)
        if (text[i] == ' ') extra += style->word_spacing;
    return extra;
}

static void add_text_piece(css_context_t *ctx, const char *text,
                           uint32_t length, const paint_style_t *style) {
    nsbk_layout_item_t *item;
    uint32_t offset;
    int32_t width;
    char local[256];
    char *converted = NULL;
    const char *display = text;
    if (!ctx || !text || length == 0U || !style->visible || style->opacity == 0U)
        return;
    if (style->text_transform != CSS_TEXT_TRANSFORM_NONE &&
        style->text_transform != CSS_TEXT_TRANSFORM_INHERIT) {
        converted = length < sizeof(local) ? local : (char *)malloc(length);
        if (converted) {
            transformed_text(text, length, style->text_transform, converted);
            display = converted;
        }
    }
    width = (int32_t)bk_gui_text_width_px(display, length,
        (uint16_t)style->font_px, style->monospace, style->bold);
    width += text_spacing_width(display, length, style);
    if (width < 1) width = 1;
    if (!style->no_wrap && ctx->cursor_x + width > ctx->line_right &&
        ctx->cursor_x > ctx->line_start_x) finish_line(ctx);
    if (!nsbk_layout_store_text(ctx->layout, display, length, &offset)) {
        if (converted && converted != local) free(converted);
        return;
    }
    item = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_TEXT |
        (style->bold ? NSBK_LAYOUT_BOLD : 0U) |
        (style->underline ? NSBK_LAYOUT_UNDERLINE : 0U));
    if (!item) {
        if (converted && converted != local) free(converted);
        return;
    }
    item->x = ctx->cursor_x;
    item->y = ctx->cursor_y;
    item->width = width;
    item->height = style->line_height;
    item->foreground = style->color;
    item->text_offset = offset;
    item->text_length = (uint16_t)length;
    item->font_px = (uint8_t)style->font_px;
    item->font_flags = (style->italic ? NSBK_FONT_ITALIC : 0U) |
                       (style->monospace ? NSBK_FONT_MONOSPACE : 0U);
    item->letter_spacing = (int8_t)style->letter_spacing;
    item->word_spacing = (int8_t)style->word_spacing;
    item->opacity = style->opacity;
    item->link_index = ctx->active_link;
    ctx->cursor_x += width;
    if (style->line_height > ctx->line_height) ctx->line_height = style->line_height;
    if (ctx->cursor_x > ctx->layout->width) ctx->layout->width = ctx->cursor_x;
    if (converted && converted != local) free(converted);
}

static void emit_ascii_run(css_context_t *ctx, const char *text,
                           uint32_t length, const paint_style_t *style) {
    uint32_t start = 0U;
    if (style && style->no_wrap) {
        add_text_piece(ctx, text, length, style);
        return;
    }
    while (start < length) {
        int32_t available_px = ctx->line_right - ctx->cursor_x;
        uint32_t piece = 0U;
        uint32_t best_break = 0U;
        if (layout_should_abort(ctx)) return;
        if (available_px <= 0) {
            int32_t previous_y = ctx->cursor_y;
            int32_t previous_x = ctx->cursor_x;
            finish_line(ctx);
            /* Una caja con ancho CSS degenerado podía dejar finish_line() sin
               nada que avanzar. El continue repetía entonces para siempre. */
            if (ctx->cursor_y == previous_y && ctx->cursor_x == previous_x) {
                ctx->line_right = ctx->line_start_x + 1;
                ctx->cursor_x = ctx->line_start_x;
            }
            continue;
        }
        while (start + piece < length) {
            uint32_t next = piece + 1U;
            int32_t width = (int32_t)bk_gui_text_width_px(text + start, next,
                (uint16_t)style->font_px, style->monospace, style->bold) +
                text_spacing_width(text + start, next, style);
            if (width > available_px) break;
            piece = next;
            if (text[start + piece - 1U] == ' ' ||
                text[start + piece - 1U] == '-' ||
                text[start + piece - 1U] == '/') best_break = piece;
        }
        if (piece == 0U) {
            if (ctx->cursor_x > ctx->line_start_x) { finish_line(ctx); continue; }
            piece = 1U;
        } else if (start + piece < length && best_break > 0U) {
            piece = best_break;
        }
        add_text_piece(ctx, text + start, piece, style);
        start += piece;
        if (start < length) finish_line(ctx);
    }
}

static uint32_t utf8_decode_one(const char *text, uint32_t length,
                                uint32_t *consumed) {
    uint8_t a, b, c, d;
    if (!text || !length || !consumed) return '?';
    a = (uint8_t)text[0];
    *consumed = 1U;
    if (a < 0x80U) return a;
    if (a >= 0xC2U && a <= 0xDFU && length >= 2U) {
        b = (uint8_t)text[1];
        if ((b & 0xC0U) == 0x80U) {
            *consumed = 2U;
            return ((uint32_t)(a & 0x1FU) << 6U) | (b & 0x3FU);
        }
    } else if (a >= 0xE0U && a <= 0xEFU && length >= 3U) {
        b = (uint8_t)text[1]; c = (uint8_t)text[2];
        if ((b & 0xC0U) == 0x80U && (c & 0xC0U) == 0x80U &&
            !(a == 0xE0U && b < 0xA0U) && !(a == 0xEDU && b >= 0xA0U)) {
            *consumed = 3U;
            return ((uint32_t)(a & 0x0FU) << 12U) |
                   ((uint32_t)(b & 0x3FU) << 6U) | (c & 0x3FU);
        }
    } else if (a >= 0xF0U && a <= 0xF4U && length >= 4U) {
        b = (uint8_t)text[1]; c = (uint8_t)text[2]; d = (uint8_t)text[3];
        if ((b & 0xC0U) == 0x80U && (c & 0xC0U) == 0x80U &&
            (d & 0xC0U) == 0x80U && !(a == 0xF0U && b < 0x90U) &&
            !(a == 0xF4U && b >= 0x90U)) {
            *consumed = 4U;
            return ((uint32_t)(a & 0x07U) << 18U) |
                   ((uint32_t)(b & 0x3FU) << 12U) |
                   ((uint32_t)(c & 0x3FU) << 6U) | (d & 0x3FU);
        }
    }
    return '?';
}

static uint32_t display_fold(uint32_t codepoint, char output[4]) {
    output[0] = '?'; output[1] = '\0';
    if (codepoint < 0x80U) { output[0] = (char)codepoint; return 1U; }
    if (codepoint >= 0x00A0U && codepoint <= 0x00FFU) {
        output[0] = (char)(uint8_t)codepoint;
        return 1U;
    }
    if (codepoint == 0x0178U) { output[0] = (char)0xDD; return 1U; }
    if (codepoint == 0x2013U || codepoint == 0x2014U ||
        codepoint == 0x2212U) output[0] = '-';
    else if (codepoint == 0x2018U || codepoint == 0x2019U) output[0] = '\'';
    else if (codepoint == 0x201CU || codepoint == 0x201DU) output[0] = '"';
    else if (codepoint == 0x2026U) {
        output[0] = '.'; output[1] = '.'; output[2] = '.'; return 3U;
    } else if (codepoint == 0x20ACU) {
        output[0] = 'E'; output[1] = 'U'; output[2] = 'R'; return 3U;
    } else if (codepoint == 0x2022U) output[0] = '*';
    return 1U;
}

static void emit_utf8_word(css_context_t *ctx, const char *text,
                           uint32_t length, const paint_style_t *style) {
    char folded[128];
    uint32_t input = 0U, used = 0U;
    while (input < length) {
        char mapped[4];
        uint32_t consumed, count, i;
        uint32_t codepoint = utf8_decode_one(text + input, length - input,
                                             &consumed);
        count = display_fold(codepoint, mapped);
        if (used + count > sizeof(folded)) {
            emit_ascii_run(ctx, folded, used, style);
            used = 0U;
        }
        for (i = 0U; i < count; i++) folded[used++] = mapped[i];
        input += consumed;
    }
    if (used) emit_ascii_run(ctx, folded, used, style);
}

static void emit_text(css_context_t *ctx, const char *text, uint32_t length,
                      const paint_style_t *style) {
    uint32_t i = 0U, start;
    char space = ' ';
    bool pre = ctx->preformatted || style->preformatted;
    while (i < length) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r') { i++; continue; }
        if (c == '\n' && pre) { finish_line(ctx); i++; continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
            if (pre) add_text_piece(ctx, &space, 1U, style);
            else ctx->pending_space = true;
            i++; continue;
        }
        if (ctx->pending_space && ctx->cursor_x > ctx->line_start_x) {
            add_text_piece(ctx, &space, 1U, style);
            ctx->pending_space = false;
        }
        start = i;
        while (i < length) {
            c = (unsigned char)text[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f')
                break;
            i++;
        }
        emit_utf8_word(ctx, text + start, i - start, style);
    }
}

static bool is_block_display(uint8_t display) {
    return display == CSS_DISPLAY_BLOCK || display == CSS_DISPLAY_LIST_ITEM ||
        display == CSS_DISPLAY_TABLE || display == CSS_DISPLAY_TABLE_ROW_GROUP ||
        display == CSS_DISPLAY_TABLE_HEADER_GROUP ||
        display == CSS_DISPLAY_TABLE_FOOTER_GROUP ||
        display == CSS_DISPLAY_TABLE_ROW ||
        display == CSS_DISPLAY_TABLE_CAPTION || display == CSS_DISPLAY_FLEX ||
        display == CSS_DISPLAY_GRID;
}

static bool is_inline_block_display(uint8_t display) {
    return display == CSS_DISPLAY_INLINE_BLOCK ||
           display == CSS_DISPLAY_INLINE_TABLE ||
           display == CSS_DISPLAY_TABLE_CELL ||
           display == CSS_DISPLAY_INLINE_FLEX ||
           display == CSS_DISPLAY_INLINE_GRID;
}

static css_computed_style *select_node_style(css_context_t *ctx,
        nsbk_dom_node_t *node, css_computed_style *parent,
        css_select_results **results_out, css_computed_style **composed_out) {
    css_stylesheet *inline_sheet = NULL;
    const char *inline_css = nsbk_dom_attribute(node, "style");
    css_select_results *results = NULL;
    css_computed_style *style, *composed = NULL;
    *results_out = NULL; *composed_out = NULL;
    if (inline_css && inline_css[0])
        inline_sheet = create_sheet(&ctx->compatibility, (const uint8_t *)inline_css,
            (uint32_t)strlen(inline_css), true,
            ctx->result->quirks_mode != 0U,
            ctx->result->resources ?
            ctx->result->resources->document_url : "about:bleskernos");
    if (css_select_style(ctx->select, node, &ctx->units, &ctx->media,
            inline_sheet, &selector_handler, ctx, &results) != CSS_OK ||
        !results || !results->styles[CSS_PSEUDO_ELEMENT_NONE]) {
        if (inline_sheet) css_stylesheet_destroy(inline_sheet);
        if (results) css_select_results_destroy(results);
        return NULL;
    }
    if (inline_sheet) css_stylesheet_destroy(inline_sheet);
    style = results->styles[CSS_PSEUDO_ELEMENT_NONE];
    if (parent && css_computed_style_compose(parent, style, &ctx->units,
            &composed) == CSS_OK) style = composed;
    *results_out = results;
    *composed_out = composed;
    return style;
}

static uint16_t image_dimension_attribute(const nsbk_dom_node_t *node,
                                          const char *name) {
    const char *value = nsbk_dom_attribute(node, name);
    uint32_t number = 0U;
    if (!value || value[0] < '0' || value[0] > '9') return 0U;
    while (*value >= '0' && *value <= '9') {
        number = number * 10U + (uint32_t)(*value++ - '0');
        if (number > 4096U) return 4096U;
    }
    return (uint16_t)number;
}

static int32_t find_image_resource(const css_context_t *ctx,
                                   const char *reference) {
    uint32_t i;
    const nsbk_resource_environment_t *resources;
    if (!ctx || !ctx->result || !reference || !ctx->result->resources)
        return -1;
    resources = ctx->result->resources;
    for (i = 0U; i < resources->image_count; i++) {
        const char *stored = resources->images[i].reference;
        const char *url = resources->images[i].url;
        uint32_t rlen, ulen;
        if (stored && strcmp(stored, reference) == 0) return (int32_t)i;
        if (url && strcmp(url, reference) == 0) return (int32_t)i;
        rlen = (uint32_t)strlen(reference);
        ulen = url ? (uint32_t)strlen(url) : 0U;
        if (rlen && ulen >= rlen && strcmp(url + ulen - rlen, reference) == 0)
            return (int32_t)i;
    }
    return -1;
}

static void configure_box_background(css_context_t *ctx,
                                     nsbk_layout_item_t *box,
                                     const paint_style_t *style) {
    int32_t index;
    if (!ctx || !box || !style) return;
    box->background = style->background;
    box->border = style->border;
    box->opacity = style->opacity;
    if (style->has_background) box->flags |= NSBK_LAYOUT_BACKGROUND;
    if (style->has_border) box->flags |= NSBK_LAYOUT_BORDER;
    if (style->has_background_image) {
        index = find_image_resource(ctx, style->background_image);
        if (index >= 0) {
            const bk_gui_image_t *image = ctx->result->resources->images[index].image;
            box->flags |= NSBK_LAYOUT_BACKGROUND_IMAGE;
            box->image_index = index;
            box->background_repeat = style->background_repeat;
            box->background_x = style->background_x;
            box->background_y = style->background_y;
            if (style->background_x_percent)
                box->background_position_flags |= NSBK_BG_POS_X_PERCENT;
            if (style->background_y_percent)
                box->background_position_flags |= NSBK_BG_POS_Y_PERCENT;
            (void)image;
        }
    }
}

static uint32_t table_cell_span(const nsbk_dom_node_t *node) {
    return positive_attribute(node, "colspan", 1U);
}

static uint32_t table_column_count(nsbk_dom_node_t *node) {
    nsbk_dom_node_t *sibling;
    uint32_t count = 0U;
    if (!node || !node->parent) return 1U;
    for (sibling = node->parent->first_child; sibling; sibling = sibling->next)
        if (sibling->type == NSBK_DOM_ELEMENT &&
            (nsbk_dom_name_is(sibling, "td") || nsbk_dom_name_is(sibling, "th")))
            count += table_cell_span(sibling);
    return count ? count : 1U;
}

static uint32_t table_width_percent(const nsbk_dom_node_t *node) {
    const char *value = nsbk_dom_attribute(node, "width");
    uint32_t number = 0U;
    if (!value || value[0] < '0' || value[0] > '9') return 0U;
    while (*value >= '0' && *value <= '9') {
        number = number * 10U + (uint32_t)(*value++ - '0');
        if (number > 100U) number = 100U;
    }
    while (*value == ' ' || *value == '\t') value++;
    return *value == '%' ? number : 0U;
}

static int32_t table_cell_width(const nsbk_dom_node_t *node,
                                int32_t available) {
    const nsbk_dom_node_t *cell;
    int32_t automatic_total = 0;
    int32_t percentage_intrinsic_total = 0;
    int32_t desired = intrinsic_node_width(node, available);
    uint32_t percent_total = 0U;
    uint32_t own_percent = table_width_percent(node);
    uint32_t columns = table_column_count((nsbk_dom_node_t *)node);
    uint32_t span = table_cell_span(node);
    if (!node || !node->parent || available <= 0)
        return available > 16 ? available : 16;
    for (cell = node->parent->first_child; cell; cell = cell->next) {
        uint32_t percent;
        if (cell->type != NSBK_DOM_ELEMENT ||
            (!nsbk_dom_name_is(cell, "td") && !nsbk_dom_name_is(cell, "th")))
            continue;
        percent = table_width_percent(cell);
        if (percent) {
            percent_total += percent;
            percentage_intrinsic_total += intrinsic_node_width(cell, available);
        } else {
            int32_t width = intrinsic_node_width(cell, available);
            int32_t minimum = (int32_t)table_cell_span(cell) * 16;
            automatic_total += width > minimum ? width : minimum;
        }
    }
    /* With no HTML width hints, retain the predictable equal-column model.
       When percentage columns surround an automatic column (a common legacy
       layout), reserve the automatic column's intrinsic width first and share
       the remainder between the percentage columns. */
    if (percent_total == 0U)
        return available * (int32_t)span / (int32_t)columns - 3;
    if (own_percent) {
        int32_t base = automatic_total + percentage_intrinsic_total;
        int32_t width;
        if (base <= available) {
            int32_t extra = available - base;
            width = desired + extra * (int32_t)own_percent /
                              (int32_t)percent_total;
        } else {
            width = base > 0 ? desired * available / base : desired;
        }
        return width > 19 ? width - 3 : 16;
    }
    {
        int32_t base = automatic_total + percentage_intrinsic_total;
        if (base > available && base > 0)
            desired = desired * available / base;
    }
    return desired > 16 ? desired : 16;
}

static void clear_floats(css_context_t *ctx, uint8_t clear_mode) {
    bool clear_left;
    bool clear_right;
    if (!ctx || clear_mode == CSS_CLEAR_NONE || clear_mode == CSS_CLEAR_INHERIT)
        return;
    clear_left = clear_mode == CSS_CLEAR_LEFT || clear_mode == CSS_CLEAR_BOTH;
    clear_right = clear_mode == CSS_CLEAR_RIGHT || clear_mode == CSS_CLEAR_BOTH;
    if ((clear_left && ctx->float_left_width > 0) ||
        (clear_right && ctx->float_right_width > 0)) {
        finish_line(ctx);
        if (ctx->float_bottom > ctx->cursor_y) ctx->cursor_y = ctx->float_bottom;
        ctx->float_left_width = 0;
        ctx->float_right_width = 0;
        ctx->float_bottom = 0;
        refresh_float_bounds(ctx);
        ctx->cursor_x = ctx->line_start_x;
    }
}

static void add_list_marker(css_context_t *ctx, nsbk_dom_node_t *node,
                            const paint_style_t *style) {
    char marker[16];
    uint32_t used = 0U;
    if (!ctx || !node || !style || !node->parent) return;
    if (nsbk_dom_name_is(node->parent, "ol")) {
        nsbk_dom_node_t *sibling;
        uint32_t number = 1U;
        uint32_t divisor = 1U;
        for (sibling = node->parent->first_child; sibling && sibling != node;
             sibling = sibling->next)
            if (sibling->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(sibling, "li"))
                number++;
        while (number / divisor >= 10U) divisor *= 10U;
        while (divisor) {
            marker[used++] = (char)('0' + (number / divisor) % 10U);
            divisor /= 10U;
        }
        marker[used++] = '.';
        marker[used++] = ' ';
    } else {
        marker[used++] = '*';
        marker[used++] = ' ';
    }
    add_text_piece(ctx, marker, used, style);
}

static bool emit_image(css_context_t *ctx, nsbk_dom_node_t *node,
                       const paint_style_t *style) {
    char selected_source[NSBK_HTML_IMAGE_SOURCE_MAX];
    const char *src;
    const nsbk_resource_environment_t *resources;
    const bk_gui_image_t *image;
    nsbk_layout_item_t *item;
    int32_t index;
    int32_t width, height, available;
    uint16_t attr_width, attr_height;
    if (!ctx->result->resources ||
        !nsbk_dom_image_source(node, ctx->viewport_width,
                               selected_source, sizeof(selected_source)))
        return false;
    src = selected_source;
    index = find_image_resource(ctx, src);
    if (index < 0) return false;
    resources = ctx->result->resources;
    image = resources->images[index].image;
    if (!image || !image->pixels || !image->width || !image->height) return false;
    width = image->width;
    height = image->height;
    attr_width = image_dimension_attribute(node, "width");
    attr_height = image_dimension_attribute(node, "height");
    if (style->width_set) width = style->width;
    else if (attr_width) width = attr_width;
    if (style->height_set) height = style->height;
    else if (attr_height) height = attr_height;
    if ((style->width_set || attr_width) && !(style->height_set || attr_height))
        height = (int32_t)((uint32_t)width * image->height / image->width);
    else if ((style->height_set || attr_height) &&
             !(style->width_set || attr_width))
        width = (int32_t)((uint32_t)height * image->width / image->height);
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    available = ctx->line_right - ctx->line_start_x;
    if (width > available && available > 0) {
        height = height * available / width;
        width = available;
        if (height < 1) height = 1;
    }
    if (ctx->cursor_x != ctx->line_start_x &&
        ctx->cursor_x + width > ctx->line_right) finish_line(ctx);
    item = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_IMAGE);
    if (!item) return true;
    item->x = ctx->cursor_x;
    item->y = ctx->cursor_y;
    item->width = width;
    item->height = height;
    item->image_index = (int32_t)index;
    item->link_index = ctx->active_link;
    item->opacity = style->opacity;
    ctx->cursor_x += width + 2;
    if (height + 2 > ctx->line_height) ctx->line_height = height + 2;
    return true;
}

static int32_t find_control(const css_context_t *ctx,
                            const nsbk_dom_node_t *node) {
    uint32_t i;
    if (!ctx || !ctx->result || !node) return -1;
    for (i = 0U; i < ctx->result->control_count; i++)
        if (ctx->result->controls[i].node == node) return (int32_t)i;
    return -1;
}

static bool emit_control(css_context_t *ctx, nsbk_dom_node_t *node,
                         const paint_style_t *style) {
    int32_t index = find_control(ctx, node);
    nsbk_html_control_t *control;
    nsbk_layout_item_t *item;
    int32_t width, height;
    uint16_t control_font_px;
    if (index < 0) return false;
    control = &ctx->result->controls[index];
    if (control->type == NSBK_CONTROL_HIDDEN) return true;
    /* Measure controls with the same computed font size used by the painter.
       Measuring at 10 px and later drawing at 8/16 px clipped labels and made
       the CSS boxes look like empty nested rectangles. */
    control_font_px = (uint16_t)style->font_px;
    if (control_font_px < 8U) control_font_px = 8U;
    if (control_font_px > 28U) control_font_px = 28U;
    if (control->type == NSBK_CONTROL_CHECKBOX ||
        control->type == NSBK_CONTROL_RADIO) {
        width = 14; height = 14;
    } else if (control->type == NSBK_CONTROL_SUBMIT ||
               control->type == NSBK_CONTROL_BUTTON) {
        width = (int32_t)bk_gui_text_width_px(control->label,
            (uint32_t)strlen(control->label),
            control_font_px,
            style->monospace, style->bold) +
            style->padding_left + style->padding_right + 14;
        if (width < 48) width = 48;
        if (width > 320) width = 320;
        height = style->line_height + style->padding_top +
                 style->padding_bottom + 6;
        if (height < 23) height = 23;
    } else if (control->type == NSBK_CONTROL_TEXTAREA) {
        width = 240; height = 54;
    } else if (control->type == NSBK_CONTROL_SELECT) {
        width = (int32_t)bk_gui_text_width_px(control->label,
            (uint32_t)strlen(control->label),
            control_font_px,
            style->monospace, style->bold) + 32;
        if (width < 110) width = 110;
        if (width > 260) width = 260;
        height = 23;
    } else {
        width = 190; height = 23;
    }
    if (style->width_set && style->width > 4) width = style->width;
    if (style->height_set && style->height > 4) height = style->height;
    if (width > ctx->line_right - ctx->line_start_x)
        width = ctx->line_right - ctx->line_start_x;
    if (width < 8) width = 8;
    if (ctx->cursor_x != ctx->line_start_x &&
        ctx->cursor_x + width > ctx->line_right) finish_line(ctx);
    item = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_CONTROL |
        NSBK_LAYOUT_BOX | NSBK_LAYOUT_BACKGROUND | NSBK_LAYOUT_BORDER);
    if (!item) return true;
    item->x = ctx->cursor_x + style->margin_left;
    item->y = ctx->cursor_y + style->margin_top;
    item->width = width;
    item->height = height;
    item->foreground = style->color;
    item->background = style->has_background ? style->background : 0x00FFFFFFU;
    item->border = style->has_border ? style->border : 0x00808080U;
    item->font_px = control_font_px;
    item->opacity = style->opacity;
    item->font_flags = style->monospace ? NSBK_FONT_MONOSPACE : 0U;
    if (style->italic) item->font_flags |= NSBK_FONT_ITALIC;
    if (style->bold) item->flags |= NSBK_LAYOUT_BOLD;
    item->control_index = index;
    ctx->cursor_x = item->x + width + style->margin_right + 3;
    if (height + style->margin_top + style->margin_bottom > ctx->line_height)
        ctx->line_height = height + style->margin_top + style->margin_bottom;
    if (ctx->cursor_x > ctx->layout->width) ctx->layout->width = ctx->cursor_x;
    return true;
}

static void layout_node(css_context_t *ctx, nsbk_dom_node_t *node,
                        css_computed_style *parent_style);

static bool whitespace_text_node(const nsbk_dom_node_t *node) {
    uint32_t i;
    if (!node || node->type != NSBK_DOM_TEXT) return false;
    for (i = 0U; i < node->text_length; i++) {
        unsigned char c = (unsigned char)node->text[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f')
            return false;
    }
    return true;
}

static void flex_child_metadata(css_context_t *ctx, nsbk_dom_node_t *child,
                                css_computed_style *parent_style,
                                flex_group_t *group) {
    css_select_results *results = NULL;
    css_computed_style *composed = NULL;
    css_computed_style *computed;
    paint_style_t child_style;
    if (!group) return;
    group->order = 0;
    group->align_self = CSS_ALIGN_SELF_AUTO;
    if (!ctx || !child || child->type != NSBK_DOM_ELEMENT) return;
    computed = select_node_style(ctx, child, parent_style, &results, &composed);
    if (computed) {
        extract_style(ctx, computed, &child_style, false);
        group->order = child_style.order;
        group->align_self = child_style.align_self;
    }
    if (composed) css_computed_style_destroy(composed);
    if (results) css_select_results_destroy(results);
}

static uint32_t flex_order_index(const flex_group_t *groups, uint32_t count,
                                 uint32_t position, bool reverse) {
    uint32_t order[NSBK_FLEX_GROUP_MAX];
    uint32_t i, j;
    if (!groups || position >= count) return position;
    for (i = 0U; i < count; i++) order[i] = i;
    for (i = 1U; i < count; i++) {
        uint32_t current = order[i];
        j = i;
        while (j > 0U &&
               (groups[order[j - 1U]].order > groups[current].order ||
                (groups[order[j - 1U]].order == groups[current].order &&
                 order[j - 1U] > current))) {
            order[j] = order[j - 1U];
            j--;
        }
        order[j] = current;
    }
    return reverse ? order[count - 1U - position] : order[position];
}

static uint8_t flex_group_alignment(const flex_group_t *group,
                                    uint8_t parent_alignment) {
    if (!group || group->align_self == CSS_ALIGN_SELF_AUTO ||
        group->align_self == CSS_ALIGN_SELF_INHERIT)
        return parent_alignment;
    if (group->align_self == CSS_ALIGN_SELF_CENTER) return CSS_ALIGN_ITEMS_CENTER;
    if (group->align_self == CSS_ALIGN_SELF_FLEX_END) return CSS_ALIGN_ITEMS_FLEX_END;
    if (group->align_self == CSS_ALIGN_SELF_FLEX_START) return CSS_ALIGN_ITEMS_FLEX_START;
    if (group->align_self == CSS_ALIGN_SELF_STRETCH) return CSS_ALIGN_ITEMS_STRETCH;
    return parent_alignment;
}

static bool layout_flex_children(css_context_t *ctx, nsbk_dom_node_t *node,
                                 css_computed_style *parent_style,
                                 const paint_style_t *style) {
    flex_group_t groups[NSBK_FLEX_GROUP_MAX];
    nsbk_dom_node_t *child;
    nsbk_dom_node_t *saved_force;
    uint32_t count = 0U;
    uint32_t i;
    int32_t content_start;
    int32_t content_end;
    int32_t content_width;
    int32_t top;
    bool row;
    bool reverse;
    bool wrapped = false;
    if (!ctx || !node || !style) return false;
    row = style->display == CSS_DISPLAY_GRID ||
          style->display == CSS_DISPLAY_INLINE_GRID ||
          style->flex_direction == CSS_FLEX_DIRECTION_ROW ||
          style->flex_direction == CSS_FLEX_DIRECTION_ROW_REVERSE ||
          style->flex_direction == CSS_FLEX_DIRECTION_INHERIT;
    reverse = style->flex_direction == CSS_FLEX_DIRECTION_ROW_REVERSE ||
              style->flex_direction == CSS_FLEX_DIRECTION_COLUMN_REVERSE;
    content_start = ctx->line_start_x;
    content_end = ctx->line_right;
    content_width = content_end - content_start;
    top = ctx->cursor_y;
    saved_force = ctx->force_inline_node;

    if (row) {
        uint8_t saved_align = ctx->text_align;
        ctx->text_align = CSS_TEXT_ALIGN_LEFT;
        for (child = node->first_child; child && !ctx->aborted;
             child = child->next) {
            uint32_t start;
            if (whitespace_text_node(child)) continue;
            start = ctx->layout->item_count;
            ctx->force_inline_node = child->type == NSBK_DOM_ELEMENT ? child : NULL;
            layout_node(ctx, child, parent_style);
            if (count < NSBK_FLEX_GROUP_MAX &&
                item_range_bounds(ctx->layout, start, ctx->layout->item_count,
                                  &groups[count])) {
                flex_child_metadata(ctx, child, parent_style, &groups[count]);
                if (count > 0U && groups[count].min_y != groups[0].min_y)
                    wrapped = true;
                count++;
            }
            if ((++ctx->work_counter & 31U) == 0U) bk_sys_yield();
        }
        ctx->force_inline_node = saved_force;
        ctx->text_align = saved_align;
        if (count == 0U) return true;
        if (!wrapped && style->flex_wrap != CSS_FLEX_WRAP_WRAP &&
            style->flex_wrap != CSS_FLEX_WRAP_WRAP_REVERSE) {
            int32_t total_width = 0;
            int32_t max_height = 1;
            int32_t extra;
            int32_t gap = 0;
            int32_t offset = 0;
            int32_t target;
            for (i = 0U; i < count; i++) {
                int32_t width = groups[i].max_x - groups[i].min_x;
                int32_t height = groups[i].max_y - groups[i].min_y;
                total_width += width;
                if (height > max_height) max_height = height;
            }
            if (count > 1U) total_width += style->flex_gap * (int32_t)(count - 1U);
            extra = content_width - total_width;
            if (extra < 0) extra = 0;
            switch (style->justify_content) {
            case CSS_JUSTIFY_CONTENT_FLEX_END:
                offset = extra;
                break;
            case CSS_JUSTIFY_CONTENT_CENTER:
                offset = extra / 2;
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
                if (count > 1U) gap = extra / (int32_t)(count - 1U);
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
                gap = extra / (int32_t)count;
                offset = gap / 2;
                break;
            case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
                gap = extra / (int32_t)(count + 1U);
                offset = gap;
                break;
            default:
                break;
            }
            target = content_start + offset;
            for (i = 0U; i < count; i++) {
                uint32_t source = flex_order_index(groups, count, i, reverse);
                int32_t width = groups[source].max_x - groups[source].min_x;
                int32_t height = groups[source].max_y - groups[source].min_y;
                uint8_t alignment = flex_group_alignment(&groups[source],
                                                         style->align_items);
                int32_t dy = 0;
                if (alignment == CSS_ALIGN_ITEMS_CENTER)
                    dy = (max_height - height) / 2;
                else if (alignment == CSS_ALIGN_ITEMS_FLEX_END)
                    dy = max_height - height;
                shift_item_range(ctx->layout, groups[source].item_start,
                                 groups[source].item_end,
                                 target - groups[source].min_x,
                                 top + dy - groups[source].min_y);
                target += width + style->flex_gap + gap;
            }
            ctx->cursor_x = content_end;
            ctx->line_height = max_height > ctx->line_height ?
                               max_height : ctx->line_height;
            if (style->height_set && style->height > ctx->line_height)
                ctx->line_height = style->height;
            ctx->line_positioned = true;
        }
        finish_line(ctx);
        return true;
    }

    for (child = node->first_child; child && !ctx->aborted;
         child = child->next) {
        uint32_t start;
        if (whitespace_text_node(child)) continue;
        start = ctx->layout->item_count;
        ctx->force_inline_node =
            (style->align_items != CSS_ALIGN_ITEMS_STRETCH &&
             child->type == NSBK_DOM_ELEMENT) ? child : NULL;
        layout_node(ctx, child, parent_style);
        if (count < NSBK_FLEX_GROUP_MAX &&
            item_range_bounds(ctx->layout, start, ctx->layout->item_count,
                              &groups[count])) {
            flex_child_metadata(ctx, child, parent_style, &groups[count]);
            count++;
        }
        if ((++ctx->work_counter & 31U) == 0U) bk_sys_yield();
    }
    ctx->force_inline_node = saved_force;
    if (count > 0U) {
        int32_t total_height = 0;
        int32_t container_height;
        int32_t extra;
        int32_t distributed_gap = 0;
        int32_t offset = 0;
        int32_t target_y;
        for (i = 0U; i < count; i++)
            total_height += groups[i].max_y - groups[i].min_y;
        if (count > 1U) total_height += style->flex_gap * (int32_t)(count - 1U);
        container_height = style->height_set && style->height > total_height ?
                           style->height : total_height;
        extra = container_height - total_height;
        if (extra < 0) extra = 0;
        if (style->justify_content == CSS_JUSTIFY_CONTENT_FLEX_END)
            offset = extra;
        else if (style->justify_content == CSS_JUSTIFY_CONTENT_CENTER)
            offset = extra / 2;
        else if (style->justify_content == CSS_JUSTIFY_CONTENT_SPACE_BETWEEN && count > 1U)
            distributed_gap = extra / (int32_t)(count - 1U);
        else if (style->justify_content == CSS_JUSTIFY_CONTENT_SPACE_AROUND) {
            distributed_gap = extra / (int32_t)count;
            offset = distributed_gap / 2;
        } else if (style->justify_content == CSS_JUSTIFY_CONTENT_SPACE_EVENLY) {
            distributed_gap = extra / (int32_t)(count + 1U);
            offset = distributed_gap;
        }
        target_y = top + offset;
        for (i = 0U; i < count; i++) {
            uint32_t source = flex_order_index(groups, count, i, reverse);
            int32_t width = groups[source].max_x - groups[source].min_x;
            int32_t height = groups[source].max_y - groups[source].min_y;
            uint8_t alignment = flex_group_alignment(&groups[source],
                                                     style->align_items);
            int32_t dx = 0;
            if (alignment == CSS_ALIGN_ITEMS_CENTER)
                dx = (content_width - width) / 2;
            else if (alignment == CSS_ALIGN_ITEMS_FLEX_END)
                dx = content_width - width;
            shift_item_range(ctx->layout, groups[source].item_start,
                             groups[source].item_end,
                             content_start + (dx > 0 ? dx : 0) - groups[source].min_x,
                             target_y - groups[source].min_y);
            target_y += height + style->flex_gap + distributed_gap;
        }
        if (ctx->cursor_y < top + container_height)
            ctx->cursor_y = top + container_height;
    }
    return true;
}

static void layout_node(css_context_t *ctx, nsbk_dom_node_t *node,
                        css_computed_style *parent_style) {
    nsbk_dom_node_t *child;
    css_select_results *results = NULL;
    css_computed_style *composed = NULL;
    css_computed_style *computed;
    paint_style_t style;
    flow_state_t parent_flow;
    flow_state_t absolute_flow;
    bool block;
    bool inline_block;
    bool floated;
    bool absolute = false;
    bool old_pre;
    int32_t old_link;
    int32_t box_top = 0;
    int32_t box_x = 0;
    int32_t box_width = 0;
    int32_t relative_dx = 0;
    int32_t relative_dy = 0;
    uint32_t node_item_start;
    uint32_t node_anchor_start;
    nsbk_layout_item_t *box = NULL;

    if (!node || !ctx || ctx->layout->truncated ||
        layout_should_abort(ctx)) return;
    if (node->type == NSBK_DOM_TEXT) {
        paint_style_t inherited;
        memset(&inherited, 0, sizeof(inherited));
        inherited.color = 0x00000000U;
        inherited.visible = true;
        inherited.font_px = 12;
        inherited.line_height = NSBK_BASE_LINE_H;
        inherited.opacity = 255U;
        inherited.text_transform = CSS_TEXT_TRANSFORM_NONE;
        if (parent_style) extract_style(ctx, parent_style, &inherited, false);
        emit_text(ctx, node->text, node->text_length, &inherited);
        if ((++ctx->work_counter & 63U) == 0U) bk_sys_yield();
        return;
    }
    if (node->type != NSBK_DOM_ELEMENT) {
        for (child = node->first_child; child && !ctx->aborted;
             child = child->next)
            layout_node(ctx, child, parent_style);
        return;
    }

    computed = select_node_style(ctx, node, parent_style, &results, &composed);
    if (!computed) return;
    ctx->layout->styled_node_count++;
    if ((++ctx->work_counter & 31U) == 0U) bk_sys_yield();
    extract_style(ctx, computed, &style, element_parent(node) == NULL);
    {
        /* Legacy HTML presentational hints are still common in no-script
           pages.  Upstream NetSurf supplies these through its hint callback;
           this compact DOM bridge applies the text-alignment subset here. */
        const char *align = nsbk_dom_attribute(node, "align");
        if (align && text_equal_nocase(align, "center"))
            style.text_align = CSS_TEXT_ALIGN_CENTER;
        else if (align && text_equal_nocase(align, "right"))
            style.text_align = CSS_TEXT_ALIGN_RIGHT;
        else if (align && text_equal_nocase(align, "left"))
            style.text_align = CSS_TEXT_ALIGN_LEFT;
    }
    node_item_start = ctx->layout->item_count;
    node_anchor_start = ctx->layout->anchor_count;
    if (style.display == CSS_DISPLAY_NONE || !style.visible) goto done;

    clear_floats(ctx, style.clear_mode);
    if (style.position == CSS_POSITION_RELATIVE) {
        if (style.left_set) relative_dx = style.left;
        else if (style.right_set) relative_dx = -style.right;
        if (style.top_set) relative_dy = style.top;
        else if (style.bottom_set) relative_dy = -style.bottom;
    } else if (style.position == CSS_POSITION_ABSOLUTE ||
               style.position == CSS_POSITION_FIXED) {
        int32_t origin_left;
        int32_t origin_right;
        int32_t available;
        int32_t outer_width;
        int32_t origin_y;
        save_flow(ctx, &absolute_flow);
        absolute = true;
        origin_left = style.position == CSS_POSITION_FIXED ?
                      NSBK_PAGE_PAD : ctx->base_line_start;
        origin_right = style.position == CSS_POSITION_FIXED ?
                       ctx->viewport_width - NSBK_PAGE_PAD : ctx->base_line_right;
        origin_y = NSBK_PAGE_PAD;
        available = origin_right - origin_left;
        outer_width = style.width_set ? style.width :
                      intrinsic_node_width(node, available);
        if (style.width_set && !style.border_box)
            outer_width += style.padding_left + style.padding_right +
                           style.border_left + style.border_right;
        if (outer_width < 16) outer_width = 16;
        if (outer_width > available) outer_width = available;
        box_x = origin_left;
        if (style.left_set) box_x = origin_left + style.left;
        else if (style.right_set) box_x = origin_right - style.right - outer_width;
        box_top = origin_y;
        if (style.top_set) box_top = origin_y + style.top;
        else if (style.bottom_set && style.height_set)
            box_top = origin_y + 380 - style.bottom - style.height;
        ctx->cursor_x = box_x;
        ctx->cursor_y = box_top;
        ctx->base_line_start = box_x;
        ctx->base_line_right = box_x + outer_width;
        ctx->line_start_x = ctx->base_line_start;
        ctx->line_right = ctx->base_line_right;
        ctx->line_height = NSBK_BASE_LINE_H;
        ctx->line_item_start = ctx->layout->item_count;
        ctx->pending_space = false;
        ctx->line_positioned = false;
        ctx->float_left_width = 0;
        ctx->float_right_width = 0;
        ctx->float_bottom = 0;
        style.width = outer_width;
        style.width_set = true;
        style.border_box = true;
    }

    {
        const char *anchor = nsbk_dom_attribute(node, "id");
        if ((!anchor || !anchor[0]) && nsbk_dom_name_is(node, "a"))
            anchor = nsbk_dom_attribute(node, "name");
        if (anchor && anchor[0])
            nsbk_layout_add_anchor(ctx->layout, anchor, ctx->cursor_y);
    }

    old_link = ctx->active_link;
    if (nsbk_dom_name_is(node, "a")) {
        const char *href = nsbk_dom_attribute(node, "href");
        uint32_t i;
        ctx->active_link = -1;
        if (href) for (i = 0U; i < ctx->result->link_count; i++)
            if (strcmp(ctx->result->links[i].href, href) == 0) {
                ctx->active_link = (int32_t)i;
                break;
            }
    }

    if ((nsbk_dom_name_is(node, "input") ||
         nsbk_dom_name_is(node, "textarea") ||
         nsbk_dom_name_is(node, "button") ||
         nsbk_dom_name_is(node, "select")) && emit_control(ctx, node, &style))
        goto done_restore_link;
    if (nsbk_dom_name_is(node, "br")) {
        finish_line(ctx);
        goto done_restore_link;
    }
    if (nsbk_dom_name_is(node, "hr")) {
        finish_line(ctx);
        ctx->cursor_y += style.margin_top;
        box = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_RULE);
        if (box) {
            box->x = ctx->line_start_x + style.margin_left;
            box->y = ctx->cursor_y + 2;
            box->width = ctx->line_right - ctx->line_start_x -
                         style.margin_left - style.margin_right;
            box->height = style.border_top > 0 ? style.border_top : 1;
            box->border = style.border;
        }
        ctx->cursor_y += 5 + style.margin_bottom;
        goto done_restore_link;
    }

    {
        bool forced_inline = ctx->force_inline_node == node;
        block = (is_block_display(style.display) || absolute) && !forced_inline;
        inline_block = is_inline_block_display(style.display) || forced_inline;
    }
    floated = !absolute &&
              (style.float_mode == CSS_FLOAT_LEFT ||
               style.float_mode == CSS_FLOAT_RIGHT);
    if (floated) {
        inline_block = true;
        block = false;
    }
    old_pre = ctx->preformatted;

    if (inline_block && !block) {
        int32_t available;
        int32_t inline_width;
        int32_t inline_height;
        int32_t outer_width;
        bool table_cell = style.display == CSS_DISPLAY_TABLE_CELL;
        save_flow(ctx, &parent_flow);
        if (floated) {
            finish_line(ctx);
            refresh_float_bounds(ctx);
        }
        available = ctx->line_right - ctx->line_start_x;
        if (style.width_set) inline_width = style.width;
        else if (style.flex_basis_set) inline_width = style.flex_basis;
        else if (table_cell)
            inline_width = table_cell_width(node, available);
        else
            inline_width = intrinsic_node_width(node, available);
        outer_width = inline_width;
        if (!style.border_box && !table_cell)
            outer_width += style.padding_left + style.padding_right +
                           style.border_left + style.border_right;
        if (style.min_width_set && outer_width < style.min_width)
            outer_width = style.min_width;
        if (style.max_width_set && outer_width > style.max_width)
            outer_width = style.max_width;
        if (outer_width > available && ctx->float_bottom > ctx->cursor_y) {
            ctx->cursor_y = ctx->float_bottom;
            ctx->float_left_width = 0;
            ctx->float_right_width = 0;
            ctx->float_bottom = 0;
            refresh_float_bounds(ctx);
            parent_flow.cursor_y = ctx->cursor_y;
            parent_flow.line_start_x = ctx->line_start_x;
            parent_flow.line_right = ctx->line_right;
            parent_flow.float_left_width = 0;
            parent_flow.float_right_width = 0;
            parent_flow.float_bottom = 0;
            available = ctx->line_right - ctx->line_start_x;
        }
        if (outer_width > available) outer_width = available;
        if (outer_width < 16) outer_width = 16;
        if (!floated && ctx->cursor_x != ctx->line_start_x &&
            ctx->cursor_x + style.margin_left + outer_width > ctx->line_right) {
            finish_line(ctx);
            save_flow(ctx, &parent_flow);
        }
        if (floated && style.float_mode == CSS_FLOAT_RIGHT)
            box_x = ctx->line_right - outer_width - style.margin_right;
        else
            box_x = (floated ? ctx->line_start_x : ctx->cursor_x) + style.margin_left;
        box_top = ctx->cursor_y + style.margin_top;
        box_width = outer_width;
        if (style.has_background || style.has_border || style.has_background_image) {
            box = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_BOX);
            if (box) {
                box->x = box_x;
                box->y = box_top;
                box->width = box_width;
                configure_box_background(ctx, box, &style);
            }
        }
        ctx->base_line_start = box_x + style.border_left + style.padding_left;
        ctx->base_line_right = box_x + box_width - style.border_right -
                               style.padding_right;
        if (ctx->base_line_right <= ctx->base_line_start)
            ctx->base_line_right = ctx->base_line_start + 1;
        ctx->line_start_x = ctx->base_line_start;
        ctx->line_right = ctx->base_line_right;
        ctx->cursor_x = ctx->line_start_x + style.text_indent;
        if (ctx->cursor_x < ctx->line_start_x) ctx->cursor_x = ctx->line_start_x;
        if (ctx->cursor_x >= ctx->line_right) ctx->cursor_x = ctx->line_start_x;
        ctx->cursor_y = box_top + style.border_top + style.padding_top;
        ctx->line_height = NSBK_BASE_LINE_H;
        ctx->line_item_start = ctx->layout->item_count;
        ctx->text_align = style.text_align;
        ctx->preformatted = old_pre || style.preformatted;
        ctx->pending_space = false;
        ctx->float_left_width = 0;
        ctx->float_right_width = 0;
        ctx->float_bottom = 0;
        if ((nsbk_dom_name_is(node, "img") || nsbk_dom_name_is(node, "video") ||
             (nsbk_dom_name_is(node, "input") && nsbk_dom_attribute(node, "type") &&
              text_equal_nocase(nsbk_dom_attribute(node, "type"), "image"))) &&
            !emit_image(ctx, node, &style)) {
            const char *alt = nsbk_dom_attribute(node, "alt");
            if (alt && alt[0]) emit_text(ctx, alt, (uint32_t)strlen(alt), &style);
        }
        if (style.display == CSS_DISPLAY_FLEX ||
            style.display == CSS_DISPLAY_INLINE_FLEX ||
            style.display == CSS_DISPLAY_GRID ||
            style.display == CSS_DISPLAY_INLINE_GRID)
            layout_flex_children(ctx, node, computed, &style);
        else
            for (child = node->first_child; child && !ctx->aborted;
                 child = child->next)
                layout_node(ctx, child, computed);
        finish_line(ctx);
        if (ctx->float_bottom > ctx->cursor_y) ctx->cursor_y = ctx->float_bottom;
        inline_height = ctx->cursor_y - box_top + style.padding_bottom +
                        style.border_bottom;
        if (style.height_set && !style.border_box)
            style.height += style.padding_top + style.padding_bottom +
                            style.border_top + style.border_bottom;
        if (style.height_set && inline_height < style.height)
            inline_height = style.height;
        if (style.min_height_set && inline_height < style.min_height)
            inline_height = style.min_height;
        if (style.max_height_set && inline_height > style.max_height)
            inline_height = style.max_height;
        if (inline_height < style.line_height) inline_height = style.line_height;
        if (box) box->height = inline_height;
        restore_flow(ctx, &parent_flow);
        if (floated) {
            int32_t occupied = outer_width + style.margin_left + style.margin_right + 2;
            if (style.float_mode == CSS_FLOAT_LEFT)
                ctx->float_left_width += occupied;
            else
                ctx->float_right_width += occupied;
            if (box_top + inline_height + style.margin_bottom > ctx->float_bottom)
                ctx->float_bottom = box_top + inline_height + style.margin_bottom;
            refresh_float_bounds(ctx);
            ctx->cursor_x = ctx->line_start_x;
        } else {
            ctx->cursor_x = box_x + box_width + style.margin_right + 2;
            if (inline_height + style.margin_top + style.margin_bottom > ctx->line_height)
                ctx->line_height = inline_height + style.margin_top + style.margin_bottom;
        }
        if (ctx->cursor_x > ctx->layout->width) ctx->layout->width = ctx->cursor_x;
        goto done_restore_link;
    }

    if (block) {
        int32_t available;
        int32_t requested;
        finish_line(ctx);
        save_flow(ctx, &parent_flow);
        ctx->cursor_y += style.margin_top;
        available = parent_flow.line_right - parent_flow.line_start_x;
        requested = available - style.margin_left - style.margin_right;
        if (style.width_set) {
            requested = style.width;
            if (!style.border_box)
                requested += style.padding_left + style.padding_right +
                             style.border_left + style.border_right;
        }
        if (style.min_width_set && requested < style.min_width)
            requested = style.min_width;
        if (style.max_width_set && requested > style.max_width)
            requested = style.max_width;
        if (requested > available) requested = available;
        if (requested < 16) requested = 16;
        box_width = requested;
        if (style.margin_left_auto && style.margin_right_auto && box_width < available)
            box_x = parent_flow.line_start_x + (available - box_width) / 2;
        else if (style.margin_left_auto && box_width < available)
            box_x = parent_flow.line_right - style.margin_right - box_width;
        else
            box_x = parent_flow.line_start_x + style.margin_left;
        box_top = ctx->cursor_y;
        if (style.has_background || style.has_border || style.has_background_image) {
            box = nsbk_layout_add(ctx->layout, NSBK_LAYOUT_BOX);
            if (box) {
                box->x = box_x;
                box->y = box_top;
                box->width = box_width;
                configure_box_background(ctx, box, &style);
            }
        }
        ctx->base_line_start = box_x + style.border_left + style.padding_left;
        ctx->base_line_right = box_x + box_width - style.border_right -
                               style.padding_right;
        if (ctx->base_line_right <= ctx->base_line_start)
            ctx->base_line_right = ctx->base_line_start + 1;
        ctx->line_start_x = ctx->base_line_start;
        ctx->line_right = ctx->base_line_right;
        ctx->cursor_x = ctx->line_start_x + style.text_indent;
        if (ctx->cursor_x < ctx->line_start_x) ctx->cursor_x = ctx->line_start_x;
        if (ctx->cursor_x >= ctx->line_right) ctx->cursor_x = ctx->line_start_x;
        ctx->line_item_start = ctx->layout->item_count;
        ctx->text_align = style.text_align;
        ctx->cursor_y += style.border_top + style.padding_top;
        ctx->float_left_width = 0;
        ctx->float_right_width = 0;
        ctx->float_bottom = 0;
        if (style.display == CSS_DISPLAY_LIST_ITEM)
            add_list_marker(ctx, node, &style);
    }

    ctx->preformatted = old_pre || style.preformatted;
    if ((nsbk_dom_name_is(node, "img") || nsbk_dom_name_is(node, "video") ||
         (nsbk_dom_name_is(node, "input") && nsbk_dom_attribute(node, "type") &&
          text_equal_nocase(nsbk_dom_attribute(node, "type"), "image"))) &&
        !emit_image(ctx, node, &style)) {
        const char *alt = nsbk_dom_attribute(node, "alt");
        if (alt && alt[0]) {
            emit_text(ctx, "[", 1U, &style);
            emit_text(ctx, alt, (uint32_t)strlen(alt), &style);
            emit_text(ctx, "]", 1U, &style);
        } else emit_text(ctx, "[imagen]", 8U, &style);
    }
    if (style.display == CSS_DISPLAY_FLEX ||
        style.display == CSS_DISPLAY_INLINE_FLEX ||
        style.display == CSS_DISPLAY_GRID ||
        style.display == CSS_DISPLAY_INLINE_GRID)
        layout_flex_children(ctx, node, computed, &style);
    else
        for (child = node->first_child; child && !ctx->aborted;
             child = child->next)
            layout_node(ctx, child, computed);

    if (block) {
        int32_t end_y;
        finish_line(ctx);
        if (ctx->float_bottom > ctx->cursor_y) ctx->cursor_y = ctx->float_bottom;
        ctx->cursor_y += style.padding_bottom + style.border_bottom;
        if (style.height_set && !style.border_box)
            style.height += style.padding_top + style.padding_bottom +
                            style.border_top + style.border_bottom;
        if (style.height_set && ctx->cursor_y - box_top < style.height)
            ctx->cursor_y = box_top + style.height;
        if (style.min_height_set && ctx->cursor_y - box_top < style.min_height)
            ctx->cursor_y = box_top + style.min_height;
        if (style.max_height_set && ctx->cursor_y - box_top > style.max_height)
            ctx->cursor_y = box_top + style.max_height;
        if (box) box->height = ctx->cursor_y - box_top;
        end_y = ctx->cursor_y + style.margin_bottom;
        restore_flow(ctx, &parent_flow);
        ctx->cursor_y = end_y;
        refresh_float_bounds(ctx);
        ctx->cursor_x = ctx->line_start_x;
        ctx->line_item_start = ctx->layout->item_count;
    }
    ctx->preformatted = old_pre;

done_restore_link:
    ctx->active_link = old_link;
done:
    if (style.position == CSS_POSITION_FIXED) {
        uint32_t fixed_index;
        for (fixed_index = node_item_start;
             fixed_index < ctx->layout->item_count; fixed_index++)
            ctx->layout->items[fixed_index].flags |= NSBK_LAYOUT_FIXED;
    }
    if (relative_dx || relative_dy) {
        shift_item_range(ctx->layout, node_item_start, ctx->layout->item_count,
                         relative_dx, relative_dy);
        shift_anchor_range(ctx->layout, node_anchor_start, relative_dy);
    }
    if (absolute) restore_flow(ctx, &absolute_flow);
    if (composed) css_computed_style_destroy(composed);
    if (results) css_select_results_destroy(results);
}

static void forget_node(nsbk_dom_node_t *node) {
    nsbk_dom_node_t *child;
    if (!node) return;
    for (child = node->first_child; child; child = child->next) forget_node(child);
    if (node->libcss_node_data) {
        css_libcss_node_data_handler(&selector_handler, CSS_NODE_DELETED,
            NULL, node, NULL, node->libcss_node_data);
        node->libcss_node_data = NULL;
    }
}

void nsbk_css_forget_tree(nsbk_dom_tree_t *tree) {
    if (tree && tree->document) forget_node(tree->document);
}

void nsbk_css_compiled_destroy(void *raw) {
    nsbk_compiled_css_t *compiled = (nsbk_compiled_css_t *)raw;
    uint32_t i;
    if (!compiled) return;
    for (i = compiled->sheet_count; i > 0U; i--)
        css_stylesheet_destroy(compiled->sheets[i - 1U]);
    free(compiled);
}

bool nsbk_css_layout_build(nsbk_dom_tree_t *tree,
                           nsbk_html_result_t *result) {
    css_context_t ctx;
    css_stylesheet *ua;
    uint32_t i;
    if (!tree || !tree->document || !result || !result->layout) return false;
    nsbk_layout_reset(result->layout);
    memset(&ctx, 0, sizeof(ctx));
    ctx.layout = result->layout;
    ctx.result = result;
    ctx.viewport_width = result->viewport_width > 160 ?
                         result->viewport_width : 640;
    ctx.cursor_x = NSBK_PAGE_PAD;
    ctx.cursor_y = NSBK_PAGE_PAD;
    ctx.line_start_x = NSBK_PAGE_PAD;
    ctx.line_right = ctx.viewport_width - NSBK_PAGE_PAD;
    ctx.base_line_start = ctx.line_start_x;
    ctx.base_line_right = ctx.line_right;
    ctx.line_height = NSBK_BASE_LINE_H;
    ctx.line_item_start = 0U;
    ctx.text_align = CSS_TEXT_ALIGN_LEFT;
    ctx.active_link = -1;
    ctx.deadline_ms = bk_sys_uptime_ms() + NSBK_LAYOUT_BUDGET_MS;
    nsbk_css_compat_init(&ctx.compatibility, ctx.viewport_width, 380);
    ctx.units.viewport_width = INTTOFIX(ctx.viewport_width);
    ctx.units.viewport_height = INTTOFIX(380);
    ctx.units.font_size_default = INTTOFIX(12);
    ctx.units.font_size_minimum = INTTOFIX(8);
    ctx.units.device_dpi = INTTOFIX(96);
    memset(&ctx.media, 0, sizeof(ctx.media));
    ctx.media.type = CSS_MEDIA_SCREEN;
    ctx.media.width = INTTOFIX(ctx.viewport_width);
    ctx.media.height = INTTOFIX(380);
    ctx.media.color = INTTOFIX(24);
    ctx.media.scripting = CSS_MEDIA_SCRIPTING_NONE;
    if (css_select_ctx_create(&ctx.select) != CSS_OK) return false;
    ua = create_sheet(&ctx.compatibility, (const uint8_t *)nsbk_ua_css,
                      (uint32_t)strlen(nsbk_ua_css), false,
                      result->quirks_mode != 0U, "resource:default.css");
    if (!ua || css_select_ctx_append_sheet(ctx.select, ua, CSS_ORIGIN_UA,
                                           "screen") != CSS_OK) {
        if (ua) css_stylesheet_destroy(ua);
        css_select_ctx_destroy(ctx.select);
        return false;
    }
    ctx.sheets[ctx.sheet_count] = ua;
    ctx.sheet_owned[ctx.sheet_count++] = true;
    collect_author_sheets(&ctx, tree->document);
    if (!layout_should_abort(&ctx)) layout_node(&ctx, tree->document, NULL);
    if (!ctx.aborted) finish_line(&ctx);
    {
        int32_t maximum_right = ctx.viewport_width;
        int32_t maximum_bottom = ctx.cursor_y;
        for (i = 0U; i < ctx.layout->item_count; i++) {
            int32_t right = ctx.layout->items[i].x + ctx.layout->items[i].width;
            int32_t bottom = ctx.layout->items[i].y + ctx.layout->items[i].height;
            if (right > maximum_right) maximum_right = right;
            if (bottom > maximum_bottom) maximum_bottom = bottom;
        }
        ctx.layout->width = maximum_right + NSBK_PAGE_PAD;
        ctx.layout->height = maximum_bottom + NSBK_PAGE_PAD;
    }
    ctx.layout->stylesheet_count = ctx.sheet_count;
    ctx.layout->valid = ctx.layout->item_count > 0U;
    if (ctx.aborted)
        bk_console_write("[NETSURF][LAYOUT] limite alcanzado; mostrando resultado parcial\n");
    for (i = ctx.sheet_count; i > 0U; i--)
        if (ctx.sheet_owned[i - 1U])
            css_stylesheet_destroy(ctx.sheets[i - 1U]);
    css_select_ctx_destroy(ctx.select);
    return ctx.layout->valid;
}
