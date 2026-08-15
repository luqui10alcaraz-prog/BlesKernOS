#include "layout.h"

#include <string.h>

bool nsbk_layout_init(nsbk_layout_t *layout) {
    if (!layout) return false;
    memset(layout, 0, sizeof(*layout));
    layout->items = (nsbk_layout_item_t *)bk_sys_alloc(
        sizeof(nsbk_layout_item_t) * NSBK_LAYOUT_ITEM_MAX);
    layout->text = (char *)bk_sys_alloc(NSBK_LAYOUT_TEXT_MAX);
    if (!layout->items || !layout->text) {
        nsbk_layout_destroy(layout);
        return false;
    }
    nsbk_layout_reset(layout);
    return true;
}

void nsbk_layout_reset(nsbk_layout_t *layout) {
    if (!layout) return;
    layout->item_count = 0U;
    layout->text_length = 0U;
    layout->width = 0;
    layout->height = 0;
    layout->stylesheet_count = 0U;
    layout->styled_node_count = 0U;
    layout->anchor_count = 0U;
    layout->valid = false;
    layout->truncated = false;
    if (layout->text) layout->text[0] = '\0';
}

void nsbk_layout_destroy(nsbk_layout_t *layout) {
    if (!layout) return;
    if (layout->items) bk_sys_free(layout->items);
    if (layout->text) bk_sys_free(layout->text);
    memset(layout, 0, sizeof(*layout));
}

nsbk_layout_item_t *nsbk_layout_add(nsbk_layout_t *layout, uint16_t flags) {
    nsbk_layout_item_t *item;
    if (!layout || !layout->items || layout->item_count >= NSBK_LAYOUT_ITEM_MAX) {
        if (layout) layout->truncated = true;
        return NULL;
    }
    item = &layout->items[layout->item_count++];
    memset(item, 0, sizeof(*item));
    item->flags = flags;
    item->link_index = -1;
    item->image_index = -1;
    item->control_index = -1;
    item->opacity = 255U;
    return item;
}

bool nsbk_layout_store_text(nsbk_layout_t *layout, const char *text,
                            uint32_t length, uint32_t *offset) {
    uint32_t start;
    if (!layout || !layout->text || !text || !offset) return false;
    if (length + 1U > NSBK_LAYOUT_TEXT_MAX - layout->text_length) {
        layout->truncated = true;
        return false;
    }
    start = layout->text_length;
    memcpy(layout->text + start, text, length);
    layout->text[start + length] = '\0';
    layout->text_length += length + 1U;
    *offset = start;
    return true;
}


static char nsbk_anchor_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

bool nsbk_layout_add_anchor(nsbk_layout_t *layout, const char *name, int32_t y) {
    uint32_t i, n;
    nsbk_layout_anchor_t *anchor;
    if (!layout || !name || !name[0]) return false;
    for (i = 0U; i < layout->anchor_count; i++) {
        const char *a = layout->anchors[i].name;
        for (n = 0U; a[n] && name[n] &&
             nsbk_anchor_lower(a[n]) == nsbk_anchor_lower(name[n]); n++) { }
        if (!a[n] && !name[n]) return true;
    }
    if (layout->anchor_count >= NSBK_LAYOUT_ANCHOR_MAX) return false;
    anchor = &layout->anchors[layout->anchor_count++];
    for (n = 0U; name[n] && n + 1U < NSBK_LAYOUT_ANCHOR_NAME_MAX; n++)
        anchor->name[n] = name[n];
    anchor->name[n] = '\0';
    anchor->y = y;
    return true;
}

bool nsbk_layout_find_anchor(const nsbk_layout_t *layout, const char *name,
                             int32_t *y) {
    uint32_t i, n;
    if (!layout || !name || !y) return false;
    for (i = 0U; i < layout->anchor_count; i++) {
        const char *a = layout->anchors[i].name;
        for (n = 0U; a[n] && name[n] &&
             nsbk_anchor_lower(a[n]) == nsbk_anchor_lower(name[n]); n++) { }
        if (!a[n] && !name[n]) { *y = layout->anchors[i].y; return true; }
    }
    return false;
}
