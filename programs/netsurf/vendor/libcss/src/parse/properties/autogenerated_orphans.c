/* Restored output of LibCSS gen_parser. */
#include <assert.h>
#include <string.h>
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

css_error css__parse_orphans(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	int32_t orig_ctx = *ctx;
	css_error error;
	const css_token *token;
	bool match;
	token = parserutils_vector_iterate(vector, ctx);
	if (token == NULL || (token->type != CSS_TOKEN_IDENT &&
			token->type != CSS_TOKEN_NUMBER)) {
		*ctx = orig_ctx;
		return CSS_INVALID;
	}
	if (token->type == CSS_TOKEN_IDENT &&
		lwc_string_caseless_isequal(token->idata, c->strings[INHERIT],
			&match) == lwc_error_ok && match) {
		error = css_stylesheet_style_inherit(result, CSS_PROP_ORPHANS);
	} else if (token->type == CSS_TOKEN_IDENT &&
		lwc_string_caseless_isequal(token->idata, c->strings[INITIAL],
			&match) == lwc_error_ok && match) {
		error = css_stylesheet_style_initial(result, CSS_PROP_ORPHANS);
	} else if (token->type == CSS_TOKEN_IDENT &&
		lwc_string_caseless_isequal(token->idata, c->strings[REVERT],
			&match) == lwc_error_ok && match) {
		error = css_stylesheet_style_revert(result, CSS_PROP_ORPHANS);
	} else if (token->type == CSS_TOKEN_IDENT &&
		lwc_string_caseless_isequal(token->idata, c->strings[UNSET],
			&match) == lwc_error_ok && match) {
		error = css_stylesheet_style_unset(result, CSS_PROP_ORPHANS);
	} else if (token->type == CSS_TOKEN_NUMBER) {
		css_fixed num;
		size_t consumed = 0;
		num = css__number_from_lwc_string(token->idata, true, &consumed);
		if (consumed != lwc_string_length(token->idata) || num < 0) {
			*ctx = orig_ctx;
			return CSS_INVALID;
		}
		error = css__stylesheet_style_appendOPV(result, CSS_PROP_ORPHANS,
				0, ORPHANS_SET);
		if (error == CSS_OK)
			error = css__stylesheet_style_append(result, num);
	} else {
		error = CSS_INVALID;
	}
	if (error != CSS_OK) *ctx = orig_ctx;
	return error;
}
