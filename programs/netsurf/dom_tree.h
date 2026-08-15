#ifndef BLESKERNOS_NETSURF_DOM_TREE_H
#define BLESKERNOS_NETSURF_DOM_TREE_H

#include <hubbub/hubbub.h>
#include <hubbub/tree.h>
#include <libwapcaplet/libwapcaplet.h>

#include "html_parser.h"

typedef enum {
    NSBK_DOM_DOCUMENT,
    NSBK_DOM_DOCTYPE,
    NSBK_DOM_COMMENT,
    NSBK_DOM_ELEMENT,
    NSBK_DOM_TEXT
} nsbk_dom_node_type_t;

typedef struct {
    hubbub_ns ns;
    lwc_string *name;
    char *value;
    uint32_t value_length;
} nsbk_dom_attribute_t;

typedef struct nsbk_dom_node {
    nsbk_dom_node_type_t type;
    uint32_t references;
    hubbub_ns ns;
    lwc_string *name;
    char *text;
    uint32_t text_length;
    nsbk_dom_attribute_t *attributes;
    uint32_t attribute_count;
    struct nsbk_dom_node *parent;
    struct nsbk_dom_node *first_child;
    struct nsbk_dom_node *last_child;
    struct nsbk_dom_node *previous;
    struct nsbk_dom_node *next;
    struct nsbk_dom_node *form;
    lwc_string **css_classes;
    uint32_t css_class_count;
    lwc_string *css_id;
    void *libcss_node_data;
    bool css_classes_ready;
    bool css_id_ready;
} nsbk_dom_node_t;

typedef struct nsbk_dom_tree {
    nsbk_dom_node_t *document;
    hubbub_tree_handler handler;
    hubbub_quirks_mode quirks_mode;
    uint32_t node_count;
    uint32_t element_count;
    uint32_t errors;
    bool out_of_memory;
    char requested_charset[32];
    char charset[32];
} nsbk_dom_tree_t;

bool nsbk_dom_tree_init(nsbk_dom_tree_t *tree);
void nsbk_dom_tree_destroy(nsbk_dom_tree_t *tree);
const char *nsbk_dom_attribute(const nsbk_dom_node_t *node,
                               const char *name);
bool nsbk_dom_name_is(const nsbk_dom_node_t *node, const char *name);
bool nsbk_dom_image_source(const nsbk_dom_node_t *node, int32_t viewport_width,
                           char *destination, uint32_t capacity);
void nsbk_dom_render(nsbk_dom_tree_t *tree, nsbk_html_result_t *result);

#endif
