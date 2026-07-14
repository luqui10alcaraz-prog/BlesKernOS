#include "common.h"

#define BACKUP_HEADER_SIZE 20U
#define BACKUP_ENTRY_SIZE  12U
#define BACKUP_MAX_SIZE     (48U * 1024U * 1024U)
#define BACKUP_MAX_DEPTH    32U
#define BACKUP_KIND_FILE    1U
#define BACKUP_KIND_DIR     2U

typedef struct {
    uint64_t bytes;
    uint32_t entries;
    uint32_t files;
    uint32_t directories;
    const char *archive_path;
    bool failed;
} backup_plan_t;

typedef struct {
    uint8_t *cursor;
    uint8_t *end;
} backup_writer_t;

static int backup_read(const char *archive_path, const char *operation,
                       const char *destination);

static void put16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint16_t get16(const uint8_t *input) {
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t get32(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static uint32_t crc32(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return ~crc;
}

static bool dot_entry(const char *name) {
    return command_is(name, ".") || command_is(name, "..");
}

static void join_path(char *output, uint32_t capacity, const char *base,
                      const char *name) {
    uint32_t used;
    bk_runtime_strncpy(output, base && base[0] ? base : "/", capacity - 1U);
    output[capacity - 1U] = '\0';
    used = (uint32_t)bk_runtime_strlen(output);
    if (used && output[used - 1U] != '/' && used + 1U < capacity) {
        output[used++] = '/';
        output[used] = '\0';
    }
    if (name && used + bk_runtime_strlen(name) < capacity)
        bk_runtime_strcat(output, name);
}

static void join_relative(char *output, uint32_t capacity, const char *base,
                          const char *name) {
    uint32_t used = 0;
    if (!output || !capacity) return;
    output[0] = '\0';
    if (base && base[0]) {
        bk_runtime_strncpy(output, base, capacity - 1U);
        output[capacity - 1U] = '\0';
        used = (uint32_t)bk_runtime_strlen(output);
        if (used + 1U < capacity) {
            output[used++] = '/';
            output[used] = '\0';
        }
    }
    if (name && used + bk_runtime_strlen(name) < capacity)
        bk_runtime_strcat(output, name);
}

static const char *base_name(const char *path) {
    const char *name = path;
    if (!path) return "archivo";
    for (; *path; path++)
        if (*path == '/' || *path == '\\') name = path + 1;
    return name[0] ? name : "raiz";
}

static bool writer_entry(backup_writer_t *writer, const char *relative,
                         uint8_t kind, uint8_t attributes,
                         const uint8_t *data, uint32_t size) {
    uint32_t path_length = (uint32_t)bk_runtime_strlen(relative);
    uint32_t needed = BACKUP_ENTRY_SIZE + path_length + size;
    uint8_t *header;
    if (!writer || !relative || !path_length || path_length >= BK_PATH_MAX ||
        needed > (uint32_t)(writer->end - writer->cursor)) return false;
    header = writer->cursor;
    put16(header, (uint16_t)path_length);
    header[2] = kind;
    header[3] = attributes;
    put32(header + 4, size);
    put32(header + 8, kind == BACKUP_KIND_FILE ? crc32(data, size) : 0U);
    writer->cursor += BACKUP_ENTRY_SIZE;
    for (uint32_t i = 0; i < path_length; i++)
        *writer->cursor++ = (uint8_t)relative[i];
    for (uint32_t i = 0; i < size; i++) *writer->cursor++ = data[i];
    return true;
}

static void plan_entry(backup_plan_t *plan, const char *relative,
                       bool directory, uint32_t size) {
    uint64_t addition;
    if (!plan || !relative) return;
    addition = BACKUP_ENTRY_SIZE + bk_runtime_strlen(relative) + size;
    if (plan->bytes + addition > BACKUP_MAX_SIZE) {
        plan->failed = true;
        return;
    }
    plan->bytes += addition;
    plan->entries++;
    if (directory) plan->directories++;
    else plan->files++;
}

static bool directory_snapshot(const char *path, bk_file_entry_t **entries,
                               uint32_t *count) {
    bk_file_entry_t *list;
    if (!entries || !count) return false;
    *entries = NULL;
    *count = 0;
    list = (bk_file_entry_t *)bk_sys_alloc(
        sizeof(bk_file_entry_t) * BK_DIRECTORY_MAX);
    if (!list) return false;
    if (!bk_file_list_dir(path, list, BK_DIRECTORY_MAX, count) ||
        *count == BK_DIRECTORY_MAX) {
        bk_sys_free(list);
        return false;
    }
    *entries = list;
    return true;
}

static bool backup_walk_directory(const char *source, const char *relative,
                                  uint32_t depth, backup_plan_t *plan,
                                  backup_writer_t *writer) {
    bk_file_entry_t *entries;
    uint32_t count;
    if (depth > BACKUP_MAX_DEPTH ||
        !directory_snapshot(source, &entries, &count)) return false;

    /* Primero se emiten todas las entradas inmediatas. Así, durante restore,
       los directorios padre siempre aparecen antes que sus hijos. */
    for (uint32_t i = 0; i < count; i++) {
        char child[BK_PATH_MAX];
        char child_relative[BK_PATH_MAX];
        void *contents = NULL;
        uint32_t size = 0;
        bool directory;
        if (dot_entry(entries[i].name)) continue;
        join_path(child, sizeof(child), source, entries[i].name);
        join_relative(child_relative, sizeof(child_relative), relative,
                      entries[i].name);
        if (plan->archive_path && command_is(child, plan->archive_path))
            continue;
        directory = entries[i].type == BK_FILE_NODE_DIRECTORY;
        if (!writer) {
            plan_entry(plan, child_relative, directory,
                       directory ? 0U : entries[i].size);
            if (plan->failed) {
                bk_sys_free(entries);
                return false;
            }
        } else if (directory) {
            if (!writer_entry(writer, child_relative, BACKUP_KIND_DIR,
                              entries[i].attributes, NULL, 0U)) {
                bk_sys_free(entries);
                return false;
            }
        } else {
            if (!bk_file_read_all(child, &contents, &size) ||
                size != entries[i].size ||
                !writer_entry(writer, child_relative, BACKUP_KIND_FILE,
                              entries[i].attributes,
                              (const uint8_t *)contents, size)) {
                if (contents) bk_sys_free(contents);
                bk_sys_free(entries);
                return false;
            }
            bk_sys_free(contents);
        }
    }
    bk_sys_free(entries);

    /* No se conserva una lista grande a través de la recursión: se vuelve a
       consultar cada índice para mantener pequeño el uso de pila y heap. */
    for (uint32_t i = 0; i < count; i++) {
        char child[BK_PATH_MAX];
        char child_relative[BK_PATH_MAX];
        char name[BK_NAME_MAX];
        bool directory = false;
        if (!directory_snapshot(source, &entries, &count)) return false;
        if (i < count && !dot_entry(entries[i].name) &&
            entries[i].type == BK_FILE_NODE_DIRECTORY) {
            bk_runtime_strncpy(name, entries[i].name, sizeof(name) - 1U);
            name[sizeof(name) - 1U] = '\0';
            directory = true;
        }
        bk_sys_free(entries);
        if (!directory) continue;
        join_path(child, sizeof(child), source, name);
        join_relative(child_relative, sizeof(child_relative), relative, name);
        if (!backup_walk_directory(child, child_relative, depth + 1U,
                                   plan, writer)) return false;
    }
    return true;
}

static bool backup_source(const char *source, backup_plan_t *plan,
                          backup_writer_t *writer) {
    bk_file_entry_t *entries;
    uint32_t count;
    if (directory_snapshot(source, &entries, &count)) {
        bk_sys_free(entries);
        return backup_walk_directory(source, "", 0U, plan, writer);
    }
    {
        void *contents = NULL;
        uint32_t size = 0;
        const char *name = base_name(source);
        if (!bk_file_read_all(source, &contents, &size)) return false;
        if (!writer) plan_entry(plan, name, false, size);
        else if (!writer_entry(writer, name, BACKUP_KIND_FILE, 0U,
                               (const uint8_t *)contents, size)) {
            bk_sys_free(contents);
            return false;
        }
        bk_sys_free(contents);
        return !plan->failed;
    }
}

static int backup_create(const char *source, const char *archive_path) {
    backup_plan_t plan = {0};
    backup_writer_t writer;
    uint8_t *archive;
    uint32_t archive_size;
    plan.bytes = BACKUP_HEADER_SIZE;
    plan.archive_path = archive_path;
    if (!backup_source(source, &plan, NULL) || plan.failed || !plan.entries)
        return command_error("backup", "origen ilegible, vacio o demasiado grande");
    archive_size = (uint32_t)plan.bytes;
    archive = (uint8_t *)bk_sys_alloc(archive_size);
    if (!archive) return command_error("backup", "memoria insuficiente");
    archive[0] = 'B'; archive[1] = 'K'; archive[2] = 'B'; archive[3] = '1';
    put32(archive + 4, 1U);
    put32(archive + 8, plan.entries);
    put32(archive + 12, archive_size);
    put32(archive + 16, 0U);
    writer.cursor = archive + BACKUP_HEADER_SIZE;
    writer.end = archive + archive_size;
    if (!backup_source(source, &plan, &writer) ||
        writer.cursor != writer.end) {
        bk_sys_free(archive);
        return command_error("backup", "el origen cambio o no pudo leerse");
    }
    put32(archive + 16,
          crc32(archive + BACKUP_HEADER_SIZE,
                archive_size - BACKUP_HEADER_SIZE));
    if (!bk_file_write_all(archive_path, archive, archive_size)) {
        bk_sys_free(archive);
        return command_error("backup", "no se pudo escribir el archivo BKB");
    }
    kprintf("Backup creado: %s\n", archive_path);
    kprintf("%u archivo(s), %u directorio(s), %u bytes.\n",
            plan.files, plan.directories, archive_size);
    bk_sys_free(archive);
    kprintf("Verificando el archivo escrito...\n");
    return backup_read(archive_path, "verify", NULL);
}

static bool safe_relative_path(const char *path) {
    const char *component = path;
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\') return false;
    for (const char *p = path;; p++) {
        if (*p == '\\') return false;
        if (*p == '/' || *p == '\0') {
            uint32_t length = (uint32_t)(p - component);
            if (!length || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.'))
                return false;
            if (!*p) break;
            component = p + 1;
        }
    }
    return true;
}

static int backup_read(const char *archive_path, const char *operation,
                       const char *destination) {
    void *raw = NULL;
    uint8_t *archive;
    uint32_t size = 0;
    uint32_t offset = BACKUP_HEADER_SIZE;
    uint32_t entries;
    bool list = command_is(operation, "list");
    bool restore = command_is(operation, "restore");
    if (!bk_file_read_all(archive_path, &raw, &size) || size < BACKUP_HEADER_SIZE)
        return command_error("backup", "archivo BKB ilegible");
    archive = (uint8_t *)raw;
    if (archive[0] != 'B' || archive[1] != 'K' || archive[2] != 'B' ||
        archive[3] != '1' || get32(archive + 4) != 1U ||
        get32(archive + 12) != size ||
        get32(archive + 16) !=
            crc32(archive + BACKUP_HEADER_SIZE, size - BACKUP_HEADER_SIZE)) {
        bk_sys_free(raw);
        return command_error("backup", "cabecera o manifiesto BKB corrupto");
    }
    entries = get32(archive + 8);
    if (restore && (!destination || !bk_file_mkdir(destination))) {
        bk_sys_free(raw);
        return command_error("backup", "no se pudo preparar el destino");
    }
    for (uint32_t index = 0; index < entries; index++) {
        uint16_t path_length;
        uint8_t kind;
        uint32_t data_size;
        uint32_t expected_crc;
        char relative[BK_PATH_MAX];
        char target[BK_PATH_MAX];
        if (offset + BACKUP_ENTRY_SIZE > size) goto corrupt;
        path_length = get16(archive + offset);
        kind = archive[offset + 2U];
        data_size = get32(archive + offset + 4U);
        expected_crc = get32(archive + offset + 8U);
        offset += BACKUP_ENTRY_SIZE;
        if (!path_length || path_length >= sizeof(relative) ||
            offset + path_length > size) goto corrupt;
        for (uint32_t i = 0; i < path_length; i++)
            relative[i] = (char)archive[offset + i];
        relative[path_length] = '\0';
        offset += path_length;
        if (!safe_relative_path(relative) ||
            (kind != BACKUP_KIND_FILE && kind != BACKUP_KIND_DIR) ||
            (kind == BACKUP_KIND_DIR && data_size != 0U) ||
            offset + data_size > size) goto corrupt;
        if (kind == BACKUP_KIND_FILE &&
            crc32(archive + offset, data_size) != expected_crc) goto corrupt;
        if (list)
            kprintf("%s %8u %s\n",
                    kind == BACKUP_KIND_DIR ? "<DIR>" : "     ",
                    data_size, relative);
        if (restore) {
            join_path(target, sizeof(target), destination, relative);
            if ((kind == BACKUP_KIND_DIR && !bk_file_mkdir(target)) ||
                (kind == BACKUP_KIND_FILE &&
                 !bk_file_write_all(target, archive + offset, data_size))) {
                bk_sys_free(raw);
                return command_error("backup", "fallo al restaurar una entrada");
            }
        }
        offset += data_size;
    }
    if (offset != size) goto corrupt;
    kprintf("Backup %s: %u entrada(s), verificacion correcta.\n",
            restore ? "restaurado" : (list ? "listado" : "verificado"),
            entries);
    bk_sys_free(raw);
    return 0;

corrupt:
    bk_sys_free(raw);
    return command_error("backup", "entrada truncada, insegura o con CRC invalido");
}

static int run(int argc, char **argv) {
    if (argc == 4 && command_is(argv[1], "create"))
        return backup_create(argv[2], argv[3]);
    if (argc == 3 && command_is(argv[1], "list"))
        return backup_read(argv[2], "list", NULL);
    if (argc == 3 && command_is(argv[1], "verify"))
        return backup_read(argv[2], "verify", NULL);
    if (argc == 4 && command_is(argv[1], "restore"))
        return backup_read(argv[2], "restore", argv[3]);
    return command_error("backup",
        "uso: backup create origen archivo.bkb | list/verify archivo.bkb | restore archivo.bkb destino");
}

BK_COMMAND_MAIN(run)
