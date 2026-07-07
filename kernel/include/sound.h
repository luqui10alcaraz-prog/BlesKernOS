#ifndef SOUND_H
#define SOUND_H

#include "types.h"

void sound_init(void);
void sound_play(uint32_t frequency_hz);
void sound_stop(void);
void sound_beep(uint32_t frequency_hz, uint32_t duration_ms);
bool sound_start_tone(uint32_t frequency_hz, uint32_t duration_ms);
bool sound_play_pcm_u8(const uint8_t *samples, uint32_t length,
                       uint16_t sample_rate_hz, uint8_t volume);
void sound_poll(void);
bool sound_pcm_available(void);
bool sound_pcm_is_busy(void);
bool sound_has_sb16(void);
bool sound_sb16_play_tone(uint32_t frequency_hz, uint32_t duration_ms);
bool sound_sb16_is_busy(void);
const char *sound_pcm_name(void);

#endif
