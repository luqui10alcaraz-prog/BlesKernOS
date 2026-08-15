#include "css_compat.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    uint32_t length;
    uint32_t capacity;
} compat_buffer_t;

static bool buffer_reserve(compat_buffer_t *buffer, uint32_t extra) {
    uint32_t required;
    uint32_t capacity;
    char *replacement;
    if (!buffer || extra > 0xffffffffU - buffer->length - 1U) return false;
    required = buffer->length + extra + 1U;
    if (required <= buffer->capacity) return true;
    capacity = buffer->capacity ? buffer->capacity : 1024U;
    while (capacity < required) {
        if (capacity > 2U * 1024U * 1024U) { capacity = required; break; }
        capacity *= 2U;
    }
    replacement = (char *)realloc(buffer->data, capacity);
    if (!replacement) return false;
    buffer->data = replacement;
    buffer->capacity = capacity;
    return true;
}

static bool buffer_append(compat_buffer_t *buffer, const char *text,
                          uint32_t length) {
    if (!buffer || (!text && length) || !buffer_reserve(buffer, length))
        return false;
    if (length) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool buffer_character(compat_buffer_t *buffer, char character) {
    return buffer_append(buffer, &character, 1U);
}

static bool name_character(char character) {
    return isalnum((unsigned char)character) || character == '-' ||
           character == '_';
}

static uint32_t trim_left(const char *text, uint32_t start, uint32_t end) {
    while (start < end && isspace((unsigned char)text[start])) start++;
    return start;
}

static uint32_t trim_right(const char *text, uint32_t start, uint32_t end) {
    while (end > start && isspace((unsigned char)text[end - 1U])) end--;
    return end;
}

static int variable_index(const nsbk_css_compat_t *compat, const char *name,
                          uint32_t length) {
    uint32_t i;
    if (!compat || !name || length < 3U) return -1;
    for (i = 0U; i < compat->variable_count; i++)
        if (strlen(compat->variables[i].name) == length &&
            memcmp(compat->variables[i].name, name, length) == 0)
            return (int)i;
    return -1;
}

static void remember_variable(nsbk_css_compat_t *compat, const char *name,
                              uint32_t name_length, const char *value,
                              uint32_t value_length) {
    int index;
    nsbk_css_variable_t *variable;
    if (!compat || !name || name_length < 3U ||
        name_length >= NSBK_CSS_VARIABLE_NAME_MAX || !value)
        return;
    index = variable_index(compat, name, name_length);
    if (index < 0) {
        if (compat->variable_count >= NSBK_CSS_VARIABLE_MAX) return;
        index = (int)compat->variable_count++;
    }
    variable = &compat->variables[index];
    memcpy(variable->name, name, name_length);
    variable->name[name_length] = '\0';
    if (value_length >= NSBK_CSS_VARIABLE_VALUE_MAX)
        value_length = NSBK_CSS_VARIABLE_VALUE_MAX - 1U;
    memcpy(variable->value, value, value_length);
    variable->value[value_length] = '\0';
}

static bool strip_comments(const uint8_t *input, uint32_t length,
                           compat_buffer_t *output) {
    uint32_t i = 0U;
    char quote = 0;
    while (i < length) {
        char character = (char)input[i];
        if (quote) {
            if (!buffer_character(output, character)) return false;
            if (character == '\\' && i + 1U < length) {
                if (!buffer_character(output, (char)input[++i])) return false;
            } else if (character == quote) quote = 0;
            i++;
            continue;
        }
        if (character == '\'' || character == '"') {
            quote = character;
            if (!buffer_character(output, character)) return false;
            i++;
            continue;
        }
        if (character == '/' && i + 1U < length && input[i + 1U] == '*') {
            i += 2U;
            while (i + 1U < length &&
                   !(input[i] == '*' && input[i + 1U] == '/')) i++;
            if (i + 1U < length) i += 2U;
            if (!buffer_character(output, ' ')) return false;
            continue;
        }
        if (!buffer_character(output, character)) return false;
        i++;
    }
    return true;
}

static void collect_variables(nsbk_css_compat_t *compat, const char *css,
                              uint32_t length) {
    uint32_t i = 0U;
    char quote = 0;
    while (i + 3U < length) {
        uint32_t name_start, name_end, value_start, value_end;
        int depth;
        char character = css[i];
        if (quote) {
            if (character == '\\' && i + 1U < length) i++;
            else if (character == quote) quote = 0;
            i++;
            continue;
        }
        if (character == '\'' || character == '"') { quote = character; i++; continue; }
        if (character != '-' || css[i + 1U] != '-' ||
            (i > 0U && name_character(css[i - 1U]))) { i++; continue; }
        name_start = i;
        i += 2U;
        while (i < length && name_character(css[i])) i++;
        name_end = i;
        while (i < length && isspace((unsigned char)css[i])) i++;
        if (i >= length || css[i] != ':') continue;
        value_start = trim_left(css, i + 1U, length);
        i = value_start;
        depth = 0;
        quote = 0;
        while (i < length) {
            character = css[i];
            if (quote) {
                if (character == '\\' && i + 1U < length) i++;
                else if (character == quote) quote = 0;
            } else if (character == '\'' || character == '"') quote = character;
            else if (character == '(') depth++;
            else if (character == ')' && depth > 0) depth--;
            else if ((character == ';' || character == '}') && depth == 0) break;
            i++;
        }
        value_end = trim_right(css, value_start, i);
        remember_variable(compat, css + name_start, name_end - name_start,
                          css + value_start, value_end - value_start);
    }
}

static uint32_t matching_parenthesis(const char *text, uint32_t open,
                                     uint32_t length) {
    uint32_t i;
    int depth = 1;
    char quote = 0;
    for (i = open + 1U; i < length; i++) {
        char character = text[i];
        if (quote) {
            if (character == '\\' && i + 1U < length) i++;
            else if (character == quote) quote = 0;
            continue;
        }
        if (character == '\'' || character == '"') quote = character;
        else if (character == '(') depth++;
        else if (character == ')' && --depth == 0) return i;
    }
    return length;
}

static bool expand_variables_once(const nsbk_css_compat_t *compat,
                                  const char *input, uint32_t length,
                                  compat_buffer_t *output) {
    uint32_t i = 0U;
    while (i < length) {
        if (i + 4U <= length && memcmp(input + i, "var(", 4U) == 0) {
            uint32_t close = matching_parenthesis(input, i + 3U, length);
            uint32_t name_start, name_end, fallback;
            int index;
            if (close >= length) return buffer_append(output, input + i, length - i);
            name_start = trim_left(input, i + 4U, close);
            name_end = name_start;
            while (name_end < close && name_character(input[name_end])) name_end++;
            fallback = name_end;
            while (fallback < close && input[fallback] != ',') fallback++;
            index = variable_index(compat, input + name_start, name_end - name_start);
            if (index >= 0) {
                const char *value = compat->variables[index].value;
                if (!buffer_append(output, value, (uint32_t)strlen(value))) return false;
            } else if (fallback < close) {
                uint32_t start = trim_left(input, fallback + 1U, close);
                uint32_t end = trim_right(input, start, close);
                if (!buffer_append(output, input + start, end - start)) return false;
            }
            i = close + 1U;
            continue;
        }
        if (!buffer_character(output, input[i++])) return false;
    }
    return true;
}

static int32_t parse_integer(const char *text, uint32_t *position,
                             uint32_t end, bool *valid) {
    int32_t value = 0;
    bool negative = false;
    bool found = false;
    uint32_t i = *position;
    while (i < end && isspace((unsigned char)text[i])) i++;
    if (i < end && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    while (i < end && isdigit((unsigned char)text[i])) {
        found = true;
        if (value < 1000000) value = value * 10 + (text[i] - '0');
        i++;
    }
    if (i < end && text[i] == '.') {
        i++;
        while (i < end && isdigit((unsigned char)text[i])) i++;
    }
    *position = i;
    *valid = found;
    return negative ? -value : value;
}

static int32_t unit_to_px(const nsbk_css_compat_t *compat, int32_t value,
                          const char *unit, uint32_t unit_length) {
    if (unit_length == 1U && unit[0] == '%')
        return value * compat->viewport_width / 100;
    if (unit_length == 2U && unit[0] == 'v' && unit[1] == 'w')
        return value * compat->viewport_width / 100;
    if (unit_length == 2U && unit[0] == 'v' && unit[1] == 'h')
        return value * compat->viewport_height / 100;
    if (unit_length == 2U && unit[0] == 'e' && unit[1] == 'm') return value * 12;
    if (unit_length == 3U && unit[0] == 'r' && unit[1] == 'e' && unit[2] == 'm')
        return value * 12;
    return value;
}

static bool simplify_calc(const nsbk_css_compat_t *compat, const char *text,
                          uint32_t start, uint32_t end, char result[40]) {
    uint32_t position = start;
    int32_t total = 0;
    char common_unit[8];
    uint32_t common_length = 0U;
    bool mixed = false;
    bool first = true;
    char operation = '+';
    while (position < end) {
        int32_t number;
        uint32_t unit_start, unit_end;
        bool valid;
        while (position < end && isspace((unsigned char)text[position])) position++;
        if (!first && position < end && (text[position] == '+' || text[position] == '-'))
            operation = text[position++];
        number = parse_integer(text, &position, end, &valid);
        if (!valid) return false;
        unit_start = position;
        while (position < end && (isalpha((unsigned char)text[position]) ||
                                  text[position] == '%')) position++;
        unit_end = position;
        if (first) {
            common_length = unit_end - unit_start;
            if (common_length >= sizeof(common_unit)) return false;
            memcpy(common_unit, text + unit_start, common_length);
            common_unit[common_length] = '\0';
            total = number;
            first = false;
        } else {
            if (common_length != unit_end - unit_start ||
                memcmp(common_unit, text + unit_start, common_length) != 0)
                mixed = true;
            if (mixed) {
                if (common_length) total = unit_to_px(compat, total, common_unit, common_length);
                common_length = 2U; common_unit[0] = 'p'; common_unit[1] = 'x'; common_unit[2] = '\0';
                number = unit_to_px(compat, number, text + unit_start, unit_end - unit_start);
            }
            total = operation == '-' ? total - number : total + number;
        }
        while (position < end && isspace((unsigned char)text[position])) position++;
        if (position < end && text[position] != '+' && text[position] != '-') return false;
    }
    if (first) return false;
    snprintf(result, 40U, "%d%s", total, common_unit);
    return true;
}

static bool expand_calculations(const nsbk_css_compat_t *compat,
                                const char *input, uint32_t length,
                                compat_buffer_t *output) {
    uint32_t i = 0U;
    while (i < length) {
        if (i + 5U <= length && memcmp(input + i, "calc(", 5U) == 0) {
            uint32_t close = matching_parenthesis(input, i + 4U, length);
            char simplified[40];
            if (close < length && simplify_calc(compat, input, i + 5U, close,
                                                simplified)) {
                if (!buffer_append(output, simplified,
                                   (uint32_t)strlen(simplified))) return false;
                i = close + 1U;
                continue;
            }
        }
        if (!buffer_character(output, input[i++])) return false;
    }
    return true;
}

static bool contains_token(const char *text, uint32_t length,
                           const char *token) {
    uint32_t token_length = (uint32_t)strlen(token);
    uint32_t i;
    if (token_length > length) return false;
    for (i = 0U; i + token_length <= length; i++)
        if (memcmp(text + i, token, token_length) == 0) return true;
    return false;
}

static bool declaration_value(const char *body, uint32_t length,
                              const char *property, uint32_t *value_start,
                              uint32_t *value_end) {
    uint32_t property_length = (uint32_t)strlen(property);
    uint32_t i;
    for (i = 0U; i + property_length < length; i++) {
        uint32_t position;
        if (i > 0U && name_character(body[i - 1U])) continue;
        if (memcmp(body + i, property, property_length) != 0) continue;
        position = i + property_length;
        while (position < length && isspace((unsigned char)body[position])) position++;
        if (position >= length || body[position] != ':') continue;
        *value_start = trim_left(body, position + 1U, length);
        *value_end = *value_start;
        while (*value_end < length && body[*value_end] != ';' &&
               body[*value_end] != '}') (*value_end)++;
        *value_end = trim_right(body, *value_start, *value_end);
        return true;
    }
    return false;
}

static bool value_is_grid(const char *value, uint32_t length) {
    uint32_t start = trim_left(value, 0U, length);
    uint32_t end = trim_right(value, start, length);
    return (end - start == 4U && memcmp(value + start, "grid", 4U) == 0) ||
           (end - start == 11U && memcmp(value + start, "inline-grid", 11U) == 0);
}

static uint32_t grid_track_count(const char *value, uint32_t length) {
    uint32_t i = trim_left(value, 0U, length);
    uint32_t count = 0U;
    int depth = 0;
    bool in_track = false;
    if (i + 7U < length && memcmp(value + i, "repeat(", 7U) == 0) {
        uint32_t position = i + 7U;
        bool valid;
        int32_t repeat = parse_integer(value, &position, length, &valid);
        if (valid && repeat > 0 && repeat <= 12) return (uint32_t)repeat;
    }
    for (; i < length; i++) {
        char c = value[i];
        if (c == '(') { depth++; in_track = true; }
        else if (c == ')' && depth > 0) depth--;
        else if (depth == 0 && isspace((unsigned char)c)) {
            if (in_track) { count++; in_track = false; }
        } else if (depth == 0 && c == '/') break;
        else in_track = true;
    }
    if (in_track) count++;
    return count > 12U ? 0U : count;
}

static bool append_grid_selector(compat_buffer_t *output, const char *selector,
                                 uint32_t length, uint32_t columns,
                                 const char *gap, uint32_t gap_length) {
    uint32_t start = 0U;
    char declaration[160];
    int declaration_length;
    uint32_t percentage = columns ? 100U / columns : 100U;
    if (!columns) return true;
    declaration_length = snprintf(declaration, sizeof(declaration),
        "{box-sizing:border-box;width:%u%%;max-width:%u%%;}",
        percentage, percentage);
    while (start < length) {
        uint32_t end = start;
        while (end < length && selector[end] != ',') end++;
        start = trim_left(selector, start, end);
        end = trim_right(selector, start, end);
        if (end > start && selector[start] != '@') {
            if (!buffer_append(output, selector + start, end - start) ||
                !buffer_append(output, ">*", 2U) ||
                !buffer_append(output, declaration, (uint32_t)declaration_length))
                return false;
        }
        start = end + 1U;
    }
    start = trim_left(selector, 0U, length);
    length = trim_right(selector, start, length);
    if (length > start && selector[start] != '@') {
        if (!buffer_append(output, selector + start, length - start) ||
            !buffer_append(output, "{flex-wrap:wrap", 15U)) return false;
        if (gap && gap_length) {
            if (!buffer_append(output, ";column-gap:", 12U) ||
                !buffer_append(output, gap, gap_length)) return false;
        }
        if (!buffer_append(output, ";}", 2U)) return false;
    }
    return true;
}

static bool append_grid_compatibility(const char *css, uint32_t length,
                                      compat_buffer_t *output) {
    uint32_t stack_header[32];
    uint32_t stack_body[32];
    uint32_t depth = 0U;
    uint32_t boundary = 0U;
    uint32_t i;
    char quote = 0;
    for (i = 0U; i < length; i++) {
        char c = css[i];
        if (quote) {
            if (c == '\\' && i + 1U < length) i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '{') {
            if (depth < 32U) {
                stack_header[depth] = boundary;
                stack_body[depth] = i + 1U;
                depth++;
            }
            boundary = i + 1U;
        } else if (c == '}' && depth) {
            uint32_t header_start, header_end, body_start, body_end;
            const char *body;
            uint32_t body_length;
            depth--;
            header_start = trim_left(css, stack_header[depth], i);
            header_end = trim_right(css, header_start, stack_body[depth] - 1U);
            body_start = stack_body[depth];
            body_end = i;
            body = css + body_start;
            body_length = body_end - body_start;
            {
                uint32_t display_start = 0U, display_end = 0U;
                if (header_end > header_start && css[header_start] != '@' &&
                    declaration_value(body, body_length, "display",
                                      &display_start, &display_end) &&
                    value_is_grid(body + display_start,
                                  display_end - display_start)) {
                    uint32_t value_start = 0U, value_end = 0U;
                    if (declaration_value(body, body_length,
                                          "grid-template-columns",
                                          &value_start, &value_end)) {
                        uint32_t gap_start = 0U, gap_end = 0U;
                        uint32_t columns;
                        columns = grid_track_count(body + value_start,
                                                   value_end - value_start);
                        (void)declaration_value(body, body_length, "gap",
                                                &gap_start, &gap_end);
                        if (!append_grid_selector(output, css + header_start,
                            header_end - header_start, columns,
                            gap_end > gap_start ? body + gap_start : NULL,
                            gap_end - gap_start)) return false;
                    }
                }
            }
            boundary = i + 1U;
        } else if (c == ';' && depth == 0U) boundary = i + 1U;
    }
    return true;
}

void nsbk_css_compat_init(nsbk_css_compat_t *compat, int32_t viewport_width,
                          int32_t viewport_height) {
    if (!compat) return;
    memset(compat, 0, sizeof(*compat));
    compat->viewport_width = viewport_width > 0 ? viewport_width : 640;
    compat->viewport_height = viewport_height > 0 ? viewport_height : 380;
}

bool nsbk_css_compat_process(nsbk_css_compat_t *compat, const uint8_t *input,
                             uint32_t input_length, uint8_t **output,
                             uint32_t *output_length) {
    compat_buffer_t stripped = {0}, expanded = {0}, next = {0};
    compat_buffer_t calculated = {0}, grid = {0};
    uint32_t pass;
    if (!compat || !output || !output_length || (!input && input_length)) return false;
    *output = NULL; *output_length = 0U;
    if (!strip_comments(input, input_length, &stripped)) goto fail;
    collect_variables(compat, stripped.data, stripped.length);
    expanded = stripped;
    memset(&stripped, 0, sizeof(stripped));
    for (pass = 0U; pass < 4U && contains_token(expanded.data, expanded.length, "var("); pass++) {
        memset(&next, 0, sizeof(next));
        if (!expand_variables_once(compat, expanded.data, expanded.length, &next)) goto fail;
        free(expanded.data);
        expanded = next;
        memset(&next, 0, sizeof(next));
    }
    if (!expand_calculations(compat, expanded.data, expanded.length, &calculated)) goto fail;
    if (!append_grid_compatibility(calculated.data, calculated.length, &grid)) goto fail;
    if (!buffer_append(&calculated, grid.data, grid.length)) goto fail;
    free(grid.data);
    free(expanded.data);
    *output = (uint8_t *)calculated.data;
    *output_length = calculated.length;
    return true;
fail:
    free(stripped.data);
    free(expanded.data);
    free(next.data);
    free(calculated.data);
    free(grid.data);
    return false;
}
