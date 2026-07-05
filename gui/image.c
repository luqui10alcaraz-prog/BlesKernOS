#include "image.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/vfs.h"

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void gui_image_free(gui_image_t *image) {
    if (!image) return;
    if (image->pixels) kfree(image->pixels);
    image->pixels = NULL;
    image->width = 0;
    image->height = 0;
}

void gui_gif_animation_free(gui_gif_animation_t *animation) {
    if (!animation) return;
    if (animation->frames) {
        for (uint16_t i = 0; i < animation->frame_count; i++)
            gui_image_free(&animation->frames[i]);
        kfree(animation->frames);
    }
    if (animation->delays_cs) kfree(animation->delays_cs);
    animation->frames = NULL;
    animation->delays_cs = NULL;
    animation->frame_count = 0;
    animation->width = 0;
    animation->height = 0;
}

static bool skip_blocks(const uint8_t *data, uint32_t length, uint32_t *pos) {
    while (*pos < length) {
        uint8_t size = data[(*pos)++];
        if (!size) return true;
        if (*pos > length - size) return false;
        *pos += size;
    }
    return false;
}

static bool skip_palette(const uint8_t *data UNUSED, uint32_t length,
                         uint32_t *pos, uint32_t count) {
    uint32_t bytes = count * 3U;
    if (!pos || *pos > length || bytes > length - *pos) return false;
    *pos += bytes;
    return true;
}

static bool read_blocks(const uint8_t *data, uint32_t length, uint32_t *pos,
                        uint8_t **out, uint32_t *out_length) {
    uint8_t *packed = (uint8_t *)kmalloc(length ? length : 1U);
    uint32_t used = 0;
    if (!packed) return false;
    while (*pos < length) {
        uint8_t size = data[(*pos)++];
        if (!size) {
            *out = packed;
            *out_length = used;
            return true;
        }
        if (*pos > length - size) break;
        kmemcpy(packed + used, data + *pos, size);
        used += size;
        *pos += size;
    }
    kfree(packed);
    return false;
}

static bool read_palette(const uint8_t *data, uint32_t length, uint32_t *pos,
                         uint32_t *palette, uint32_t count) {
    if (*pos > length || count * 3U > length - *pos) return false;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t r = data[(*pos)++];
        uint8_t g = data[(*pos)++];
        uint8_t b = data[(*pos)++];
        palette[i] = 0xFF000000U | ((uint32_t)r << 16) |
                     ((uint32_t)g << 8) | b;
    }
    return true;
}

static bool gif_read_header(const uint8_t *data, uint32_t length,
                            uint32_t *pos, uint16_t *canvas_w,
                            uint16_t *canvas_h, uint32_t *global_palette,
                            uint32_t *global_count) {
    if (!data || length < 13U || !pos || !canvas_w || !canvas_h ||
        !global_palette || !global_count)
        return false;
    if (kmemcmp(data, "GIF87a", 6) && kmemcmp(data, "GIF89a", 6))
        return false;

    *pos = 13U;
    *canvas_w = le16(data + 6);
    *canvas_h = le16(data + 8);
    if (!*canvas_w || !*canvas_h) return false;

    *global_count = 0;
    if (data[10] & 0x80U) {
        *global_count = 1U << ((data[10] & 7U) + 1U);
        if (!read_palette(data, length, pos, global_palette, *global_count))
            return false;
    }
    return true;
}

static bool gif_count_frames(const uint8_t *data, uint32_t length,
                             uint16_t *canvas_w, uint16_t *canvas_h,
                             uint16_t *frame_count) {
    uint32_t pos = 0, global_palette[256], global_count = 0;
    uint32_t count = 0;

    if (!canvas_w || !canvas_h || !frame_count) return false;
    if (!gif_read_header(data, length, &pos, canvas_w, canvas_h,
                         global_palette, &global_count))
        return false;

    while (pos < length) {
        uint8_t id = data[pos++];
        if (id == 0x3BU) break;
        if (id == 0x21U) {
            uint8_t label;
            if (pos >= length) return false;
            label = data[pos++];
            if (label == 0xF9U) {
                if (pos + 6U > length || data[pos] != 4U) return false;
                pos += 6U;
            } else if (!skip_blocks(data, length, &pos)) {
                return false;
            }
            continue;
        }
        if (id != 0x2CU || pos + 9U > length) return false;
        {
            uint8_t flags = data[pos + 8];
            pos += 9U;
            if (flags & 0x80U) {
                uint32_t local_count = 1U << ((flags & 7U) + 1U);
                if (!skip_palette(data, length, &pos, local_count))
                    return false;
            }
            if (pos >= length || data[pos] > 8U) return false;
            pos++;
            if (!skip_blocks(data, length, &pos)) return false;
            if (count == 0xFFFFU) return false;
            count++;
        }
    }

    if (!count) return false;
    *frame_count = (uint16_t)count;
    return true;
}

static bool gif_decode_frame_pixels(const uint8_t *data, uint32_t length,
                                    uint32_t *pos, const uint32_t *global_palette,
                                    uint32_t global_count, uint32_t transparent,
                                    uint32_t *canvas, uint16_t canvas_w,
                                    uint16_t canvas_h, uint16_t *out_left,
                                    uint16_t *out_top, uint16_t *out_width,
                                    uint16_t *out_height) {
    uint16_t left, top, width, height;
    uint8_t flags, min_size;
    uint32_t local_palette[256], local_count = global_count;
    const uint32_t *palette = global_palette;
    uint8_t *packed = NULL, *suffix = NULL, *stack = NULL;
    uint16_t *prefix = NULL;
    uint32_t packed_len = 0, clear, end, available, code_size, mask;
    uint32_t datum = 0, bits = 0, input = 0, stack_size = 0;
    uint32_t first = 0, x = 0, y = 0, pass = 0;
    int32_t old = -1;
    bool interlaced;
    static const uint8_t row_start[4] = {0, 4, 2, 1};
    static const uint8_t row_step[4] = {8, 8, 4, 2};

    if (!data || !pos || *pos + 9U > length || !canvas) return false;

    left = le16(data + *pos);
    top = le16(data + *pos + 2);
    width = le16(data + *pos + 4);
    height = le16(data + *pos + 6);
    flags = data[*pos + 8];
    interlaced = (flags & 0x40U) != 0;
    *pos += 9U;

    if (!width || !height) return false;
    if (flags & 0x80U) {
        local_count = 1U << ((flags & 7U) + 1U);
        if (!read_palette(data, length, pos, local_palette, local_count))
            return false;
        palette = local_palette;
    }
    if (!local_count || *pos >= length) return false;

    min_size = data[(*pos)++];
    if (min_size > 8U ||
        !read_blocks(data, length, pos, &packed, &packed_len))
        return false;

    prefix = (uint16_t *)kmalloc(4096U * sizeof(uint16_t));
    suffix = (uint8_t *)kmalloc(4096U);
    stack = (uint8_t *)kmalloc(4097U);
    if (!prefix || !suffix || !stack) goto fail;

    clear = 1U << min_size;
    end = clear + 1U;
    available = clear + 2U;
    code_size = (uint32_t)min_size + 1U;
    mask = (1U << code_size) - 1U;
    for (uint32_t i = 0; i < clear; i++) {
        prefix[i] = 0;
        suffix[i] = (uint8_t)i;
    }

    for (uint32_t written = 0; written < (uint32_t)width * height;) {
        if (!stack_size) {
            uint32_t code, original;
            while (bits < code_size && input < packed_len) {
                datum |= (uint32_t)packed[input++] << bits;
                bits += 8U;
            }
            if (bits < code_size) break;
            code = datum & mask;
            datum >>= code_size;
            bits -= code_size;
            if (code == clear) {
                code_size = (uint32_t)min_size + 1U;
                mask = (1U << code_size) - 1U;
                available = clear + 2U;
                old = -1;
                continue;
            }
            if (code == end) break;
            if (code >= 4096U) goto fail;
            if (old < 0) {
                stack[stack_size++] = suffix[code];
                first = code;
                old = (int32_t)code;
            } else {
                original = code;
                if (code >= available) {
                    stack[stack_size++] = (uint8_t)first;
                    code = (uint32_t)old;
                }
                while (code >= clear) {
                    if (code >= available || stack_size >= 4096U)
                        goto fail;
                    stack[stack_size++] = suffix[code];
                    code = prefix[code];
                }
                first = suffix[code];
                stack[stack_size++] = (uint8_t)first;
                if (available < 4096U) {
                    prefix[available] = (uint16_t)old;
                    suffix[available++] = (uint8_t)first;
                    if (available == (1U << code_size) && code_size < 12U)
                        mask = (1U << ++code_size) - 1U;
                }
                old = (int32_t)original;
            }
        }
        if (!stack_size) continue;
        {
            uint8_t index = stack[--stack_size];
            uint32_t dx = (uint32_t)left + x;
            uint32_t dy = (uint32_t)top + y;
            if (index != transparent &&
                index < local_count && dx < canvas_w && dy < canvas_h)
                canvas[dy * canvas_w + dx] = palette[index];
        }
        written++;
        if (++x >= width) {
            x = 0;
            if (!interlaced) y++;
            else {
                y += row_step[pass];
                while (y >= height && ++pass < 4U)
                    y = row_start[pass];
            }
        }
    }

    kfree(packed);
    kfree(prefix);
    kfree(suffix);
    kfree(stack);
    if (out_left) *out_left = left;
    if (out_top) *out_top = top;
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    return true;

fail:
    if (packed) kfree(packed);
    if (prefix) kfree(prefix);
    if (suffix) kfree(suffix);
    if (stack) kfree(stack);
    return false;
}

static bool gif_store_canvas_frame(gui_image_t *frame, const uint32_t *canvas,
                                   uint16_t width, uint16_t height) {
    uint32_t pixels;

    if (!frame || !canvas || !width || !height) return false;
    gui_image_free(frame);
    pixels = (uint32_t)width * height;
    frame->pixels = (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
    if (!frame->pixels) return false;
    kmemcpy(frame->pixels, canvas, pixels * sizeof(uint32_t));
    frame->width = width;
    frame->height = height;
    return true;
}

static void gif_clear_rect(uint32_t *canvas, uint16_t canvas_w, uint16_t canvas_h,
                           uint16_t left, uint16_t top,
                           uint16_t width, uint16_t height) {
    if (!canvas) return;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t dy = (uint32_t)top + y;
        if (dy >= canvas_h) break;
        for (uint32_t x = 0; x < width; x++) {
            uint32_t dx = (uint32_t)left + x;
            if (dx >= canvas_w) break;
            canvas[dy * canvas_w + dx] = 0;
        }
    }
}

bool gui_gif_decode(gui_image_t *image, const uint8_t *data, uint32_t length) {
    uint32_t pos = 13, global_palette[256], global_count = 0;
    uint32_t transparent = 0xFFFFFFFFU;
    uint16_t canvas_w, canvas_h;

    if (!image || !data || length < 13U) return false;
    gui_image_free(image);
    if (kmemcmp(data, "GIF87a", 6) && kmemcmp(data, "GIF89a", 6))
        return false;
    canvas_w = le16(data + 6);
    canvas_h = le16(data + 8);
    if (!canvas_w || !canvas_h) return false;
    if (data[10] & 0x80U) {
        global_count = 1U << ((data[10] & 7U) + 1U);
        if (!read_palette(data, length, &pos, global_palette, global_count))
            return false;
    }

    while (pos < length) {
        uint8_t id = data[pos++];
        if (id == 0x3BU) return false;
        if (id == 0x21U) {
            uint8_t label;
            if (pos >= length) return false;
            label = data[pos++];
            if (label == 0xF9U) {
                if (pos + 6U > length || data[pos] != 4U) return false;
                if (data[pos + 1] & 1U) transparent = data[pos + 4];
                pos += 6U;
            } else if (!skip_blocks(data, length, &pos)) {
                return false;
            }
            continue;
        }
        if (id != 0x2CU || pos + 9U > length) return false;

        {
            uint16_t left = le16(data + pos), top = le16(data + pos + 2);
            uint16_t width = le16(data + pos + 4), height = le16(data + pos + 6);
            uint8_t flags = data[pos + 8], min_size;
            uint32_t local_palette[256], local_count = global_count;
            const uint32_t *palette = global_palette;
            uint8_t *packed = NULL, *suffix = NULL, *stack = NULL;
            uint16_t *prefix = NULL;
            uint32_t packed_len = 0, clear, end, available, code_size, mask;
            uint32_t datum = 0, bits = 0, input = 0, stack_size = 0;
            uint32_t first = 0, x = 0, y = 0, pass = 0;
            int32_t old = -1;
            bool interlaced = (flags & 0x40U) != 0;
            static const uint8_t row_start[4] = {0, 4, 2, 1};
            static const uint8_t row_step[4] = {8, 8, 4, 2};

            pos += 9U;
            if (!width || !height) return false;
            if (flags & 0x80U) {
                local_count = 1U << ((flags & 7U) + 1U);
                if (!read_palette(data, length, &pos,
                                  local_palette, local_count)) return false;
                palette = local_palette;
            }
            if (!local_count || pos >= length) return false;
            min_size = data[pos++];
            if (min_size > 8U ||
                !read_blocks(data, length, &pos, &packed, &packed_len))
                return false;

            prefix = (uint16_t *)kmalloc(4096U * sizeof(uint16_t));
            suffix = (uint8_t *)kmalloc(4096U);
            stack = (uint8_t *)kmalloc(4097U);
            image->pixels = (uint32_t *)kzalloc((uint32_t)canvas_w * canvas_h *
                                                sizeof(uint32_t));
            if (!prefix || !suffix || !stack || !image->pixels) goto fail;
            image->width = canvas_w;
            image->height = canvas_h;
            clear = 1U << min_size;
            end = clear + 1U;
            available = clear + 2U;
            code_size = (uint32_t)min_size + 1U;
            mask = (1U << code_size) - 1U;
            for (uint32_t i = 0; i < clear; i++) {
                prefix[i] = 0;
                suffix[i] = (uint8_t)i;
            }

            for (uint32_t written = 0; written < (uint32_t)width * height;) {
                if (!stack_size) {
                    uint32_t code, original;
                    while (bits < code_size && input < packed_len) {
                        datum |= (uint32_t)packed[input++] << bits;
                        bits += 8U;
                    }
                    if (bits < code_size) break;
                    code = datum & mask;
                    datum >>= code_size;
                    bits -= code_size;
                    if (code == clear) {
                        code_size = (uint32_t)min_size + 1U;
                        mask = (1U << code_size) - 1U;
                        available = clear + 2U;
                        old = -1;
                        continue;
                    }
                    if (code == end) break;
                    if (code >= 4096U) goto fail;
                    if (old < 0) {
                        stack[stack_size++] = suffix[code];
                        first = code;
                        old = (int32_t)code;
                    } else {
                        original = code;
                        if (code >= available) {
                            stack[stack_size++] = (uint8_t)first;
                            code = (uint32_t)old;
                        }
                        while (code >= clear) {
                            if (code >= available || stack_size >= 4096U)
                                goto fail;
                            stack[stack_size++] = suffix[code];
                            code = prefix[code];
                        }
                        first = suffix[code];
                        stack[stack_size++] = (uint8_t)first;
                        if (available < 4096U) {
                            prefix[available] = (uint16_t)old;
                            suffix[available++] = (uint8_t)first;
                            if (available == (1U << code_size) &&
                                code_size < 12U)
                                mask = (1U << ++code_size) - 1U;
                        }
                        old = (int32_t)original;
                    }
                }
                if (!stack_size) continue;
                {
                    uint8_t index = stack[--stack_size];
                    uint32_t dx = (uint32_t)left + x;
                    uint32_t dy = (uint32_t)top + y;
                    if (index < local_count && dx < canvas_w && dy < canvas_h)
                        image->pixels[dy * canvas_w + dx] =
                            index == transparent ? 0 : palette[index];
                }
                written++;
                if (++x >= width) {
                    x = 0;
                    if (!interlaced) y++;
                    else {
                        y += row_step[pass];
                        while (y >= height && ++pass < 4U)
                            y = row_start[pass];
                    }
                }
            }
            kfree(packed);
            kfree(prefix);
            kfree(suffix);
            kfree(stack);
            return true;
fail:
            if (packed) kfree(packed);
            if (prefix) kfree(prefix);
            if (suffix) kfree(suffix);
            if (stack) kfree(stack);
            gui_image_free(image);
            return false;
        }
    }
    return false;
}

bool gui_gif_load(gui_image_t *image, const char *path) {
    void *data = NULL;
    uint32_t length = 0;
    bool ok;
    if (!image || !path) return false;
    gui_image_free(image);
    if (!vfs_read_all(path, &data, &length) || !data) return false;
    ok = gui_gif_decode(image, (const uint8_t *)data, length);
    kfree(data);
    return ok;
}

bool gui_gif_load_animation_limited(gui_gif_animation_t *animation,
                                    const char *path, uint16_t max_frames) {
    void *data = NULL;
    uint32_t length = 0;
    uint16_t canvas_w = 0, canvas_h = 0, frame_count = 0;
    uint16_t stored_capacity = 0, source_index = 0, frame_step = 1;
    uint32_t pos = 0, global_palette[256], global_count = 0;
    uint32_t *canvas = NULL, *restore = NULL;
    uint16_t frame_index = 0;
    uint16_t pending_delay = 10U;
    uint32_t pending_transparent = 0xFFFFFFFFU;
    uint8_t pending_disposal = 0U;
    bool ok = false;

    if (!animation || !path) return false;
    gui_gif_animation_free(animation);

    if (!vfs_read_all(path, &data, &length) || !data) return false;
    if (!gif_count_frames((const uint8_t *)data, length,
                          &canvas_w, &canvas_h, &frame_count))
        goto done;

    stored_capacity = frame_count;
    if (max_frames && stored_capacity > max_frames) {
        frame_step = (uint16_t)((frame_count + max_frames - 1U) / max_frames);
        stored_capacity =
            (uint16_t)((frame_count + frame_step - 1U) / frame_step);
    }
    animation->frames = (gui_image_t *)kzalloc((uint32_t)stored_capacity *
                                               sizeof(gui_image_t));
    animation->delays_cs = (uint16_t *)kzalloc((uint32_t)stored_capacity *
                                               sizeof(uint16_t));
    canvas = (uint32_t *)kzalloc((uint32_t)canvas_w * canvas_h *
                                 sizeof(uint32_t));
    if (!animation->frames || !animation->delays_cs || !canvas) goto done;

    animation->frame_count = stored_capacity;
    animation->width = canvas_w;
    animation->height = canvas_h;

    if (!gif_read_header((const uint8_t *)data, length, &pos, &canvas_w, &canvas_h,
                         global_palette, &global_count))
        goto done;

    while (pos < length && source_index < frame_count) {
        uint8_t id = ((const uint8_t *)data)[pos++];
        if (id == 0x3BU) break;
        if (id == 0x21U) {
            uint8_t label;
            if (pos >= length) goto done;
            label = ((const uint8_t *)data)[pos++];
            if (label == 0xF9U) {
                if (pos + 6U > length || ((const uint8_t *)data)[pos] != 4U)
                    goto done;
                pending_disposal = (uint8_t)((((const uint8_t *)data)[pos + 1] >> 2) & 7U);
                pending_delay = le16((const uint8_t *)data + pos + 2);
                if (!pending_delay) pending_delay = 10U;
                pending_transparent = (((const uint8_t *)data)[pos + 1] & 1U) ?
                    ((const uint8_t *)data)[pos + 4] : 0xFFFFFFFFU;
                pos += 6U;
            } else if (!skip_blocks((const uint8_t *)data, length, &pos)) {
                goto done;
            }
            continue;
        }
        if (id != 0x2CU) goto done;
        {
            uint16_t left = 0, top = 0, width = 0, height = 0;
            uint32_t pixels = (uint32_t)canvas_w * canvas_h * sizeof(uint32_t);

            if (pending_disposal == 3U) {
                if (!restore)
                    restore = (uint32_t *)kmalloc(pixels);
                if (!restore) goto done;
                kmemcpy(restore, canvas, pixels);
            }

            if (!gif_decode_frame_pixels((const uint8_t *)data, length, &pos,
                                         global_palette, global_count,
                                         pending_transparent, canvas,
                                         canvas_w, canvas_h,
                                         &left, &top, &width, &height))
                goto done;
            if ((source_index % frame_step) == 0U) {
                if (frame_index >= stored_capacity ||
                    !gif_store_canvas_frame(&animation->frames[frame_index],
                                            canvas, canvas_w, canvas_h))
                    goto done;
                animation->delays_cs[frame_index] = pending_delay;
                frame_index++;
            } else if (frame_index) {
                uint32_t combined =
                    (uint32_t)animation->delays_cs[frame_index - 1U] +
                    pending_delay;
                animation->delays_cs[frame_index - 1U] =
                    (uint16_t)(combined > 0xFFFFU ? 0xFFFFU : combined);
            }
            source_index++;

            if (pending_disposal == 2U) {
                gif_clear_rect(canvas, canvas_w, canvas_h,
                               left, top, width, height);
            } else if (pending_disposal == 3U && restore) {
                kmemcpy(canvas, restore, pixels);
            }

            pending_delay = 10U;
            pending_transparent = 0xFFFFFFFFU;
            pending_disposal = 0U;
        }
    }

    ok = frame_index > 0;
    if (ok) animation->frame_count = frame_index;

done:
    if (restore) kfree(restore);
    if (canvas) kfree(canvas);
    if (data) kfree(data);
    if (!ok) gui_gif_animation_free(animation);
    return ok;
}

bool gui_gif_load_animation(gui_gif_animation_t *animation, const char *path) {
    return gui_gif_load_animation_limited(animation, path, 0);
}
