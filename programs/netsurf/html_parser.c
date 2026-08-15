#include "html_parser.h"
#include "dom_tree.h"
#include "css_engine.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hubbub/hubbub.h>
#include <hubbub/parser.h>

static void copy_charset(char destination[32], const char *source) {
    uint32_t i = 0U;
    if (!source || !source[0]) source = "UTF-8";
    while (source[i] && i + 1U < 32U) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void parse_error_handler(uint32_t line, uint32_t column,
                                const char *message, void *private_word) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)private_word;
    (void)line;
    (void)column;
    (void)message;
    if (tree) tree->errors++;
}

bool nsbk_html_parse_retained(const uint8_t *data, uint32_t length,
                              const char *charset, nsbk_html_result_t *result,
                              struct nsbk_dom_tree **tree_out) {
    nsbk_dom_tree_t *tree;
    hubbub_parser *parser = NULL;
    hubbub_parser_optparams params;
    hubbub_charset_source source;
    const char *detected_charset;
    hubbub_error error;
    uint32_t offset = 0U;
    bool success = false;

    if (!data || !result || !tree_out || !result->text ||
        result->text_capacity == 0U)
        return false;
    *tree_out = NULL;

    result->text_length = 0U;
    result->text[0] = '\0';
    result->title[0] = '\0';
    result->base_href[0] = '\0';
    result->link_count = 0U;
    result->stylesheet_ref_count = 0U;
    result->image_ref_count = 0U;
    result->parse_errors = 0U;
    result->node_count = 0U;
    result->element_count = 0U;
    result->quirks_mode = 0U;
    result->built_dom = false;
    result->truncated = false;
    result->styled_layout = false;
    copy_charset(result->charset, charset);

    tree = (nsbk_dom_tree_t *)calloc(1U, sizeof(*tree));
    if (!tree || !nsbk_dom_tree_init(tree)) {
        if (tree) free(tree);
        return false;
    }

    error = hubbub_parser_create(result->charset, false, &parser);
    if (error != HUBBUB_OK || !parser) goto cleanup;

    params.tree_handler = &tree->handler;
    error = hubbub_parser_setopt(parser, HUBBUB_PARSER_TREE_HANDLER, &params);
    if (error != HUBBUB_OK) goto cleanup;

    params.document_node = tree->document;
    error = hubbub_parser_setopt(parser, HUBBUB_PARSER_DOCUMENT_NODE, &params);
    if (error != HUBBUB_OK) goto cleanup;

    params.error_handler.handler = parse_error_handler;
    params.error_handler.pw = tree;
    error = hubbub_parser_setopt(parser, HUBBUB_PARSER_ERROR_HANDLER, &params);
    if (error != HUBBUB_OK) goto cleanup;

    params.enable_scripting = false;
    error = hubbub_parser_setopt(parser, HUBBUB_PARSER_ENABLE_SCRIPTING,
                                 &params);
    if (error != HUBBUB_OK) goto cleanup;

    while (offset < length) {
        uint32_t chunk = length - offset;
        if (chunk > 4096U) chunk = 4096U;
        error = hubbub_parser_parse_chunk(parser, data + offset, chunk);
        if (error != HUBBUB_OK) goto cleanup;
        offset += chunk;
    }

    error = hubbub_parser_completed(parser);
    if (error != HUBBUB_OK) goto cleanup;

    detected_charset = hubbub_parser_read_charset(parser, &source);
    if (detected_charset && detected_charset[0])
        copy_charset(result->charset, detected_charset);
    else if (tree->requested_charset[0])
        copy_charset(result->charset, tree->requested_charset);
    copy_charset(tree->charset, result->charset);

    result->quirks_mode = (uint32_t)tree->quirks_mode;
    nsbk_dom_render(tree, result);
    if (result->layout)
        result->styled_layout = nsbk_css_layout_build(tree, result);
    result->parse_errors = tree->errors;
    result->node_count = tree->node_count;
    result->element_count = tree->element_count;
    result->quirks_mode = (uint32_t)tree->quirks_mode;
    result->built_dom = true;
    success = !tree->out_of_memory;

cleanup:
    if (parser) hubbub_parser_destroy(parser);
    if (!success) {
        result->parse_errors = tree->errors;
        result->node_count = tree->node_count;
        result->element_count = tree->element_count;
        result->quirks_mode = (uint32_t)tree->quirks_mode;
    }
    if (success) {
        *tree_out = tree;
    } else {
        nsbk_css_forget_tree(tree);
        nsbk_dom_tree_destroy(tree);
        free(tree);
    }
    return success;
}

bool nsbk_html_render_retained(struct nsbk_dom_tree *raw_tree,
                               nsbk_html_result_t *result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)raw_tree;
    if (!tree || !tree->document || !result || !result->text ||
        result->text_capacity == 0U) return false;
    result->text_length = 0U;
    result->text[0] = '\0';
    result->title[0] = '\0';
    result->base_href[0] = '\0';
    result->link_count = 0U;
    result->stylesheet_ref_count = 0U;
    result->image_ref_count = 0U;
    result->form_count = 0U;
    result->control_count = 0U;
    result->option_count = 0U;
    result->styled_layout = false;
    copy_charset(result->charset, tree->charset);
    result->quirks_mode = (uint32_t)tree->quirks_mode;
    nsbk_css_forget_tree(tree);
    nsbk_dom_render(tree, result);
    if (result->layout)
        result->styled_layout = nsbk_css_layout_build(tree, result);
    result->parse_errors = tree->errors;
    result->node_count = tree->node_count;
    result->element_count = tree->element_count;
    result->quirks_mode = (uint32_t)tree->quirks_mode;
    result->built_dom = true;
    result->truncated = tree->out_of_memory;
    return !tree->out_of_memory;
}

void nsbk_html_destroy_retained(struct nsbk_dom_tree *raw_tree) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)raw_tree;
    if (!tree) return;
    nsbk_css_forget_tree(tree);
    nsbk_dom_tree_destroy(tree);
    free(tree);
}

bool nsbk_html_parse(const uint8_t *data, uint32_t length,
                     const char *charset, nsbk_html_result_t *result) {
    struct nsbk_dom_tree *tree = NULL;
    bool success = nsbk_html_parse_retained(data, length, charset, result,
                                            &tree);
    nsbk_html_destroy_retained(tree);
    return success;
}
