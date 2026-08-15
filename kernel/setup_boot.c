#include "include/setup_boot.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/elf_loader.h"
#include "../gui/gui.h"
#include "string.h"
#include "stdio.h"

#define SETUP_START_INI "/SYSTEM/USER/START.INI"
#define SETUP_DEFAULT_PROGRAM "/SYSTEM/PROGRAMS/SETUP.BEX"

static bool setup_value(const char *text, const char *key,
                        char *value, uint32_t capacity) {
    uint32_t key_len;
    if (!text || !key || !value || capacity < 2U) return false;
    key_len = (uint32_t)kstrlen(key);
    while (*text) {
        const char *line = text;
        const char *end = line;
        while (*end && *end != '\r' && *end != '\n') end++;
        if ((uint32_t)(end - line) > key_len &&
            kstrncmp(line, key, key_len) == 0 && line[key_len] == '=') {
            uint32_t length = (uint32_t)(end - line - key_len - 1U);
            if (length >= capacity) length = capacity - 1U;
            kmemcpy(value, line + key_len + 1U, length);
            value[length] = '\0';
            return true;
        }
        text = end;
        while (*text == '\r' || *text == '\n') text++;
    }
    return false;
}

bool setup_boot_requested(void) {
    void *raw = NULL;
    uint32_t size = 0U;
    char mode[32];
    bool requested = false;
    if (!vfs_read_all(SETUP_START_INI, &raw, &size) || !raw) return false;
    requested = setup_value((const char *)raw, "Boot", mode, sizeof(mode)) &&
                (kstrcmp(mode, "Setup") == 0 || kstrcmp(mode, "SETUP") == 0);
    kfree(raw);
    return requested;
}

bool setup_boot_launch(void) {
    void *raw = NULL;
    uint32_t size = 0U;
    char program[VFS_MAX_PATH];
    int pid;
    kstrcpy(program, SETUP_DEFAULT_PROGRAM);
    if (vfs_read_all(SETUP_START_INI, &raw, &size) && raw) {
        (void)setup_value((const char *)raw, "Program", program,
                          sizeof(program));
        kfree(raw);
    }
    pid = elf_spawn_program_ex(program, gui_get_desktop(), NULL);
    if (pid < 0) {
        kprintf("[SETUP] No se pudo iniciar %s: %s\n", program,
                elf_last_error());
        return false;
    }
    kprintf("[SETUP] Asistente grafico iniciado, pid=%d\n", pid);
    return true;
}
