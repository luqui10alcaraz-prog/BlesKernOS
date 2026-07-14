#include "../kernel/include/api.h"

#define MIDAMP_MAX_PLAYLIST   40
#define MIDAMP_ACTION_QUEUE   32
#define MIDAMP_BUTTON_COUNT   8
#define MIDAMP_LIST_ROW_H     9
#define MIDAMP_DBLCLICK_TICKS 500
#define MIDAMP_SKIN_W         275
#define MIDAMP_SKIN_H         458
#define MIDAMP_HEADER_H       28
#define MIDAMP_PROGRESS_W     251
#define MIDAMP_PROGRESS_H     5
#define MIDAMP_BOTTOM_BTN_W   25
#define MIDAMP_BOTTOM_BTN_H   18
#define MIDAMP_MIN_W          MIDAMP_SKIN_W
#define MIDAMP_MIN_H          MIDAMP_SKIN_H

typedef enum {
    MIDAMP_ACT_NONE = 0,
    MIDAMP_ACT_PLAY_SELECTED,
    MIDAMP_ACT_TOGGLE_PAUSE,
    MIDAMP_ACT_STOP,
    MIDAMP_ACT_NEXT,
    MIDAMP_ACT_PREV,
    MIDAMP_ACT_BROWSE_WAV,
    MIDAMP_ACT_TOGGLE_SHUFFLE,
    MIDAMP_ACT_TOGGLE_REPEAT,
    MIDAMP_ACT_TOGGLE_MUTE,
    MIDAMP_ACT_TRACK_DELTA,
    MIDAMP_ACT_CHANNEL_DELTA,
    MIDAMP_ACT_TRANSPOSE_DELTA,
    MIDAMP_ACT_RESET_SELECTORS
} midamp_action_code_t;

typedef struct {
    bool rest;
    uint8_t note;
    uint32_t duration_ms;
} midamp_note_t;

typedef struct {
    uint8_t code;
    int16_t value;
} midamp_action_t;

typedef struct {
    midamp_action_t items[MIDAMP_ACTION_QUEUE];
    uint8_t head;
    uint8_t tail;
} midamp_action_queue_t;

typedef struct {
    midamp_note_t *notes;
    uint32_t count;
    uint32_t capacity;
    uint32_t total_ms;
    uint16_t track_count;
    bool valid_file;
    bool found_track;
} midamp_parse_result_t;

typedef struct {
    gui_rect_t bounds;
    uint8_t action;
    int16_t value;
} midamp_skin_button_t;

typedef struct {
    gui_desktop_t *desktop;
    gui_window_t *window;
    gui_event_queue_t events;
    midamp_action_queue_t actions;
    char playlist[MIDAMP_MAX_PLAYLIST][VFS_MAX_PATH];
    int playlist_count;
    int selected_index;
    int current_index;
    int scroll;
    int last_click_index;
    uint32_t last_click_tick;
    uint8_t selected_track;
    uint8_t selected_channel;
    int8_t transpose;
    bool shuffle;
    bool repeat;
    bool mute;
    bool playing;
    bool paused;
    bool tone_running;
    bool current_note_active;
    bool current_note_rest;
    uint8_t current_note_value;
    uint32_t current_note_duration_ms;
    uint32_t current_note_started_tick;
    uint32_t current_note_end_tick;
    uint32_t played_ms;
    uint32_t next_note_index;
    uint16_t available_tracks;
    midamp_note_t *sequence;
    uint32_t sequence_count;
    uint32_t sequence_capacity;
    uint32_t sequence_total_ms;
    char loaded_path[VFS_MAX_PATH];
    char title_name[48];
    char status[80];
    gui_image_t skin_header;
    gui_image_t skin_buttons;
    gui_image_t skin_bottom;
    int pressed_button;
    bool skin_loaded;
} midamp_state_t;

static midamp_state_t *g_midamp;

static const midamp_skin_button_t g_midamp_buttons[MIDAMP_BUTTON_COUNT] = {
    {{  0, 42, 21, 16}, MIDAMP_ACT_PREV,           0},
    {{ 23, 42, 21, 16}, MIDAMP_ACT_PLAY_SELECTED,  0},
    {{ 46, 42, 21, 16}, MIDAMP_ACT_TOGGLE_PAUSE,   0},
    {{ 69, 42, 21, 16}, MIDAMP_ACT_STOP,           0},
    {{ 92, 42, 21, 16}, MIDAMP_ACT_NEXT,           0},
    {{120, 43, 20, 15}, MIDAMP_ACT_BROWSE_WAV,     0},
    {{149, 44, 44, 12}, MIDAMP_ACT_TOGGLE_SHUFFLE, 0},
    {{195, 44, 26, 12}, MIDAMP_ACT_TOGGLE_REPEAT,  0},
};

static const int g_midamp_bottom_x[5] = {10, 40, 70, 100, 240};
static const int g_midamp_bottom_frames[5] = {0, 4, 9, 13, 17};

static void midamp_main(void *argument);
static void midamp_set_status(midamp_state_t *st, const char *text);
static void midamp_extract_name(const char *path, char *out, size_t out_len);
static void midamp_stop_playback(midamp_state_t *st, bool stop_sound);

static void midamp_wav_selected(const char *path, void *context) {
    midamp_state_t *st = (midamp_state_t *)context;
    if (!st || st != g_midamp || !path || !path[0]) return;
    midamp_stop_playback(st, true);
    midamp_extract_name(path, st->title_name, sizeof(st->title_name));
    if (bk_sound_play_file(path))
        midamp_set_status(st, "Reproduciendo WAV");
    else
        midamp_set_status(st, "No se pudo reproducir el WAV");
}

static void midamp_browse_wav(midamp_state_t *st) {
    if (!st || !st->desktop) return;
    if (!bk_file_dialog_open(st->desktop, "Abrir audio WAV", "/", ".WAV",
                             BK_FILE_DIALOG_PREVIEW_AUDIO,
                             midamp_wav_selected, st))
        midamp_set_status(st, "No se pudo abrir el explorador");
}

static void midamp_copy_text(char *dst, size_t dst_len, const char *src) {
    if (!dst || !dst_len) return;
    if (!src) src = "";
    bk_runtime_strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static void midamp_append_text(char *dst, size_t dst_len, const char *src) {
    size_t len;

    if (!dst || !dst_len || !src) return;
    len = bk_runtime_strlen(dst);
    if (len + 1 >= dst_len) return;
    bk_runtime_strncpy(dst + len, src, dst_len - len - 1);
    dst[dst_len - 1] = '\0';
}

static void midamp_u32_to_text(char *out, uint32_t value) {
    char tmp[12];
    int pos = 11;

    tmp[pos] = '\0';
    if (!value) tmp[--pos] = '0';
    while (value) {
        tmp[--pos] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    bk_runtime_strcpy(out, &tmp[pos]);
}

static void midamp_s32_to_text(char *out, int32_t value) {
    uint32_t number;
    char tmp[12];
    int pos = 11;

    tmp[pos] = '\0';
    number = value < 0 ? (uint32_t)(-(value + 1)) + 1U : (uint32_t)value;
    if (!number) tmp[--pos] = '0';
    while (number) {
        tmp[--pos] = (char)('0' + (number % 10U));
        number /= 10U;
    }
    if (value > 0) tmp[--pos] = '+';
    else if (value < 0) tmp[--pos] = '-';
    bk_runtime_strcpy(out, &tmp[pos]);
}

static void midamp_format_time(char *out, uint32_t milliseconds) {
    char number[12];
    uint32_t seconds = milliseconds / 1000U;
    uint32_t minutes = seconds / 60U;
    uint32_t remain = seconds % 60U;

    midamp_u32_to_text(number, minutes);
    bk_runtime_strcpy(out, number);
    bk_runtime_strcat(out, ":");
    if (remain < 10U) bk_runtime_strcat(out, "0");
    midamp_u32_to_text(number, remain);
    bk_runtime_strcat(out, number);
}

static void midamp_set_status(midamp_state_t *st, const char *text) {
    if (!st) return;
    midamp_copy_text(st->status, sizeof(st->status), text);
    if (st->window) st->window->dirty = true;
}

static void midamp_extract_name(const char *path, char *out, size_t out_len) {
    const char *name = path;

    if (!path) {
        midamp_copy_text(out, out_len, "");
        return;
    }
    for (const char *p = path; *p; p++)
        if (*p == '/') name = p + 1;
    midamp_copy_text(out, out_len, name);
}

static bool midamp_is_midi_name(const char *name) {
    const char *dot = name;

    if (!name) return false;
    while (*dot && *dot != '.') dot++;
    if (!*dot) return false;
    return bk_runtime_strcmp(dot, ".MID") == 0 || bk_runtime_strcmp(dot, ".mid") == 0 ||
           bk_runtime_strcmp(dot, ".KAR") == 0 || bk_runtime_strcmp(dot, ".kar") == 0;
}

static void midamp_normalize_path(char *path) {
    size_t len;

    if (!path || !path[0]) return;
    for (char *p = path; *p; p++)
        if (*p == '\\') *p = '/';
    if (path[0] == '/') return;
    len = bk_runtime_strlen(path);
    if (len + 1 >= VFS_MAX_PATH) return;
    for (int i = (int)len; i >= 0; i--) path[i + 1] = path[i];
    path[0] = '/';
}

static bool midamp_paths_equal(const char *a, const char *b) {
    return a && b && bk_runtime_strcmp(a, b) == 0;
}

static void midamp_join_path(char *dst, size_t dst_len,
                             const char *base, const char *name) {
    size_t len;

    if (!dst || !dst_len) return;
    dst[0] = '\0';
    if (!base || !name) return;

    midamp_copy_text(dst, dst_len, base);
    len = bk_runtime_strlen(dst);
    if (len == 0) return;
    if (len > 1 && dst[len - 1] != '/' && len + 1 < dst_len) {
        dst[len++] = '/';
        dst[len] = '\0';
    }
    if (len == 1 && dst[0] == '/') {
        midamp_copy_text(dst + 1, dst_len - 1, name);
    } else {
        bk_runtime_strncpy(dst + len, name, dst_len - len - 1);
        dst[dst_len - 1] = '\0';
    }
}

/* GIF decoding lives in gui/image.c so every program can use it. */
#if 0
static bool midamp_decode_gif(gui_image_t *image,
                              const uint8_t *data, uint32_t length) {
    uint32_t pos = 13;
    uint32_t palette[256];
    uint32_t transparent_index = 0xFFFFFFFFU;
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint8_t header_packed;
    uint32_t global_colors = 0;

    if (!image || !data || length < 13U) return false;
    if (bk_runtime_memcmp(data, "GIF87a", 6) != 0 && bk_runtime_memcmp(data, "GIF89a", 6) != 0)
        return false;

    canvas_w = midamp_gif_le16(data + 6);
    canvas_h = midamp_gif_le16(data + 8);
    header_packed = data[10];
    bk_runtime_memset(palette, 0, sizeof(palette));

    if (header_packed & 0x80U) {
        global_colors = 1U << ((header_packed & 0x07U) + 1U);
        if (pos + global_colors * 3U > length) return false;
        for (uint32_t i = 0; i < global_colors; i++) {
            uint8_t r = data[pos++];
            uint8_t g = data[pos++];
            uint8_t b = data[pos++];
            palette[i] = ((uint32_t)0xFF << 24) |
                         ((uint32_t)r << 16) |
                         ((uint32_t)g << 8) | b;
        }
    }

    while (pos < length) {
        uint8_t block_id = data[pos++];

        if (block_id == 0x3BU) return false;

        if (block_id == 0x21U) {
            uint8_t label;

            if (pos >= length) return false;
            label = data[pos++];

            if (label == 0xF9U) {
                uint8_t block_size;
                uint8_t packed;

                if (pos >= length) return false;
                block_size = data[pos++];
                if (block_size != 4U || pos + 4U > length) return false;
                packed = data[pos];
                if (packed & 0x01U) transparent_index = data[pos + 3];
                pos += 4U;
                if (pos >= length || data[pos++] != 0U) return false;
                continue;
            }

            if (!midamp_skip_subblocks(data, length, &pos)) return false;
            continue;
        }

        if (block_id != 0x2CU) return false;

        if (pos + 9U > length) return false;
        {
            uint16_t img_left = midamp_gif_le16(data + pos);
            uint16_t img_top = midamp_gif_le16(data + pos + 2);
            uint16_t img_w = midamp_gif_le16(data + pos + 4);
            uint16_t img_h = midamp_gif_le16(data + pos + 6);
            uint8_t img_packed = data[pos + 8];
            bool interlaced = (img_packed & 0x40U) != 0;
            uint32_t local_palette[256];
            const uint32_t *active_palette = palette;
            uint32_t active_color_count = global_colors;
            uint8_t min_code_size;
            uint8_t *compressed = NULL;
            uint32_t compressed_len = 0;
            uint16_t *prefix = NULL;
            uint8_t *suffix = NULL;
            uint8_t *stack = NULL;
            uint32_t pixel_count;
            uint32_t datum = 0;
            uint32_t bits = 0;
            uint32_t byte_pos = 0;
            uint32_t clear_code;
            uint32_t end_code;
            uint32_t available;
            uint32_t code_size;
            int32_t old_code = -1;
            uint32_t code_mask;
            uint32_t first = 0;
            uint32_t stack_top = 0;
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t pass = 0;
            uint32_t row_start[4] = {0, 4, 2, 1};
            uint32_t row_step[4] = {8, 8, 4, 2};

            pos += 9U;
            if (!canvas_w || !canvas_h || !img_w || !img_h) return false;

            if (img_packed & 0x80U) {
                active_color_count = 1U << ((img_packed & 0x07U) + 1U);
                if (pos + active_color_count * 3U > length) return false;
                for (uint32_t i = 0; i < active_color_count; i++) {
                    uint8_t r = data[pos++];
                    uint8_t g = data[pos++];
                    uint8_t b = data[pos++];
                    local_palette[i] = ((uint32_t)0xFF << 24) |
                                       ((uint32_t)r << 16) |
                                       ((uint32_t)g << 8) | b;
                }
                active_palette = local_palette;
            }

            if (pos >= length) return false;
            min_code_size = data[pos++];
            if (min_code_size > 8U) return false;
            if (!midamp_read_subblocks(data, length, &pos,
                                       &compressed, &compressed_len))
                return false;

            prefix = (uint16_t *)bk_sys_alloc(4096U * sizeof(uint16_t));
            suffix = (uint8_t *)bk_sys_alloc(4096U);
            stack = (uint8_t *)bk_sys_alloc(4097U);
            image->pixels = (uint32_t *)bk_sys_alloc_zero((uint32_t)canvas_w * canvas_h *
                                                sizeof(uint32_t));
            if (!prefix || !suffix || !stack || !image->pixels) {
                if (compressed) bk_sys_free(compressed);
                if (prefix) bk_sys_free(prefix);
                if (suffix) bk_sys_free(suffix);
                if (stack) bk_sys_free(stack);
                midamp_image_free(image);
                return false;
            }

            image->width = canvas_w;
            image->height = canvas_h;
            pixel_count = (uint32_t)img_w * img_h;

            clear_code = 1U << min_code_size;
            end_code = clear_code + 1U;
            available = clear_code + 2U;
            code_size = (uint32_t)min_code_size + 1U;
            code_mask = (1U << code_size) - 1U;

            for (uint32_t i = 0; i < clear_code; i++) {
                prefix[i] = 0;
                suffix[i] = (uint8_t)i;
            }

            for (uint32_t written = 0; written < pixel_count;) {
                if (stack_top == 0U) {
                    uint32_t code;
                    uint32_t in_code;

                    while (bits < code_size) {
                        if (byte_pos >= compressed_len) break;
                        datum |= (uint32_t)compressed[byte_pos++] << bits;
                        bits += 8U;
                    }
                    if (bits < code_size) break;

                    code = datum & code_mask;
                    datum >>= code_size;
                    bits -= code_size;

                    if (code == clear_code) {
                        code_size = (uint32_t)min_code_size + 1U;
                        code_mask = (1U << code_size) - 1U;
                        available = clear_code + 2U;
                        old_code = -1;
                        continue;
                    }
                    if (code == end_code) break;

                    if (old_code < 0) {
                        stack[stack_top++] = suffix[code];
                        old_code = (int32_t)code;
                        first = code;
                    } else {
                        in_code = code;
                        if (code >= available) {
                            stack[stack_top++] = (uint8_t)first;
                            code = (uint32_t)old_code;
                        }

                        while (code >= clear_code) {
                            stack[stack_top++] = suffix[code];
                            code = prefix[code];
                        }
                        first = suffix[code];
                        stack[stack_top++] = (uint8_t)first;

                        if (available < 4096U) {
                            prefix[available] = (uint16_t)old_code;
                            suffix[available] = (uint8_t)first;
                            available++;
                            if (available == (1U << code_size) &&
                                code_size < 12U) {
                                code_size++;
                                code_mask = (1U << code_size) - 1U;
                            }
                        }
                        old_code = (int32_t)in_code;
                    }
                }

                if (stack_top == 0U) continue;

                stack_top--;
                {
                    uint8_t palette_index = stack[stack_top];
                    uint32_t dst_x = x + img_left;
                    uint32_t dst_y = y + img_top;

                    if (palette_index < active_color_count &&
                        x < img_w && y < img_h &&
                        dst_x < canvas_w && dst_y < canvas_h) {
                        uint32_t pixel = active_palette[palette_index];
                        if (transparent_index != 0xFFFFFFFFU &&
                            palette_index == transparent_index) {
                            pixel = 0;
                        }
                        image->pixels[dst_y * canvas_w + dst_x] = pixel;
                    }
                }

                written++;
                x++;
                if (x >= img_w) {
                    x = 0;
                    if (!interlaced) {
                        y++;
                    } else {
                        y += row_step[pass];
                        while (y >= img_h) {
                            pass++;
                            if (pass >= 4U) break;
                            y = row_start[pass];
                        }
                    }
                }
            }

            bk_sys_free(compressed);
            bk_sys_free(prefix);
            bk_sys_free(suffix);
            bk_sys_free(stack);
            return true;
        }
    }

    return false;
}
#endif

static bool midamp_load_gif_candidates(gui_image_t *image,
                                       const char *primary,
                                       const char *secondary) {
    if (bk_gui_gif_load(image, primary)) return true;
    if (secondary && secondary[0] &&
        bk_gui_gif_load(image, secondary))
        return true;
    return false;
}

static void midamp_skin_free(midamp_state_t *st) {
    if (!st) return;
    bk_gui_image_free(&st->skin_header);
    bk_gui_image_free(&st->skin_buttons);
    bk_gui_image_free(&st->skin_bottom);
    st->skin_loaded = false;
}

static bool midamp_skin_load(midamp_state_t *st) {
    bool ok;

    if (!st) return false;
    midamp_skin_free(st);
    ok = midamp_load_gif_candidates(&st->skin_header,
                                    "/MIDHDR.GIF",
                                    "/SYSTEM/PROGRAMS/WINMAP/HDR.GIF") &&
         midamp_load_gif_candidates(&st->skin_buttons,
                                    "/MIDBTN.GIF",
                                    "/SYSTEM/PROGRAMS/WINMAP/BUTTONS.GIF") &&
         midamp_load_gif_candidates(&st->skin_bottom,
                                    "/MIDBOT.GIF",
                                    "/SYSTEM/PROGRAMS/WINMAP/BOTTOM.GIF");
    st->skin_loaded = ok;
    if (!ok) midamp_skin_free(st);
    return ok;
}

static void midamp_draw_image_slice(gui_surface_t *surface, int dst_x, int dst_y,
                                    const gui_image_t *image,
                                    int src_x, int src_y, int width, int height) {
    if (!surface || !image || !image->pixels || width <= 0 || height <= 0) return;

    for (int py = 0; py < height; py++) {
        int sy = src_y + py;
        if (sy < 0 || sy >= image->height) continue;
        for (int px = 0; px < width; px++) {
            int sx = src_x + px;
            int dx = dst_x + px;
            int dy = dst_y + py;
            uint32_t pixel;
            uint8_t alpha;
            uint32_t rgb;
            uint32_t *dst;

            if (sx < 0 || sx >= image->width) continue;
            if (dx < 0 || dy < 0 || dx >= surface->width || dy >= surface->height)
                continue;
            if (!bk_gui_gfx_point_visible(surface, dx, dy)) continue;
            pixel = image->pixels[(uint32_t)sy * image->width + (uint32_t)sx];
            alpha = (uint8_t)(pixel >> 24);
            if (alpha == 0) continue;

            rgb = pixel & 0x00FFFFFF;
            if (alpha == 0xFF) {
                bk_gui_gfx_putpixel(surface, dx, dy, rgb);
                continue;
            }

            dst = &surface->pixels[(uint32_t)dy * surface->pitch + (uint32_t)dx];
            *dst = bk_gui_color_blend(*dst, rgb, alpha);
        }
    }
}

static bool midamp_playlist_contains(const midamp_state_t *st, const char *path) {
    if (!st || !path) return false;
    for (int i = 0; i < st->playlist_count; i++)
        if (midamp_paths_equal(st->playlist[i], path)) return true;
    return false;
}

static bool midamp_playlist_add(midamp_state_t *st, const char *path) {
    if (!st || !path || !path[0]) return false;
    if (st->playlist_count >= MIDAMP_MAX_PLAYLIST) return false;
    if (midamp_playlist_contains(st, path)) return false;
    midamp_copy_text(st->playlist[st->playlist_count],
                     sizeof(st->playlist[st->playlist_count]), path);
    st->playlist_count++;
    return true;
}

static void midamp_clear_song(midamp_state_t *st) {
    if (!st) return;
    if (st->sequence) bk_sys_free(st->sequence);
    st->sequence = NULL;
    st->sequence_count = 0;
    st->sequence_capacity = 0;
    st->sequence_total_ms = 0;
    st->available_tracks = 0;
    st->loaded_path[0] = '\0';
    st->title_name[0] = '\0';
}

static void midamp_stop_playback(midamp_state_t *st, bool stop_sound) {
    if (!st) return;
    if (stop_sound) bk_sound_stop();
    st->playing = false;
    st->paused = false;
    st->tone_running = false;
    st->current_note_active = false;
    st->current_note_rest = true;
    st->current_note_value = 0;
    st->current_note_duration_ms = 0;
    st->current_note_started_tick = 0;
    st->current_note_end_tick = 0;
    st->played_ms = 0;
    st->next_note_index = 0;
}

static void midamp_reset_playlist_state(midamp_state_t *st) {
    if (!st) return;
    st->playlist_count = 0;
    st->selected_index = -1;
    st->current_index = -1;
    st->scroll = 0;
    st->last_click_index = -1;
    st->last_click_tick = 0;
}

static bool midamp_scan_dir(midamp_state_t *st, const char *path) {
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint32_t count = 0;
    char full_path[VFS_MAX_PATH];
    bool any = false;

    if (!st || !path) return false;
    if (!bk_file_list_dir(path, entries, VFS_MAX_DIR_ENTRIES, &count)) return false;

    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type != VFS_NODE_FILE) continue;
        if (!midamp_is_midi_name(entries[i].name)) continue;
        midamp_join_path(full_path, sizeof(full_path), path, entries[i].name);
        any = midamp_playlist_add(st, full_path) || any;
    }
    return any;
}

static void midamp_load_playlist_from_text(midamp_state_t *st,
                                           const char *text, uint32_t size) {
    uint32_t start = 0;

    if (!st || !text) return;
    while (start < size) {
        uint32_t end = start;
        char line[VFS_MAX_PATH];
        uint32_t copy_len;
        uint32_t left;
        uint32_t right;

        while (end < size && text[end] != '\n' && text[end] != '\r') end++;
        copy_len = end - start;
        if (copy_len >= sizeof(line)) copy_len = sizeof(line) - 1;
        bk_runtime_memcpy(line, text + start, copy_len);
        line[copy_len] = '\0';

        left = 0;
        while (line[left] == ' ' || line[left] == '\t') left++;
        right = bk_runtime_strlen(line);
        while (right > left &&
               (line[right - 1] == ' ' || line[right - 1] == '\t'))
            line[--right] = '\0';

        if (line[left] && line[left] != '#' && line[left] != ';') {
            char path[VFS_MAX_PATH];
            midamp_copy_text(path, sizeof(path), line + left);
            midamp_normalize_path(path);
            if (midamp_is_midi_name(path)) midamp_playlist_add(st, path);
        }

        start = end;
        while (start < size && (text[start] == '\n' || text[start] == '\r')) start++;
    }
}

static void midamp_reload_playlist(midamp_state_t *st) {
    void *playlist_data = NULL;
    uint32_t playlist_size = 0;

    if (!st) return;

    midamp_stop_playback(st, true);
    midamp_clear_song(st);
    midamp_reset_playlist_state(st);

    if (bk_file_read_all("/PLAYLIST.TXT", &playlist_data, &playlist_size) &&
        playlist_data && playlist_size) {
        midamp_load_playlist_from_text(st, (const char *)playlist_data, playlist_size);
    }
    if (playlist_data) bk_sys_free(playlist_data);

    if (st->playlist_count == 0) {
        (void)midamp_scan_dir(st, "/");
        (void)midamp_scan_dir(st, "/DOCS");
        (void)midamp_scan_dir(st, "/MISC");
        (void)midamp_scan_dir(st, "/SYSTEM/PROGRAMS");
    }

    if (st->playlist_count > 0) {
        st->selected_index = 0;
        midamp_set_status(st, "Playlist cargada");
    } else {
        midamp_set_status(st, "Pon .MID/.KAR en / o usa /PLAYLIST.TXT");
    }
}

static uint16_t midamp_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t midamp_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool midamp_read_varlen(const uint8_t *data, uint32_t length,
                               uint32_t *pos, uint32_t *value) {
    uint32_t result = 0;
    uint8_t byte;
    int count = 0;

    if (!data || !pos || !value) return false;
    do {
        if (*pos >= length || count >= 4) return false;
        byte = data[(*pos)++];
        result = (result << 7) | (uint32_t)(byte & 0x7FU);
        count++;
    } while (byte & 0x80U);

    *value = result;
    return true;
}

static bool midamp_reserve_notes(midamp_parse_result_t *out, uint32_t needed) {
    uint32_t new_capacity;
    midamp_note_t *new_notes;

    if (!out) return false;
    if (needed <= out->capacity) return true;

    new_capacity = out->capacity ? out->capacity : 64U;
    while (new_capacity < needed) {
        if (new_capacity > 4096U) {
            new_capacity += 1024U;
        } else {
            new_capacity *= 2U;
        }
    }

    new_notes = (midamp_note_t *)bk_sys_realloc(out->notes,
                                          new_capacity * sizeof(midamp_note_t));
    if (!new_notes) return false;

    out->notes = new_notes;
    out->capacity = new_capacity;
    return true;
}

static bool midamp_append_event(midamp_parse_result_t *out, bool rest,
                                uint8_t note, uint32_t duration_ms) {
    midamp_note_t *last;

    if (!out || duration_ms == 0) return true;

    if (out->count) {
        last = &out->notes[out->count - 1];
        if (last->rest == rest && (rest || last->note == note)) {
            if (last->duration_ms > 0xFFFFFFFFU - duration_ms)
                last->duration_ms = 0xFFFFFFFFU;
            else
                last->duration_ms += duration_ms;
            if (out->total_ms > 0xFFFFFFFFU - duration_ms)
                out->total_ms = 0xFFFFFFFFU;
            else
                out->total_ms += duration_ms;
            return true;
        }
    }

    if (!midamp_reserve_notes(out, out->count + 1U)) return false;
    out->notes[out->count].rest = rest;
    out->notes[out->count].note = note;
    out->notes[out->count].duration_ms = duration_ms;
    out->count++;
    if (out->total_ms > 0xFFFFFFFFU - duration_ms)
        out->total_ms = 0xFFFFFFFFU;
    else
        out->total_ms += duration_ms;
    return true;
}

static uint32_t midamp_delta_to_ms(uint32_t delta_ticks,
                                   uint32_t tempo_us_per_quarter,
                                   uint16_t division) {
    uint32_t tempo_ms;
    uint32_t product;

    if (!delta_ticks || !division) return 0;
    tempo_ms = tempo_us_per_quarter / 1000U;
    if (!tempo_ms) tempo_ms = 1U;
    if (delta_ticks > 0xFFFFFFFFU / tempo_ms) return 0xFFFFFFFFU;
    product = delta_ticks * tempo_ms;
    return (product + division / 2U) / division;
}

static bool midamp_parse_track(const uint8_t *data, uint32_t length,
                               uint16_t division, uint8_t selected_channel,
                               midamp_parse_result_t *out) {
    uint32_t pos = 0;
    uint32_t absolute_ms = 0;
    uint32_t last_output_ms = 0;
    uint32_t note_start_ms = 0;
    uint32_t tempo_us_per_quarter = 500000U;
    uint8_t running_status = 0;
    int current_note = -1;

    if (!data || !out) return false;

    while (pos < length) {
        uint32_t delta_ticks = 0;
        uint8_t status;
        uint8_t kind;
        uint8_t channel;
        uint8_t a = 0;
        uint8_t b = 0;

        if (!midamp_read_varlen(data, length, &pos, &delta_ticks)) return false;
        absolute_ms += midamp_delta_to_ms(delta_ticks, tempo_us_per_quarter, division);
        if (pos >= length) break;

        status = data[pos++];
        if (status < 0x80U) {
            if (!running_status) return false;
            pos--;
            status = running_status;
        } else if (status < 0xF0U) {
            running_status = status;
        }

        if (status == 0xFFU) {
            uint8_t meta;
            uint32_t meta_len = 0;

            running_status = 0;
            if (pos >= length) return false;
            meta = data[pos++];
            if (!midamp_read_varlen(data, length, &pos, &meta_len)) return false;
            if (pos + meta_len > length) return false;

            if (meta == 0x51U && meta_len >= 3U) {
                tempo_us_per_quarter = ((uint32_t)data[pos] << 16) |
                                       ((uint32_t)data[pos + 1] << 8) |
                                       data[pos + 2];
                if (!tempo_us_per_quarter) tempo_us_per_quarter = 500000U;
            } else if (meta == 0x2FU) {
                break;
            }

            pos += meta_len;
            continue;
        }

        if (status == 0xF0U || status == 0xF7U) {
            uint32_t sysex_len = 0;

            running_status = 0;
            if (!midamp_read_varlen(data, length, &pos, &sysex_len)) return false;
            if (pos + sysex_len > length) return false;
            pos += sysex_len;
            continue;
        }

        kind = (uint8_t)(status & 0xF0U);
        channel = (uint8_t)(status & 0x0FU);

        if (kind == 0xC0U || kind == 0xD0U) {
            if (pos >= length) return false;
            a = data[pos++];
        } else {
            if (pos + 1U >= length) return false;
            a = data[pos++];
            b = data[pos++];
        }

        if (channel != selected_channel) continue;

        if (kind == 0x90U && b != 0U) {
            if (current_note >= 0) {
                if (absolute_ms > note_start_ms &&
                    !midamp_append_event(out, false, (uint8_t)current_note,
                                         absolute_ms - note_start_ms))
                    return false;
                last_output_ms = absolute_ms;
            } else if (absolute_ms > last_output_ms) {
                if (!midamp_append_event(out, true, 0,
                                         absolute_ms - last_output_ms))
                    return false;
                last_output_ms = absolute_ms;
            }
            current_note = a;
            note_start_ms = absolute_ms;
        } else if (kind == 0x80U || (kind == 0x90U && b == 0U)) {
            if (current_note >= 0) {
                if (absolute_ms > note_start_ms &&
                    !midamp_append_event(out, false, (uint8_t)current_note,
                                         absolute_ms - note_start_ms))
                    return false;
                current_note = -1;
                last_output_ms = absolute_ms;
            }
        }
    }

    if (current_note >= 0 && absolute_ms > note_start_ms) {
        if (!midamp_append_event(out, false, (uint8_t)current_note,
                                 absolute_ms - note_start_ms))
            return false;
    }

    return true;
}

static void midamp_free_parse_result(midamp_parse_result_t *result) {
    if (!result) return;
    if (result->notes) bk_sys_free(result->notes);
    bk_runtime_memset(result, 0, sizeof(*result));
}

static bool midamp_parse_file(const uint8_t *data, uint32_t size,
                              uint8_t selected_track, uint8_t selected_channel,
                              midamp_parse_result_t *out) {
    uint32_t header_size;
    uint32_t pos;
    uint16_t division;
    uint16_t track_count;
    uint8_t track_index = 0;

    if (!data || !out || size < 14U) return false;
    bk_runtime_memset(out, 0, sizeof(*out));

    if (bk_runtime_memcmp(data, "MThd", 4) != 0) return false;
    header_size = midamp_be32(data + 4);
    if (header_size < 6U || 8U + header_size > size) return false;

    division = midamp_be16(data + 12);
    if ((division & 0x8000U) != 0) return false;

    track_count = midamp_be16(data + 10);
    out->track_count = track_count;
    out->valid_file = true;

    pos = 8U + header_size;
    while (pos + 8U <= size) {
        uint32_t chunk_size = midamp_be32(data + pos + 4U);
        bool is_track = bk_runtime_memcmp(data + pos, "MTrk", 4) == 0;
        pos += 8U;
        if (pos + chunk_size > size) {
            midamp_free_parse_result(out);
            return false;
        }
        if (is_track) {
            if (track_index == selected_track) {
                out->found_track = true;
                if (!midamp_parse_track(data + pos, chunk_size,
                                        division, selected_channel, out)) {
                    midamp_free_parse_result(out);
                    return false;
                }
                return true;
            }
            track_index++;
        }
        pos += chunk_size;
    }

    return true;
}

static uint32_t midamp_ms_to_ticks(uint32_t milliseconds) {
    uint32_t pit_hz = bk_sys_tick_frequency();
    uint32_t whole_seconds = milliseconds / 1000U;
    uint32_t remaining_ms = milliseconds % 1000U;
    uint32_t ticks = 0;
    uint32_t partial;

    if (pit_hz == 0) pit_hz = 100U;
    if (whole_seconds && pit_hz <= 0xFFFFFFFFU / whole_seconds)
        ticks = whole_seconds * pit_hz;
    partial = (remaining_ms * pit_hz + 999U) / 1000U;
    if (ticks > 0xFFFFFFFFU - partial) return 0xFFFFFFFFU;
    ticks += partial;
    return ticks ? ticks : 1U;
}

static uint32_t midamp_ticks_to_ms(uint32_t ticks) {
    uint32_t pit_hz = bk_sys_tick_frequency();

    if (!ticks) return 0;
    if (pit_hz == 0) pit_hz = 100U;
    if (ticks > 0xFFFFFFFFU / 1000U) return 0xFFFFFFFFU;
    return (ticks * 1000U + pit_hz - 1U) / pit_hz;
}

static uint32_t midamp_scale_progress_width(uint32_t progress,
                                            uint32_t total,
                                            uint32_t width) {
    uint32_t result = 0;
    uint32_t accum = 0;

    if (!total || !width) return 0;
    if (progress >= total) return width;

    for (uint32_t i = 0; i < width; i++) {
        if (accum >= total - progress) {
            accum -= total - progress;
            result++;
        } else {
            accum += progress;
        }
    }

    return result;
}

static uint32_t midamp_scale_seek_target(uint32_t total,
                                         uint32_t position,
                                         uint32_t width) {
    uint32_t q;
    uint32_t r;

    if (!width || !position || !total) return 0;
    if (position >= width) return total;

    q = total / width;
    r = total % width;
    return q * position + (r * position) / width;
}

static uint32_t midamp_note_to_hz(int note) {
    static const uint16_t base_octave[12] = {
        262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
    };
    int octave_shift = 0;
    uint32_t frequency;

    while (note < 60) {
        note += 12;
        octave_shift--;
    }
    while (note >= 72) {
        note -= 12;
        octave_shift++;
    }

    if (note < 60 || note > 71) return 0;
    frequency = base_octave[note - 60];

    if (octave_shift > 0) {
        if (octave_shift > 5) octave_shift = 5;
        frequency <<= octave_shift;
    } else if (octave_shift < 0) {
        octave_shift = -octave_shift;
        if (octave_shift > 5) octave_shift = 5;
        frequency >>= octave_shift;
    }

    if (frequency < 20U) frequency = 20U;
    return frequency;
}

static void midamp_skin_origin(const midamp_state_t *st,
                               int *out_x, int *out_y) {
    int x;
    int y;
    int inner_w;
    int pad_x;

    if (!st || !st->window) return;

    x = st->window->bounds.x + (st->window->borderless ? 0 : GUI_BORDER_SIZE);
    y = st->window->bounds.y + bk_gui_window_content_top(st->window);
    inner_w = st->window->bounds.w -
              (st->window->borderless ? 0 : GUI_BORDER_SIZE * 2);
    pad_x = (inner_w - MIDAMP_SKIN_W) / 2;
    if (pad_x < 0) pad_x = 0;

    if (out_x) *out_x = x + pad_x;
    if (out_y) *out_y = y;
}

static gui_rect_t midamp_skin_area_rect(const midamp_state_t *st) {
    int x = 0;
    int y = 0;
    midamp_skin_origin(st, &x, &y);
    return (gui_rect_t){x, y, MIDAMP_SKIN_W, MIDAMP_SKIN_H};
}

static gui_rect_t midamp_progress_rect(const midamp_state_t *st) {
    gui_rect_t skin = midamp_skin_area_rect(st);
    return (gui_rect_t){skin.x + 10, skin.y + 29,
                        MIDAMP_PROGRESS_W, MIDAMP_PROGRESS_H};
}

static gui_rect_t midamp_skin_minimize_rect(const midamp_state_t *st) {
    gui_rect_t skin = midamp_skin_area_rect(st);
    return (gui_rect_t){skin.x + 246, skin.y + 3, 9, 9};
}

static gui_rect_t midamp_skin_close_rect(const midamp_state_t *st) {
    gui_rect_t skin = midamp_skin_area_rect(st);
    return (gui_rect_t){skin.x + 264, skin.y + 3, 9, 9};
}

static gui_rect_t midamp_playlist_rect(const midamp_state_t *st) {
    gui_rect_t skin = midamp_skin_area_rect(st);
    return (gui_rect_t){skin.x + 10, skin.y + 63, 255, 360};
}

static gui_rect_t midamp_button_rect(const midamp_state_t *st, int index) {
    gui_rect_t skin = midamp_skin_area_rect(st);
    gui_rect_t rect = g_midamp_buttons[index].bounds;
    rect.x += skin.x;
    rect.y += skin.y;
    return rect;
}

static int midamp_visible_rows(const midamp_state_t *st UNUSED) {
    return MIDAMP_MAX_PLAYLIST;
}

static void midamp_ensure_scroll(midamp_state_t *st) {
    int visible;

    if (!st || st->selected_index < 0) return;
    visible = midamp_visible_rows(st);
    if (visible < 1) visible = 1;
    if (st->selected_index < st->scroll) st->scroll = st->selected_index;
    if (st->selected_index >= st->scroll + visible)
        st->scroll = st->selected_index - visible + 1;
    if (st->scroll < 0) st->scroll = 0;
    if (st->playlist_count > visible &&
        st->scroll > st->playlist_count - visible)
        st->scroll = st->playlist_count - visible;
}

static void midamp_select_index(midamp_state_t *st, int index) {
    if (!st || st->playlist_count <= 0) return;
    if (index < 0) index = 0;
    if (index >= st->playlist_count) index = st->playlist_count - 1;
    st->selected_index = index;
    midamp_ensure_scroll(st);
    if (st->window) st->window->dirty = true;
}

static int midamp_random_index(const midamp_state_t *st) {
    int index;

    if (!st || st->playlist_count <= 0) return -1;
    index = (int)(bk_sys_ticks() % (uint32_t)st->playlist_count);
    if (st->playlist_count > 1 && index == st->current_index)
        index = (index + 1) % st->playlist_count;
    return index;
}

static int midamp_next_index(const midamp_state_t *st) {
    int base;

    if (!st || st->playlist_count <= 0) return -1;
    if (st->shuffle) return midamp_random_index(st);

    base = st->current_index >= 0 ? st->current_index : st->selected_index;
    if (base < 0) base = 0;
    if (base + 1 < st->playlist_count) return base + 1;
    return -1;
}

static int midamp_prev_index(const midamp_state_t *st) {
    int base;

    if (!st || st->playlist_count <= 0) return -1;
    if (st->shuffle) return midamp_random_index(st);

    base = st->current_index >= 0 ? st->current_index : st->selected_index;
    if (base > 0) return base - 1;
    return -1;
}

static bool midamp_load_song(midamp_state_t *st, const char *path,
                             int playlist_index, bool autoplay) {
    void *data = NULL;
    uint32_t size = 0;
    midamp_parse_result_t parsed;

    if (!st || !path) return false;
    bk_runtime_memset(&parsed, 0, sizeof(parsed));

    if (!bk_file_read_all(path, &data, &size) || !data || size < 14U) {
        midamp_set_status(st, "No se pudo leer el MIDI");
        if (data) bk_sys_free(data);
        return false;
    }

    if (!midamp_parse_file((const uint8_t *)data, size,
                           st->selected_track, st->selected_channel, &parsed)) {
        midamp_set_status(st, "MIDI invalido o no soportado");
        bk_sys_free(data);
        return false;
    }
    bk_sys_free(data);

    midamp_stop_playback(st, true);
    midamp_clear_song(st);

    st->sequence = parsed.notes;
    st->sequence_count = parsed.count;
    st->sequence_capacity = parsed.capacity;
    st->sequence_total_ms = parsed.total_ms;
    st->available_tracks = parsed.track_count;
    parsed.notes = NULL;

    midamp_copy_text(st->loaded_path, sizeof(st->loaded_path), path);
    midamp_extract_name(path, st->title_name, sizeof(st->title_name));

    if (playlist_index >= 0) {
        st->current_index = playlist_index;
        midamp_select_index(st, playlist_index);
    }

    if (!parsed.found_track) {
        midamp_set_status(st, "Track fuera de rango");
        midamp_free_parse_result(&parsed);
        return false;
    }
    if (st->sequence_count == 0) {
        midamp_set_status(st, "Track/canal sin notas");
        midamp_free_parse_result(&parsed);
        return false;
    }

    if (autoplay) {
        st->playing = true;
        st->paused = false;
        midamp_set_status(st, "Reproduciendo");
    } else {
        midamp_set_status(st, "Listo para reproducir");
    }

    midamp_free_parse_result(&parsed);
    return true;
}

static bool midamp_open_index(midamp_state_t *st, int index, bool autoplay) {
    if (!st || index < 0 || index >= st->playlist_count) return false;
    return midamp_load_song(st, st->playlist[index], index, autoplay);
}

static bool midamp_reload_current_song(midamp_state_t *st, bool autoplay) {
    if (!st) return false;
    if (st->loaded_path[0]) {
        return midamp_load_song(st, st->loaded_path, st->current_index, autoplay);
    }
    if (st->selected_index >= 0 && st->selected_index < st->playlist_count) {
        return midamp_load_song(st, st->playlist[st->selected_index],
                                st->selected_index, autoplay);
    }
    return false;
}

static void midamp_toggle_pause(midamp_state_t *st) {
    if (!st) return;
    if (!st->playing && !st->paused) return;

    if (!st->paused) {
        if (st->current_note_active && st->next_note_index > 0U)
            st->next_note_index--;
        st->current_note_active = false;
        st->tone_running = false;
        bk_sound_stop();
        st->paused = true;
        midamp_set_status(st, "Pausa");
    } else {
        st->paused = false;
        st->playing = true;
        midamp_set_status(st, "Reanudando");
    }
}

static void midamp_apply_track_delta(midamp_state_t *st, int delta) {
    uint16_t tracks;

    if (!st || delta == 0) return;
    tracks = st->available_tracks ? st->available_tracks : 1U;

    if (tracks > 1U) {
        int next = (int)st->selected_track + delta;
        while (next < 0) next += tracks;
        while (next >= tracks) next -= tracks;
        st->selected_track = (uint8_t)next;
    }

    if (st->loaded_path[0] || st->selected_index >= 0)
        (void)midamp_reload_current_song(st, st->playing || st->paused);
}

static void midamp_apply_channel_delta(midamp_state_t *st, int delta) {
    int next;

    if (!st || delta == 0) return;
    next = (int)st->selected_channel + delta;
    while (next < 0) next += 16;
    while (next >= 16) next -= 16;
    st->selected_channel = (uint8_t)next;

    if (st->loaded_path[0] || st->selected_index >= 0)
        (void)midamp_reload_current_song(st, st->playing || st->paused);
}

static void midamp_apply_transpose_delta(midamp_state_t *st, int delta) {
    int next;

    if (!st || delta == 0) return;
    next = (int)st->transpose + delta;
    if (next < -24) next = -24;
    if (next > 24) next = 24;
    st->transpose = (int8_t)next;

    if (st->loaded_path[0] || st->selected_index >= 0)
        (void)midamp_reload_current_song(st, st->playing || st->paused);
}

static void midamp_reset_selectors(midamp_state_t *st) {
    if (!st) return;
    st->selected_track = 0;
    st->selected_channel = 0;
    st->transpose = 0;
    if (st->loaded_path[0] || st->selected_index >= 0)
        (void)midamp_reload_current_song(st, st->playing || st->paused);
}

static void midamp_seek_to_ms(midamp_state_t *st, uint32_t target_ms) {
    uint32_t elapsed = 0;

    if (!st || st->sequence_count == 0U) return;
    if (target_ms >= st->sequence_total_ms) target_ms = st->sequence_total_ms;

    bk_sound_stop();
    st->current_note_active = false;
    st->tone_running = false;
    st->played_ms = 0;
    st->next_note_index = 0;

    for (uint32_t i = 0; i < st->sequence_count; i++) {
        uint32_t duration = st->sequence[i].duration_ms;
        if (target_ms < elapsed + duration) {
            st->next_note_index = i;
            st->played_ms = elapsed;
            if (st->playing && !st->paused)
                midamp_set_status(st, "Seek");
            if (st->window) st->window->dirty = true;
            return;
        }
        elapsed += duration;
    }

    st->next_note_index = st->sequence_count;
    st->played_ms = st->sequence_total_ms;
    if (st->window) st->window->dirty = true;
}

static int midamp_hit_skin_button(const midamp_state_t *st, int x, int y) {
    for (int i = 0; i < MIDAMP_BUTTON_COUNT; i++) {
        if (bk_gui_rect_contains(midamp_button_rect(st, i), x, y)) return i;
    }
    return -1;
}

static bool midamp_action_push(midamp_state_t *st, uint8_t code, int16_t value) {
    midamp_action_queue_t *queue;
    uint8_t next;
    bool pushed = false;

    if (!st) return false;
    queue = &st->actions;
    bk_proc_critical_enter();
    next = (uint8_t)((queue->head + 1U) % MIDAMP_ACTION_QUEUE);
    if (next != queue->tail) {
        queue->items[queue->head].code = code;
        queue->items[queue->head].value = value;
        queue->head = next;
        pushed = true;
    }
    bk_proc_critical_leave();
    return pushed;
}

static bool midamp_action_pop(midamp_state_t *st, midamp_action_t *action) {
    midamp_action_queue_t *queue;
    bool ok = false;

    if (!st || !action) return false;
    queue = &st->actions;
    bk_proc_critical_enter();
    if (queue->head != queue->tail) {
        *action = queue->items[queue->tail];
        queue->tail = (uint8_t)((queue->tail + 1U) % MIDAMP_ACTION_QUEUE);
        ok = true;
    }
    bk_proc_critical_leave();
    return ok;
}

static int midamp_hit_row(const midamp_state_t *st, int x, int y) {
    gui_rect_t list;
    int row;
    int visible;
    int index;

    if (!st || !st->window) return -1;
    list = midamp_playlist_rect(st);
    if (!bk_gui_rect_contains(list, x, y)) return -1;

    row = (y - list.y) / MIDAMP_LIST_ROW_H;
    visible = midamp_visible_rows(st);
    if (row < 0 || row >= visible) return -1;

    index = st->scroll + row;
    return index < st->playlist_count ? index : -1;
}

static void midamp_process_action(midamp_state_t *st, const midamp_action_t *action) {
    int index;

    if (!st || !action) return;

    switch (action->code) {
        case MIDAMP_ACT_PLAY_SELECTED:
            (void)midamp_open_index(st, st->selected_index, true);
            break;
        case MIDAMP_ACT_TOGGLE_PAUSE:
            midamp_toggle_pause(st);
            break;
        case MIDAMP_ACT_STOP:
            midamp_stop_playback(st, true);
            midamp_set_status(st, "Detenido");
            break;
        case MIDAMP_ACT_NEXT:
            index = midamp_next_index(st);
            if (index >= 0) (void)midamp_open_index(st, index, true);
            break;
        case MIDAMP_ACT_PREV:
            index = midamp_prev_index(st);
            if (index >= 0) (void)midamp_open_index(st, index, true);
            break;
        case MIDAMP_ACT_BROWSE_WAV:
            midamp_browse_wav(st);
            break;
        case MIDAMP_ACT_TOGGLE_SHUFFLE:
            st->shuffle = !st->shuffle;
            midamp_set_status(st, st->shuffle ? "Shuffle ON" : "Shuffle OFF");
            break;
        case MIDAMP_ACT_TOGGLE_REPEAT:
            st->repeat = !st->repeat;
            midamp_set_status(st, st->repeat ? "Repeat ON" : "Repeat OFF");
            break;
        case MIDAMP_ACT_TOGGLE_MUTE:
            st->mute = !st->mute;
            if (st->mute && st->tone_running) {
                bk_sound_stop();
                st->tone_running = false;
            }
            midamp_set_status(st, st->mute ? "Mute ON" : "Mute OFF");
            break;
        case MIDAMP_ACT_TRACK_DELTA:
            midamp_apply_track_delta(st, action->value);
            break;
        case MIDAMP_ACT_CHANNEL_DELTA:
            midamp_apply_channel_delta(st, action->value);
            break;
        case MIDAMP_ACT_TRANSPOSE_DELTA:
            midamp_apply_transpose_delta(st, action->value);
            break;
        case MIDAMP_ACT_RESET_SELECTORS:
            midamp_reset_selectors(st);
            break;
        default:
            break;
    }
}

static void midamp_process_key(midamp_state_t *st, uint8_t key) {
    int hit;

    if (!st) return;

    if (key == KEY_UP) {
        if (st->selected_index > 0) midamp_select_index(st, st->selected_index - 1);
        return;
    }
    if (key == KEY_DOWN) {
        if (st->selected_index + 1 < st->playlist_count)
            midamp_select_index(st, st->selected_index + 1);
        return;
    }

    switch (key) {
        case '\n':
        case 'x':
            (void)midamp_open_index(st, st->selected_index, true);
            break;
        case 'z':
            hit = midamp_prev_index(st);
            if (hit >= 0) (void)midamp_open_index(st, hit, true);
            break;
        case 'c':
        case ' ':
            midamp_toggle_pause(st);
            break;
        case 'v':
            midamp_stop_playback(st, true);
            midamp_set_status(st, "Detenido");
            break;
        case 'b':
            hit = midamp_next_index(st);
            if (hit >= 0) (void)midamp_open_index(st, hit, true);
            break;
        case 'm':
            st->mute = !st->mute;
            if (st->mute && st->tone_running) {
                bk_sound_stop();
                st->tone_running = false;
            }
            midamp_set_status(st, st->mute ? "Mute ON" : "Mute OFF");
            break;
        case '[':
            midamp_apply_track_delta(st, -1);
            break;
        case ']':
            midamp_apply_track_delta(st, 1);
            break;
        case ',':
            midamp_apply_channel_delta(st, -1);
            break;
        case '.':
            midamp_apply_channel_delta(st, 1);
            break;
        case '-':
            midamp_apply_transpose_delta(st, -1);
            break;
        case '=':
        case '+':
            midamp_apply_transpose_delta(st, 1);
            break;
        case '\\':
            midamp_reset_selectors(st);
            break;
        case '\b':
            (void)midamp_reload_current_song(st, true);
            break;
        case 'r':
            midamp_reload_playlist(st);
            break;
        case 27:
            if (st->window) bk_gui_close_window(st->window);
            break;
        default:
            break;
    }
}

static void midamp_process_event(midamp_state_t *st, const gui_event_t *event) {
    int hit;
    uint32_t now;

    if (!st || !event) return;

    if (event->type == GUI_EVENT_KEY) {
        midamp_process_key(st, (uint8_t)event->key);
        return;
    }

    if (event->type == GUI_EVENT_MOUSE_DOWN) {
        if (event->button != MOUSE_LEFT_BUTTON) return;
        st->pressed_button = midamp_hit_skin_button(st, event->x, event->y);
        if (st->pressed_button >= 0) {
            if (st->window) st->window->dirty = true;
            return;
        }
        if (bk_gui_rect_contains(midamp_progress_rect(st), event->x, event->y)) {
            st->pressed_button = MIDAMP_BUTTON_COUNT;
            if (st->window) st->window->dirty = true;
            return;
        }
        hit = midamp_hit_row(st, event->x, event->y);
        if (hit >= 0) midamp_select_index(st, hit);
        return;
    }

    if (event->type != GUI_EVENT_MOUSE_UP) return;

    if (event->button == MOUSE_RIGHT_BUTTON &&
        bk_gui_rect_contains(midamp_skin_area_rect(st), event->x, event->y)) {
        bk_about_show(st->desktop, &(bk_about_info_t){
            "MIDAMP", "Version 1.1", "Reproductor MIDI y WAV de BlesKernOS.",
            "Bles.INC (C) 2026", "/ICONS/MIDAMP.BMP"});
        return;
    }
    if (event->button != MOUSE_LEFT_BUTTON) return;

    if (bk_gui_rect_contains(midamp_skin_close_rect(st), event->x, event->y)) {
        if (st->window) bk_gui_close_window(st->window);
        return;
    }
    if (bk_gui_rect_contains(midamp_skin_minimize_rect(st), event->x, event->y)) {
        if (st->window) bk_gui_window_minimize(st->window);
        return;
    }

    if (st->pressed_button >= 0) {
        int pressed = st->pressed_button;
        st->pressed_button = -1;
        if (st->window) st->window->dirty = true;

        if (pressed < MIDAMP_BUTTON_COUNT) {
            if (midamp_hit_skin_button(st, event->x, event->y) == pressed) {
                (void)midamp_action_push(st, g_midamp_buttons[pressed].action,
                                         g_midamp_buttons[pressed].value);
            }
            return;
        }

        if (pressed == MIDAMP_BUTTON_COUNT) {
            gui_rect_t bar = midamp_progress_rect(st);
            if (bk_gui_rect_contains(bar, event->x, event->y) &&
                st->sequence_total_ms > 0U && bar.w > 1) {
                int rel_x = event->x - bar.x;
                if (rel_x < 0) rel_x = 0;
                if (rel_x >= bar.w) rel_x = bar.w - 1;
                midamp_seek_to_ms(st,
                                  midamp_scale_seek_target(
                                      st->sequence_total_ms,
                                      (uint32_t)rel_x,
                                      (uint32_t)(bar.w - 1)));
            }
            return;
        }
    }

    hit = midamp_hit_row(st, event->x, event->y);
    if (hit < 0 || hit != st->selected_index) return;

    now = bk_sys_ticks();
    if (st->last_click_index == hit &&
        st->last_click_tick != 0 &&
        now - st->last_click_tick < MIDAMP_DBLCLICK_TICKS) {
        st->last_click_tick = 0;
        (void)midamp_open_index(st, hit, true);
        return;
    }

    st->last_click_index = hit;
    st->last_click_tick = now;
}

static void midamp_finish_song(midamp_state_t *st) {
    int next;

    if (!st) return;

    if (st->repeat && st->current_index >= 0) {
        (void)midamp_open_index(st, st->current_index, true);
        return;
    }

    next = midamp_next_index(st);
    if (next >= 0) {
        (void)midamp_open_index(st, next, true);
        return;
    }

    midamp_stop_playback(st, false);
    midamp_set_status(st, "Fin de playlist");
}

static void midamp_start_note(midamp_state_t *st, const midamp_note_t *note,
                              uint32_t now) {
    uint32_t hz = 0;

    if (!st || !note) return;

    st->current_note_active = true;
    st->current_note_rest = note->rest;
    st->current_note_value = note->note;
    st->current_note_duration_ms = note->duration_ms ? note->duration_ms : 1U;
    st->current_note_started_tick = now;
    st->current_note_end_tick = now + midamp_ms_to_ticks(st->current_note_duration_ms);
    st->tone_running = false;

    if (!note->rest && !st->mute) {
        hz = midamp_note_to_hz((int)note->note + (int)st->transpose);
        if (hz && bk_sound_tone(hz, st->current_note_duration_ms))
            st->tone_running = true;
    }
}

static void midamp_playback_tick(midamp_state_t *st) {
    uint32_t now;

    if (!st || !st->window || !st->window->listed) return;
    if (!st->playing || st->paused || st->sequence_count == 0U) {
        bk_sys_sleep_ticks(1);
        return;
    }

    now = bk_sys_ticks();
    if (st->current_note_active) {
        if ((int32_t)(now - st->current_note_end_tick) < 0) {
            bk_sys_sleep_ticks(1);
            return;
        }
        st->played_ms += st->current_note_duration_ms;
        st->current_note_active = false;
        st->tone_running = false;
    }

    if (st->next_note_index >= st->sequence_count) {
        midamp_finish_song(st);
        return;
    }

    midamp_start_note(st, &st->sequence[st->next_note_index++], now);
}

static uint32_t midamp_current_progress_ms(const midamp_state_t *st) {
    uint32_t progress;
    uint32_t elapsed_ticks;
    uint32_t elapsed_ms;

    if (!st) return 0;
    progress = st->played_ms;
    if (!st->current_note_active) return progress;

    elapsed_ticks = bk_sys_ticks() - st->current_note_started_tick;
    elapsed_ms = midamp_ticks_to_ms(elapsed_ticks);
    if (elapsed_ms > st->current_note_duration_ms)
        elapsed_ms = st->current_note_duration_ms;

    if (progress > 0xFFFFFFFFU - elapsed_ms) return 0xFFFFFFFFU;
    progress += elapsed_ms;
    if (progress > st->sequence_total_ms) progress = st->sequence_total_ms;
    return progress;
}

static void midamp_content(gui_window_t *window,
                           gui_surface_t *surface, void *context) {
    midamp_state_t *st = (midamp_state_t *)context;
    gui_rect_t content;
    gui_rect_t skin;
    gui_rect_t list;
    gui_rect_t bar;
    gui_rect_t clip;
    char line[96];
    char current_time[16];
    char total_time[16];
    char number[16];
    char tone_text[16];
    uint32_t progress;
    int visible;
    int fill_w;

    if (!st || !window || !st->window || !st->window->visible) return;

    content = bk_gui_window_content_rect_raw(window);
    skin = midamp_skin_area_rect(st);
    list = midamp_playlist_rect(st);
    bar = midamp_progress_rect(st);

    if (content.w > 0 && content.h > 0)
        bk_gui_gfx_fill_rect(surface, content, 0x0024263C);

    if (st->skin_loaded && st->skin_header.pixels) {
        midamp_draw_image_slice(surface, skin.x, skin.y,
                                &st->skin_header, 0, 0,
                                st->skin_header.width, st->skin_header.height);
    } else {
        bk_gui_gfx_fill_rect(surface,
                          (gui_rect_t){skin.x, skin.y, MIDAMP_SKIN_W, MIDAMP_HEADER_H},
                          0x004C4645);
    }

    if (st->skin_loaded && st->skin_buttons.pixels) {
        midamp_draw_image_slice(surface, skin.x, skin.y + 42,
                                &st->skin_buttons, 0, 0,
                                st->skin_buttons.width, st->skin_buttons.height);
    }

    if (st->skin_loaded && st->skin_bottom.pixels) {
        for (int i = 0; i < 5; i++) {
            midamp_draw_image_slice(surface,
                                    skin.x + g_midamp_bottom_x[i],
                                    skin.y + 431,
                                    &st->skin_bottom,
                                    0,
                                    g_midamp_bottom_frames[i] *
                                        MIDAMP_BOTTOM_BTN_H,
                                    MIDAMP_BOTTOM_BTN_W,
                                    MIDAMP_BOTTOM_BTN_H);
        }
    }

    bk_gui_gfx_fill_rect(surface, list, 0x00000000);
    bk_gui_gfx_fill_rect(surface, bar, 0x004C4645);
    bk_gui_gfx_draw_rect(surface,
                      (gui_rect_t){bar.x - 1, bar.y - 1,
                                   bar.w + 2, bar.h + 2},
                      0x00605C58);

    progress = midamp_current_progress_ms(st);
    midamp_format_time(current_time, progress);
    midamp_format_time(total_time, st->sequence_total_ms);
    if (st->sequence_total_ms > 0U) {
        fill_w = (int)midamp_scale_progress_width(progress,
                                                  st->sequence_total_ms,
                                                  MIDAMP_PROGRESS_W);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > MIDAMP_PROGRESS_W) fill_w = MIDAMP_PROGRESS_W;
        if (fill_w > 0) {
            bk_gui_gfx_fill_rect(surface,
                              (gui_rect_t){bar.x, bar.y, fill_w, bar.h},
                              0x0084706A);
        }
    }

    clip = (gui_rect_t){skin.x + 12, skin.y + 12, 108, 12};
    midamp_copy_text(line, sizeof(line),
                     st->title_name[0] ? st->title_name : "sin archivo");
    bk_gui_font_draw_string_clipped(surface, skin.x + 12, skin.y + 15, line,
                                 0x00ECCE7C, clip);

    line[0] = '\0';
    midamp_append_text(line, sizeof(line), "tone>");
    midamp_s32_to_text(tone_text, (int32_t)st->transpose);
    midamp_append_text(line, sizeof(line), tone_text);
    midamp_append_text(line, sizeof(line), " chnl>");
    midamp_u32_to_text(number, (uint32_t)st->selected_channel + 1U);
    midamp_append_text(line, sizeof(line), number);
    midamp_append_text(line, sizeof(line), " <trk");
    midamp_u32_to_text(number, (uint32_t)st->selected_track + 1U);
    midamp_append_text(line, sizeof(line), number);
    clip = (gui_rect_t){skin.x + 132, skin.y + 12, 132, 12};
    bk_gui_font_draw_string_clipped(surface, skin.x + 132, skin.y + 15, line,
                                 0x00F0F000, clip);

    clip = (gui_rect_t){skin.x + 124, skin.y + 24, 48, 12};
    bk_gui_font_draw_string_clipped(surface, skin.x + 124, skin.y + 28,
                                 total_time, 0x00F0F000, clip);
    clip = (gui_rect_t){skin.x + 226, skin.y + 12, 40, 12};
    bk_gui_font_draw_string_clipped(surface, skin.x + 226, skin.y + 15,
                                 current_time, 0x00F0F000, clip);

    if (st->shuffle) {
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){skin.x + 154, skin.y + 48, 3, 2},
                          0x0000D600);
    }
    if (st->repeat) {
        bk_gui_gfx_fill_rect(surface, (gui_rect_t){skin.x + 200, skin.y + 48, 3, 2},
                          0x0000D600);
    }

    visible = midamp_visible_rows(st);
    for (int row = 0; row < visible; row++) {
        int index = st->scroll + row;
        gui_rect_t row_rect = {list.x, list.y + row * MIDAMP_LIST_ROW_H,
                               list.w, MIDAMP_LIST_ROW_H};
        char label[64];
        uint32_t fg = 0x0000FF00;

        if (index >= st->playlist_count) break;

        if (index == st->selected_index) {
            bk_gui_gfx_fill_rect(surface, row_rect, 0x000000C6);
            fg = 0x00FFFFFF;
        } else if (index == st->current_index) {
            fg = 0x00FFFFFF;
        }

        midamp_extract_name(st->playlist[index], label, sizeof(label));
        bk_gui_font_draw_string_clipped(surface, row_rect.x + 3, row_rect.y + 1,
                                     label, fg, row_rect);
    }

    clip = (gui_rect_t){skin.x + 132, skin.y + 434, 102, 10};
    bk_gui_font_draw_string_clipped(surface, skin.x + 132, skin.y + 436,
                                 st->status, 0x00ECFEFC, clip);
    bk_gui_font_draw_string_clipped(surface, skin.x + 132, skin.y + 446,
                                 "[] trk  ,. ch  -+ ton  m mute",
                                 0x00A3AEBD,
                                 (gui_rect_t){skin.x + 132, skin.y + 444,
                                              124, 10});

    if (st->pressed_button >= 0 && st->pressed_button < MIDAMP_BUTTON_COUNT) {
        bk_gui_gfx_draw_rect(surface, midamp_button_rect(st, st->pressed_button),
                          0x00ECBE64);
    } else if (st->pressed_button == MIDAMP_BUTTON_COUNT) {
        bk_gui_gfx_draw_rect(surface,
                          (gui_rect_t){bar.x - 1, bar.y - 1,
                                       bar.w + 2, bar.h + 2},
                          0x00ECBE64);
    }
}

static bool midamp_window_event(gui_window_t *window UNUSED,
                                const gui_event_t *event, void *context) {
    midamp_state_t *st = (midamp_state_t *)context;

    if (!st || !event) return false;
    if (event->type != GUI_EVENT_KEY &&
        event->type != GUI_EVENT_MOUSE_DOWN &&
        event->type != GUI_EVENT_MOUSE_UP)
        return false;
    return bk_gui_event_queue_push(&st->events, event);
}

static void midamp_cleanup(midamp_state_t *st) {
    if (!st) return;
    bk_sound_stop();
    if (st->window) {
        bk_gui_desktop_remove_window(st->desktop, st->window);
        bk_gui_window_destroy_raw(st->window);
        bk_proc_bind_window(NULL);
    }
    if (st->sequence) bk_sys_free(st->sequence);
    midamp_skin_free(st);
    if (g_midamp == st) g_midamp = NULL;
    bk_sys_free(st);
}

bool midamp_get_runtime_info(program_runtime_info_t *info) {
    if (!info || !g_midamp) return false;
    info->window = g_midamp->window;
    info->memory_bytes = (uint32_t)sizeof(*g_midamp) +
                         g_midamp->sequence_capacity *
                         (uint32_t)sizeof(midamp_note_t);
    info->memory_bytes += (uint32_t)g_midamp->skin_header.width *
                          g_midamp->skin_header.height * sizeof(uint32_t);
    info->memory_bytes += (uint32_t)g_midamp->skin_buttons.width *
                          g_midamp->skin_buttons.height * sizeof(uint32_t);
    info->memory_bytes += (uint32_t)g_midamp->skin_bottom.width *
                          g_midamp->skin_bottom.height * sizeof(uint32_t);
    if (g_midamp->window) {
        info->memory_bytes += (uint32_t)sizeof(gui_window_t);
    }
    return true;
}

void midamp_open_from_desktop(gui_desktop_t *desktop) {
    midamp_state_t *st;

    if (!desktop) return;

    if (g_midamp && g_midamp->window) {
        bk_gui_window_restore(g_midamp->window);
        bk_gui_desktop_raise_window(desktop, g_midamp->window);
        bk_gui_focus_window(desktop, g_midamp->window);
        return;
    }

    st = (midamp_state_t *)bk_sys_alloc_zero(sizeof(*st));
    if (!st) return;
    st->desktop = desktop;
    st->selected_index = -1;
    st->current_index = -1;
    st->last_click_index = -1;
    st->pressed_button = -1;
    midamp_copy_text(st->status, sizeof(st->status), "Inicializando...");
    g_midamp = st;

    if (bk_proc_spawn_thread("midamp", midamp_main, st) < 0) {
        g_midamp = NULL;
        bk_sys_free(st);
    }
}

static void midamp_main(void *argument) {
    midamp_state_t *st = (midamp_state_t *)argument;
    gui_event_t event;
    midamp_action_t action;
    uint32_t last_ui_tick = 0;

    if (!st || !st->desktop) {
        midamp_cleanup(st);
        bk_proc_exit();
    }

    bk_proc_set_memory_hint(sizeof(*st));
    bk_gui_event_queue_reset(&st->events);

    st->window = bk_gui_create_window(st->desktop, 78, 0,
                                           MIDAMP_MIN_W, MIDAMP_MIN_H,
                                           "WinMap MIDAMP");
    if (st->window) {
        bk_gui_window_set_borderless(st->window, true, MIDAMP_HEADER_H);
        bk_gui_set_window_content(st->window, midamp_content, st);
        bk_gui_set_window_event_handler(st->window, midamp_window_event, st);
        bk_gui_set_window_min_size(st->window, MIDAMP_MIN_W, MIDAMP_MIN_H);
        st->window->owner_pid = bk_sys_getpid();
        bk_proc_bind_window(st->window);
    } else {
        midamp_cleanup(st);
        bk_proc_exit();
        return;
    }

    if (midamp_skin_load(st)) {
        bk_proc_set_memory_hint(sizeof(*st) +
                             (uint32_t)st->skin_header.width *
                                 st->skin_header.height * sizeof(uint32_t) +
                             (uint32_t)st->skin_buttons.width *
                                 st->skin_buttons.height * sizeof(uint32_t) +
                             (uint32_t)st->skin_bottom.width *
                                 st->skin_bottom.height * sizeof(uint32_t));
    } else {
        midamp_set_status(st, "Skin GIF no encontrada");
    }
    midamp_reload_playlist(st);

    while (!bk_proc_exit_requested()) {
        while (bk_gui_event_queue_pop(&st->events, &event))
            midamp_process_event(st, &event);
        while (midamp_action_pop(st, &action))
            midamp_process_action(st, &action);

        midamp_playback_tick(st);

        if (st->playing || st->paused) {
            uint32_t now = bk_sys_ticks();
            if (st->window && now != last_ui_tick) {
                st->window->dirty = true;
                last_ui_tick = now;
            }
        }

        if (!st->window || !st->window->listed) break;
    }

    midamp_cleanup(st);
    bk_proc_exit();
}

void midamp_install(gui_desktop_t *desktop UNUSED) {}

void bleskernos_program_main(gui_desktop_t *desktop) {
    midamp_open_from_desktop(desktop);
}
