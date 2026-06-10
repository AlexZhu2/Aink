#include "camera_service.h"

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

#include "esp_heap_caps.h"
#include "img_converters.h"

#include <Arduino.h>
#include <esp32-hal-ledc.h>

static bool s_ready = false;

static void setupLedFlash(int pin) {
#if defined(LED_GPIO_NUM)
  ledcAttach(pin, 5000, 8);
#else
  (void)pin;
#endif
}

static camera_config_t buildDefaultConfig(bool usePsram) {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  /* 240x240 is all we need; UXGA DMA buffers exhaust PSRAM alongside LVGL/WiFi. */
  config.frame_size = FRAMESIZE_240X240;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = usePsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  if (usePsram) {
    config.jpeg_quality = 10;
    /* Snapshot use: one buffer, stop when full — avoids FB-OVF while CPU is busy (HTTP). */
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  return config;
}

static void applySensorDefaults(void) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    return;
  }

  if (sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }

  sensor->set_framesize(sensor, FRAMESIZE_240X240);
}

bool camera_service_init(void) {
  if (s_ready) {
    return true;
  }

  const bool hasPsram = psramFound();
  Serial.printf("[Camera] PSRAM=%s freeHeap=%u freePsram=%u\r\n",
                hasPsram ? "yes" : "no",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());

  camera_config_t config = buildDefaultConfig(hasPsram);
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK && hasPsram) {
    Serial.printf("[Camera] PSRAM init failed 0x%x, retrying in DRAM fb_count=1\r\n", err);
    esp_camera_deinit();
    config = buildDefaultConfig(false);
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    err = esp_camera_init(&config);
  }
  if (err != ESP_OK) {
    Serial.printf("[Camera] init failed 0x%x\r\n", err);
    return false;
  }

  applySensorDefaults();

#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  s_ready = true;
  Serial.println("[Camera] ready (240x240 JPEG)");
  return true;
}

bool camera_service_is_ready(void) {
  return s_ready;
}

void camera_service_pause(void) {
  if (!s_ready) {
    return;
  }
  esp_camera_deinit();
  s_ready = false;
}

camera_fb_t *camera_service_capture(void) {
  if (!s_ready) {
    return nullptr;
  }
  return esp_camera_fb_get();
}

void camera_service_release(camera_fb_t *fb) {
  if (fb != nullptr) {
    esp_camera_fb_return(fb);
  }
}

framesize_t camera_service_get_framesize(void) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr || sensor->status.framesize == 0) {
    return FRAMESIZE_INVALID;
  }
  return static_cast<framesize_t>(sensor->status.framesize);
}

bool camera_service_set_framesize(framesize_t size) {
  if (!s_ready) {
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    return false;
  }

  return sensor->set_framesize(sensor, size) == 0;
}

bool camera_service_frame_to_rgb888(const camera_fb_t *fb, uint8_t **outRgb, size_t *outSize) {
  if (fb == nullptr || outRgb == nullptr || outSize == nullptr) {
    return false;
  }

  const size_t rgbSize = static_cast<size_t>(fb->width) * fb->height * 3;
  uint8_t *rgb = static_cast<uint8_t *>(heap_caps_malloc(rgbSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rgb == nullptr) {
    rgb = static_cast<uint8_t *>(malloc(rgbSize));
  }
  if (rgb == nullptr) {
    return false;
  }

  if (!fmt2rgb888(fb->buf, fb->len, fb->format, rgb)) {
    free(rgb);
    return false;
  }

  *outRgb = rgb;
  *outSize = rgbSize;
  return true;
}
