#include "../stdio.h"
#include "../stdlib.h"
#include "../string.h"
#include "../ctype.h"
#include "../errno.h"
#include "../limits.h"
#include "../include/vfs.h"
#include "../include/vga.h"

typedef enum {
    FILE_KIND_CONSOLE = 1,
    FILE_KIND_VFS = 2,
} file_kind_t;

struct FILE {
    file_kind_t kind;
    bool readable;
    bool writable;
    bool append;
    bool eof;
    bool error;
    bool dirty;
    bool dynamic;
    int fd;
    char path[VFS_MAX_PATH];
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t position;
};

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t total;
} format_buffer_t;

static FILE g_stdin = {FILE_KIND_CONSOLE, true, false, false, false, false, false, false, 0, {0}, NULL, 0, 0, 0};
static FILE g_stdout = {FILE_KIND_CONSOLE, false, true, false, false, false, false, false, 1, {0}, NULL, 0, 0, 0};
static FILE g_stderr = {FILE_KIND_CONSOLE, false, true, false, false, false, false, false, 2, {0}, NULL, 0, 0, 0};

FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

static bool file_ensure_capacity(FILE *stream, size_t wanted) {
    uint8_t *grown;
    size_t new_capacity;

    if (wanted <= stream->capacity) return true;
    new_capacity = stream->capacity ? stream->capacity : 256;
    while (new_capacity < wanted) {
        if (new_capacity > UINT_MAX / 2U) {
            new_capacity = wanted;
            break;
        }
        new_capacity *= 2U;
    }
    grown = (uint8_t *)realloc(stream->data, new_capacity);
    if (!grown) {
        stream->error = true;
        errno = ENOMEM;
        return false;
    }
    stream->data = grown;
    stream->capacity = new_capacity;
    return true;
}

static void format_putc(format_buffer_t *out, char c) {
    if (out->capacity && out->length + 1 < out->capacity) {
        out->buffer[out->length] = c;
    }
    out->length++;
    out->total++;
}

static void format_puts(format_buffer_t *out, const char *text, int width,
                        int precision, bool left_align) {
    int len = 0;
    int pad;

    if (!text) text = "(null)";
    while (text[len] && (precision < 0 || len < precision)) len++;
    pad = width > len ? width - len : 0;

    if (!left_align) while (pad-- > 0) format_putc(out, ' ');
    for (int i = 0; i < len; i++) format_putc(out, text[i]);
    if (left_align) while (pad-- > 0) format_putc(out, ' ');
}

static int uint_to_text(unsigned long value, unsigned int base,
                        bool upper, char *buffer, int buffer_size) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = upper ? digits_upper : digits_lower;
    int length = 0;

    if (buffer_size <= 0) return 0;
    if (value == 0) {
        buffer[length++] = '0';
    } else {
        while (value && length < buffer_size) {
            buffer[length++] = digits[value % base];
            value /= base;
        }
    }
    for (int i = 0; i < length / 2; i++) {
        char tmp = buffer[i];
        buffer[i] = buffer[length - 1 - i];
        buffer[length - 1 - i] = tmp;
    }
    return length;
}

static void format_number(format_buffer_t *out, unsigned long value,
                          bool negative, unsigned int base, bool upper,
                          int width, int precision, bool left_align,
                          bool zero_pad, const char *prefix) {
    char digits[32];
    int digit_count;
    int prefix_len = prefix ? (int)strlen(prefix) : 0;
    int zeros = 0;
    int total_len;
    int pad;

    digit_count = uint_to_text(value, base, upper, digits, (int)sizeof(digits));
    if (precision == 0 && value == 0) digit_count = 0;
    if (precision > digit_count) zeros = precision - digit_count;
    if (zero_pad && !left_align && precision < 0) {
        int content = digit_count + zeros + prefix_len + (negative ? 1 : 0);
        if (width > content) zeros += width - content;
    }

    total_len = digit_count + zeros + prefix_len + (negative ? 1 : 0);
    pad = width > total_len ? width - total_len : 0;

    if (!left_align) while (pad-- > 0) format_putc(out, ' ');
    if (negative) format_putc(out, '-');
    while (prefix_len-- > 0) format_putc(out, *prefix++);
    while (zeros-- > 0) format_putc(out, '0');
    for (int i = 0; i < digit_count; i++) format_putc(out, digits[i]);
    if (left_align) while (pad-- > 0) format_putc(out, ' ');
}

static void format_float(format_buffer_t *out, double value, int width,
                         int precision, bool left_align, bool zero_pad) {
    unsigned long integer;
    double frac;
    unsigned long scale = 1;
    unsigned long frac_part;
    bool negative = false;
    char intbuf[32];
    char fracbuf[32];
    char text[96];
    int intlen;
    int index = 0;

    if (precision < 0) precision = 6;
    if (value < 0.0) {
        negative = true;
        value = -value;
    }

    integer = (unsigned long)value;
    frac = value - (double)integer;
    for (int i = 0; i < precision; i++) scale *= 10UL;
    frac_part = (unsigned long)(frac * (double)scale + 0.5);
    if (frac_part >= scale) {
        integer++;
        frac_part -= scale;
    }

    intlen = uint_to_text(integer, 10U, false, intbuf, (int)sizeof(intbuf));
    if (negative) text[index++] = '-';
    for (int i = 0; i < intlen; i++) text[index++] = intbuf[i];
    if (precision > 0) {
        text[index++] = '.';
        for (int i = precision - 1; i >= 0; i--) {
            fracbuf[i] = (char)('0' + (frac_part % 10UL));
            frac_part /= 10UL;
        }
        for (int i = 0; i < precision; i++) text[index++] = fracbuf[i];
    }
    text[index] = '\0';

    if (zero_pad && !left_align && width > index) {
        char padded[96];
        int pos = 0;
        int zeros = width - index;
        if (negative) {
            padded[pos++] = '-';
            for (int i = 0; i < zeros; i++) padded[pos++] = '0';
            for (int i = 1; text[i]; i++) padded[pos++] = text[i];
        } else {
            for (int i = 0; i < zeros; i++) padded[pos++] = '0';
            for (int i = 0; text[i]; i++) padded[pos++] = text[i];
        }
        padded[pos] = '\0';
        format_puts(out, padded, width, -1, left_align);
        return;
    }

    format_puts(out, text, width, -1, left_align);
}

static int format_vprintf_to_buffer(char *buffer, size_t size,
                                    const char *fmt, va_list args) {
    format_buffer_t out = {buffer, size, 0, 0};

    while (*fmt) {
        bool left_align = false;
        bool zero_pad = false;
        int width = 0;
        int precision = -1;

        if (*fmt != '%') {
            format_putc(&out, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            format_putc(&out, *fmt++);
            continue;
        }

        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_align = true;
            if (*fmt == '0') zero_pad = true;
            fmt++;
        }
        while (isdigit((unsigned char)*fmt)) {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == '.') {
            precision = 0;
            fmt++;
            while (isdigit((unsigned char)*fmt)) {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't') fmt++;

        switch (*fmt) {
            case 'd':
            case 'i': {
                long value = va_arg(args, int);
                bool negative = value < 0;
                unsigned long uvalue = negative ? (unsigned long)(-value) : (unsigned long)value;
                format_number(&out, uvalue, negative, 10U, false,
                              width, precision, left_align, zero_pad, NULL);
                break;
            }
            case 'u': {
                unsigned long value = va_arg(args, unsigned int);
                format_number(&out, value, false, 10U, false,
                              width, precision, left_align, zero_pad, NULL);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long value = va_arg(args, unsigned int);
                format_number(&out, value, false, 16U, *fmt == 'X',
                              width, precision, left_align, zero_pad, NULL);
                break;
            }
            case 'p': {
                uintptr_t value = (uintptr_t)va_arg(args, void *);
                format_number(&out, (unsigned long)value, false, 16U, false,
                              width ? width : 10, precision, left_align, true, "0x");
                break;
            }
            case 'c': {
                char text[2];
                text[0] = (char)va_arg(args, int);
                text[1] = '\0';
                format_puts(&out, text, width, 1, left_align);
                break;
            }
            case 's': {
                const char *text = va_arg(args, const char *);
                format_puts(&out, text, width, precision, left_align);
                break;
            }
            case 'f': {
                double value = va_arg(args, double);
                format_float(&out, value, width, precision, left_align, zero_pad);
                break;
            }
            default:
                format_putc(&out, *fmt);
                break;
        }

        if (*fmt) fmt++;
    }

    if (size) {
        size_t term = out.length < size ? out.length : size - 1;
        buffer[term] = '\0';
    }

    return (int)out.total;
}

static int file_write_text(FILE *stream, const char *text, size_t length) {
    if (!stream || !text) return -1;

    if (stream->kind == FILE_KIND_CONSOLE) {
        for (size_t i = 0; i < length; i++) vga_putchar(text[i]);
        return (int)length;
    }

    if (!stream->writable) {
        stream->error = true;
        errno = EACCES;
        return -1;
    }

    if (stream->append) stream->position = stream->size;
    if (!file_ensure_capacity(stream, stream->position + length + 1)) {
        return -1;
    }
    memcpy(stream->data + stream->position, text, length);
    stream->position += length;
    if (stream->position > stream->size) stream->size = stream->position;
    stream->data[stream->size] = '\0';
    stream->dirty = true;
    return (int)length;
}

static bool parse_mode(const char *mode, bool *readable, bool *writable,
                       bool *append, bool *truncate) {
    *readable = false;
    *writable = false;
    *append = false;
    *truncate = false;
    if (!mode || !*mode) return false;

    switch (*mode) {
        case 'r':
            *readable = true;
            break;
        case 'w':
            *writable = true;
            *truncate = true;
            break;
        case 'a':
            *writable = true;
            *append = true;
            break;
        default:
            return false;
    }

    while (*mode) {
        if (*mode == '+') {
            *readable = true;
            *writable = true;
        }
        mode++;
    }
    return true;
}

FILE *fopen(const char *path, const char *mode) {
    FILE *stream;
    bool readable, writable, append, truncate;
    void *buffer = NULL;
    uint32_t size = 0;

    if (!path || !parse_mode(mode, &readable, &writable, &append, &truncate)) {
        errno = EINVAL;
        return NULL;
    }

    stream = (FILE *)calloc(1, sizeof(FILE));
    if (!stream) return NULL;
    stream->kind = FILE_KIND_VFS;
    stream->readable = readable;
    stream->writable = writable;
    stream->append = append;
    stream->dynamic = true;
    strncpy(stream->path, path, sizeof(stream->path) - 1);

    if (!truncate && vfs_read_all(path, &buffer, &size)) {
        stream->data = (uint8_t *)buffer;
        stream->size = size;
        stream->capacity = size + 1;
        stream->position = append ? size : 0;
        return stream;
    }

    if (readable && !writable) {
        free(stream);
        errno = ENOENT;
        return NULL;
    }

    stream->data = (uint8_t *)malloc(1);
    if (!stream->data) {
        free(stream);
        errno = ENOMEM;
        return NULL;
    }
    stream->data[0] = '\0';
    stream->capacity = 1;
    stream->size = 0;
    stream->position = 0;
    stream->dirty = writable;
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (stream->kind == FILE_KIND_VFS) {
        if (fflush(stream) == EOF) return EOF;
        if (stream->dynamic) {
            free(stream->data);
            free(stream);
        }
    }
    return 0;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
    size_t wanted;
    size_t available;
    size_t got;

    if (!stream || !buffer || size == 0 || count == 0 || !stream->readable) {
        errno = EINVAL;
        return 0;
    }
    if (stream->kind != FILE_KIND_VFS) return 0;

    wanted = size * count;
    available = stream->position < stream->size ? stream->size - stream->position : 0;
    got = wanted < available ? wanted : available;
    if (got) memcpy(buffer, stream->data + stream->position, got);
    stream->position += got;
    stream->eof = got < wanted;
    return got / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
    size_t total = size * count;
    if (file_write_text(stream, (const char *)buffer, total) < 0) return 0;
    return count;
}

int fflush(FILE *stream) {
    if (!stream) return 0;
    if (stream->kind == FILE_KIND_CONSOLE) return 0;
    if (!stream->dirty) return 0;
    if (!vfs_write_all(stream->path, stream->data, (uint32_t)stream->size)) {
        stream->error = true;
        errno = EIO;
        return EOF;
    }
    stream->dirty = false;
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    size_t base = 0;
    long target;

    if (!stream || stream->kind != FILE_KIND_VFS) {
        errno = EINVAL;
        return -1;
    }

    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = stream->position;
    else if (whence == SEEK_END) base = stream->size;
    else {
        errno = EINVAL;
        return -1;
    }

    target = (long)base + offset;
    if (target < 0) {
        errno = EINVAL;
        return -1;
    }
    stream->position = (size_t)target;
    stream->eof = false;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    return (long)stream->position;
}

int feof(FILE *stream) {
    return stream ? (stream->eof ? 1 : 0) : 0;
}

int ferror(FILE *stream) {
    return stream ? (stream->error ? 1 : 0) : 0;
}

int fileno(FILE *stream) {
    return stream ? stream->fd : -1;
}

int vfprintf(FILE *stream, const char *fmt, va_list args) {
    va_list copy;
    char local[1024];
    int length;

    if (!stream || !fmt) return -1;
    va_copy(copy, args);
    length = format_vprintf_to_buffer(local, sizeof(local), fmt, copy);
    va_end(copy);

    if ((size_t)length >= sizeof(local)) {
        char *dynamic = (char *)malloc((size_t)length + 1);
        if (!dynamic) return -1;
        va_copy(copy, args);
        format_vprintf_to_buffer(dynamic, (size_t)length + 1, fmt, copy);
        va_end(copy);
        file_write_text(stream, dynamic, (size_t)length);
        free(dynamic);
        return length;
    }

    file_write_text(stream, local, (size_t)length);
    return length;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list args;
    int result;
    va_start(args, fmt);
    result = vfprintf(stream, fmt, args);
    va_end(args);
    return result;
}

int printf(const char *fmt, ...) {
    va_list args;
    int result;
    va_start(args, fmt);
    result = vfprintf(stdout, fmt, args);
    va_end(args);
    return result;
}

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args) {
    return format_vprintf_to_buffer(buffer, size, fmt, args);
}

int snprintf(char *buffer, size_t size, const char *fmt, ...) {
    va_list args;
    int result;
    va_start(args, fmt);
    result = vsnprintf(buffer, size, fmt, args);
    va_end(args);
    return result;
}

static int parse_number_token(const char **buffer, int base, bool allow_sign,
                              bool auto_base, bool *negative, unsigned long *value) {
    const char *p = *buffer;
    bool neg = false;
    unsigned long result = 0;
    int digits = 0;

    while (isspace((unsigned char)*p)) p++;
    if (allow_sign && (*p == '-' || *p == '+')) {
        neg = (*p == '-');
        p++;
    }
    if (auto_base && *p == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            base = 16;
            p += 2;
        } else {
            base = 8;
            p++;
            digits++;
        }
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * (unsigned long)base + (unsigned long)digit;
        digits++;
        p++;
    }

    if (digits == 0) return 0;
    *buffer = p;
    *negative = neg;
    *value = result;
    return 1;
}

int sscanf(const char *buffer, const char *fmt, ...) {
    va_list args;
    int assigned = 0;

    if (!buffer || !fmt) return 0;
    va_start(args, fmt);

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt)) fmt++;
            while (isspace((unsigned char)*buffer)) buffer++;
            continue;
        }
        if (*fmt != '%') {
            if (*buffer != *fmt) break;
            buffer++;
            fmt++;
            continue;
        }

        fmt++;
        switch (*fmt) {
            case 'd':
            case 'i':
            case 'u':
            case 'x': {
                bool negative = false;
                unsigned long value = 0;
                int base = (*fmt == 'x') ? 16 : 10;
                bool ok = parse_number_token(&buffer, base, *fmt != 'u',
                                             *fmt == 'i', &negative, &value);
                int *out;
                if (!ok) goto done;
                out = va_arg(args, int *);
                if (*fmt == 'u' || !negative) *out = (int)value;
                else *out = -(int)value;
                assigned++;
                break;
            }
            case 'f': {
                char token[64];
                int index = 0;
                double value;
                float *out;

                while (isspace((unsigned char)*buffer)) buffer++;
                if (*buffer == '+' || *buffer == '-') token[index++] = *buffer++;
                while ((*buffer >= '0' && *buffer <= '9') || *buffer == '.') {
                    if (index + 1 >= (int)sizeof(token)) break;
                    token[index++] = *buffer++;
                }
                token[index] = '\0';
                if (index == 0 || (index == 1 && (token[0] == '+' || token[0] == '-'))) goto done;
                value = atof(token);
                out = va_arg(args, float *);
                *out = (float)value;
                assigned++;
                break;
            }
            case 'c': {
                char *out = va_arg(args, char *);
                *out = *buffer++;
                assigned++;
                break;
            }
            case 's': {
                char *out = va_arg(args, char *);
                while (isspace((unsigned char)*buffer)) buffer++;
                if (!*buffer) goto done;
                while (*buffer && !isspace((unsigned char)*buffer)) {
                    *out++ = *buffer++;
                }
                *out = '\0';
                assigned++;
                break;
            }
            default:
                goto done;
        }
        fmt++;
    }

done:
    va_end(args);
    return assigned;
}

int putchar(int c) {
    char ch = (char)c;
    file_write_text(stdout, &ch, 1);
    return c;
}

int puts(const char *s) {
    int len = s ? (int)strlen(s) : 0;
    if (s) file_write_text(stdout, s, (size_t)len);
    file_write_text(stdout, "\n", 1);
    return len + 1;
}

int mkdir(const char *path, int mode) {
    (void)mode;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    return vfs_mkdir(path) ? 0 : -1;
}

int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

int access(const char *path, int mode) {
    void *buffer = NULL;
    uint32_t size = 0;
    (void)mode;
    if (vfs_read_all(path, &buffer, &size)) {
        free(buffer);
        return 0;
    }
    return -1;
}
