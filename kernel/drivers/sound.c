#include "../include/sound.h"
#include "../include/pit.h"
#include "../include/task.h"
#include "../include/pic.h"
#include "../include/memory.h"
#include "../include/vga.h"
#include "../include/driver.h"

#define PIT_BASE_FREQUENCY   1193182U
#define U32_MAX_VALUE        0xFFFFFFFFU

#define SB16_BASE_0          0x220
#define SB16_BASE_1          0x240
#define SB16_BASE_2          0x260
#define SB16_BASE_3          0x280

#define SB16_MIXER_ADDR_OFF  0x04
#define SB16_MIXER_DATA_OFF  0x05
#define SB16_RESET_OFF       0x06
#define SB16_READ_OFF        0x0A
#define SB16_WRITE_OFF       0x0C
#define SB16_READ_STATUS_OFF 0x0E

#define SB16_IRQ_LINE        5
#define SB16_IRQ_MIXER_BITS  0x02
#define SB16_DMA_BUFFER_SIZE 32768U
#define SB16_MIX_RATE_HZ     11025U
#define SB16_MIX_CHUNK       512U
#define SB16_MIX_VOICES      8U
#define SB16_SAMPLE_RATE_HZ  11025U

typedef struct {
    uint16_t base;
    uint8_t version_major;
    uint8_t version_minor;
    volatile uint32_t irq_count;
    volatile bool irq_fired;
    volatile bool playing;
    volatile uint32_t playback_deadline_tick;
    bool present;
} sb16_state_t;

typedef struct {
    const uint8_t *samples;
    uint32_t length;
    /*
     * Posicion de fuente en formato 16.16. Debe ser de 64 bits: con 32 bits
     * la parte entera se desborda al llegar a 65536 muestras y un WAV largo
     * vuelve a reproducirse desde el principio.
     */
    uint64_t position_fp;
    uint32_t step_fp;
    uint8_t volume;
    volatile bool active;
} sb16_mix_voice_t;

static sb16_state_t g_sb16;
static uint8_t g_sb16_dma_buffer[SB16_DMA_BUFFER_SIZE] __attribute__((aligned(65536)));
static sb16_mix_voice_t g_mix_voices[SB16_MIX_VOICES];
static volatile bool g_mixer_worker_started;
static uint8_t g_volume_lut[256][256];
static bool g_volume_lut_ready;
static volatile uint32_t g_tone_deadline_tick;
static volatile bool g_tone_active;
static volatile bool g_tone_sb16;

static uint32_t sound_ms_to_ticks(uint32_t duration_ms, uint32_t pit_hz);
static void sb16_mixer_worker(void *argument);

static void sound_init_volume_lut(void) {
    for (uint32_t volume = 0; volume < 256; volume++) {
        for (uint32_t sample = 0; sample < 256; sample++) {
            int centered = (int)sample - 128;
            int scaled = (centered * (int)volume) / 255;
            scaled += 128;
            if (scaled < 0) scaled = 0;
            if (scaled > 255) scaled = 255;
            g_volume_lut[volume][sample] = (uint8_t)scaled;
        }
    }
    g_volume_lut_ready = true;
}

static uint8_t sound_scale_pcm_sample(uint8_t sample, uint8_t volume) {
    if (volume == 255U) return sample;
    if (g_volume_lut_ready) return g_volume_lut[volume][sample];

    int centered = (int)sample - 128;
    int scaled = (centered * (int)volume) / 255;
    scaled += 128;
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    return (uint8_t)scaled;
}

static uint16_t sound_compute_divisor(uint32_t frequency_hz) {
    uint32_t divisor;

    if (frequency_hz == 0) return 0;

    divisor = PIT_BASE_FREQUENCY / frequency_hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    return (uint16_t)divisor;
}

static void sound_pc_stop(void) {
    uint8_t speaker = inb(0x61);
    outb(0x61, (uint8_t)(speaker & 0xFC));
}

static bool sb16_wait_write_ready(uint16_t base) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb((uint16_t)(base + SB16_WRITE_OFF)) & 0x80) == 0) return true;
    }
    return false;
}

static bool sb16_wait_read_ready(uint16_t base) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb((uint16_t)(base + SB16_READ_STATUS_OFF)) & 0x80) return true;
    }
    return false;
}

static bool sb16_write_dsp(uint16_t base, uint8_t value) {
    if (!sb16_wait_write_ready(base)) return false;
    outb((uint16_t)(base + SB16_WRITE_OFF), value);
    return true;
}

static bool sb16_read_dsp(uint16_t base, uint8_t *value) {
    if (!value || !sb16_wait_read_ready(base)) return false;
    *value = inb((uint16_t)(base + SB16_READ_OFF));
    return true;
}

static void sb16_mixer_write(uint16_t base, uint8_t reg, uint8_t value) {
    outb((uint16_t)(base + SB16_MIXER_ADDR_OFF), reg);
    io_wait();
    outb((uint16_t)(base + SB16_MIXER_DATA_OFF), value);
}

static bool sb16_reset(uint16_t base, uint8_t *major, uint8_t *minor) {
    uint8_t ack;
    uint8_t version_major = 0;
    uint8_t version_minor = 0;

    outb((uint16_t)(base + SB16_RESET_OFF), 1);
    for (uint32_t i = 0; i < 64; i++) io_wait();
    outb((uint16_t)(base + SB16_RESET_OFF), 0);

    if (!sb16_read_dsp(base, &ack) || ack != 0xAA) return false;
    if (!sb16_write_dsp(base, 0xE1)) return false;
    if (!sb16_read_dsp(base, &version_major)) return false;
    if (!sb16_read_dsp(base, &version_minor)) return false;

    if (major) *major = version_major;
    if (minor) *minor = version_minor;
    return true;
}

static void sb16_irq_handler(registers_t *regs UNUSED) {
    if (!g_sb16.present) return;
    (void)inb((uint16_t)(g_sb16.base + SB16_READ_STATUS_OFF));
    g_sb16.irq_count++;
    g_sb16.irq_fired = true;
    g_sb16.playing = false;
    g_sb16.playback_deadline_tick = 0;
}

static bool sb16_program_dma(const void *buffer, uint32_t length) {
    uintptr_t address = (uintptr_t)buffer;
    uint16_t offset;
    uint8_t page;
    uint16_t dma_count;

    if (!buffer || length == 0 || length > SB16_DMA_BUFFER_SIZE) return false;
    if (address > 0x00FFFFFFU) return false;
    if ((address & 0xFFFFU) + length > 0x10000U) return false;

    offset = (uint16_t)(address & 0xFFFFU);
    page = (uint8_t)((address >> 16) & 0xFFU);
    dma_count = (uint16_t)(length - 1U);

    outb(0x0A, 0x05);
    outb(0x0C, 0xFF);
    outb(0x0B, 0x49);
    outb(0x83, page);
    outb(0x02, (uint8_t)(offset & 0xFF));
    outb(0x02, (uint8_t)((offset >> 8) & 0xFF));
    outb(0x03, (uint8_t)(dma_count & 0xFF));
    outb(0x03, (uint8_t)((dma_count >> 8) & 0xFF));
    outb(0x0A, 0x01);
    return true;
}

static bool sb16_start_pcm_u8(const uint8_t *samples, uint32_t length,
                              uint16_t sample_rate_hz, uint8_t volume) {
    uint32_t duration_ms;
    uint32_t pit_hz;

    if (!g_sb16.present || !samples || length == 0 || length > SB16_DMA_BUFFER_SIZE) return false;
    if (sample_rate_hz < 4000U || sample_rate_hz > 44100U) return false;

    for (uint32_t i = 0; i < length; i++)
        g_sb16_dma_buffer[i] = sound_scale_pcm_sample(samples[i], volume);
    if (!sb16_program_dma(g_sb16_dma_buffer, length)) return false;

    sb16_mixer_write(g_sb16.base, 0x22, 0xFF);
    if (!sb16_write_dsp(g_sb16.base, 0xD1)) return false;
    if (!sb16_write_dsp(g_sb16.base, 0x41)) return false;
    if (!sb16_write_dsp(g_sb16.base, (uint8_t)((sample_rate_hz >> 8) & 0xFF))) return false;
    if (!sb16_write_dsp(g_sb16.base, (uint8_t)(sample_rate_hz & 0xFF))) return false;
    if (!sb16_write_dsp(g_sb16.base, 0xC0)) return false;
    if (!sb16_write_dsp(g_sb16.base, 0x00)) return false;
    if (!sb16_write_dsp(g_sb16.base, (uint8_t)((length - 1U) & 0xFF))) return false;
    if (!sb16_write_dsp(g_sb16.base, (uint8_t)(((length - 1U) >> 8) & 0xFF))) return false;

    duration_ms = ((length * 1000U) + sample_rate_hz - 1U) / sample_rate_hz;
    if (duration_ms == 0) duration_ms = 1;
    duration_ms += 80U;

    pit_hz = pit_get_frequency_hz();
    if (pit_hz == 0) pit_hz = 100;

    g_sb16.irq_fired = false;
    g_sb16.playback_deadline_tick = pit_get_ticks() +
        sound_ms_to_ticks(duration_ms, pit_hz);
    g_sb16.playing = true;
    return true;
}

static void sb16_fill_square_tone(uint8_t *buffer, uint32_t sample_count,
                                  uint32_t sample_rate_hz, uint32_t frequency_hz) {
    uint32_t period_samples;
    uint32_t half_period;

    if (!buffer || sample_count == 0) return;
    if (frequency_hz == 0 || frequency_hz >= sample_rate_hz) {
        kmemset(buffer, 128, sample_count);
        return;
    }

    period_samples = sample_rate_hz / frequency_hz;
    if (period_samples < 2U) period_samples = 2U;
    half_period = period_samples / 2U;
    if (half_period == 0U) half_period = 1U;

    for (uint32_t i = 0; i < sample_count; i++) {
        buffer[i] = ((i % period_samples) < half_period) ? 224U : 32U;
    }
}

static void sb16_refresh_busy_state(void) {
    uint32_t now;

    if (!g_sb16.present || !g_sb16.playing) return;
    if (g_sb16.playback_deadline_tick == 0) return;

    now = pit_get_ticks();
    if ((int32_t)(now - g_sb16.playback_deadline_tick) >= 0) {
        /*
         * Safety net: if the SB16 IRQ is lost/misconfigured, do not leave
         * playback marked busy forever.
         */
        g_sb16.playing = false;
        g_sb16.irq_fired = true;
        g_sb16.playback_deadline_tick = 0;
    }
}

static bool sb16_mixer_has_active_voice(void) {
    for (uint32_t i = 0; i < SB16_MIX_VOICES; i++) {
        if (g_mix_voices[i].active) return true;
    }
    return false;
}

static int sb16_clamp_sample(int value) {
    if (value < -128) return -128;
    if (value > 127) return 127;
    return value;
}

static uint32_t sb16_mixer_render(uint8_t *out, uint32_t max_samples) {
    uint32_t produced = 0;

    if (!out || max_samples == 0) return 0;

    for (uint32_t i = 0; i < max_samples; i++) {
        int mixed = 0;
        bool any = false;

        for (uint32_t v = 0; v < SB16_MIX_VOICES; v++) {
            sb16_mix_voice_t *voice = &g_mix_voices[v];
            uint32_t pos;
            uint8_t scaled;

            if (!voice->active) continue;

            pos = voice->position_fp >> 16;
            if (pos >= voice->length) {
                voice->active = false;
                continue;
            }

            scaled = sound_scale_pcm_sample(voice->samples[pos], voice->volume);
            mixed += ((int)scaled - 128);
            voice->position_fp += voice->step_fp;
            any = true;

            if ((voice->position_fp >> 16) >= voice->length) {
                voice->active = false;
            }
        }

        if (!any) break;

        out[i] = (uint8_t)(sb16_clamp_sample(mixed) + 128);
        produced++;
    }

    return produced;
}

static void sb16_mixer_clear_voices(void) {
    for (uint32_t i = 0; i < SB16_MIX_VOICES; i++) {
        g_mix_voices[i].active = false;
        g_mix_voices[i].samples = NULL;
        g_mix_voices[i].length = 0;
        g_mix_voices[i].position_fp = 0;
        g_mix_voices[i].step_fp = 0;
        g_mix_voices[i].volume = 0;
    }
}

static bool sb16_mixer_add_voice(const uint8_t *samples, uint32_t length,
                                 uint16_t sample_rate_hz, uint8_t volume) {
    uint32_t slot = SB16_MIX_VOICES;
    uint32_t largest_pos = 0;

    if (!samples || length == 0) return false;
    if (sample_rate_hz < 4000U || sample_rate_hz > 44100U) return false;

    /*
     * Prefer an empty voice. If all voices are busy, replace the voice that is
     * furthest through its sample. That keeps new high-priority Doom SFX audible
     * without blocking the game thread.
     */
    for (uint32_t i = 0; i < SB16_MIX_VOICES; i++) {
        if (!g_mix_voices[i].active) {
            slot = i;
            break;
        }

        if ((g_mix_voices[i].position_fp >> 16) >= largest_pos) {
            largest_pos = g_mix_voices[i].position_fp >> 16;
            slot = i;
        }
    }

    if (slot >= SB16_MIX_VOICES) return false;

    g_mix_voices[slot].samples = samples;
    g_mix_voices[slot].length = length;
    g_mix_voices[slot].position_fp = 0;
    g_mix_voices[slot].step_fp =
        ((uint32_t)sample_rate_hz << 16) / SB16_MIX_RATE_HZ;
    if (g_mix_voices[slot].step_fp == 0) g_mix_voices[slot].step_fp = 1;
    g_mix_voices[slot].volume = volume;
    g_mix_voices[slot].active = true;
    return true;
}

static void sb16_mixer_worker(void *argument UNUSED) {
    while (true) {
        sb16_refresh_busy_state();

        if (g_sb16.present && !g_sb16.playing && !g_tone_active &&
            sb16_mixer_has_active_voice()) {
            uint32_t produced = sb16_mixer_render(g_sb16_dma_buffer,
                                                  SB16_MIX_CHUNK);

            if (produced > 0) {
                /*
                 * Start one small mixed chunk. The IRQ or timeout will mark it
                 * done, then the worker renders the next chunk.
                 */
                (void)sb16_start_pcm_u8(g_sb16_dma_buffer, produced,
                                        SB16_MIX_RATE_HZ, 255U);
            }
        }

        task_sleep(1);
    }
}

static uint32_t sound_ms_to_ticks(uint32_t duration_ms, uint32_t pit_hz) {
    uint32_t whole_seconds = duration_ms / 1000U;
    uint32_t remaining_ms = duration_ms % 1000U;
    uint32_t ticks;
    uint32_t partial_ticks;

    if (whole_seconds && pit_hz > (U32_MAX_VALUE / whole_seconds)) {
        return U32_MAX_VALUE;
    }

    ticks = whole_seconds * pit_hz;
    partial_ticks = ((remaining_ms * pit_hz) + 999U) / 1000U;
    if (ticks > (U32_MAX_VALUE - partial_ticks)) {
        return U32_MAX_VALUE;
    }

    ticks += partial_ticks;
    return ticks ? ticks : 1U;
}

void sound_poll(void) {
    uint32_t now;

    sb16_refresh_busy_state();

    if (!g_tone_active) return;
    now = pit_get_ticks();
    if ((int32_t)(now - g_tone_deadline_tick) < 0) return;

    g_tone_active = false;
    g_tone_sb16 = false;
    sound_stop();
}

void sound_init(void) {
    static const uint16_t probe_bases[] = {SB16_BASE_0, SB16_BASE_1, SB16_BASE_2, SB16_BASE_3};

    kmemset(&g_sb16, 0, sizeof(g_sb16));
    sb16_mixer_clear_voices();
    g_mixer_worker_started = false;
    g_volume_lut_ready = false;
    sound_init_volume_lut();
    g_tone_deadline_tick = 0;
    g_tone_active = false;
    g_tone_sb16 = false;
    sound_pc_stop();

    for (uint32_t i = 0; i < sizeof(probe_bases) / sizeof(probe_bases[0]); i++) {
        uint8_t major;
        uint8_t minor;

        if (!sb16_reset(probe_bases[i], &major, &minor)) continue;
        g_sb16.base = probe_bases[i];
        g_sb16.version_major = major;
        g_sb16.version_minor = minor;
        g_sb16.present = true;
        sb16_mixer_write(g_sb16.base, 0x80, SB16_IRQ_MIXER_BITS);
        irq_install_handler(SB16_IRQ_LINE, sb16_irq_handler);
        if (task_create("sndmix", sb16_mixer_worker, NULL) >= 0) {
            g_mixer_worker_started = true;
        }
        kprintf("  [SND] SB16 %u.%u en 0x%x irq=%u dma=1 mixer=%s\n",
                g_sb16.version_major, g_sb16.version_minor,
                g_sb16.base, SB16_IRQ_LINE,
                g_mixer_worker_started ? "on" : "off");
        return;
    }

    kprintf("  [SND] PC speaker activo, SB16 no detectada\n");
}

void sound_play(uint32_t frequency_hz) {
    uint16_t divisor;
    uint8_t speaker;

    divisor = sound_compute_divisor(frequency_hz);
    if (!divisor) {
        sound_stop();
        return;
    }

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    speaker = inb(0x61);
    if ((speaker & 0x03) != 0x03) {
        outb(0x61, (uint8_t)(speaker | 0x03));
    }
}

void sound_stop(void) {
    g_tone_active = false;
    g_tone_sb16 = false;
    sb16_mixer_clear_voices();
    sound_pc_stop();
    if (!g_sb16.present) return;

    if (g_sb16.playing) {
        (void)sb16_write_dsp(g_sb16.base, 0xD0);
        g_sb16.playing = false;
    }
    g_sb16.playback_deadline_tick = 0;
    (void)sb16_write_dsp(g_sb16.base, 0xD3);
}

bool sound_play_pcm_u8(const uint8_t *samples, uint32_t length,
                       uint16_t sample_rate_hz, uint8_t volume) {
    if (!g_sb16.present || !samples || length == 0) return false;
    if (!g_mixer_worker_started) {
        if (task_create("sndmix", sb16_mixer_worker, NULL) >= 0) {
            g_mixer_worker_started = true;
        }
    }
    if (!g_mixer_worker_started) return false;

    /*
     * Mixer path:
     * This call is now non-blocking. Doom only registers a voice and returns
     * immediately; the sndmix task renders and starts DMA chunks.
     */
    return sb16_mixer_add_voice(samples, length, sample_rate_hz, volume);
}

void sound_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (!frequency_hz) {
        sound_stop();
        return;
    }
    if (duration_ms == 0) {
        sound_play(frequency_hz);
        return;
    }
    if (!sound_start_tone(frequency_hz, duration_ms)) return;
    while (g_tone_active) task_sleep(1);
}

bool sound_has_sb16(void) {
    return g_sb16.present;
}

bool sound_pcm_available(void) {
    return g_sb16.present;
}

bool sound_start_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    uint32_t sample_count;
    uint32_t actual_ms = duration_ms;
    uint32_t pit_hz;
    uint32_t ticks;
    uint32_t max_duration_ms;

    if (!frequency_hz) {
        sound_stop();
        return false;
    }

    if (g_sb16.playing || g_tone_active || sb16_mixer_has_active_voice())
        sound_stop();

    if (g_sb16.present && duration_ms != 0) {
        max_duration_ms = (SB16_DMA_BUFFER_SIZE * 1000U) / SB16_SAMPLE_RATE_HZ;
        if (duration_ms > max_duration_ms) duration_ms = max_duration_ms;

        sample_count = (SB16_SAMPLE_RATE_HZ * duration_ms) / 1000U;
        if (sample_count == 0) sample_count = 1;
        if (sample_count > SB16_DMA_BUFFER_SIZE) sample_count = SB16_DMA_BUFFER_SIZE;

        sb16_fill_square_tone(g_sb16_dma_buffer, sample_count,
                              SB16_SAMPLE_RATE_HZ, frequency_hz);
        if (!sb16_start_pcm_u8(g_sb16_dma_buffer, sample_count,
                               SB16_SAMPLE_RATE_HZ, 255U))
            return false;

        actual_ms = (sample_count * 1000U) / SB16_SAMPLE_RATE_HZ;
        if (actual_ms == 0) actual_ms = duration_ms;
        g_tone_sb16 = true;
    } else {
        sound_play(frequency_hz);
        g_tone_sb16 = false;
    }

    if (duration_ms == 0) return true;

    pit_hz = pit_get_frequency_hz();
    if (pit_hz == 0) pit_hz = 100;
    ticks = sound_ms_to_ticks(actual_ms, pit_hz);

    g_tone_deadline_tick = pit_get_ticks() + ticks;
    g_tone_active = true;
    return true;
}

bool sound_sb16_play_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    if (!g_sb16.present || frequency_hz == 0 || duration_ms == 0) return false;
    if (!sound_start_tone(frequency_hz, duration_ms)) return false;
    while (g_tone_active) task_sleep(1);
    return true;
}

bool sound_sb16_is_busy(void) {
    sb16_refresh_busy_state();
    return g_sb16.present &&
           (g_sb16.playing || sb16_mixer_has_active_voice());
}

bool sound_pcm_is_busy(void) {
    sb16_refresh_busy_state();
    return g_sb16.present &&
           (g_sb16.playing || sb16_mixer_has_active_voice());
}

const char *sound_pcm_name(void) {
    return g_sb16.present ? "sb16" : "pc-speaker-only";
}

static bool sound_driver_init(void) {
    static const sound_driver_ops_t ops = {
        sound_poll,
        sound_play,
        sound_stop,
        sound_beep,
        sound_start_tone,
        sound_play_pcm_u8,
        sound_pcm_available,
        sound_pcm_is_busy,
        sound_has_sb16,
        sound_sb16_play_tone,
        sound_sb16_is_busy,
        sound_pcm_name,
        100U
    };

    sound_init();
    return sound_register_driver(&ops);
}

const bk_driver_module_t *bleskernos_driver_query(void) {
    static const bk_driver_module_t module = {
        BK_DRIVER_ABI_VERSION,
        sizeof(bk_driver_module_t),
        "sb16",
        "PC speaker y Sound Blaster 16",
        sound_driver_init,
        sound_stop
    };
    return &module;
}
