#ifndef STARTUP_SOUND_H
#define STARTUP_SOUND_H

#include "types.h"

#define BK_STARTUP_SOUND_PATH   "/SYSTEM/SOUNDS/START.WAV"
#define BK_SOUND_CONFIG_PATH    "/SYSTEM/USER/CONFIG/SOUND.INI"

bool startup_sound_enabled(void);
bool startup_sound_set_enabled(bool enabled);
void startup_sound_play(void);

#endif
