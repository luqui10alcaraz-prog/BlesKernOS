/* Restored output of LibCSS gen_parser. */
#include <assert.h>
#include <string.h>
#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

css_error css__parse_border_left(css_language *c,
		const parserutils_vector *vector, int32_t *ctx, css_style *result)
{
	return css__parse_border_side(c, vector, ctx, result, BORDER_SIDE_LEFT);
}
