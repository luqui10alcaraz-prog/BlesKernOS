#include "include/sound.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/vga.h"
#include "string.h"

typedef struct {
    char path[VFS_MAX_PATH];
} sound_file_request_t;

static uint16_t wav_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t wav_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool wav_id(const uint8_t *p, const char *id) {
    return p && id && kmemcmp(p, id, 4) == 0;
}

static void sound_file_worker(void *argument) {
    sound_file_request_t *request = (sound_file_request_t *)argument;
    uint8_t *file = NULL;
    uint32_t size = 0;
    const uint8_t *samples = NULL;
    uint32_t sample_bytes = 0;
    uint32_t sample_rate = 0;
    uint16_t format = 0, channels = 0, bits = 0;
    bool have_format = false;
    bool started = false;
    const char *failure = "WAV invalido";

    if (!request) goto done;
    if (!vfs_read_all(request->path, (void **)&file, &size) || !file) {
        failure = "archivo no encontrado";
        goto done;
    }
    if (size < 12 || !wav_id(file, "RIFF") || !wav_id(file + 8, "WAVE"))
        goto done;

    for (uint32_t offset = 12; offset + 8U <= size;) {
        const uint8_t *chunk = file + offset;
        uint32_t chunk_size = wav_le32(chunk + 4);
        uint32_t data_offset = offset + 8U;
        uint32_t padded;
        if (chunk_size > size - data_offset) goto done;
        if (wav_id(chunk, "fmt ")) {
            if (chunk_size < 16U) goto done;
            format = wav_le16(file + data_offset);
            channels = wav_le16(file + data_offset + 2U);
            sample_rate = wav_le32(file + data_offset + 4U);
            bits = wav_le16(file + data_offset + 14U);
            have_format = true;
        } else if (wav_id(chunk, "data") && !samples) {
            samples = file + data_offset;
            sample_bytes = chunk_size;
        }
        padded = chunk_size + (chunk_size & 1U);
        if (padded > size - data_offset) break;
        offset = data_offset + padded;
    }

    if (!have_format || !samples || !sample_bytes || format != 1U ||
        channels != 1U || bits != 8U || sample_rate < 4000U ||
        sample_rate > 44100U) {
        failure = "se requiere PCM mono de 8 bits";
        goto done;
    }

    started = sound_play_pcm_u8(samples, sample_bytes,
                                (uint16_t)sample_rate, 220U);
    if (!started) failure = "driver PCM ocupado o no disponible";
    if (started) {
        kprintf("[SOUND] WAV %s: %u bytes a %u Hz\n",
                request->path, sample_bytes, sample_rate);
        /* La voz conserva un puntero al WAV: no liberar hasta que termine. */
        while (sound_pcm_is_busy()) task_sleep(2);
    }

done:
    if (request && !started)
        kprintf("[SOUND] No se pudo reproducir %s: %s\n",
                request->path, failure);
    if (file) kfree(file);
    if (request) kfree(request);
    task_exit();
}

bool sound_play_file(const char *path) {
    sound_file_request_t *request;
    if (!path || !path[0] || !sound_pcm_available()) return false;
    request = (sound_file_request_t *)kzalloc(sizeof(*request));
    if (!request) return false;
    kstrncpy(request->path, path, sizeof(request->path) - 1U);
    if (task_create("wav-player", sound_file_worker, request) < 0) {
        kfree(request);
        return false;
    }
    return true;
}
