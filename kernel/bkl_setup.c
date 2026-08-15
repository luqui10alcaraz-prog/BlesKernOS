#include "include/bkl_setup.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/vga.h"
#include "include/keyboard.h"
#include "include/pic.h"
#include "include/pit.h"
#include "include/types.h"
#include "stdio.h"
#include "string.h"

#define BKL_PATH_MAX 260U
#define BKL_IO_BUFFER 4096U
#define BKL_MAX_PARTS 256U
#define VGA_TEXT_MEMORY ((volatile uint16_t *)0x000B8000U)
#define VGA_WIDTH 80U
#define VGA_HEIGHT 25U

#define BKL_REC_END 0U
#define BKL_REC_DIR 1U
#define BKL_REC_FILE 2U

#define BKL_METHOD_STORE 0U
#define BKL_METHOD_LZSS 1U
#define BKL_LZ_WINDOW 4096U
#define BKL_LZ_MIN_MATCH 3U
#define BKL_DEBUG_PROGRESS_BYTES (256U * 1024U)
#define BKL_DEBUG_HEARTBEAT_MS 2000U
#define BKL_UI_PROGRESS_BYTES (16U * 1024U)

typedef struct {
    uint32_t part_count;
    uint32_t part_index;
    int fd;
    uint32_t part_offset;
    const char *single_path;
    uint32_t total_read;
} bkl_stream_t;

static uint16_t bkl_cell(char c, uint8_t attr) {
    return (uint16_t)(((uint16_t)attr << 8) | (uint8_t)c);
}

static void bkl_screen_clear(void) {
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_TEXT_MEMORY[i] = bkl_cell(' ', 0x1FU);
}

static void bkl_screen_text(uint32_t x, uint32_t y, const char *text,
                            uint8_t attr) {
    if (!text || y >= VGA_HEIGHT) return;
    while (*text && x < VGA_WIDTH)
        VGA_TEXT_MEMORY[y * VGA_WIDTH + x++] = bkl_cell(*text++, attr);
}

static void bkl_screen_progress(uint32_t current, uint32_t total,
                                const char *path) {
    char line[80];
    uint32_t fill = total ? (current * 50U) / total : 0U;
    if (fill > 50U) fill = 50U;
    bkl_screen_clear();
    bkl_screen_text(19, 2, "BlesKernOS - finalizando instalacion", 0x1FU);
    bkl_screen_text(13, 6, "Descomprimiendo el sistema. No apague el equipo.", 0x1FU);
    bkl_screen_text(14, 10, "[", 0x1FU);
    for (uint32_t i = 0; i < 50U; i++)
        bkl_screen_text(15U + i, 10, i < fill ? "#" : ".", 0x1FU);
    bkl_screen_text(65, 10, "]", 0x1FU);
    snprintf(line, sizeof(line), "Archivo %u de %u", current, total);
    bkl_screen_text(30, 13, line, 0x1FU);
    if (path) bkl_screen_text(4, 17, path, 0x1FU);
    vga_set_cursor(79, 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool stream_open_part(bkl_stream_t *stream, uint32_t index) {
    char path[64];
    const char *open_path;
    if (!stream || index >= stream->part_count) {
        kprintf("[SETUP:BKL3] stream_open_part invalid index=%u count=%u\n",
                index, stream ? stream->part_count : 0U);
        return false;
    }
    if (stream->fd >= 0) vfs_close(stream->fd);
    if (stream->single_path) {
        if (index != 0U) return false;
        open_path = stream->single_path;
    } else {
        snprintf(path, sizeof(path), "/SETUP/PART%03u.BKL", index);
        open_path = path;
    }
    kprintf("[SETUP:BKL3] opening part %u/%u: %s\n",
            index + 1U, stream->part_count, open_path);
    stream->fd = vfs_open(open_path, VFS_O_RDONLY);
    stream->part_index = index;
    stream->part_offset = 0U;
    if (stream->fd < 0) {
        kprintf("[SETUP:BKL3] ERROR open failed: %s\n", open_path);
        return false;
    }
    return true;
}

static bool stream_read(bkl_stream_t *stream, void *buffer, uint32_t size) {
    uint8_t *out = (uint8_t *)buffer;
    uint32_t done = 0U;
    if (!stream || (!buffer && size)) return false;
    while (done < size) {
        int got;
        if (stream->fd < 0 && !stream_open_part(stream, stream->part_index))
            return false;
        got = vfs_read(stream->fd, out + done, size - done);
        if (got < 0) {
            kprintf("[SETUP:BKL3] ERROR read part=%u offset=%u request=%u\n",
                    stream->part_index, stream->part_offset, size - done);
            return false;
        }
        if (got == 0) {
            if (stream->part_index + 1U >= stream->part_count) {
                kprintf("[SETUP:BKL3] ERROR unexpected end of stream after %u bytes\n",
                        stream->total_read);
                return false;
            }
            if (!stream_open_part(stream, stream->part_index + 1U)) return false;
            continue;
        }
        done += (uint32_t)got;
        stream->part_offset += (uint32_t)got;
        stream->total_read += (uint32_t)got;
    }
    return true;
}

static bool stream_u8(bkl_stream_t *stream, uint8_t *value) {
    return stream_read(stream, value, 1U);
}

static bool stream_u16(bkl_stream_t *stream, uint16_t *value) {
    uint8_t data[2];
    if (!stream_read(stream, data, sizeof(data))) return false;
    *value = get16(data);
    return true;
}

static bool stream_u32(bkl_stream_t *stream, uint32_t *value) {
    uint8_t data[4];
    if (!stream_read(stream, data, sizeof(data))) return false;
    *value = get32(data);
    return true;
}

static uint32_t g_bkl_crc32_table[256];
static bool g_bkl_crc32_ready;

static void bkl_crc32_init(void) {
    if (g_bkl_crc32_ready) return;
    for (uint32_t i = 0; i < 256U; i++) {
        uint32_t crc = i;
        for (uint32_t bit = 0; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U &
                  (uint32_t)(-(int32_t)(crc & 1U)));
        g_bkl_crc32_table[i] = crc;
    }
    g_bkl_crc32_ready = true;
}

static uint32_t bkl_crc32_byte(uint32_t crc, uint8_t value) {
    return g_bkl_crc32_table[(crc ^ value) & 0xFFU] ^ (crc >> 8);
}

static uint32_t bkl_elapsed_ms(uint32_t start_tick) {
    uint32_t hz = pit_get_frequency_hz();
    uint32_t elapsed = pit_get_ticks() - start_tick;
    return hz ? (uint32_t)(((uint64_t)elapsed * 1000U) / hz) : elapsed;
}

static const char *bkl_method_name(uint8_t method) {
    if (method == BKL_METHOD_STORE) return "STORE";
    if (method == BKL_METHOD_LZSS) return "LZSS";
    return "UNKNOWN";
}

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint8_t window[BKL_LZ_WINDOW];
    uint32_t window_pos;
    uint32_t produced;
    uint32_t crc;
    bkl_stream_t *stream;
    const char *debug_path;
    uint32_t debug_original_size;
    uint32_t debug_encoded_size;
    uint32_t debug_file_index;
    uint32_t debug_file_total;
    uint32_t debug_start_tick;
    uint32_t debug_last_tick;
    uint32_t debug_next_bytes;
    uint32_t ui_next_bytes;
} bkl_writer_t;

static bkl_writer_t g_bkl_writer;

static volatile uint32_t g_bkl_progress_sequence;
static bkl_setup_progress_t g_bkl_progress;

static void bkl_progress_publish(uint32_t active, bkl_setup_phase_t phase,
                                 uint32_t file_index, uint32_t file_total,
                                 uint32_t bytes_done, uint32_t bytes_total,
                                 const bkl_stream_t *stream,
                                 const char *path) {
    g_bkl_progress_sequence++;
    __asm__ volatile ("" ::: "memory");
    g_bkl_progress.active = active;
    g_bkl_progress.phase = (uint32_t)phase;
    g_bkl_progress.file_index = file_index;
    g_bkl_progress.file_total = file_total;
    g_bkl_progress.file_bytes_done = bytes_done;
    g_bkl_progress.file_bytes_total = bytes_total;
    g_bkl_progress.stream_bytes_read = stream ? stream->total_read : 0U;
    g_bkl_progress.part_index = stream ? stream->part_index + 1U : 0U;
    g_bkl_progress.part_total = stream ? stream->part_count : 0U;
    if (path) {
        kstrncpy(g_bkl_progress.path, path,
                 sizeof(g_bkl_progress.path) - 1U);
        g_bkl_progress.path[sizeof(g_bkl_progress.path) - 1U] = '\0';
    } else {
        g_bkl_progress.path[0] = '\0';
    }
    __asm__ volatile ("" ::: "memory");
    g_bkl_progress_sequence++;
    g_bkl_progress.generation = g_bkl_progress_sequence;
}

bool bkl_setup_get_progress(bkl_setup_progress_t *progress) {
    uint32_t before;
    uint32_t after;
    if (!progress) return false;
    for (uint32_t retry = 0U; retry < 16U; retry++) {
        before = g_bkl_progress_sequence;
        if (before & 1U) continue;
        __asm__ volatile ("" ::: "memory");
        kmemcpy(progress, &g_bkl_progress, sizeof(*progress));
        __asm__ volatile ("" ::: "memory");
        after = g_bkl_progress_sequence;
        if (before == after && !(after & 1U)) {
            progress->generation = after;
            return true;
        }
    }
    return false;
}

static bool bkl_debug_report_due(const bkl_writer_t *writer) {
    uint32_t hz;
    if (!writer) return false;
    if (writer->produced >= writer->debug_next_bytes) return true;
    hz = pit_get_frequency_hz();
    return hz && (uint32_t)(pit_get_ticks() - writer->debug_last_tick) >=
                 (uint32_t)(((uint64_t)hz * BKL_DEBUG_HEARTBEAT_MS) / 1000U);
}

static void bkl_debug_progress(bkl_writer_t *writer, bool force) {
    uint32_t percent;
    if (!writer || (!force && !bkl_debug_report_due(writer))) return;
    percent = writer->debug_original_size
        ? (uint32_t)(((uint64_t)writer->produced * 100U) /
                     writer->debug_original_size)
        : 100U;
    if (percent > 100U) percent = 100U;
    kprintf("[SETUP:BKL3] decode file=%u/%u %u%% out=%u/%u "
            "part=%u/%u offset=%u stream=%u elapsed=%ums path=%s\n",
            writer->debug_file_index, writer->debug_file_total, percent,
            writer->produced, writer->debug_original_size,
            writer->stream ? writer->stream->part_index + 1U : 0U,
            writer->stream ? writer->stream->part_count : 0U,
            writer->stream ? writer->stream->part_offset : 0U,
            writer->stream ? writer->stream->total_read : 0U,
            bkl_elapsed_ms(writer->debug_start_tick),
            writer->debug_path ? writer->debug_path : "(unknown)");
    bkl_progress_publish(1U, BKL_SETUP_PHASE_DECODING,
                         writer->debug_file_index, writer->debug_file_total,
                         writer->produced, writer->debug_original_size,
                         writer->stream, writer->debug_path);
    writer->debug_last_tick = pit_get_ticks();
    writer->debug_next_bytes = writer->produced + BKL_DEBUG_PROGRESS_BYTES;
}

static bool bkl_writer_byte(bkl_writer_t *writer, uint8_t value) {
    if (!writer || writer->produced >= writer->capacity ||
        (writer->capacity && !writer->data))
        return false;
    if (writer->capacity) writer->data[writer->produced] = value;
    writer->window[writer->window_pos] = value;
    writer->window_pos = (writer->window_pos + 1U) & (BKL_LZ_WINDOW - 1U);
    writer->produced++;
    writer->crc = bkl_crc32_byte(writer->crc, value);
    if (writer->produced >= writer->ui_next_bytes) {
        bkl_progress_publish(1U, BKL_SETUP_PHASE_DECODING,
                             writer->debug_file_index,
                             writer->debug_file_total,
                             writer->produced,
                             writer->debug_original_size,
                             writer->stream,
                             writer->debug_path);
        writer->ui_next_bytes = writer->produced + BKL_UI_PROGRESS_BYTES;
    }
    return true;
}

static bool bkl_decode_store(bkl_stream_t *stream, bkl_writer_t *writer,
                             uint32_t encoded_size, uint32_t original_size) {
    uint8_t buffer[BKL_IO_BUFFER];
    uint32_t remaining = encoded_size;
    if (encoded_size != original_size) return false;
    while (remaining) {
        uint32_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        if (!stream_read(stream, buffer, chunk)) return false;
        for (uint32_t i = 0; i < chunk; i++)
            if (!bkl_writer_byte(writer, buffer[i])) return false;
        remaining -= chunk;
        if (bkl_debug_report_due(writer)) bkl_debug_progress(writer, true);
    }
    return true;
}

static bool bkl_decode_lzss(bkl_stream_t *stream, bkl_writer_t *writer,
                            uint32_t encoded_size, uint32_t original_size) {
    uint32_t consumed = 0U;
    while (consumed < encoded_size && writer->produced < original_size) {
        uint8_t flags;
        if (!stream_u8(stream, &flags)) return false;
        consumed++;
        for (uint32_t bit = 0; bit < 8U && writer->produced < original_size; bit++) {
            if (flags & (1U << bit)) {
                uint8_t literal;
                if (consumed >= encoded_size || !stream_u8(stream, &literal)) return false;
                consumed++;
                if (!bkl_writer_byte(writer, literal)) return false;
            } else {
                uint8_t token_bytes[2];
                uint16_t token;
                uint32_t distance;
                uint32_t length;
                if (consumed + 2U > encoded_size ||
                    !stream_read(stream, token_bytes, sizeof(token_bytes))) return false;
                consumed += 2U;
                token = get16(token_bytes);
                distance = ((uint32_t)token >> 4) + 1U;
                length = ((uint32_t)token & 0x0FU) + BKL_LZ_MIN_MATCH;
                if (!distance || distance > BKL_LZ_WINDOW ||
                    distance > writer->produced ||
                    writer->produced + length > original_size) return false;
                for (uint32_t i = 0; i < length; i++) {
                    uint32_t source = (writer->window_pos + BKL_LZ_WINDOW - distance) &
                                      (BKL_LZ_WINDOW - 1U);
                    uint8_t value = writer->window[source];
                    if (!bkl_writer_byte(writer, value)) return false;
                }
            }
        }
        if (bkl_debug_report_due(writer)) bkl_debug_progress(writer, true);
    }
    return consumed == encoded_size && writer->produced == original_size;
}

static bool bkl_decode_file(bkl_stream_t *stream, const char *path,
                            uint8_t method, uint32_t original_size,
                            uint32_t encoded_size, uint32_t expected_crc,
                            uint32_t file_index, uint32_t file_total) {
    bkl_writer_t *writer = &g_bkl_writer;
    bool ok;
    uint32_t actual_crc;
    uint32_t write_start;
    uint32_t start_tick = pit_get_ticks();
    kmemset(writer, 0, sizeof(*writer));
    writer->capacity = original_size;
    writer->stream = stream;
    writer->debug_path = path;
    writer->debug_original_size = original_size;
    writer->debug_encoded_size = encoded_size;
    writer->debug_file_index = file_index;
    writer->debug_file_total = file_total;
    writer->debug_start_tick = start_tick;
    writer->debug_last_tick = start_tick;
    writer->debug_next_bytes = BKL_DEBUG_PROGRESS_BYTES;
    writer->ui_next_bytes = BKL_UI_PROGRESS_BYTES;
    writer->crc = 0xFFFFFFFFU;
    if (original_size) {
        writer->data = (uint8_t *)kmalloc(original_size);
        if (!writer->data) {
            kprintf("[SETUP:BKL3] ERROR no memory raw=%u path=%s\n",
                    original_size, path);
            return false;
        }
    }

    kprintf("[SETUP:BKL3] file begin %u/%u method=%s raw=%u packed=%u "
            "crc=%x path=%s\n", file_index, file_total,
            bkl_method_name(method), original_size, encoded_size,
            expected_crc, path);
    bkl_progress_publish(1U, BKL_SETUP_PHASE_DECODING,
                         file_index, file_total, 0U, original_size,
                         stream, path);

    if (method == BKL_METHOD_STORE)
        ok = bkl_decode_store(stream, writer, encoded_size, original_size);
    else if (method == BKL_METHOD_LZSS)
        ok = bkl_decode_lzss(stream, writer, encoded_size, original_size);
    else {
        kprintf("[SETUP:BKL3] ERROR invalid method=%u path=%s\n", method, path);
        ok = false;
    }

    if (!ok || writer->produced != original_size) {
        kprintf("[SETUP:BKL3] ERROR decode/size got=%u expected=%u path=%s\n",
                writer->produced, original_size, path);
        if (writer->data) kfree(writer->data);
        writer->data = NULL;
        return false;
    }
    bkl_progress_publish(1U, BKL_SETUP_PHASE_VERIFYING,
                         file_index, file_total, original_size, original_size,
                         stream, path);
    actual_crc = writer->crc ^ 0xFFFFFFFFU;
    if (actual_crc != expected_crc) {
        kprintf("[SETUP:BKL3] ERROR CRC mismatch got=%x expected=%x path=%s\n",
                actual_crc, expected_crc, path);
        if (writer->data) kfree(writer->data);
        writer->data = NULL;
        return false;
    }
    bkl_debug_progress(writer, true);

    bkl_progress_publish(1U, BKL_SETUP_PHASE_COMMITTING,
                         file_index, file_total, original_size, original_size,
                         stream, path);
    write_start = pit_get_ticks();
    kprintf("[SETUP:BKL3] commit begin bytes=%u fat-cache=%u path=%s\n",
            original_size, fat_bulk_write_is_active() ? 1U : 0U, path);
    if (!vfs_write_all(path, writer->data, original_size)) {
        kprintf("[SETUP:BKL3] ERROR commit failed reason=%s path=%s\n",
                vfs_last_error_text(), path);
        if (writer->data) kfree(writer->data);
        writer->data = NULL;
        return false;
    }
    kprintf("[SETUP:BKL3] commit done bytes=%u elapsed=%ums path=%s\n",
            original_size, bkl_elapsed_ms(write_start), path);
    if (writer->data) kfree(writer->data);
    writer->data = NULL;
    kprintf("[SETUP:BKL3] file done %u/%u raw=%u packed=%u crc=%x "
            "elapsed=%ums path=%s\n", file_index, file_total,
            original_size, encoded_size, actual_crc,
            bkl_elapsed_ms(start_tick), path);
    bkl_progress_publish(1U, BKL_SETUP_PHASE_FILE_DONE,
                         file_index, file_total, original_size, original_size,
                         stream, path);
    return true;
}

bool bkl_setup_pending(void) {
    vfs_dir_entry_t entry;
    return vfs_stat("/SETUP/PENDING.DAT", &entry) &&
           entry.type == VFS_NODE_FILE;
}

static void bkl_cleanup(uint32_t part_count) {
    char path[64];
    vfs_remove("/SETUP/PENDING.DAT");
    for (uint32_t i = 0; i < part_count; i++) {
        snprintf(path, sizeof(path), "/SETUP/PART%03u.BKL", i);
        vfs_remove(path);
    }
    vfs_remove("/SETUP");
}

static bool bkl_extract_stream(bkl_stream_t *stream, bool text_progress) {
    uint8_t header[16];
    uint32_t file_total;
    uint32_t file_done = 0U;
    uint32_t dir_done = 0U;
    uint32_t start_tick = pit_get_ticks();

    bkl_progress_publish(1U, BKL_SETUP_PHASE_READING_HEADER,
                         0U, 0U, 0U, 0U, stream,
                         stream ? stream->single_path : NULL);
    if (!stream || !stream_read(stream, header, sizeof(header))) {
        kprintf("[SETUP:BKL3] ERROR could not read package header\n");
        return false;
    }
    if (header[0] != 'B' || header[1] != 'K' || header[2] != 'L' ||
        header[3] != '3' || get32(header + 4U) != 1U) {
        kprintf("[SETUP:BKL3] ERROR invalid header magic=%c%c%c%c version=%u\n",
                header[0], header[1], header[2], header[3], get32(header + 4U));
        return false;
    }
    file_total = get32(header + 8U);
    kprintf("[SETUP:BKL3] package header ok files=%u flags=%x parts=%u\n",
            file_total, get32(header + 12U), stream->part_count);
    bkl_progress_publish(1U, BKL_SETUP_PHASE_READING_HEADER,
                         0U, file_total, 0U, 0U, stream,
                         stream->single_path);

    if (text_progress)
        bkl_screen_progress(0U, file_total, "Leyendo paquete BKL3...");
    for (;;) {
        uint8_t type;
        uint16_t path_length = 0U;
        char path[BKL_PATH_MAX];
        if (!stream_u8(stream, &type)) {
            kprintf("[SETUP:BKL3] ERROR reading record type after file=%u\n", file_done);
            return false;
        }
        if (type == BKL_REC_END) break;
        if (!stream_u16(stream, &path_length) || !path_length ||
            path_length >= sizeof(path) ||
            !stream_read(stream, path, path_length)) {
            kprintf("[SETUP:BKL3] ERROR invalid record path length=%u\n", path_length);
            return false;
        }
        path[path_length] = '\0';
        if (path[0] != '/') {
            kprintf("[SETUP:BKL3] ERROR non-absolute path=%s\n", path);
            return false;
        }
        if (type == BKL_REC_DIR) {
            vfs_dir_entry_t entry;
            dir_done++;
            if (!vfs_stat(path, &entry) && !vfs_mkdir(path)) {
                kprintf("[SETUP:BKL3] ERROR mkdir failed path=%s\n", path);
                return false;
            }
        } else if (type == BKL_REC_FILE) {
            uint8_t method;
            uint32_t original_size;
            uint32_t encoded_size;
            uint32_t expected_crc;
            if (!stream_u8(stream, &method) ||
                !stream_u32(stream, &original_size) ||
                !stream_u32(stream, &encoded_size) ||
                !stream_u32(stream, &expected_crc)) {
                kprintf("[SETUP:BKL3] ERROR truncated metadata path=%s\n", path);
                return false;
            }
            file_done++;
            if (text_progress)
                bkl_screen_progress(file_done, file_total, path);
            if (!bkl_decode_file(stream, path, method, original_size,
                                 encoded_size, expected_crc,
                                 file_done, file_total)) return false;
        } else {
            kprintf("[SETUP:BKL3] ERROR unknown record type=%u path=%s\n", type, path);
            return false;
        }
    }
    if (file_done != file_total) {
        kprintf("[SETUP:BKL3] ERROR file count mismatch got=%u expected=%u\n",
                file_done, file_total);
        return false;
    }
    kprintf("[SETUP:BKL3] package done files=%u dirs=%u bytes-read=%u elapsed=%ums\n",
            file_done, dir_done, stream->total_read, bkl_elapsed_ms(start_tick));
    bkl_progress_publish(1U, BKL_SETUP_PHASE_PACKAGE_DONE,
                         file_done, file_total, 0U, 0U, stream,
                         stream->single_path);
    return true;
}

bool bkl_setup_extract_package(const char *path) {
    bkl_stream_t stream;
    bool ok;
    uint32_t start_tick = pit_get_ticks();
    if (!path || path[0] != '/') {
        kprintf("[SETUP:BKL3] ERROR invalid package path\n");
        return false;
    }
    bkl_crc32_init();
    bkl_progress_publish(1U, BKL_SETUP_PHASE_OPENING,
                         0U, 0U, 0U, 0U, NULL, path);
    kprintf("[SETUP:BKL3] extract request path=%s\n", path);
    kmemset(&stream, 0, sizeof(stream));
    stream.part_count = 1U;
    stream.fd = -1;
    stream.single_path = path;
    if (!stream_open_part(&stream, 0U)) return false;
    if (!vfs_bulk_write_begin("/")) {
        kprintf("[SETUP:BKL3] ERROR could not start FAT bulk cache\n");
        vfs_close(stream.fd);
        return false;
    }
    ok = bkl_extract_stream(&stream, false);
    if (stream.fd >= 0) vfs_close(stream.fd);
    vfs_bulk_write_end();
    kprintf("[SETUP:BKL3] extract %s path=%s bytes-read=%u elapsed=%ums\n",
            ok ? "SUCCESS" : "FAILED", path, stream.total_read,
            bkl_elapsed_ms(start_tick));
    bkl_progress_publish(ok ? 0U : 1U,
                         ok ? BKL_SETUP_PHASE_PACKAGE_DONE : BKL_SETUP_PHASE_ERROR,
                         g_bkl_progress.file_index, g_bkl_progress.file_total,
                         g_bkl_progress.file_bytes_done,
                         g_bkl_progress.file_bytes_total,
                         &stream, path);
    return ok;
}

bool bkl_setup_finalize(void) {
    uint8_t pending[16];
    uint32_t pending_size = 0U;
    void *raw = NULL;
    bkl_stream_t stream;
    bool ok;

    bkl_crc32_init();
    kprintf("[SETUP:BKL3] finalize multi-floppy setup\n");
    if (!vfs_read_all("/SETUP/PENDING.DAT", &raw, &pending_size) ||
        pending_size < 8U) {
        kprintf("[SETUP:BKL3] ERROR missing or invalid /SETUP/PENDING.DAT\n");
        return false;
    }
    kmemcpy(pending, raw, pending_size > sizeof(pending) ? sizeof(pending) : pending_size);
    kfree(raw);
    if (pending[0] != 'B' || pending[1] != 'K' || pending[2] != 'P' ||
        pending[3] != '1') return false;

    kmemset(&stream, 0, sizeof(stream));
    stream.part_count = get32(pending + 4U);
    stream.fd = -1;
    if (!stream.part_count || stream.part_count > BKL_MAX_PARTS ||
        !stream_open_part(&stream, 0U)) return false;
    if (!vfs_bulk_write_begin("/")) {
        vfs_close(stream.fd);
        return false;
    }
    ok = bkl_extract_stream(&stream, true);
    if (stream.fd >= 0) vfs_close(stream.fd);
    vfs_bulk_write_end();
    if (!ok) return false;

    bkl_cleanup(stream.part_count);
    bkl_screen_clear();
    bkl_screen_text(22, 8, "Instalacion finalizada correctamente.", 0x1FU);
    bkl_screen_text(25, 11, "Reiniciando BlesKernOS...", 0x1FU);
    for (uint32_t wait = 0; wait < 300000U; wait++) io_wait();
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("hlt");
}
