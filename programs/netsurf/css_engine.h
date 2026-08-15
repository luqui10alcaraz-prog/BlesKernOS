#ifndef BLESKERNOS_NETSURF_CSS_ENGINE_H
#define BLESKERNOS_NETSURF_CSS_ENGINE_H

#include "dom_tree.h"
#include "html_parser.h"

bool nsbk_css_layout_build(nsbk_dom_tree_t *tree,
                           nsbk_html_result_t *result);
void nsbk_css_forget_tree(nsbk_dom_tree_t *tree);
void nsbk_css_compiled_destroy(void *compiled);

#endif
