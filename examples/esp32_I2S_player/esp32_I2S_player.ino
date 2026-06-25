#include "I2S.h"
#include <FS.h>
#include <SD.h>

const int offset = 0x2C;
char data[800];

void setup() {
  Serial.begin(115200);
  SD.begin();
  File file = SD.open("/sound.wav");  // 16kHz/44.1kHz 16-bit stereo PCM recommended
  if (!file) {
    Serial.println("sound.wav not found, playing test tone");
    I2S_Init();
    I2S_PlayTone(440, 500, 7000);
    return;
  }
  file.seek(offset);
  I2S_Init();
  while (file.readBytes(data, sizeof(data))) {
    I2S_Write(data, sizeof(data));
  }
  file.close();
}

void loop() {
}
