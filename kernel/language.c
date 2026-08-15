#include "include/language.h"
#include "include/memory.h"
#include "include/vfs.h"
#include "include/user_config.h"
#include "include/vga.h"
#include "include/compat_mode.h"
#include "string.h"

#define LANGUAGE_CONFIG_PATH "/SYSTEM/USER/CONFIG/LANGUAGE.INI"
#define LANGUAGE_TABLE_MIN   256U
#define LANGUAGE_TABLE_MAX   4096U

/* Los .LNG son UTF-8 de texto plano: KEY=valor. */
typedef struct {
    uint32_t hash;
    const char *key;
    const char *value;
} language_entry_t;

typedef struct {
    const char *code;
    const char *name;
    const char *path;
} language_descriptor_t;

static const language_descriptor_t g_languages[] = {
    {"ES", "Espanol",  "/SYSTEM/LENG/ESPANOL.LNG"},
    {"EN", "English",  "/SYSTEM/LENG/ENGLISH.LNG"},
    {"IT", "Italiano", "/SYSTEM/LENG/ITALIANO.LNG"},
};

static language_entry_t *g_table;
static uint32_t g_table_capacity;
static char *g_catalog_buffer;
static uint32_t g_catalog_size;
static char g_current[BK_LANGUAGE_CODE_MAX] = "ES";
static uint32_t g_generation;
static bool g_ready;
static bool g_loading;
static bool g_linear_catalog;

static uint32_t language_hash(const char *text) {
    uint32_t hash = 2166136261U;
    if (!text) return hash;
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619U;
    }
    return hash ? hash : 1U;
}

static bool language_equal_ci(const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

static const language_descriptor_t *language_descriptor(const char *code) {
    uint32_t count = (uint32_t)(sizeof(g_languages) / sizeof(g_languages[0]));
    for (uint32_t i = 0; i < count; i++)
        if (language_equal_ci(code, g_languages[i].code)) return &g_languages[i];
    return NULL;
}

static uint32_t language_next_power_of_two(uint32_t value) {
    uint32_t result = LANGUAGE_TABLE_MIN;
    while (result < value && result < LANGUAGE_TABLE_MAX) result <<= 1U;
    return result;
}

static void language_free_catalog(language_entry_t *table, char *buffer) {
    if (table) kfree(table);
    if (buffer) kfree(buffer);
}

static bool language_insert(language_entry_t *table, uint32_t capacity,
                            const char *key, const char *value) {
    uint32_t hash;
    uint32_t slot;
    if (!table || !capacity || !key || !*key || !value) return false;
    hash = language_hash(key);
    slot = hash & (capacity - 1U);
    for (uint32_t probe = 0; probe < capacity; probe++) {
        language_entry_t *entry = &table[(slot + probe) & (capacity - 1U)];
        if (!entry->key || (entry->hash == hash && kstrcmp(entry->key, key) == 0)) {
            entry->hash = hash;
            entry->key = key;
            entry->value = value;
            return true;
        }
    }
    return false;
}

static const char *language_find(const char *key) {
    uint32_t hash;
    uint32_t slot;
    if (!g_ready || !key || !*key) return NULL;
    if (g_linear_catalog) {
        const char *scan = g_catalog_buffer;
        const char *end = g_catalog_buffer + g_catalog_size;
        while (scan && scan < end && *scan) {
            const char *entry_key = scan;
            while (scan < end && *scan) scan++;
            if (scan >= end) return NULL;
            scan++;
            const char *entry_value = scan;
            while (scan < end && *scan) scan++;
            if (scan >= end) return NULL;
            if (kstrcmp(entry_key, key) == 0) return entry_value;
            scan++;
        }
        return NULL;
    }
    if (!g_table || !g_table_capacity) return NULL;
    hash = language_hash(key);
    slot = hash & (g_table_capacity - 1U);
    for (uint32_t probe = 0; probe < g_table_capacity; probe++) {
        const language_entry_t *entry =
            &g_table[(slot + probe) & (g_table_capacity - 1U)];
        if (!entry->key) return NULL;
        if (entry->hash == hash && kstrcmp(entry->key, key) == 0)
            return entry->value;
    }
    return NULL;
}

static char language_hex_digit(uint32_t value) {
    value &= 15U;
    return (char)(value < 10U ? ('0' + value) : ('A' + value - 10U));
}

static void language_hash_key(const char *source, char key[10]) {
    uint32_t hash = language_hash(source);
    key[0] = 'H';
    for (uint32_t i = 0; i < 8U; i++)
        key[1U + i] = language_hex_digit(hash >> ((7U - i) * 4U));
    key[9] = '\0';
}

static char *language_trim_left(char *text) {
    while (text && (*text == ' ' || *text == '\t')) text++;
    return text;
}

static void language_trim_right(char *text) {
    uint32_t length;
    if (!text) return;
    length = (uint32_t)kstrlen(text);
    while (length && (text[length - 1U] == ' ' || text[length - 1U] == '\t'))
        text[--length] = '\0';
}

/* Convierte escapes y UTF-8 latino a un byte ISO-8859-1 in-place. */
static void language_decode_value(char *value) {
    uint8_t *read = (uint8_t *)value;
    uint8_t *write = (uint8_t *)value;
    if (!value) return;
    while (*read) {
        if (*read == '\\' && read[1]) {
            read++;
            if (*read == 'n') *write++ = '\n';
            else if (*read == 'r') *write++ = '\r';
            else if (*read == 't') *write++ = '\t';
            else *write++ = *read;
            read++;
            continue;
        }
        if (*read < 0x80U) {
            *write++ = *read++;
            continue;
        }
        if ((*read & 0xE0U) == 0xC0U && (read[1] & 0xC0U) == 0x80U) {
            uint32_t cp = ((uint32_t)(read[0] & 0x1FU) << 6U) |
                          (uint32_t)(read[1] & 0x3FU);
            *write++ = cp <= 0xFFU ? (uint8_t)cp : (uint8_t)'?';
            read += 2;
            continue;
        }
        if ((*read & 0xF0U) == 0xE0U &&
            (read[1] & 0xC0U) == 0x80U && (read[2] & 0xC0U) == 0x80U) {
            uint32_t cp = ((uint32_t)(read[0] & 0x0FU) << 12U) |
                          ((uint32_t)(read[1] & 0x3FU) << 6U) |
                          (uint32_t)(read[2] & 0x3FU);
            /* Caracteres frecuentes que caben en la fuente de BlesKernOS. */
            if (cp == 0x20ACU) *write++ = 'E';
            else *write++ = cp <= 0xFFU ? (uint8_t)cp : (uint8_t)'?';
            read += 3;
            continue;
        }
        *write++ = '?';
        read++;
    }
    *write = '\0';
}

static uint32_t language_count_lines(char *buffer) {
    uint32_t count = 0;
    char *p = buffer;
    while (p && *p) {
        char *line = p;
        char *equals = NULL;
        while (*p && *p != '\n' && *p != '\r') {
            if (*p == '=' && !equals) equals = p;
            p++;
        }
        if (equals) {
            char *trim = language_trim_left(line);
            if (*trim && *trim != ';' && *trim != '#') count++;
        }
        while (*p == '\n' || *p == '\r') p++;
    }
    return count;
}

static bool language_parse(char *buffer, language_entry_t *table,
                           uint32_t capacity) {
    char *p = buffer;
    uint32_t inserted = 0;
    while (p && *p) {
        char *line = p;
        char *equals = NULL;
        while (*p && *p != '\n' && *p != '\r') {
            if (*p == '=' && !equals) equals = p;
            p++;
        }
        if (*p) {
            *p++ = '\0';
            while (*p == '\n' || *p == '\r') p++;
        }
        line = language_trim_left(line);
        language_trim_right(line);
        if (!*line || *line == ';' || *line == '#' || !equals) continue;
        *equals = '\0';
        char *key = language_trim_left(line);
        char *value = language_trim_left(equals + 1);
        language_trim_right(key);
        language_trim_right(value);
        language_decode_value(value);
        if (language_insert(table, capacity, key, value)) inserted++;
    }
    return inserted != 0U;
}

/* En 4-7 MiB un hash de miles de slots cuesta mas que el catalogo. Esta ruta
 * compacta el archivo in-place como key\0value\0... y hace busqueda lineal.
 * Las traducciones se consultan pocas veces por frame, por lo que el ahorro de
 * RAM es mucho mas valioso que unos pocos cientos de comparaciones al iniciar. */
static uint32_t language_pack_linear(char *buffer, uint32_t capacity) {
    char *read = buffer;
    char *write = buffer;
    char *limit = buffer + capacity;
    uint32_t inserted = 0U;

    while (read && read < limit && *read) {
        char *line = read;
        char *equals = NULL;
        while (read < limit && *read && *read != '\n' && *read != '\r') {
            if (*read == '=' && !equals) equals = read;
            read++;
        }
        if (read < limit && *read) {
            *read++ = '\0';
            while (read < limit && (*read == '\n' || *read == '\r')) read++;
        }
        line = language_trim_left(line);
        language_trim_right(line);
        if (!*line || *line == ';' || *line == '#' || !equals) continue;
        *equals = '\0';
        char *key = language_trim_left(line);
        char *value = language_trim_left(equals + 1);
        language_trim_right(key);
        language_trim_right(value);
        language_decode_value(value);
        if (!*key) continue;
        while (*key && write + 1 < limit) *write++ = *key++;
        if (write + 1 >= limit) break;
        *write++ = '\0';
        while (*value && write + 1 < limit) *write++ = *value++;
        if (write + 1 >= limit) break;
        *write++ = '\0';
        inserted++;
    }
    if (write < limit) *write++ = '\0';
    return inserted ? (uint32_t)(write - buffer) : 0U;
}

static bool language_load_descriptor(const language_descriptor_t *descriptor,
                                     bool persist) {
    void *raw = NULL;
    uint32_t size = 0;
    char *buffer;
    language_entry_t *table = NULL;
    uint32_t count = 0U;
    uint32_t capacity = 0U;
    uint32_t packed_size = 0U;
    bool linear;
    if (!descriptor || g_loading) return false;
    g_loading = true;
    if (!vfs_read_all(descriptor->path, &raw, &size) || !raw || !size) {
        if (raw) kfree(raw);
        g_loading = false;
        return false;
    }

    /* Normalmente vfs_read_all ya devuelve una allocation del heap. Extenderla
     * evita mantener simultaneamente dos copias completas del .LNG. */
    buffer = (char *)krealloc(raw, size + 2U);
    if (!buffer) {
        buffer = (char *)kmalloc(size + 2U);
        if (!buffer) {
            kfree(raw);
            g_loading = false;
            return false;
        }
        kmemcpy(buffer, raw, size);
        kfree(raw);
    }
    buffer[size] = '\0';
    buffer[size + 1U] = '\0';
    linear = compat_mode_use_compact_language();

    if (linear) {
        packed_size = language_pack_linear(buffer, size + 2U);
        if (!packed_size) {
            kfree(buffer);
            g_loading = false;
            return false;
        }
    } else {
        count = language_count_lines(buffer);
        capacity = language_next_power_of_two(count * 2U + 1U);
        table = (language_entry_t *)kzalloc(sizeof(language_entry_t) * capacity);
        if (!table || !language_parse(buffer, table, capacity)) {
            language_free_catalog(table, buffer);
            g_loading = false;
            return false;
        }
    }

    language_entry_t *old_table = g_table;
    char *old_buffer = g_catalog_buffer;
    g_table = table;
    g_table_capacity = capacity;
    g_catalog_buffer = buffer;
    g_catalog_size = linear ? packed_size : size + 1U;
    g_linear_catalog = linear;
    kstrncpy(g_current, descriptor->code, sizeof(g_current) - 1U);
    g_current[sizeof(g_current) - 1U] = '\0';
    g_ready = true;
    g_generation++;
    language_free_catalog(old_table, old_buffer);

    if (persist) {
        char config[32];
        uint32_t used = 0;
        const char prefix[] = "language=";
        for (uint32_t i = 0; prefix[i] && used + 1U < sizeof(config); i++)
            config[used++] = prefix[i];
        for (uint32_t i = 0; descriptor->code[i] && used + 2U < sizeof(config); i++)
            config[used++] = descriptor->code[i];
        config[used++] = '\r';
        config[used++] = '\n';
        config[used] = '\0';
        (void)vfs_write_all(LANGUAGE_CONFIG_PATH, config, used);
    }
    g_loading = false;
    return true;
}

static void language_read_config(char code[BK_LANGUAGE_CODE_MAX]) {
    void *raw = NULL;
    uint32_t size = 0;
    code[0] = 'E'; code[1] = 'S'; code[2] = '\0';
    if (!vfs_read_all(LANGUAGE_CONFIG_PATH, &raw, &size) || !raw) return;
    const char *p = (const char *)raw;
    const char prefix[] = "language=";
    for (uint32_t i = 0; i + sizeof(prefix) - 1U <= size; i++) {
        uint32_t j = 0;
        while (j < sizeof(prefix) - 1U && p[i + j] == prefix[j]) j++;
        if (j != sizeof(prefix) - 1U) continue;
        uint32_t out = 0;
        i += j;
        while (i < size && out + 1U < BK_LANGUAGE_CODE_MAX &&
               p[i] != '\r' && p[i] != '\n' && p[i] != ' ' && p[i] != '\t')
            code[out++] = p[i++];
        code[out] = '\0';
        break;
    }
    kfree(raw);
}

void language_init(void) {
    char configured[BK_LANGUAGE_CODE_MAX];
    const language_descriptor_t *descriptor;
    if (g_ready || g_loading) return;
    language_read_config(configured);
    descriptor = language_descriptor(configured);
    if (!descriptor) descriptor = &g_languages[0];
    if (!language_load_descriptor(descriptor, false) && descriptor != &g_languages[0])
        (void)language_load_descriptor(&g_languages[0], false);
}

const char *language_get(const char *key) {
    const char *value;
    if (!key) return "";
    if (!g_ready && !g_loading) language_init();
    value = language_find(key);
    return value ? value : key;
}

const char *language_translate(const char *source) {
    char key[10];
    const char *value;
    if (!source || !*source) return source ? source : "";
    if (!g_ready || g_loading) return source;
    if (source[0] == '@' && source[1]) {
        value = language_find(source + 1);
        return value ? value : source + 1;
    }
    language_hash_key(source, key);
    value = language_find(key);
    return value ? value : source;
}

static bool language_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

const char *language_expand(const char *source, char *output,
                            uint32_t output_size) {
    const char *whole;
    const char *scan;
    uint32_t used = 0U;

    if (!output || !output_size) return source ? source : "";
    output[0] = '\0';
    if (!source) return output;

    whole = language_translate(source);
    if (source[0] == '@') {
        if (whole != source && whole != source + 1) source = whole;
    } else if (whole != source) {
        source = whole;
    }
    /* La ruta habitual no necesita copiar ni imponer un límite artificial a
     * texto largo (por ejemplo documentos). */
    scan = source;
    while (*scan && !(scan[0] == '@' && scan[1] == 'H')) scan++;
    if (!*scan) return source;
    while (*source && used + 1U < output_size) {
        if (source[0] == '@' && source[1] == 'H') {
            bool valid = true;
            char key[11];
            const char *value;
            for (uint32_t i = 0U; i < 8U; i++) {
                if (!source[i + 2U] ||
                    !language_is_hex_digit(source[i + 2U])) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                for (uint32_t i = 0U; i < 10U; i++) key[i] = source[i];
                key[10] = '\0';
                value = language_translate(key);
                if (value && value != key + 1) {
                    while (*value && used + 1U < output_size)
                        output[used++] = *value++;
                    source += 10;
                    continue;
                }
            }
        }
        output[used++] = *source++;
    }
    output[used] = '\0';
    return output;
}

const char *language_current(void) {
    return g_current;
}

uint32_t language_generation(void) {
    return g_generation;
}

uint32_t language_count(void) {
    return (uint32_t)(sizeof(g_languages) / sizeof(g_languages[0]));
}

bool language_info(uint32_t index, bk_language_info_t *info) {
    if (!info || index >= language_count()) return false;
    kstrncpy(info->code, g_languages[index].code, sizeof(info->code) - 1U);
    info->code[sizeof(info->code) - 1U] = '\0';
    kstrncpy(info->name, g_languages[index].name, sizeof(info->name) - 1U);
    info->name[sizeof(info->name) - 1U] = '\0';
    return true;
}

bool language_set(const char *code) {
    const language_descriptor_t *descriptor = language_descriptor(code);
    if (!descriptor) return false;
    if (language_equal_ci(descriptor->code, g_current) && g_ready) return true;
    return language_load_descriptor(descriptor, true);
}
