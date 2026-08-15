#include <bleskernos_print.h>

#define PRINT_INITIAL_CAPACITY 512U
#define PRINT_TITLE_OFFSET 24U
#define PRINT_TITLE_CAPACITY 64U
#define PRINT_ID_OFFSET 88U
#define PRINT_ID_CAPACITY 32U

struct bk_print_job {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    uint32_t command_count;
    uint32_t page_count;
    uint32_t sequence;
    bool page_open;
    bool failed;
};

static uint32_t g_print_sequence;

static void bytes_zero(void *pointer, uint32_t size) {
    uint8_t *bytes = (uint8_t *)pointer;
    while (bytes && size--) *bytes++ = 0;
}

static void bytes_copy(void *destination, const void *source, uint32_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    while (size--) *dst++ = *src++;
}

static uint32_t text_length_limited(const char *text, uint32_t limit) {
    uint32_t length = 0;
    while (text && text[length] && length < limit) length++;
    return length;
}

static void text_copy_fixed(uint8_t *destination, uint32_t capacity,
                            const char *text) {
    uint32_t length;
    if (!destination || !capacity) return;
    bytes_zero(destination, capacity);
    length = text_length_limited(text, capacity - 1U);
    if (length) bytes_copy(destination, text, length);
}

static void default_printer_id(char *destination, uint32_t capacity) {
    static const char prefix[] = "Default=";
    void *raw = NULL;
    uint32_t size = 0;
    uint32_t position = 0;

    if (!destination || !capacity) return;
    destination[0] = '\0';
    if (bk_file_read_all("/SYSTEM/USER/CONFIG/PRINTER.INI", &raw, &size) && raw) {
        const char *text = (const char *)raw;
        while (position + sizeof(prefix) - 1U <= size) {
            uint32_t i = 0;
            while (i + 1U < sizeof(prefix) &&
                   text[position + i] == prefix[i]) i++;
            if (i + 1U == sizeof(prefix)) {
                uint32_t out = 0;
                position += sizeof(prefix) - 1U;
                while (position < size && text[position] != '\r' &&
                       text[position] != '\n' && out + 1U < capacity)
                    destination[out++] = text[position++];
                destination[out] = '\0';
                break;
            }
            while (position < size && text[position] != '\n') position++;
            if (position < size) position++;
        }
        bk_sys_free(raw);
    }
    if (!destination[0]) {
        uint32_t length = text_length_limited(BK_PRINT_DEFAULT_PRINTER,
                                               capacity - 1U);
        if (length) bytes_copy(destination, BK_PRINT_DEFAULT_PRINTER, length);
        destination[length] = '\0';
    }
}

static void put_u16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static bool job_reserve(bk_print_job_t *job, uint32_t extra) {
    uint32_t required;
    uint32_t capacity;
    uint8_t *replacement;

    if (!job || job->failed) return false;
    required = job->size + extra;
    if (required < job->size || required > BK_PRINT_JOB_MAX_SIZE) {
        job->failed = true;
        return false;
    }
    if (required <= job->capacity) return true;
    capacity = job->capacity ? job->capacity : PRINT_INITIAL_CAPACITY;
    while (capacity < required) {
        uint32_t next = capacity << 1;
        if (next <= capacity || next > BK_PRINT_JOB_MAX_SIZE) {
            capacity = BK_PRINT_JOB_MAX_SIZE;
            break;
        }
        capacity = next;
    }
    if (capacity < required) {
        job->failed = true;
        return false;
    }
    replacement = (uint8_t *)bk_sys_alloc(capacity);
    if (!replacement) {
        job->failed = true;
        return false;
    }
    if (job->data && job->size) bytes_copy(replacement, job->data, job->size);
    if (job->data) bk_sys_free(job->data);
    job->data = replacement;
    job->capacity = capacity;
    return true;
}

static bool job_append(bk_print_job_t *job, const void *data, uint32_t size) {
    if (!job_reserve(job, size)) return false;
    if (size) bytes_copy(job->data + job->size, data, size);
    job->size += size;
    return true;
}

static bool job_append_command(bk_print_job_t *job, uint16_t type,
                               uint16_t flags, const void *payload,
                               uint32_t payload_size) {
    uint8_t header[8];
    put_u16(header, type);
    put_u16(header + 2, flags);
    put_u32(header + 4, payload_size);
    if (!job_append(job, header, sizeof(header)) ||
        !job_append(job, payload, payload_size)) return false;
    job->command_count++;
    return true;
}

static void append_hex4(char *output, uint16_t value) {
    static const char digits[] = "0123456789ABCDEF";
    output[0] = digits[(value >> 12) & 15U];
    output[1] = digits[(value >> 8) & 15U];
    output[2] = digits[(value >> 4) & 15U];
    output[3] = digits[value & 15U];
}

static void make_job_path(char *path, const char *extension,
                          uint32_t sequence) {
    uint16_t left = (uint16_t)(bk_sys_ticks() ^ (bk_sys_getpid() << 5));
    uint16_t right = (uint16_t)(sequence ^ (bk_sys_getpid() * 257U));
    const char prefix[] = "/TEMP/SPOOL/J";
    uint32_t i = 0;
    while (prefix[i]) { path[i] = prefix[i]; i++; }
    append_hex4(path + i, left); i += 4;
    append_hex4(path + i, right); i += 4;
    path[i++] = '.';
    while (extension && *extension) path[i++] = *extension++;
    path[i] = '\0';
}

bk_print_job_t *print_begin(const char *title, const char *printer_id) {
    bk_print_job_t *job;
    char selected_printer[PRINT_ID_CAPACITY];

    if (printer_id && printer_id[0])
        text_copy_fixed((uint8_t *)selected_printer, sizeof(selected_printer),
                        printer_id);
    else
        default_printer_id(selected_printer, sizeof(selected_printer));

    job = (bk_print_job_t *)bk_sys_alloc(sizeof(*job));
    if (!job) return NULL;
    bytes_zero(job, sizeof(*job));
    job->sequence = ++g_print_sequence;
    if (!job_reserve(job, BK_PRINT_JOB_HEADER_SIZE)) {
        print_cancel(job);
        return NULL;
    }
    bytes_zero(job->data, BK_PRINT_JOB_HEADER_SIZE);
    job->size = BK_PRINT_JOB_HEADER_SIZE;
    job->data[0] = BK_PRINT_JOB_MAGIC_0;
    job->data[1] = BK_PRINT_JOB_MAGIC_1;
    job->data[2] = BK_PRINT_JOB_MAGIC_2;
    job->data[3] = BK_PRINT_JOB_MAGIC_3;
    put_u16(job->data + 4, BK_PRINT_JOB_VERSION);
    put_u16(job->data + 6, BK_PRINT_JOB_HEADER_SIZE);
    text_copy_fixed(job->data + PRINT_TITLE_OFFSET, PRINT_TITLE_CAPACITY,
                    title && title[0] ? title : "Documento");
    text_copy_fixed(job->data + PRINT_ID_OFFSET, PRINT_ID_CAPACITY,
                    selected_printer);
    if (!print_begin_page(job, BK_PRINT_A4_WIDTH_POINTS,
                          BK_PRINT_A4_HEIGHT_POINTS, 300U)) {
        print_cancel(job);
        return NULL;
    }
    return job;
}

bool print_begin_page(bk_print_job_t *job, uint32_t width_points,
                      uint32_t height_points, uint32_t raster_dpi) {
    uint8_t payload[12];
    if (!job || job->page_open || !width_points || !height_points ||
        raster_dpi < 60U || raster_dpi > 1200U) return false;
    put_u32(payload, width_points);
    put_u32(payload + 4, height_points);
    put_u32(payload + 8, raster_dpi);
    if (!job_append_command(job, BK_PRINT_CMD_BEGIN_PAGE, 0, payload,
                            sizeof(payload))) return false;
    job->page_open = true;
    job->page_count++;
    return true;
}

bool print_set_font(bk_print_job_t *job, bk_print_font_family_t family,
                    uint16_t points, uint32_t flags) {
    uint8_t payload[8];
    if (!job || !job->page_open || family > BK_PRINT_FONT_MONO ||
        points < 4U || points > 144U) return false;
    put_u16(payload, (uint16_t)family);
    put_u16(payload + 2, points);
    put_u32(payload + 4, flags & (BK_PRINT_FONT_BOLD |
                                 BK_PRINT_FONT_ITALIC |
                                 BK_PRINT_FONT_UNDERLINE));
    return job_append_command(job, BK_PRINT_CMD_SET_FONT, 0, payload,
                              sizeof(payload));
}

bool print_text(bk_print_job_t *job, int32_t x_points, int32_t y_points,
                const char *text) {
    uint8_t prefix[12];
    uint32_t length = text_length_limited(text, 4096U);
    uint8_t header[8];
    if (!job || !job->page_open || !text || !length) return false;
    put_u32(prefix, (uint32_t)x_points);
    put_u32(prefix + 4, (uint32_t)y_points);
    put_u32(prefix + 8, length);
    put_u16(header, BK_PRINT_CMD_TEXT);
    put_u16(header + 2, 0);
    put_u32(header + 4, sizeof(prefix) + length);
    if (!job_append(job, header, sizeof(header)) ||
        !job_append(job, prefix, sizeof(prefix)) ||
        !job_append(job, text, length)) return false;
    job->command_count++;
    return true;
}

bool print_line(bk_print_job_t *job, int32_t x1_points, int32_t y1_points,
                int32_t x2_points, int32_t y2_points,
                uint16_t width_points) {
    uint8_t payload[20];
    if (!job || !job->page_open || !width_points || width_points > 72U)
        return false;
    put_u32(payload, (uint32_t)x1_points);
    put_u32(payload + 4, (uint32_t)y1_points);
    put_u32(payload + 8, (uint32_t)x2_points);
    put_u32(payload + 12, (uint32_t)y2_points);
    put_u32(payload + 16, width_points);
    return job_append_command(job, BK_PRINT_CMD_LINE, 0, payload,
                              sizeof(payload));
}

bool print_bitmap_mono(bk_print_job_t *job, int32_t x_points,
                       int32_t y_points, uint32_t width_pixels,
                       uint32_t height_pixels, uint32_t stride_bytes,
                       uint16_t source_dpi, const void *pixels) {
    uint8_t prefix[28];
    uint8_t header[8];
    uint32_t data_size;
    uint32_t minimum_stride;
    if (!job || !job->page_open || !pixels || !width_pixels ||
        !height_pixels || source_dpi < 60U || source_dpi > 1200U)
        return false;
    minimum_stride = (width_pixels + 7U) >> 3;
    if (stride_bytes < minimum_stride || height_pixels > 16384U ||
        stride_bytes > BK_PRINT_JOB_MAX_SIZE / height_pixels) return false;
    data_size = stride_bytes * height_pixels;
    put_u32(prefix, (uint32_t)x_points);
    put_u32(prefix + 4, (uint32_t)y_points);
    put_u32(prefix + 8, width_pixels);
    put_u32(prefix + 12, height_pixels);
    put_u32(prefix + 16, stride_bytes);
    put_u32(prefix + 20, data_size);
    put_u16(prefix + 24, source_dpi);
    put_u16(prefix + 26, 1U);
    put_u16(header, BK_PRINT_CMD_BITMAP_MONO);
    put_u16(header + 2, 0);
    put_u32(header + 4, sizeof(prefix) + data_size);
    if (!job_append(job, header, sizeof(header)) ||
        !job_append(job, prefix, sizeof(prefix)) ||
        !job_append(job, pixels, data_size)) return false;
    job->command_count++;
    return true;
}

bool print_end_page(bk_print_job_t *job) {
    if (!job || !job->page_open) return false;
    if (!job_append_command(job, BK_PRINT_CMD_END_PAGE, 0, NULL, 0))
        return false;
    job->page_open = false;
    return true;
}

bool print_submit(bk_print_job_t *job) {
    char temporary[40];
    char queued[40];
    bool ok = false;
    if (!job || job->failed) {
        print_cancel(job);
        return false;
    }
    if (job->page_open && !print_end_page(job)) {
        print_cancel(job);
        return false;
    }
    if (!job->page_count || !job->command_count) {
        print_cancel(job);
        return false;
    }
    put_u32(job->data + 8, job->size);
    put_u32(job->data + 12, 0U);
    put_u32(job->data + 16, job->page_count);
    put_u32(job->data + 20, job->command_count);
    (void)bk_file_mkdir("/TEMP");
    (void)bk_file_mkdir("/TEMP/SPOOL");
    make_job_path(temporary, "TMP", job->sequence);
    make_job_path(queued, "BPJ", job->sequence);
    if (bk_file_write_all(temporary, job->data, job->size)) {
        if (bk_file_rename(temporary, queued)) ok = true;
        else (void)bk_file_remove(temporary);
    }
    print_cancel(job);
    return ok;
}

void print_cancel(bk_print_job_t *job) {
    if (!job) return;
    if (job->data) bk_sys_free(job->data);
    bk_sys_free(job);
}
