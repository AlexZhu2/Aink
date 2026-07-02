#ifndef I2S_H
#define I2S_H

#include <stdint.h>

#define SAMPLE_RATE 16000
#define PIN_I2S_BCLK 3
#define PIN_I2S_LRC 5
#define PIN_I2S_DOUT 6

void I2S_Init(void);
void I2S_InitAtRate(uint32_t sampleRate);
uint32_t I2S_GetSampleRate(void);
void I2S_Shutdown(void);
void I2S_Write(const char *data, int numData);
void I2S_PlayTone(uint16_t freqHz, uint32_t durationMs, uint16_t amplitude);

#endif
