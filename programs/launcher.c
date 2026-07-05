#include "programs.h"
#include "../kernel/include/elf_loader.h"

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
    return dot && program_name_is(dot, ".O");
}

bool program_execute_path(gui_desktop_t *desktop, const char *path) {
    const char *name;

    if (!desktop || !path || !program_is_object(path)) return false;
    name = program_filename(path);

    if (program_name_is(name, "SHELL.O")) {
        shelllauncher_open_from_desktop(desktop);
    } else if (program_name_is(name, "FILEBROW.O") ||
               program_name_is(name, "FILEBROWSER.O")) {
        filebrowser_open_from_desktop(desktop);
    } else if (program_name_is(name, "TEXTEDIT.O")) {
        texteditor_open_from_desktop(desktop);
    } else if (program_name_is(name, "CALC.O")) {
        calculator_open_from_desktop(desktop);
    } else if (program_name_is(name, "PROCMAN.O")) {
        processmanager_open_from_desktop(desktop);
    } else if (program_name_is(name, "MIDAMP.O")) {
        midamp_open_from_desktop(desktop);
    } else if (program_name_is(name, "VIEWER.O")) {
        imageviewer_open_from_desktop(desktop);
    } else if (program_name_is(name, "GEARS.O")) {
        gears_open_from_desktop(desktop);
    } else if (program_name_is(name, "PAINT.O")) {
        paint_open_from_desktop(desktop);
    } else if (program_name_is(name, "GAMES.O")) {
        games_open_from_desktop(desktop);
    } else if (program_name_is(name, "SETTINGS.O")) {
        settings_open_from_desktop(desktop);
    } else {
        return elf_execute_program(path, desktop);
    }
    return true;
}
