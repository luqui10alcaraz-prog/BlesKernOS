#ifndef COMPAT_MODE_H
#define COMPAT_MODE_H

#include "types.h"

typedef enum {
    BK_MEMORY_PROFILE_NORMAL = 0,
    BK_MEMORY_PROFILE_LOW,
    BK_MEMORY_PROFILE_TINY
} bk_memory_profile_t;

void compat_mode_init(void);
bk_memory_profile_t compat_mode_profile(void);
bool compat_mode_is_low_memory(void);
bool compat_mode_is_tiny(void);
bool compat_mode_force_vga13h(void);
bool compat_mode_force_vga12h(void);
bool compat_mode_allow_optional_services(void);
bool compat_mode_allow_user_programs(void);
bool compat_mode_allow_pe(void);
bool compat_mode_allow_wallpaper(void);
bool compat_mode_allow_icon_images(void);
bool compat_mode_defer_driver(const char *path);
bool compat_mode_allow_screensaver(void);
bool compat_mode_allow_startup_sound(void);
bool compat_mode_use_compact_language(void);
bool compat_mode_use_front_shadow(void);
/* Escritorio normal fijo. Los perfiles de memoria reducida conservan sus
 * modos VGA de emergencia para poder arrancar con 4-15 MiB. */
bool compat_mode_prefer_800x600(void);
uint32_t compat_mode_task_limit(void);
uint32_t compat_mode_stack_size(const char *name, bool user);
uint32_t compat_mode_gui_target_fps(void);
uint32_t compat_mode_gui_idle_hz(void);
uint32_t compat_mode_timer_hz(void);
const char *compat_mode_name(void);

#endif
