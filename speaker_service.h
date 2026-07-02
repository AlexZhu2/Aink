#ifndef SPEAKER_SERVICE_H
#define SPEAKER_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void speaker_service_init(void);
void speaker_service_stop(void);
bool speaker_service_is_playing(void);

/** Play a sine tone (blocking). freqHz=0 skips output. */
void speaker_service_play_tone(uint16_t freqHz, uint32_t durationMs);

/** Short ascending chime for boot / UI feedback. */
void speaker_service_play_boot_chime(void);

/** Play in a background task (non-blocking). Takes ownership of wav buffer. */
void speaker_service_play_wav_async(uint8_t *wav, size_t wavLen);

/** Play in a background task (non-blocking). */
void speaker_service_play_boot_chime_async(void);
void speaker_service_play_notify_async(void);

#endif
