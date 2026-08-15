#ifndef BLESKERNOS_NETSURF_LAYOUT_H
#define BLESKERNOS_NETSURF_LAYOUT_H

#include <bleskernos_api.h>
#include <stdint.h>

#define NSBK_LAYOUT_ITEM_MAX 600U
#define NSBK_LAYOUT_TEXT_MAX 24576U
#define NSBK_LAYOUT_ANCHOR_MAX 48U
#define NSBK_LAYOUT_ANCHOR_NAME_MAX 64U

#define NSBK_LAYOUT_BOLD      0x01U
#define NSBK_LAYOUT_UNDERLINE 0x02U
#define NSBK_LAYOUT_BOX       0x04U
#define NSBK_LAYOUT_TEXT      0x08U
#define NSBK_LAYOUT_RULE      0x10U
#define NSBK_LAYOUT_BACKGROUND 0x20U
#define NSBK_LAYOUT_BORDER     0x40U
#define NSBK_LAYOUT_IMAGE      0x0080U
#define NSBK_LAYOUT_CONTROL    0x0100U
#define NSBK_LAYOUT_BACKGROUND_IMAGE 0x0200U
#define NSBK_LAYOUT_FIXED            0x0400U
#define NSBK_FONT_ITALIC              0x01U
#define NSBK_FONT_MONOSPACE           0x02U
#define NSBK_BG_POS_X_PERCENT         0x01U
#define NSBK_BG_POS_Y_PERCENT         0x02U

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t foreground;
    uint32_t background;
    uint32_t border;
    uint32_t text_offset;
    int32_t link_index;
    int32_t image_index;
    int32_t control_index;
    int16_t background_x;
    int16_t background_y;
    int8_t letter_spacing;
    int8_t word_spacing;
    uint8_t background_repeat;
    uint8_t font_px;
    uint8_t font_flags;
    uint8_t opacity;
    uint8_t background_position_flags;
    uint8_t reserved;
    uint16_t text_length;
    uint16_t flags;
} nsbk_layout_item_t;


typedef struct {
    char name[NSBK_LAYOUT_ANCHOR_NAME_MAX];
    int32_t y;
} nsbk_layout_anchor_t;

typedef struct nsbk_layout {
    nsbk_layout_item_t *items;
    char *text;
    uint32_t item_count;
    uint32_t text_length;
    int32_t width;
    int32_t height;
    uint32_t stylesheet_count;
    uint32_t styled_node_count;
    nsbk_layout_anchor_t anchors[NSBK_LAYOUT_ANCHOR_MAX];
    uint32_t anchor_count;
    bool valid;
    bool truncated;
} nsbk_layout_t;

bool nsbk_layout_init(nsbk_layout_t *layout);
void nsbk_layout_reset(nsbk_layout_t *layout);
void nsbk_layout_destroy(nsbk_layout_t *layout);
nsbk_layout_item_t *nsbk_layout_add(nsbk_layout_t *layout, uint16_t flags);
bool nsbk_layout_store_text(nsbk_layout_t *layout, const char *text,
                            uint32_t length, uint32_t *offset);
bool nsbk_layout_add_anchor(nsbk_layout_t *layout, const char *name, int32_t y);
bool nsbk_layout_find_anchor(const nsbk_layout_t *layout, const char *name,
                             int32_t *y);

#endif
