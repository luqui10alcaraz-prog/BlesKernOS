#include "common.h"

static bool exists(const char *path) {
    int fd = bk_file_open(path, BK_FILE_READ_ONLY);
    if (fd < 0) return false;
    (void)bk_file_close(fd);
    return true;
}

static bool has_separator(const char *text) {
    while (text && *text) {
        if (*text == '/' || *text == '\\') return true;
        text++;
    }
    return false;
}

static bool has_extension(const char *text) {
    const char *last = text;
    while (text && *text) {
        if (*text == '/' || *text == '\\') last = text + 1;
        text++;
    }
    while (last && *last) if (*last++ == '.') return true;
    return false;
}

static void arguments(char *out, uint32_t capacity, int argc, char **argv) {
    out[0] = '\0';
    for (int i = 2; i < argc; i++) {
        uint32_t used = (uint32_t)bk_runtime_strlen(out);
        uint32_t length = (uint32_t)bk_runtime_strlen(argv[i]);
        if (used + length + 2U >= capacity) break;
        if (used) bk_runtime_strcat(out, " ");
        bk_runtime_strcat(out, argv[i]);
    }
}

static int launch(const char *path, const char *args) {
    if (!exists(path)) return -1;
    if (!bk_app_launch(path, args))
        return command_error("start", "el cargador rechazo el ejecutable");
    kprintf("Iniciado: %s\n", path);
    return 0;
}

static int run(int argc, char **argv) {
    char path[VFS_MAX_PATH];
    char args[COMMAND_ARG_MAX];
    int result;
    if (argc < 2)
        return command_error("start", "uso: start programa|ruta [argumentos]");
    arguments(args, sizeof(args), argc, argv);
    if (has_separator(argv[1]) || has_extension(argv[1])) {
        result = launch(argv[1], args);
        return result >= 0 ? result : command_error("start", "archivo no encontrado");
    }
    bk_runtime_strncpy(path, "/SYSTEM/PROGRAMS/", sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    bk_runtime_strcat(path, argv[1]);
    bk_runtime_strcat(path, ".O");
    result = launch(path, args);
    if (result >= 0) return result;
    bk_runtime_strncpy(path, "/SYSTEM/WIN32/", sizeof(path) - 1U);
    path[sizeof(path) - 1U] = '\0';
    bk_runtime_strcat(path, argv[1]);
    bk_runtime_strcat(path, ".EXE");
    result = launch(path, args);
    if (result >= 0) return result;
    return command_error("start", "programa no encontrado en PROGRAMS ni WIN32");
}

BK_COMMAND_MAIN(run)
