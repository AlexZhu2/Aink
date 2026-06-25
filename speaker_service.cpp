#include "speaker_service.h"

#include "I2S.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static volatile bool s_playing = false;
static TaskHandle_t s_playTask = nullptr;

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

static void start_async_task(TaskFunction_t fn) {
  if (s_playTask != nullptr) {
    return;
  }
  if (xTaskCreate(fn, "spk", 4096, nullptr, 1, &s_playTask) != pdPASS) {
    s_playTask = nullptr;
    Serial.println("[Speaker] play task create failed");
  }
}

void speaker_service_play_boot_chime_async(void) {
  start_async_task(boot_play_task);
}

void speaker_service_play_notify_async(void) {
  start_async_task(notify_play_task);
}

void speaker_service_init(void) {
  I2S_Init();
}

void speaker_service_stop(void) {
  if (s_playTask != nullptr) {
    vTaskDelete(s_playTask);
    s_playTask = nullptr;
  }
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
