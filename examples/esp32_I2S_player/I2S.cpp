#include "I2S.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

static I2SClass s_speaker;
static bool s_i2sReady = false;

void I2S_Init(void) {
  if (s_i2sReady) {
    return;
  }

  s_speaker.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  s_speaker.setTimeout(1000);
  if (!s_speaker.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[I2S] speaker init failed");
    return;
  }

  s_i2sReady = true;
  Serial.printf("[I2S] speaker ready BCLK=%d LRC=%d DOUT=%d @ %d Hz\r\n",
                PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT, SAMPLE_RATE);
}

void I2S_Shutdown(void) {
  if (!s_i2sReady) {
    return;
  }
  s_speaker.end();
  s_i2sReady = false;
}

void I2S_Write(const char *data, int numData) {
  if (!s_i2sReady || data == nullptr || numData <= 0) {
    return;
  }
  s_speaker.write(reinterpret_cast<const uint8_t *>(data), (size_t)numData);
}

void I2S_PlayTone(uint16_t freqHz, uint32_t durationMs, uint16_t amplitude) {
  if (!s_i2sReady || durationMs == 0 || freqHz == 0 || amplitude == 0) {
    return;
  }

  const size_t totalSamples = ((size_t)SAMPLE_RATE * durationMs) / 1000U;
  int16_t chunk[128];
  size_t produced = 0;
  float phase = 0.0f;
  const float phaseStep = (2.0f * (float)M_PI * (float)freqHz) / (float)SAMPLE_RATE;

  while (produced < totalSamples) {
    const size_t framesLeft = totalSamples - produced;
    const size_t frameCount = framesLeft < 64U ? framesLeft : 64U;
    for (size_t i = 0; i < frameCount; i++) {
      const int16_t sample = (int16_t)(sinf(phase) * (float)amplitude);
      phase += phaseStep;
      if (phase >= 2.0f * (float)M_PI) {
        phase -= 2.0f * (float)M_PI;
      }
      chunk[i * 2] = sample;
      chunk[i * 2 + 1] = sample;
    }
    I2S_Write(reinterpret_cast<const char *>(chunk), (int)(frameCount * 2U * sizeof(int16_t)));
    produced += frameCount;
  }
}
