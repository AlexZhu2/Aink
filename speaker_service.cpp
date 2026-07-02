#include "speaker_service.h"

#include "I2S.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define WAV_HEADER_BYTES 44U
#define SPEAKER_PLAY_TASK_STACK 6144

struct WavPlayJob {
  uint8_t *wav;
  size_t wavLen;
};

static volatile bool s_playing = false;
static TaskHandle_t s_playTask = nullptr;
static uint8_t *s_ownedWav = nullptr;
static WavPlayJob *s_currentJob = nullptr;

static uint16_t readLe16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t readLe32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void freeOwnedWav(void) {
  if (s_ownedWav != nullptr) {
    free(s_ownedWav);
    s_ownedWav = nullptr;
  }
}

static void run_boot_chime(void) {
  I2S_Init();
  I2S_PlayTone(523, 120, 6000);
  I2S_PlayTone(659, 120, 6000);
  I2S_PlayTone(784, 180, 6000);
}

static void run_notify_chime(void) {
  I2S_Init();
  I2S_PlayTone(880, 90, 5000);
  I2S_PlayTone(988, 140, 5000);
}

static bool playWavBuffer(const uint8_t *wav, size_t wavLen) {
  if (wav == nullptr || wavLen <= WAV_HEADER_BYTES) {
    return false;
  }
  if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
    Serial.println("[Speaker] invalid WAV header");
    return false;
  }

  const uint16_t channels = readLe16(wav + 22);
  const uint32_t sampleRate = readLe32(wav + 24);
  const uint16_t bitsPerSample = readLe16(wav + 34);
  if (channels == 0 || bitsPerSample != 16 || sampleRate == 0) {
    Serial.printf("[Speaker] unsupported WAV ch=%u rate=%u bits=%u\r\n",
                  (unsigned)channels, (unsigned)sampleRate, (unsigned)bitsPerSample);
    return false;
  }

  I2S_InitAtRate(sampleRate);

  const uint8_t *pcm = wav + WAV_HEADER_BYTES;
  const size_t pcmLen = wavLen - WAV_HEADER_BYTES;
  const size_t frameBytes = (size_t)channels * 2U;
  if (pcmLen < frameBytes) {
    return false;
  }

  const size_t totalFrames = pcmLen / frameBytes;
  int16_t stereoChunk[128];
  size_t frameIndex = 0;

  Serial.printf("[Speaker] playing WAV rate=%u ch=%u frames=%u\r\n",
                (unsigned)sampleRate, (unsigned)channels, (unsigned)totalFrames);

  while (frameIndex < totalFrames && s_playTask != nullptr) {
    const size_t framesLeft = totalFrames - frameIndex;
    const size_t batchFrames = framesLeft < 64U ? framesLeft : 64U;

    if (channels == 1) {
      for (size_t i = 0; i < batchFrames; i++) {
        const int16_t sample =
            (int16_t)((uint16_t)pcm[(frameIndex + i) * 2] |
                      ((uint16_t)pcm[(frameIndex + i) * 2 + 1] << 8));
        stereoChunk[i * 2] = sample;
        stereoChunk[i * 2 + 1] = sample;
      }
      I2S_Write(reinterpret_cast<const char *>(stereoChunk),
                (int)(batchFrames * 2U * sizeof(int16_t)));
    } else {
      I2S_Write(reinterpret_cast<const char *>(pcm + frameIndex * frameBytes),
                (int)(batchFrames * frameBytes));
    }
    frameIndex += batchFrames;
    yield();
  }

  return true;
}

static void freePlayJob(void) {
  freeOwnedWav();
  if (s_currentJob != nullptr) {
    free(s_currentJob);
    s_currentJob = nullptr;
  }
}

static void wav_play_task(void *param) {
  WavPlayJob *job = static_cast<WavPlayJob *>(param);
  s_playing = true;
  if (job != nullptr && job->wav != nullptr) {
    (void)playWavBuffer(job->wav, job->wavLen);
    free(job->wav);
    job->wav = nullptr;
  }
  s_ownedWav = nullptr;
  s_currentJob = nullptr;
  free(job);
  s_playing = false;
  s_playTask = nullptr;
  vTaskDelete(nullptr);
}

static void boot_play_task(void *param) {
  (void)param;
  s_playing = true;
  run_boot_chime();
  s_playing = false;
  s_playTask = nullptr;
  vTaskDelete(nullptr);
}

static void notify_play_task(void *param) {
  (void)param;
  s_playing = true;
  run_notify_chime();
  s_playing = false;
  s_playTask = nullptr;
  vTaskDelete(nullptr);
}

static void start_async_task(TaskFunction_t fn, void *param, const char *name) {
  if (s_playTask != nullptr) {
    return;
  }
  if (xTaskCreate(fn, name, SPEAKER_PLAY_TASK_STACK, param, 1, &s_playTask) != pdPASS) {
    s_playTask = nullptr;
    Serial.printf("[Speaker] play task create failed (%s)\r\n", name);
  }
}

void speaker_service_play_boot_chime_async(void) {
  start_async_task(boot_play_task, nullptr, "spk");
}

void speaker_service_play_notify_async(void) {
  start_async_task(notify_play_task, nullptr, "spk");
}

void speaker_service_play_wav_async(uint8_t *wav, size_t wavLen) {
  if (wav == nullptr || wavLen == 0) {
    free(wav);
    return;
  }
  if (s_playTask != nullptr) {
    free(wav);
    return;
  }

  WavPlayJob *job =
      static_cast<WavPlayJob *>(malloc(sizeof(WavPlayJob)));
  if (job == nullptr) {
    free(wav);
    Serial.println("[Speaker] WAV job alloc failed");
    return;
  }
  job->wav = wav;
  job->wavLen = wavLen;
  s_ownedWav = wav;
  s_currentJob = job;
  s_playing = true;
  start_async_task(wav_play_task, job, "spkWav");
  if (s_playTask == nullptr) {
    s_playing = false;
    freePlayJob();
  }
}

void speaker_service_init(void) {
  I2S_Init();
}

void speaker_service_stop(void) {
  if (s_playTask != nullptr) {
    TaskHandle_t task = s_playTask;
    s_playTask = nullptr;
    vTaskDelete(task);
  }
  freePlayJob();
  s_playing = false;
  I2S_Shutdown();
}

bool speaker_service_is_playing(void) {
  return s_playing;
}

void speaker_service_play_tone(uint16_t freqHz, uint32_t durationMs) {
  I2S_Init();
  I2S_PlayTone(freqHz, durationMs, 6000);
}

void speaker_service_play_boot_chime(void) {
  run_boot_chime();
}
