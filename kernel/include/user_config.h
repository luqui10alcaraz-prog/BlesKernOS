#ifndef BK_USER_CONFIG_H
#define BK_USER_CONFIG_H

#include "memory.h"
#include "vfs.h"

#define BK_USER_CONFIG_DIR                "/SYSTEM/USER/CONFIG"

#define BK_DESKTOP_CONFIG_PATH            BK_USER_CONFIG_DIR "/DESKTOP.INI"
#define BK_DESKTOP_CONFIG_LEGACY_PATH     "/Desktop.INI"

#define BK_DATETIME_CONFIG_PATH           BK_USER_CONFIG_DIR "/DATETIME.INI"
#define BK_DATETIME_CONFIG_LEGACY_PATH    "/DATETIME.INI"

#define BK_MOUSE_CONFIG_PATH              BK_USER_CONFIG_DIR "/MOUSE.INI"
#define BK_MOUSE_CONFIG_LEGACY_PATH       "/MOUSE.INI"

#define BK_SCREENSAVER_CONFIG_PATH        BK_USER_CONFIG_DIR "/SCREENSV.INI"
#define BK_SCREENSAVER_CONFIG_LEGACY_PATH "/SCREENSV.INI"

static inline void bk_user_config_ensure_dirs(void) {
    (void)vfs_mkdir("/SYSTEM");
    (void)vfs_mkdir("/SYSTEM/USER");
    (void)vfs_mkdir(BK_USER_CONFIG_DIR);
}

static inline bool bk_user_config_read_all(const char *path,
                                           const char *legacy_path,
                                           void **buffer,
                                           uint32_t *size) {
    if (!buffer || !size) return false;
    *buffer = NULL;
    *size = 0;

    if (path && vfs_read_all(path, buffer, size) && *buffer)
        return true;
    if (legacy_path && vfs_read_all(legacy_path, buffer, size) && *buffer)
        return true;

    *buffer = NULL;
    *size = 0;
    return false;
}

static inline bool bk_user_config_write_all(const char *path,
                                            const void *buffer,
                                            uint32_t size) {
    if (!path || (size && !buffer)) return false;
    bk_user_config_ensure_dirs();
    return vfs_write_all(path, buffer, size);
}

static inline bool bk_user_config_write_text(const char *path,
                                             const char *text) {
    return path && text &&
           bk_user_config_write_all(path, text, (uint32_t)kstrlen(text));
}

#endif
