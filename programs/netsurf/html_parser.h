#ifndef BLESKERNOS_NETSURF_HTML_PARSER_H
#define BLESKERNOS_NETSURF_HTML_PARSER_H

#include <bleskernos_api.h>

#define NSBK_HTML_TITLE_MAX 128U
#define NSBK_HTML_LINK_MAX   48U
#define NSBK_HTML_LINK_TEXT_MAX 96U
#define NSBK_HTML_STYLESHEET_MAX 16U
#define NSBK_HTML_RESOURCE_URL_MAX 1024U
#define NSBK_HTML_MEDIA_MAX 160U
#define NSBK_HTML_IMAGE_MAX 24U
#define NSBK_HTML_FORM_MAX 8U
#define NSBK_HTML_CONTROL_MAX 32U
#define NSBK_HTML_OPTION_MAX 64U
#define NSBK_HTML_OPTION_TEXT_MAX 80U
#define NSBK_HTML_FORM_NAME_MAX 48U
#define NSBK_HTML_FORM_VALUE_MAX 160U
#define NSBK_HTML_FORM_LABEL_MAX 80U
#define NSBK_HTML_IMAGE_SOURCE_MAX 1024U

struct nsbk_layout;
struct nsbk_dom_tree;

typedef struct {
    char href[256];
    char text[NSBK_HTML_LINK_TEXT_MAX];
} nsbk_html_link_t;

typedef enum {
    NSBK_CONTROL_TEXT,
    NSBK_CONTROL_SEARCH,
    NSBK_CONTROL_PASSWORD,
    NSBK_CONTROL_HIDDEN,
    NSBK_CONTROL_SUBMIT,
    NSBK_CONTROL_BUTTON,
    NSBK_CONTROL_CHECKBOX,
    NSBK_CONTROL_RADIO,
    NSBK_CONTROL_TEXTAREA,
    NSBK_CONTROL_SELECT
} nsbk_html_control_type_t;

typedef struct {
    char action[256];
    char method[8];
    uint16_t first_control;
    uint16_t control_count;
    const void *node;
} nsbk_html_form_t;

typedef struct {
    char name[NSBK_HTML_FORM_NAME_MAX];
    char value[NSBK_HTML_FORM_VALUE_MAX];
    char label[NSBK_HTML_FORM_LABEL_MAX];
    char placeholder[NSBK_HTML_FORM_LABEL_MAX];
    uint16_t form_index;
    uint16_t first_option;
    uint16_t option_count;
    uint16_t selected_option;
    uint8_t type;
    bool checked;
    bool disabled;
    const void *node;
} nsbk_html_control_t;


typedef struct {
    char value[NSBK_HTML_OPTION_TEXT_MAX];
    char label[NSBK_HTML_OPTION_TEXT_MAX];
    bool disabled;
} nsbk_html_option_t;

typedef struct {
    char href[NSBK_HTML_RESOURCE_URL_MAX];
    char media[NSBK_HTML_MEDIA_MAX];
    bool alternate;
    bool preload;
} nsbk_html_stylesheet_ref_t;

typedef struct {
    char src[NSBK_HTML_IMAGE_SOURCE_MAX];
    char alt[NSBK_HTML_LINK_TEXT_MAX];
    uint16_t width_hint;
    uint16_t height_hint;
} nsbk_html_image_ref_t;

typedef struct {
    const char *reference;
    const char *url;
    const char *media;
    const uint8_t *data;
    uint32_t length;
    bool imported;
    void *compiled;
} nsbk_css_resource_t;

typedef struct {
    const char *reference;
    const char *url;
    const bk_gui_image_t *image;
} nsbk_image_resource_t;

typedef struct {
    const char *document_url;
    const nsbk_css_resource_t *stylesheets;
    uint32_t stylesheet_count;
    const nsbk_image_resource_t *images;
    uint32_t image_count;
} nsbk_resource_environment_t;

typedef struct {
    char *text;
    uint32_t text_capacity;
    uint32_t text_length;
    char title[NSBK_HTML_TITLE_MAX];
    char charset[32];
    char base_href[256];
    nsbk_html_link_t links[NSBK_HTML_LINK_MAX];
    uint32_t link_count;
    nsbk_html_stylesheet_ref_t stylesheet_refs[NSBK_HTML_STYLESHEET_MAX];
    uint32_t stylesheet_ref_count;
    nsbk_html_image_ref_t image_refs[NSBK_HTML_IMAGE_MAX];
    uint32_t image_ref_count;
    nsbk_html_form_t forms[NSBK_HTML_FORM_MAX];
    uint32_t form_count;
    nsbk_html_control_t controls[NSBK_HTML_CONTROL_MAX];
    uint32_t control_count;
    nsbk_html_option_t options[NSBK_HTML_OPTION_MAX];
    uint32_t option_count;
    uint32_t parse_errors;
    uint32_t node_count;
    uint32_t element_count;
    uint32_t quirks_mode;
    bool built_dom;
    bool truncated;
    struct nsbk_layout *layout;
    int32_t viewport_width;
    bool styled_layout;
    const nsbk_resource_environment_t *resources;
} nsbk_html_result_t;

bool nsbk_html_parse(const uint8_t *data, uint32_t length,
                     const char *charset, nsbk_html_result_t *result);
bool nsbk_html_parse_retained(const uint8_t *data, uint32_t length,
                              const char *charset, nsbk_html_result_t *result,
                              struct nsbk_dom_tree **tree_out);
bool nsbk_html_render_retained(struct nsbk_dom_tree *tree,
                               nsbk_html_result_t *result);
void nsbk_html_destroy_retained(struct nsbk_dom_tree *tree);

#endif
