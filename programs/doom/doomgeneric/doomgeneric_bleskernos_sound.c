#include "../../../kernel/include/sound.h"

#include <stdio.h>
#include <string.h>

#include "deh_str.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#define DOOM_DMXSOUND_MIN_LENGTH 48U
#define DOOM_DMXSOUND_SKIP_BYTES 16U

static boolean g_use_sfx_prefix;
static int g_active_handle = -1;
static int g_next_handle = 1;
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

#define BK_SFX_CACHE_MAX 96

typedef struct {
    boolean used;
    char name[16];
    byte *data;
    byte *samples;
    uint32_t length;
    uint16_t sample_rate_hz;
    int lumpnum;
} bk_cached_sfx_t;

static bk_cached_sfx_t g_sfx_cache[BK_SFX_CACHE_MAX];

static void DG_BK_ClearSfxCache(void) {
    for (int i = 0; i < BK_SFX_CACHE_MAX; i++) {
        if (!g_sfx_cache[i].used) continue;
        W_ReleaseLumpNum(g_sfx_cache[i].lumpnum);
        memset(&g_sfx_cache[i], 0, sizeof(g_sfx_cache[i]));
    }
}

static bk_cached_sfx_t *DG_BK_FindCachedSfx(const char *name) {
    if (!name) return NULL;

    for (int i = 0; i < BK_SFX_CACHE_MAX; i++) {
        if (g_sfx_cache[i].used && strcmp(g_sfx_cache[i].name, name) == 0)
            return &g_sfx_cache[i];
    }

    return NULL;
}

static bk_cached_sfx_t *DG_BK_AllocCachedSfx(void) {
    for (int i = 0; i < BK_SFX_CACHE_MAX; i++) {
        if (!g_sfx_cache[i].used) return &g_sfx_cache[i];
    }

    return NULL;
}

static void DG_BK_GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len) {
    if (sfx && sfx->link != NULL) sfx = sfx->link;

    if (g_use_sfx_prefix) {
        snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    } else {
        snprintf(buf, buf_len, "%s", DEH_String(sfx->name));
    }
}

static boolean DG_BK_IsMissingOrSilentSfxName(const char *name)
{
    if (!name) return true;

    /*
     * Doom's first/sentinel sound is "none".
     * With the DMX prefix this becomes "dsnone", but DOOM1.WAD has no DSNONE
     * lump. Calling W_GetNumForName("dsnone") is fatal, so skip it.
     */
    return strcmp(name, "none") == 0 || strcmp(name, "dsnone") == 0;
}


static boolean DG_BK_LoadSfx(sfxinfo_t *sfxinfo,
                             byte **samples_out,
                             uint32_t *length_out,
                             uint16_t *sample_rate_out,
                             int *lumpnum_out) {
    char namebuf[16];
    int lumpnum;
    byte *data;
    uint32_t lumplen;
    uint32_t dmx_length;
    bk_cached_sfx_t *cached;
    bk_cached_sfx_t *slot;

    if (!sfxinfo || !samples_out || !length_out || !sample_rate_out ||
        !lumpnum_out) {
        return false;
    }

    *lumpnum_out = -1;

    DG_BK_GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));
    if (DG_BK_IsMissingOrSilentSfxName(namebuf)) {
        return false;
    }

    /*
     * Important for BlesKernOS:
     * Do not read/cache/release the WAD lump every time a sound starts.
     * That causes a visible hitch exactly when Doom tries to play SFX.
     */
    cached = DG_BK_FindCachedSfx(namebuf);
    if (cached) {
        *sample_rate_out = cached->sample_rate_hz;
        *samples_out = cached->samples;
        *length_out = cached->length;
        return true;
    }

    lumpnum = W_CheckNumForName(namebuf);
    if (lumpnum < 0) {
        return false;
    }

    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = (uint32_t)W_LumpLength((unsigned int)lumpnum);

    if (!data || lumplen < 8U || data[0] != 0x03 || data[1] != 0x00) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    dmx_length = (uint32_t)data[4] |
                 ((uint32_t)data[5] << 8) |
                 ((uint32_t)data[6] << 16) |
                 ((uint32_t)data[7] << 24);

    if (dmx_length > lumplen - 8U || dmx_length <= DOOM_DMXSOUND_MIN_LENGTH) {
        W_ReleaseLumpNum(lumpnum);
        return false;
    }

    slot = DG_BK_AllocCachedSfx();
    if (!slot) {
        /*
         * Cache full. Still play the sound once; the caller will release it
         * after sound_play_pcm_u8() copies it into the SB16 DMA buffer.
         */
        *sample_rate_out = (uint16_t)((uint16_t)data[2] |
                                      ((uint16_t)data[3] << 8));
        *samples_out = data + 8U + DOOM_DMXSOUND_SKIP_BYTES;
        *length_out = dmx_length - (DOOM_DMXSOUND_SKIP_BYTES * 2U);
        *lumpnum_out = lumpnum;
        return true;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    strncpy(slot->name, namebuf, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->data = data;
    slot->lumpnum = lumpnum;
    slot->sample_rate_hz = (uint16_t)((uint16_t)data[2] |
                                      ((uint16_t)data[3] << 8));
    slot->samples = data + 8U + DOOM_DMXSOUND_SKIP_BYTES;
    slot->length = dmx_length - (DOOM_DMXSOUND_SKIP_BYTES * 2U);

    *sample_rate_out = slot->sample_rate_hz;
    *samples_out = slot->samples;
    *length_out = slot->length;
    return true;
}

static uint8_t DG_BK_VolumeToKernel(int vol) {
    if (vol <= 0) return 0;
    if (vol >= 127) return 255;
    return (uint8_t)((vol * 255) / 127);
}

static boolean I_BK_InitSound(boolean use_sfx_prefix) {
    g_use_sfx_prefix = use_sfx_prefix;
    g_active_handle = -1;
    g_next_handle = 1;
    memset(g_sfx_cache, 0, sizeof(g_sfx_cache));
    return sound_pcm_available();
}

static void I_BK_ShutdownSound(void) {
    g_active_handle = -1;
    sound_stop();
    DG_BK_ClearSfxCache();
}

static int I_BK_GetSfxLumpNum(sfxinfo_t *sfxinfo) {
    char namebuf[16];

    DG_BK_GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));
    if (DG_BK_IsMissingOrSilentSfxName(namebuf)) {
        return -1;
    }
    return W_CheckNumForName(namebuf);
}

static void I_BK_UpdateSound(void) {
    if (g_active_handle >= 0 && !sound_pcm_is_busy()) {
        g_active_handle = -1;
    }
}

static void I_BK_UpdateSoundParams(int handle UNUSED,
                                   int vol UNUSED,
                                   int sep UNUSED) {
}

static int I_BK_StartSound(sfxinfo_t *sfxinfo, int channel UNUSED,
                           int vol, int sep UNUSED) {
    byte *samples;
    uint32_t length;
    uint16_t sample_rate_hz;
    int lumpnum;
    int handle;
    boolean ok;

    if (!sound_pcm_available()) return -1;
    if (!DG_BK_LoadSfx(sfxinfo, &samples, &length, &sample_rate_hz, &lumpnum))
        return -1;

    ok = sound_play_pcm_u8(samples, length, sample_rate_hz,
                           DG_BK_VolumeToKernel(vol));
    if (lumpnum >= 0) W_ReleaseLumpNum(lumpnum);
    if (!ok) return -1;

    handle = g_next_handle++;
    if (g_next_handle <= 0) g_next_handle = 1;
    g_active_handle = handle;
    return handle;
}

static void I_BK_StopSound(int handle) {
    if (handle != g_active_handle && handle >= 0) return;
    g_active_handle = -1;
    sound_stop();
}

static boolean I_BK_SoundIsPlaying(int handle) {
    return handle >= 0 &&
           handle == g_active_handle &&
           sound_pcm_is_busy();
}

static void I_BK_CacheSounds(sfxinfo_t *sounds, int num_sounds) {
    /*
     * Optional pre-cache. If Doom calls this during startup/map setup, later
     * SFX starts will avoid WAD lump reads in the gameplay path.
     */
    if (!sounds || num_sounds <= 0) return;

    for (int i = 0; i < num_sounds; i++) {
        byte *samples;
        uint32_t length;
        uint16_t sample_rate_hz;
        int lumpnum;

        if (DG_BK_LoadSfx(&sounds[i], &samples, &length,
                          &sample_rate_hz, &lumpnum)) {
            if (lumpnum >= 0) W_ReleaseLumpNum(lumpnum);
        }
    }
}

static boolean I_BK_InitMusic(void) {
    return true;
}

static void I_BK_ShutdownMusic(void) {
}

static void I_BK_SetMusicVolume(int volume UNUSED) {
}

static void I_BK_PauseMusic(void) {
}

static void I_BK_ResumeMusic(void) {
}

static void *I_BK_RegisterSong(void *data UNUSED, int len UNUSED) {
    return NULL;
}

static void I_BK_UnRegisterSong(void *handle UNUSED) {
}

static void I_BK_PlaySong(void *handle UNUSED, boolean looping UNUSED) {
}

static void I_BK_StopSong(void) {
}

static boolean I_BK_MusicIsPlaying(void) {
    return false;
}

static void I_BK_PollMusic(void) {
}

static snddevice_t g_bk_sound_devices[] = {
    SNDDEVICE_SB,
};

static snddevice_t g_bk_music_devices[] = {
    SNDDEVICE_SB,
};

sound_module_t DG_sound_module = {
    g_bk_sound_devices,
    1,
    I_BK_InitSound,
    I_BK_ShutdownSound,
    I_BK_GetSfxLumpNum,
    I_BK_UpdateSound,
    I_BK_UpdateSoundParams,
    I_BK_StartSound,
    I_BK_StopSound,
    I_BK_SoundIsPlaying,
    I_BK_CacheSounds,
};

music_module_t DG_music_module = {
    g_bk_music_devices,
    1,
    I_BK_InitMusic,
    I_BK_ShutdownMusic,
    I_BK_SetMusicVolume,
    I_BK_PauseMusic,
    I_BK_ResumeMusic,
    I_BK_RegisterSong,
    I_BK_UnRegisterSong,
    I_BK_PlaySong,
    I_BK_StopSong,
    I_BK_MusicIsPlaying,
    I_BK_PollMusic,
};
