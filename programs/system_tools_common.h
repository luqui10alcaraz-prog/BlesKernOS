#ifndef BLESKERNOS_SYSTEM_TOOLS_COMMON_H
#define BLESKERNOS_SYSTEM_TOOLS_COMMON_H

#include <bleskernos_api.h>

#define ST_FACE       0x00D4D0C8U
#define ST_LIGHT      0x00FFFFFFU
#define ST_SHADOW     0x00808080U
#define ST_DARK       0x00404040U
#define ST_TEXT       0x00202020U
#define ST_MUTED      0x00606060U
#define ST_BLUE       0x00002070U
#define ST_GREEN      0x00007020U
#define ST_RED        0x00900000U
#define ST_PANEL      0x00FFFFFFU
#define ST_SELECT     0x000060A0U
#define ST_SELECT_TXT 0x00FFFFFFU
#define ST_LOCAL static __attribute__((unused))

ST_LOCAL void st_zero(void *pointer, uint32_t size) {
    uint8_t *bytes = (uint8_t *)pointer;
    while (bytes && size--) *bytes++ = 0;
}

ST_LOCAL uint32_t st_length(const char *text) {
    uint32_t length = 0;
    while (text && text[length]) length++;
    return length;
}

ST_LOCAL void st_copy(char *destination, uint32_t capacity, const char *source) {
    uint32_t i = 0;
    if (!destination || !capacity) return;
    while (source && source[i] && i + 1U < capacity) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

ST_LOCAL void st_append(char *destination, uint32_t capacity, const char *source) {
    uint32_t at = st_length(destination);
    uint32_t i = 0;
    if (!destination || !capacity || at >= capacity) return;
    while (source && source[i] && at + 1U < capacity) {
        destination[at++] = source[i++];
    }
    destination[at] = '\0';
}

ST_LOCAL char st_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - 'a' + 'A');
    return value;
}

ST_LOCAL bool st_equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right) {
        if (st_upper(*left++) != st_upper(*right++)) return false;
    }
    return !*left && !*right;
}

ST_LOCAL bool st_contains_ci(const char *text, const char *needle) {
    if (!text || !needle) return false;
    if (!*needle) return true;
    for (; *text; text++) {
        const char *a = text;
        const char *b = needle;
        while (*a && *b && st_upper(*a) == st_upper(*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

ST_LOCAL void st_u32(char *output, uint32_t capacity, uint32_t value) {
    char digits[11];
    uint32_t count = 0;
    uint32_t at = 0;
    if (!output || !capacity) return;
    if (!value) {
        st_copy(output, capacity, "0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (count && at + 1U < capacity) output[at++] = digits[--count];
    output[at] = '\0';
}

ST_LOCAL void st_u64_mb(char *output, uint32_t capacity, uint64_t bytes) {
    uint32_t mb = (uint32_t)(bytes / (1024ULL * 1024ULL));
    st_u32(output, capacity, mb);
    st_append(output, capacity, " MB");
}

ST_LOCAL void st_join(char *output, uint32_t capacity,
                    const char *directory, const char *name) {
    st_copy(output, capacity, directory && *directory ? directory : "/");
    if (st_length(output) > 1U && output[st_length(output) - 1U] == '/') {
        /* already separated */
    } else if (st_length(output) == 1U && output[0] == '/') {
        /* root already separated */
    } else {
        st_append(output, capacity, "/");
    }
    st_append(output, capacity, name ? name : "");
}

ST_LOCAL void st_parent(char *output, uint32_t capacity, const char *path) {
    uint32_t length;
    int32_t slash = -1;
    st_copy(output, capacity, path && *path ? path : "/");
    length = st_length(output);
    if (length <= 1U) {
        st_copy(output, capacity, "/");
        return;
    }
    while (length > 1U && output[length - 1U] == '/') output[--length] = '\0';
    for (uint32_t i = 0; i < length; i++) if (output[i] == '/') slash = (int32_t)i;
    if (slash <= 0) st_copy(output, capacity, "/");
    else output[slash] = '\0';
}

ST_LOCAL bool st_rect_contains(bk_gui_rect_t rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
           x < rect.x + rect.w && y < rect.y + rect.h;
}

ST_LOCAL void st_draw_panel(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                          uint32_t face) {
    bk_gui_surface_fill_rect(surface, rect, face);
    bk_gui_surface_draw_rect(surface, rect, ST_DARK);
    if (rect.w > 2 && rect.h > 2) {
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + 1, rect.y + 1, rect.w - 2, 1}, ST_LIGHT);
        bk_gui_surface_fill_rect(surface,
            (bk_gui_rect_t){rect.x + 1, rect.y + 1, 1, rect.h - 2}, ST_LIGHT);
    }
}

ST_LOCAL void st_draw_button(bk_gui_surface_t *surface, bk_gui_rect_t rect,
                           const char *label, bool enabled, bool pressed,
                           const bk_gui_image_t *icon) {
    uint32_t face = enabled ? ST_FACE : 0x00C4C4BCU;
    uint32_t text = enabled ? ST_TEXT : ST_SHADOW;
    int tx;
    if (pressed) face = 0x00B8B8B0U;
    st_draw_panel(surface, rect, face);
    tx = rect.x + (rect.w - (int)bk_gui_text_width_px(
        label ? label : "", st_length(label), 8, false, false) -
        (icon && icon->pixels ? 20 : 0)) / 2;
    if (tx < rect.x + 3) tx = rect.x + 3;
    if (icon && icon->pixels) {
        bk_gui_surface_draw_image(surface,
            (bk_gui_rect_t){tx + (pressed ? 1 : 0),
                            rect.y + (rect.h - 16) / 2 +
                            (pressed ? 1 : 0), 16, 16},
            rect, icon);
        tx += 20;
    }
    bk_gui_surface_draw_text(surface, tx + (pressed ? 1 : 0),
                             rect.y + 7 + (pressed ? 1 : 0),
                             label ? label : "", text, face, false);
}

ST_LOCAL int st_draw_wrapped(bk_gui_surface_t *surface, bk_gui_rect_t clip,
                           int x, int y, const char *text, uint32_t color,
                           int line_height) {
    char line[128];
    uint32_t line_len = 0;
    const char *cursor = text ? text : "";
    int max_chars = (clip.w - (x - clip.x) - 8) / 7;
    if (max_chars < 8) max_chars = 8;
    while (*cursor && y + line_height <= clip.y + clip.h) {
        uint32_t word_start;
        while (*cursor == ' ') cursor++;
        if (*cursor == '\n' || *cursor == '\r') {
            while (*cursor == '\n' || *cursor == '\r') cursor++;
            y += line_height;
            line_len = 0;
            continue;
        }
        line_len = 0;
        while (*cursor && *cursor != '\n' && *cursor != '\r' &&
               line_len < (uint32_t)max_chars) {
            word_start = line_len;
            while (*cursor && *cursor != ' ' && *cursor != '\n' &&
                   *cursor != '\r' && line_len + 1U < sizeof(line)) {
                line[line_len++] = *cursor++;
            }
            if (*cursor == ' ' && line_len + 1U < sizeof(line)) {
                line[line_len++] = *cursor++;
            }
            if (line_len >= (uint32_t)max_chars) {
                if (word_start > 0U && cursor[-1] != ' ') {
                    uint32_t rewind = line_len - word_start;
                    cursor -= rewind;
                    line_len = word_start;
                }
                break;
            }
        }
        while (line_len && line[line_len - 1U] == ' ') line_len--;
        line[line_len] = '\0';
        if (line_len)
            bk_gui_surface_draw_text(surface, x, y, line, color, 0, false);
        y += line_height;
        while (*cursor == '\n' || *cursor == '\r') cursor++;
    }
    return y;
}

ST_LOCAL void st_ipv4_text(char *output, uint32_t capacity,
                         const uint8_t address[4]) {
    char number[12];
    output[0] = '\0';
    for (uint32_t i = 0; i < 4U; i++) {
        st_u32(number, sizeof(number), address[i]);
        st_append(output, capacity, number);
        if (i != 3U) st_append(output, capacity, ".");
    }
}

ST_LOCAL bool st_parse_ipv4(const char *text, uint8_t address[4]) {
    uint32_t part = 0;
    uint32_t value = 0;
    bool have_digit = false;
    if (!text || !address) return false;
    while (*text) {
        if (*text >= '0' && *text <= '9') {
            value = value * 10U + (uint32_t)(*text - '0');
            if (value > 255U) return false;
            have_digit = true;
        } else if (*text == '.' && have_digit && part < 3U) {
            address[part++] = (uint8_t)value;
            value = 0;
            have_digit = false;
        } else return false;
        text++;
    }
    if (!have_digit || part != 3U) return false;
    address[3] = (uint8_t)value;
    return true;
}

ST_LOCAL void st_mac_text(char *output, uint32_t capacity,
                        const uint8_t mac[6]) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t at = 0;
    if (!output || capacity < 18U) return;
    for (uint32_t i = 0; i < 6U; i++) {
        output[at++] = hex[(mac[i] >> 4) & 15U];
        output[at++] = hex[mac[i] & 15U];
        if (i != 5U) output[at++] = ':';
    }
    output[at] = '\0';
}

#endif
