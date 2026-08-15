#include "image.h"
#include "../kernel/include/memory.h"
#include "../kernel/include/vfs.h"
#include "../kernel/include/inflate.h"

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

static bool png_unfilter(uint8_t *rows, uint32_t row_bytes, uint32_t height,
                         uint32_t bytes_per_pixel) {
    uint32_t y, x;
    if (!rows || !row_bytes || !height || !bytes_per_pixel) return false;
    for (y = 0U; y < height; y++) {
        uint8_t filter = rows[y * (row_bytes + 1U)];
        uint8_t *row = rows + y * (row_bytes + 1U) + 1U;
        uint8_t *previous = y ? rows + (y - 1U) * (row_bytes + 1U) + 1U : NULL;
        if (filter > 4U) return false;
        for (x = 0U; x < row_bytes; x++) {
            uint8_t left = x >= bytes_per_pixel ? row[x - bytes_per_pixel] : 0U;
            uint8_t up = previous ? previous[x] : 0U;
            uint8_t upper_left = previous && x >= bytes_per_pixel ?
                                 previous[x - bytes_per_pixel] : 0U;
            if (filter == 1U) row[x] = (uint8_t)(row[x] + left);
            else if (filter == 2U) row[x] = (uint8_t)(row[x] + up);
            else if (filter == 3U)
                row[x] = (uint8_t)(row[x] + ((uint16_t)left + up) / 2U);
            else if (filter == 4U)
                row[x] = (uint8_t)(row[x] + png_paeth(left, up, upper_left));
        }
    }
    return true;
}

static uint8_t png_sample(const uint8_t *row, uint32_t x, uint8_t depth) {
    uint32_t bit;
    uint8_t mask;
    if (depth == 8U) return row[x];
    bit = x * depth;
    mask = (uint8_t)((1U << depth) - 1U);
    return (uint8_t)((row[bit >> 3U] >> (8U - depth - (bit & 7U))) & mask);
}

bool gui_png_decode(gui_image_t *image, const uint8_t *data, uint32_t length) {
    static const uint8_t signature[8] = {137,80,78,71,13,10,26,10};
    uint32_t position = 8U, width = 0U, height = 0U;
    uint8_t depth = 0U, color_type = 0U, interlace = 0U;
    uint8_t palette[256][4];
    uint32_t palette_count = 0U;
    uint8_t *packed = NULL, *rows = NULL;
    uint32_t packed_length = 0U, packed_capacity = 0U;
    uint32_t channels, row_bits, row_bytes, filtered_size, bpp;
    uint32_t *pixels = NULL;
    int32_t inflated;
    uint32_t x, y;
    if (!image || !data || length < 33U ||
        kmemcmp(data, signature, sizeof(signature)) != 0) return false;
    image->pixels = NULL; image->width = 0U; image->height = 0U;
    for (x = 0U; x < 256U; x++) {
        palette[x][0] = palette[x][1] = palette[x][2] = 0U;
        palette[x][3] = 0xFFU;
    }
    while (position + 12U <= length) {
        uint32_t chunk_length = be32(data + position);
        const uint8_t *type = data + position + 4U;
        const uint8_t *chunk = data + position + 8U;
        if (chunk_length > length - position - 12U) goto fail;
        if (type[0]=='I' && type[1]=='H' && type[2]=='D' && type[3]=='R') {
            if (chunk_length != 13U) goto fail;
            width = be32(chunk); height = be32(chunk + 4U);
            depth = chunk[8]; color_type = chunk[9]; interlace = chunk[12];
            if (!width || !height || width > 4096U || height > 4096U ||
                width > 0xFFFFFFFFU / height / 4U || chunk[10] || chunk[11] ||
                interlace != 0U) goto fail;
            if (color_type == 3U) {
                if (depth != 1U && depth != 2U && depth != 4U && depth != 8U)
                    goto fail;
            } else if (depth != 8U) goto fail;
            if (color_type != 0U && color_type != 2U && color_type != 3U &&
                color_type != 4U && color_type != 6U) goto fail;
        } else if (type[0]=='P' && type[1]=='L' && type[2]=='T' && type[3]=='E') {
            if (chunk_length == 0U || chunk_length % 3U || chunk_length > 768U)
                goto fail;
            palette_count = chunk_length / 3U;
            for (x = 0U; x < palette_count; x++) {
                palette[x][0] = chunk[x * 3U];
                palette[x][1] = chunk[x * 3U + 1U];
                palette[x][2] = chunk[x * 3U + 2U];
            }
        } else if (type[0]=='t' && type[1]=='R' && type[2]=='N' && type[3]=='S') {
            if (color_type == 3U) {
                uint32_t count = chunk_length < palette_count ? chunk_length : palette_count;
                for (x = 0U; x < count; x++) palette[x][3] = chunk[x];
            }
        } else if (type[0]=='I' && type[1]=='D' && type[2]=='A' && type[3]=='T') {
            uint32_t needed;
            if (!width || !height || chunk_length > 4U * 1024U * 1024U)
                goto fail;
            needed = packed_length + chunk_length;
            if (needed < packed_length) goto fail;
            if (needed > packed_capacity) {
                uint32_t next = packed_capacity ? packed_capacity : 4096U;
                uint8_t *replacement;
                while (next < needed) {
                    if (next > 4U * 1024U * 1024U / 2U) { next = needed; break; }
                    next *= 2U;
                }
                replacement = (uint8_t *)kmalloc(next);
                if (!replacement) goto fail;
                if (packed_length) kmemcpy(replacement, packed, packed_length);
                if (packed) kfree(packed);
                packed = replacement; packed_capacity = next;
            }
            kmemcpy(packed + packed_length, chunk, chunk_length);
            packed_length += chunk_length;
        } else if (type[0]=='I' && type[1]=='E' && type[2]=='N' && type[3]=='D') {
            break;
        }
        position += chunk_length + 12U;
    }
    if (!width || !height || !packed_length) goto fail;
    channels = color_type == 0U ? 1U : color_type == 2U ? 3U :
               color_type == 3U ? 1U : color_type == 4U ? 2U : 4U;
    row_bits = width * channels * depth;
    if (width && row_bits / width != channels * depth) goto fail;
    row_bytes = (row_bits + 7U) / 8U;
    if (!row_bytes || height > 0xFFFFFFFFU / (row_bytes + 1U)) goto fail;
    filtered_size = (row_bytes + 1U) * height;
    rows = (uint8_t *)kmalloc(filtered_size);
    if (!rows) goto fail;
    inflated = inflate_zlib(packed, packed_length, rows, filtered_size);
    if (inflated != (int32_t)filtered_size) goto fail;
    bpp = color_type == 3U || depth < 8U ? 1U : channels;
    if (!png_unfilter(rows, row_bytes, height, bpp)) goto fail;
    pixels = (uint32_t *)kmalloc(width * height * 4U);
    if (!pixels) goto fail;
    for (y = 0U; y < height; y++) {
        const uint8_t *row = rows + y * (row_bytes + 1U) + 1U;
        for (x = 0U; x < width; x++) {
            uint8_t r=0U,g=0U,b=0U,a=0xFFU;
            if (color_type == 0U) r = g = b = row[x];
            else if (color_type == 2U) {
                r=row[x*3U]; g=row[x*3U+1U]; b=row[x*3U+2U];
            } else if (color_type == 3U) {
                uint8_t index = png_sample(row, x, depth);
                if (index >= palette_count) goto fail;
                r=palette[index][0]; g=palette[index][1];
                b=palette[index][2]; a=palette[index][3];
            } else if (color_type == 4U) {
                r=g=b=row[x*2U]; a=row[x*2U+1U];
            } else {
                r=row[x*4U]; g=row[x*4U+1U]; b=row[x*4U+2U]; a=row[x*4U+3U];
            }
            pixels[y*width+x] = ((uint32_t)a<<24)|((uint32_t)r<<16)|
                                ((uint32_t)g<<8)|b;
        }
    }
    if (packed) kfree(packed);
    if (rows) kfree(rows);
    image->pixels = pixels; image->width = (uint16_t)width;
    image->height = (uint16_t)height;
    return true;
fail:
    if (pixels) kfree(pixels);
    if (rows) kfree(rows);
    if (packed) kfree(packed);
    return false;
}

bool gui_bmp_decode(gui_image_t *image, const uint8_t *data, uint32_t length) {
    uint32_t offset, stride, compression;
    int32_t width, raw_height, height;
    uint16_t bpp;
    uint32_t *pixels;
    if (!image || !data || length < 54U) return false;
    image->pixels = NULL;
    image->width = 0U;
    image->height = 0U;
    if (data[0] != 'B' || data[1] != 'M' || le32(data + 14U) < 40U)
        return false;
    offset = le32(data + 10U);
    width = (int32_t)le32(data + 18U);
    raw_height = (int32_t)le32(data + 22U);
    bpp = le16(data + 28U);
    compression = le32(data + 30U);
    if (width <= 0 || raw_height == 0 || raw_height == (int32_t)0x80000000U ||
        width > 4096 || raw_height > 4096 || raw_height < -4096 ||
        (bpp != 24U && bpp != 32U) || compression != 0U) return false;
    height = raw_height < 0 ? -raw_height : raw_height;
    stride = ((uint32_t)width * (uint32_t)(bpp / 8U) + 3U) & ~3U;
    if (offset >= length || stride > length ||
        (uint32_t)height > (length - offset) / stride) return false;
    if ((uint32_t)width > 0xFFFFFFFFU / (uint32_t)height / 4U) return false;
    pixels = (uint32_t *)kmalloc((uint32_t)width * (uint32_t)height * 4U);
    if (!pixels) return false;
    for (int32_t y = 0; y < height; y++) {
        int32_t source_y = raw_height > 0 ? height - 1 - y : y;
        const uint8_t *row = data + offset + (uint32_t)source_y * stride;
        for (int32_t x = 0; x < width; x++) {
            const uint8_t *pixel = row + (uint32_t)x * (uint32_t)(bpp / 8U);
            uint32_t alpha = bpp == 32U ? pixel[3] : 0xFFU;
            if (bpp == 32U && alpha == 0U) alpha = 0xFFU;
            pixels[(uint32_t)y * (uint32_t)width + (uint32_t)x] =
                (alpha << 24) | ((uint32_t)pixel[2] << 16) |
                ((uint32_t)pixel[1] << 8) | pixel[0];
        }
    }
    image->pixels = pixels;
    image->width = (uint16_t)width;
    image->height = (uint16_t)height;
    return true;
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
