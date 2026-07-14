#include "include/startup_sound.h"
#include "include/sound.h"
#include "include/user_config.h"
#include "include/vga.h"
#include "string.h"

static bool startup_config_value(const char *text, const char *key,
                                 bool fallback) {
    size_t key_length;
    if (!text || !key) return fallback;
    key_length = kstrlen(key);
    while (*text) {
        const char *line = text;
        const char *end = line;
        while (*end && *end != '\r' && *end != '\n') end++;
        if ((size_t)(end - line) > key_length &&
            kstrncmp(line, key, key_length) == 0 && line[key_length] == '=')
            return line[key_length + 1U] != '0';
        text = end;
        while (*text == '\r' || *text == '\n') text++;
    }
    return fallback;
}

bool startup_sound_enabled(void) {
    void *buffer = NULL;
    uint32_t size = 0;
    bool enabled = true;
    if (bk_user_config_read_all(BK_SOUND_CONFIG_PATH, "/SOUND.INI",
                                &buffer, &size) && buffer) {
        enabled = startup_config_value((const char *)buffer,
                                       "StartupEnabled", true);
        kfree(buffer);
    }
    return enabled;
}

bool startup_sound_set_enabled(bool enabled) {
    const char *on = "[SOUND]\r\nStartupEnabled=1\r\n"
                     "StartupSound=/SYSTEM/SOUNDS/START.WAV\r\n";
    const char *off = "[SOUND]\r\nStartupEnabled=0\r\n"
                      "StartupSound=/SYSTEM/SOUNDS/START.WAV\r\n";
    return bk_user_config_write_text(BK_SOUND_CONFIG_PATH, enabled ? on : off);
}

void startup_sound_play(void) {
    if (!startup_sound_enabled()) {
        kprintf("[SOUND] Sonido de inicio desactivado\n");
        return;
    }
    if (!sound_play_file(BK_STARTUP_SOUND_PATH))
        kprintf("[SOUND] Sonido de inicio no disponible\n");
}
