#include "../kernel/include/api.h"

static const char *program_filename(const char *path) {
    const char *name = path;
    if (!path) return "";
    while (*path) {
        if (*path++ == '/') name = path;
    }
    return name;
}

static bool program_name_is(const char *name, const char *expected) {
    while (*name && *expected) {
        char left = *name++;
        char right = *expected++;
        if (left >= 'a' && left <= 'z') left = (char)(left - 'a' + 'A');
        if (right >= 'a' && right <= 'z') right = (char)(right - 'a' + 'A');
        if (left != right) return false;
    }
    return *name == '\0' && *expected == '\0';
}

bool program_is_object(const char *path) {
    const char *name = program_filename(path);
    const char *dot = NULL;
    while (*name) {
        if (*name == '.') dot = name;
        name++;
    }
    return dot && (program_name_is(dot, ".O") ||
                   program_name_is(dot, ".CPL") ||
                   program_name_is(dot, ".SCV"));
}

bool program_is_win32_executable(const char *path) {
    const char *name = program_filename(path);
    const char *dot = NULL;

    while (*name) {
        if (*name == '.') dot = name;
        name++;
    }
    return dot && program_name_is(dot, ".EXE");
}

const char *program_launch_arg(void) {
    const char *arg = bk_proc_launch_arg();

    return arg ? arg : "";
}

bool program_execute_path(gui_desktop_t *desktop, const char *path) {
    return program_execute_path_arg(desktop, path, NULL);
}

bool program_execute_path_arg(gui_desktop_t *desktop, const char *path,
                              const char *launch_arg) {
    if (!desktop || !path) return false;

    if (program_is_win32_executable(path)) {
        /* Los argumentos Win32 se agregaran cuando exista PEB/ProcessParameters. */
        (void)launch_arg;
        if (!bk_app_pe_execute(path)) {
            kprintf("[PE] no se pudo ejecutar %s: %s\n", path, bk_app_pe_last_error());
            return false;
        }
        return true;
    }

    if (!program_is_object(path)) return false;
    if (!bk_app_elf_execute(path, desktop, launch_arg)) {
        kprintf("[ELF] no se pudo ejecutar %s: %s\n", path, bk_app_elf_last_error());
        return false;
    }
    return true;
}
