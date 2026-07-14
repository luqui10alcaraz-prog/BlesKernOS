#include "../kernel/include/api.h"

#define SHELL_ALIAS_MAX 16
#define SHELL_VAR_MAX 16
#define SHELL_NAME_MAX 24

typedef struct {
    char name[SHELL_NAME_MAX];
    char value[SHELL_MAX_CMD];
} shell_pair_t;

static char history[SHELL_HISTORY_LEN][SHELL_MAX_CMD];
static int history_count;
static int history_idx;
static shell_pair_t aliases[SHELL_ALIAS_MAX];
static shell_pair_t variables[SHELL_VAR_MAX];
static bool exit_requested;
static char previous_cwd[VFS_MAX_PATH] = "/";

static void copy_text(char *dst, uint32_t capacity, const char *src) {
    if (!dst || !capacity) return;
    bk_runtime_strncpy(dst, src ? src : "", capacity - 1U);
    dst[capacity - 1U] = '\0';
}

static bool append_text(char *dst, uint32_t capacity, const char *src) {
    uint32_t used = (uint32_t)bk_runtime_strlen(dst);
    uint32_t add = (uint32_t)bk_runtime_strlen(src ? src : "");
    if (used + add >= capacity) return false;
    bk_runtime_strcat(dst, src ? src : "");
    return true;
}

static int pair_find(shell_pair_t *pairs, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (pairs[i].name[0] && bk_runtime_strcmp(pairs[i].name, name) == 0)
            return i;
    return -1;
}

static bool pair_set(shell_pair_t *pairs, int count, const char *name,
                     const char *value) {
    int slot = pair_find(pairs, count, name);
    if (slot < 0) {
        for (int i = 0; i < count; i++) {
            if (!pairs[i].name[0]) { slot = i; break; }
        }
    }
    if (slot < 0 || !name || !name[0]) return false;
    copy_text(pairs[slot].name, sizeof(pairs[slot].name), name);
    copy_text(pairs[slot].value, sizeof(pairs[slot].value), value);
    return true;
}

static void pair_remove(shell_pair_t *pairs, int count, const char *name) {
    int slot = pair_find(pairs, count, name);
    if (slot >= 0) bk_runtime_memset(&pairs[slot], 0, sizeof(pairs[slot]));
}

static void history_add(const char *command) {
    int slot;
    if (!command || !command[0]) return;
    if (history_count && bk_runtime_strcmp(
            history[(history_count - 1) % SHELL_HISTORY_LEN], command) == 0)
        return;
    slot = history_count % SHELL_HISTORY_LEN;
    copy_text(history[slot], sizeof(history[slot]), command);
    history_count++;
    history_idx = history_count;
}

static void erase_input(size_t count) {
    while (count--) vga_putchar('\b');
}

static void readline(char *buffer, size_t capacity) {
    size_t pos = 0;
    bk_runtime_memset(buffer, 0, capacity);
    history_idx = history_count;
    for (;;) {
        char c = bk_input_getchar();
        if (c == '\n' || c == '\r') {
            vga_putchar('\n');
            buffer[pos] = '\0';
            return;
        }
        if (c == '\b') {
            if (pos) { buffer[--pos] = '\0'; vga_putchar('\b'); }
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            int oldest = history_count > SHELL_HISTORY_LEN
                       ? history_count - SHELL_HISTORY_LEN : 0;
            if (c == KEY_UP && history_idx > oldest) history_idx--;
            if (c == KEY_DOWN && history_idx < history_count) history_idx++;
            erase_input(pos);
            pos = 0;
            buffer[0] = '\0';
            if (history_idx < history_count) {
                const char *item = history[history_idx % SHELL_HISTORY_LEN];
                copy_text(buffer, capacity, item);
                pos = bk_runtime_strlen(buffer);
                vga_puts(buffer);
            }
            continue;
        }
        if (c >= 32 && pos + 1 < capacity) {
            buffer[pos++] = c;
            buffer[pos] = '\0';
            vga_putchar(c);
        }
    }
}

static int parse_args(char *line, char **argv, int maximum) {
    int argc = 0;
    char *read = line;
    char *write = line;
    while (*read && argc < maximum) {
        char quote = 0;
        while (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r') read++;
        if (!*read) break;
        argv[argc++] = write;
        while (*read) {
            if (!quote && (*read == ' ' || *read == '\t' ||
                           *read == '\n' || *read == '\r')) break;
            if (*read == '\\' && read[1]) {
                read++;
                *write++ = *read++;
                continue;
            }
            if (*read == '\'' || *read == '"') {
                if (!quote) { quote = *read++; continue; }
                if (quote == *read) { quote = 0; read++; continue; }
            }
            *write++ = *read++;
        }
        /* read puede coincidir con write cuando no hubo comillas. Guardar y
           saltar los separadores antes de escribir el NUL evita perder todos
           los argumentos posteriores al primero. */
        char *next = read;
        while (*next == ' ' || *next == '\t' || *next == '\n' || *next == '\r') next++;
        *write++ = '\0';
        read = next;
    }
    argv[argc] = NULL;
    return argc;
}

static void expand_variables(const char *source, char *output,
                             uint32_t capacity) {
    uint32_t out = 0;
    for (uint32_t i = 0; source && source[i] && out + 1 < capacity;) {
        if (source[i] != '$') { output[out++] = source[i++]; continue; }
        i++;
        char name[SHELL_NAME_MAX];
        uint32_t n = 0;
        while (source[i] && n + 1 < sizeof(name) &&
               ((source[i] >= 'A' && source[i] <= 'Z') ||
                (source[i] >= 'a' && source[i] <= 'z') ||
                (source[i] >= '0' && source[i] <= '9') || source[i] == '_'))
            name[n++] = source[i++];
        name[n] = '\0';
        int slot = pair_find(variables, SHELL_VAR_MAX, name);
        const char *value = slot >= 0 ? variables[slot].value : "";
        while (*value && out + 1 < capacity) output[out++] = *value++;
    }
    output[out] = '\0';
}

static int cmd_cd(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    char old[VFS_MAX_PATH];
    if (argc > 2) { kprintf("cd: uso: cd [directorio]\n"); return 2; }
    if (bk_runtime_strcmp(path, "~") == 0) path = "/";
    else if (bk_runtime_strcmp(path, "-") == 0) path = previous_cwd;
    copy_text(old, sizeof(old), bk_file_getcwd());
    if (!bk_file_chdir(path)) {
        kprintf("cd: '%s': directorio inexistente o inaccesible\n", path);
        return 1;
    }
    copy_text(previous_cwd, sizeof(previous_cwd), old);
    (void)pair_set(variables, SHELL_VAR_MAX, "OLDPWD", previous_cwd);
    (void)pair_set(variables, SHELL_VAR_MAX, "PWD", bk_file_getcwd());
    if (argc > 1 && bk_runtime_strcmp(argv[1], "-") == 0)
        kprintf("%s\n", bk_file_getcwd());
    return 0;
}

static int cmd_pwd(int argc UNUSED, char **argv UNUSED) {
    kprintf("%s\n", bk_file_getcwd());
    return 0;
}

static int cmd_clear(int argc UNUSED, char **argv UNUSED) { vga_clear(); return 0; }

static int cmd_history(int argc UNUSED, char **argv UNUSED) {
    int first = history_count > SHELL_HISTORY_LEN
              ? history_count - SHELL_HISTORY_LEN : 0;
    for (int i = first; i < history_count; i++)
        kprintf("%u  %s\n", i + 1, history[i % SHELL_HISTORY_LEN]);
    return 0;
}

static int cmd_alias(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < SHELL_ALIAS_MAX; i++)
            if (aliases[i].name[0])
                kprintf("alias %s='%s'\n", aliases[i].name, aliases[i].value);
        return 0;
    }
    char *equal = argv[1];
    while (*equal && *equal != '=') equal++;
    if (*equal == '=') {
        *equal++ = '\0';
        return pair_set(aliases, SHELL_ALIAS_MAX, argv[1], equal) ? 0 : 1;
    }
    if (argc < 3) { kprintf("Uso: alias nombre comando\n"); return 1; }
    char value[SHELL_MAX_CMD] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) append_text(value, sizeof(value), " ");
        append_text(value, sizeof(value), argv[i]);
    }
    return pair_set(aliases, SHELL_ALIAS_MAX, argv[1], value) ? 0 : 1;
}

static int cmd_unalias(int argc, char **argv) {
    if (argc != 2) { kprintf("Uso: unalias nombre\n"); return 1; }
    pair_remove(aliases, SHELL_ALIAS_MAX, argv[1]);
    return 0;
}

static int cmd_set(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < SHELL_VAR_MAX; i++)
            if (variables[i].name[0])
                kprintf("%s=%s\n", variables[i].name, variables[i].value);
        return 0;
    }
    char *equal = argv[1];
    while (*equal && *equal != '=') equal++;
    if (*equal == '=') {
        *equal++ = '\0';
        return pair_set(variables, SHELL_VAR_MAX, argv[1], equal) ? 0 : 1;
    }
    if (argc < 3) { kprintf("Uso: set NOMBRE=valor\n"); return 1; }
    return pair_set(variables, SHELL_VAR_MAX, argv[1], argv[2]) ? 0 : 1;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_puts(argv[i]);
    }
    vga_putchar('\n');
    return 0;
}

static int cmd_ver(int argc UNUSED, char **argv UNUSED) {
    kprintf("BlesKernOS 0.6 - Shell 1.0 - API %u\n", bk_sys_api_version());
    return 0;
}

static int cmd_exit(int argc UNUSED, char **argv UNUSED) {
    exit_requested = true;
    return 0;
}

bool shell_take_exit_request(void) {
    bool requested = exit_requested;
    exit_requested = false;
    return requested;
}

static const shell_cmd_t builtins[] = {
    {"cd", "Cambia el directorio", cmd_cd},
    {"pwd", "Muestra el directorio", cmd_pwd},
    {"exit", "Cierra la shell", cmd_exit},
    {"clear", "Limpia la terminal", cmd_clear},
    {"history", "Muestra el historial", cmd_history},
    {"alias", "Crea o lista alias", cmd_alias},
    {"unalias", "Elimina un alias", cmd_unalias},
    {"set", "Define variables", cmd_set},
    {"echo", "Imprime texto", cmd_echo},
    {"ver", "Muestra la version", cmd_ver},
    {NULL, NULL, NULL}
};

static bool append_argument(char *output, uint32_t capacity,
                            const char *argument) {
    bool quote = false;
    const char *p = argument ? argument : "";
    for (; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '"') quote = true;
    if (!append_text(output, capacity, " ")) return false;
    if (!quote) return append_text(output, capacity, argument ? argument : "");
    if (!append_text(output, capacity, "\"")) return false;
    for (p = argument ? argument : ""; *p; p++) {
        char part[3] = {0, 0, 0};
        if (*p == '"' || *p == '\\') part[0] = '\\', part[1] = *p;
        else part[0] = *p;
        if (!append_text(output, capacity, part)) return false;
    }
    return append_text(output, capacity, "\"");
}

static bool launch_external(const char *name, int argc, char **argv) {
    char path[VFS_MAX_PATH] = "/SYSTEM/COMMANDS/";
    char argument[SHELL_MAX_CMD] = "";
    append_text(path, sizeof(path), name);
    append_text(path, sizeof(path), ".O");
    append_text(argument, sizeof(argument), name);
    if (!append_argument(argument, sizeof(argument), "--cwd") ||
        !append_argument(argument, sizeof(argument), bk_file_getcwd()))
        return false;
    for (int i = 1; i < argc; i++)
        if (!append_argument(argument, sizeof(argument), argv[i])) return false;
    return bk_app_execute_path_arg(bk_gui_get_desktop(), path, argument);
}

static void execute(char *line) {
    char original[SHELL_MAX_CMD];
    char expanded[SHELL_MAX_CMD];
    char aliased[SHELL_MAX_CMD];
    char *argv[SHELL_MAX_ARGS + 1];
    int argc;

    copy_text(original, sizeof(original), line);
    history_add(original);
    expand_variables(original, expanded, sizeof(expanded));
    copy_text(aliased, sizeof(aliased), expanded);
    argc = parse_args(aliased, argv, SHELL_MAX_ARGS);
    if (!argc) return;

    int alias = pair_find(aliases, SHELL_ALIAS_MAX, argv[0]);
    if (alias >= 0) {
        char merged[SHELL_MAX_CMD];
        copy_text(merged, sizeof(merged), aliases[alias].value);
        for (int i = 1; i < argc; i++) {
            append_text(merged, sizeof(merged), " ");
            append_text(merged, sizeof(merged), argv[i]);
        }
        copy_text(aliased, sizeof(aliased), merged);
        argc = parse_args(aliased, argv, SHELL_MAX_ARGS);
        if (!argc) return;
    }

    for (int i = 0; builtins[i].name; i++) {
        if (bk_runtime_strcmp(argv[0], builtins[i].name) == 0) {
            builtins[i].func(argc, argv);
            return;
        }
    }
    if (!launch_external(argv[0], argc, argv))
        kprintf("%s: comando no encontrado en /SYSTEM/COMMANDS\n", argv[0]);
}

void shell_execute_line(const char *line) {
    char command[SHELL_MAX_CMD];
    if (!line) return;
    copy_text(command, sizeof(command), line);
    execute(command);
}

void shell_run(void) {
    char line[SHELL_MAX_CMD];
    vga_clear();
    kprintf("BlesKernOS Terminal 1.1 - escriba 'help' para obtener ayuda\n");
    while (!exit_requested) {
        kprintf("bles@bleskernos:%s> ", bk_file_getcwd());
        readline(line, sizeof(line));
        execute(line);
    }
    exit_requested = false;
}
