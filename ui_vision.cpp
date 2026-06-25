#include "ui_vision.h"

#include "app_locale.h"
#include "camera_service.h"
#include "ui_fonts.h"
#include "ui_lvgl.h"
#include "vision_service.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#define VISION_SCREEN_SIZE       200
#define VISION_PREVIEW_DISPLAY_SIZE VISION_SCREEN_SIZE
#define VISION_PREVIEW_BITMAP_BYTES ((VISION_PREVIEW_DISPLAY_SIZE * VISION_PREVIEW_DISPLAY_SIZE + 7) / 8)
#define VISION_CAPTURE_MAX_JPEG_BYTES (64 * 1024)

#define VISION_TASK_STACK_BYTES 16384
#define VISION_PREVIEW_TASK_STACK_BYTES 12288
#define VISION_WAIT_ANIM_MS    800U
#define VISION_PREVIEW_MS      800U

typedef enum {
  VISION_VIEW_PREVIEW = 0,
  VISION_VIEW_BUSY,
  VISION_VIEW_RESULT,
} VisionView;

static VisionView s_view = VISION_VIEW_PREVIEW;

static lv_obj_t *s_screenVision = nullptr;
static lv_obj_t *s_previewFrame = nullptr;
static lv_obj_t *s_previewCanvas = nullptr;
static lv_obj_t *s_busyLabel = nullptr;
static lv_obj_t *s_resultLabel = nullptr;
static lv_color_t *s_previewBuf = nullptr;
static bool s_captureRequested = false;
static bool s_busy = false;
static TaskHandle_t s_captureTask = nullptr;
static TaskHandle_t s_previewTask = nullptr;
static portMUX_TYPE s_captureMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_captureRunning = false;
static bool s_captureDone = false;
static VisionResult s_captureCode = VISION_RESULT_HTTP_FAIL;
static char s_captureText[256];
static uint8_t *s_captureJpeg = nullptr;
static size_t s_captureJpegLen = 0;
static bool s_previewEnabled = false;
static bool s_previewFrozen = false;
static bool s_previewHasFrame = false;
static uint32_t s_previewSeq = 0;
static uint32_t s_previewAppliedSeq = 0;
static uint8_t s_previewBits[VISION_PREVIEW_BITMAP_BYTES];
static unsigned long s_waitAnimLastMs = 0;
static uint8_t s_waitAnimDots = 1;

static void vision_preview_task(void *param);
static void show_preview_view(void);
static void show_busy_view(void);
static void show_result_view(const char *text);
static void set_waiting_text(uint8_t dots);

static const char *vision_error_text(VisionResult result) {
  switch (result) {
    case VISION_RESULT_NO_CAMERA:
      return app_tr(TR_VISION_NO_CAMERA);
    case VISION_RESULT_NO_WIFI:
      return app_tr(TR_VISION_NO_WIFI);
    case VISION_RESULT_NO_API:
      return app_tr(TR_VISION_NO_API);
    case VISION_RESULT_UNSUPPORTED:
      return app_tr(TR_VISION_UNSUPPORTED);
    default:
      return app_tr(TR_VISION_FAIL);
  }
}

static void style_label(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
}

static void build_waiting_text(char *out, size_t outLen, uint8_t dots) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  if (dots < 1) dots = 1;
  if (dots > 3) dots = 3;

  const char *suffix = dots == 1 ? "。" : (dots == 2 ? "。。" : "。。。");
  snprintf(out, outLen, "少女祈福中%s", suffix);
}

static void copy_text(char *dst, size_t dstLen, const char *src) {
  if (dst == nullptr || dstLen == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }

  size_t i = 0;
  while (i + 1 < dstLen && src[i] != '\0') {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static bool ensure_preview_buffer(void) {
  if (s_previewBuf != nullptr) {
    return true;
  }

  const size_t bufSize =
      sizeof(lv_color_t) * VISION_PREVIEW_DISPLAY_SIZE * VISION_PREVIEW_DISPLAY_SIZE;
  s_previewBuf = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (s_previewBuf == nullptr) {
    s_previewBuf = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_8BIT));
  }
  if (s_previewBuf == nullptr) {
    Serial.printf("[Vision] preview buffer alloc failed size=%u\r\n", (unsigned)bufSize);
    return false;
  }

  Serial.printf("[Vision] preview buffer %u bytes allocated in %s\r\n",
                (unsigned)bufSize,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0 ? "PSRAM/heap" : "heap");
  return true;
}

static void clear_preview_canvas(void) {
  if (s_previewCanvas == nullptr) {
    return;
  }

  for (int y = 0; y < VISION_PREVIEW_DISPLAY_SIZE; y++) {
    for (int x = 0; x < VISION_PREVIEW_DISPLAY_SIZE; x++) {
      lv_canvas_set_px(s_previewCanvas, x, y, lv_color_white());
    }
  }
  lv_obj_invalidate(s_previewCanvas);
  if (s_previewFrame != nullptr) {
    lv_obj_invalidate(s_previewFrame);
  }
}

static void apply_preview_bits(const uint8_t *bits) {
  if (bits == nullptr || s_previewCanvas == nullptr) {
    return;
  }

  const int outW = VISION_PREVIEW_DISPLAY_SIZE;
  for (int y = 0; y < outW; y++) {
    for (int x = 0; x < outW; x++) {
      const int bitIndex = y * outW + x;
      const bool white = (bits[bitIndex / 8] & (0x80 >> (bitIndex % 8))) != 0;
      lv_canvas_set_px(s_previewCanvas, x, y, white ? lv_color_white() : lv_color_black());
    }
  }
  lv_obj_invalidate(s_previewCanvas);
  if (s_previewFrame != nullptr) {
    lv_obj_invalidate(s_previewFrame);
  }
}

static void store_preview_bits(const uint8_t *bits, bool alreadyApplied) {
  if (bits == nullptr) {
    return;
  }

  portENTER_CRITICAL(&s_captureMux);
  memcpy(s_previewBits, bits, VISION_PREVIEW_BITMAP_BYTES);
  s_previewHasFrame = true;
  s_previewSeq++;
  if (alreadyApplied) {
    s_previewAppliedSeq = s_previewSeq;
  }
  portEXIT_CRITICAL(&s_captureMux);
}

static bool consume_preview_frame(void) {
  uint8_t bits[VISION_PREVIEW_BITMAP_BYTES];
  bool hasFrame = false;

  portENTER_CRITICAL(&s_captureMux);
  if (s_previewHasFrame && s_previewSeq != s_previewAppliedSeq) {
    memcpy(bits, s_previewBits, VISION_PREVIEW_BITMAP_BYTES);
    s_previewAppliedSeq = s_previewSeq;
    hasFrame = true;
  }
  portEXIT_CRITICAL(&s_captureMux);

  if (!hasFrame) {
    return false;
  }

  apply_preview_bits(bits);
  return true;
}

static void set_preview_state(bool enabled, bool frozen) {
  portENTER_CRITICAL(&s_captureMux);
  s_previewEnabled = enabled;
  s_previewFrozen = frozen;
  portEXIT_CRITICAL(&s_captureMux);
}

static bool preview_capture_allowed(void) {
  bool allowed = false;
  portENTER_CRITICAL(&s_captureMux);
  allowed = s_previewEnabled && !s_previewFrozen && !s_busy && !s_captureRunning;
  portEXIT_CRITICAL(&s_captureMux);
  return allowed;
}

static bool ensure_preview_camera_ready(void) {
  if (!camera_service_lock(500)) {
    Serial.println("[Vision] preview camera busy");
    return false;
  }
  const bool ready = camera_service_start_preview();
  camera_service_unlock();
  return ready;
}

static void pause_preview_camera(void) {
  if (camera_service_lock(300)) {
    camera_service_pause();
    camera_service_unlock();
  }
}

static void set_waiting_text(uint8_t dots) {
  char text[48];
  build_waiting_text(text, sizeof(text), dots);
  lv_label_set_text(s_busyLabel, text);
  lv_obj_invalidate(s_busyLabel);
}

static bool capture_running(void) {
  portENTER_CRITICAL(&s_captureMux);
  const bool running = s_captureRunning;
  portEXIT_CRITICAL(&s_captureMux);
  return running;
}

static void vision_preview_task(void *param) {
  (void)param;

  uint8_t bits[VISION_PREVIEW_BITMAP_BYTES];
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(VISION_PREVIEW_MS));

    if (!preview_capture_allowed()) {
      continue;
    }

    if (!camera_service_lock(30)) {
      continue;
    }

    bool ok = false;
    if (preview_capture_allowed() &&
        camera_service_is_ready() &&
        camera_service_mode() == CAMERA_SERVICE_MODE_PREVIEW) {
      camera_fb_t *fb = camera_service_capture();
      if (fb != nullptr) {
        ok = camera_service_frame_to_mono_bitmap(
            fb, VISION_PREVIEW_DISPLAY_SIZE, VISION_PREVIEW_DISPLAY_SIZE,
            bits, sizeof(bits));
        camera_service_release(fb);
      }
    }

    camera_service_unlock();

    if (ok) {
      store_preview_bits(bits, false);
    }
  }
}

static void ensure_preview_task(void) {
  if (s_previewTask != nullptr) {
    return;
  }

  if (xTaskCreate(vision_preview_task, "vprev", VISION_PREVIEW_TASK_STACK_BYTES,
                  nullptr, 1, &s_previewTask) != pdPASS) {
    s_previewTask = nullptr;
    Serial.println("[Vision] preview task create failed");
  }
}

static uint8_t *alloc_jpeg_copy(size_t len) {
  uint8_t *copy = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (copy == nullptr) {
    copy = static_cast<uint8_t *>(malloc(len));
  }
  return copy;
}

static VisionResult capture_frame_for_ai(uint8_t **outJpeg, size_t *outLen) {
  if (outJpeg == nullptr || outLen == nullptr) {
    return VISION_RESULT_CAPTURE_FAIL;
  }
  *outJpeg = nullptr;
  *outLen = 0;

  if (!camera_service_lock(1600)) {
    Serial.println("[Vision] camera busy while capture");
    return VISION_RESULT_CAPTURE_FAIL;
  }

  VisionResult status = VISION_RESULT_CAPTURE_FAIL;
  camera_fb_t *fb = nullptr;
  uint8_t previewBits[VISION_PREVIEW_BITMAP_BYTES];
  bool previewOk = false;

  if (!camera_service_start_photo()) {
    status = VISION_RESULT_NO_CAMERA;
  } else {
    fb = camera_service_capture();
    if (fb == nullptr || fb->len == 0 || fb->len > VISION_CAPTURE_MAX_JPEG_BYTES) {
      Serial.printf("[Vision] capture fail fb=%p len=%u\r\n",
                    (void *)fb, fb != nullptr ? (unsigned)fb->len : 0U);
      status = VISION_RESULT_CAPTURE_FAIL;
    } else {
      previewOk = camera_service_frame_to_mono_bitmap(
          fb, VISION_PREVIEW_DISPLAY_SIZE, VISION_PREVIEW_DISPLAY_SIZE,
          previewBits, sizeof(previewBits));

      uint8_t *copy = alloc_jpeg_copy(fb->len);
      if (copy == nullptr) {
        Serial.printf("[Vision] jpeg copy alloc failed len=%u\r\n", (unsigned)fb->len);
        status = VISION_RESULT_CAPTURE_FAIL;
      } else {
        memcpy(copy, fb->buf, fb->len);
        *outJpeg = copy;
        *outLen = fb->len;
        status = VISION_RESULT_OK;
        Serial.printf("[Vision] frozen JPEG %u bytes\r\n", (unsigned)fb->len);
      }
    }
  }

  if (fb != nullptr) {
    camera_service_release(fb);
  }

  camera_service_pause();
  camera_service_unlock();

  if (status == VISION_RESULT_OK && previewOk) {
    apply_preview_bits(previewBits);
    store_preview_bits(previewBits, true);
  }

  return status;
}

static void vision_capture_task(void *param) {
  (void)param;

  char result[256];
  result[0] = '\0';

  uint8_t *jpeg = nullptr;
  size_t jpegLen = 0;

  portENTER_CRITICAL(&s_captureMux);
  jpeg = s_captureJpeg;
  jpegLen = s_captureJpegLen;
  s_captureJpeg = nullptr;
  s_captureJpegLen = 0;
  portEXIT_CRITICAL(&s_captureMux);

  VisionResult code = VISION_RESULT_CAPTURE_FAIL;
  if (jpeg != nullptr && jpegLen > 0) {
    code = vision_service_describe_jpeg(jpeg, jpegLen, result, sizeof(result));
  }
  free(jpeg);

  portENTER_CRITICAL(&s_captureMux);
  s_captureCode = code;
  copy_text(s_captureText, sizeof(s_captureText), result);
  s_captureDone = true;
  s_captureRunning = false;
  s_captureTask = nullptr;
  portEXIT_CRITICAL(&s_captureMux);

  vTaskDelete(nullptr);
}

static void show_preview_view(void) {
  s_view = VISION_VIEW_PREVIEW;
  s_busy = false;

  lv_obj_clear_flag(s_previewFrame, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_busyLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_resultLabel, LV_OBJ_FLAG_HIDDEN);

  set_preview_state(ensure_preview_camera_ready(), false);
  lv_obj_invalidate(s_screenVision);
}

static void show_busy_view(void) {
  s_view = VISION_VIEW_BUSY;
  s_busy = true;

  lv_obj_clear_flag(s_previewFrame, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_busyLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_resultLabel, LV_OBJ_FLAG_HIDDEN);

  set_preview_state(true, true);
  set_waiting_text(s_waitAnimDots);
  lv_obj_invalidate(s_screenVision);
}

static void show_result_view(const char *text) {
  s_view = VISION_VIEW_RESULT;
  s_busy = false;

  set_preview_state(false, false);
  pause_preview_camera();

  lv_obj_add_flag(s_previewFrame, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_busyLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_resultLabel, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_resultLabel, text != nullptr ? text : "-");
  lv_obj_invalidate(s_resultLabel);
  lv_obj_invalidate(s_screenVision);
}

void ui_vision_init(void) {
  s_screenVision = lv_obj_create(nullptr);
  ui_lvgl_configure_fullscreen(s_screenVision);
  lv_obj_set_style_bg_color(s_screenVision, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenVision, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenVision, LV_OBJ_FLAG_SCROLLABLE);

  s_previewFrame = lv_obj_create(s_screenVision);
  lv_obj_set_size(s_previewFrame, VISION_SCREEN_SIZE, VISION_SCREEN_SIZE);
  lv_obj_set_pos(s_previewFrame, 0, 0);
  lv_obj_set_style_bg_color(s_previewFrame, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_previewFrame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_previewFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_previewFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_previewFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(s_previewFrame, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_previewFrame, LV_OBJ_FLAG_SCROLLABLE);

  if (ensure_preview_buffer()) {
    s_previewCanvas = lv_canvas_create(s_previewFrame);
    lv_canvas_set_buffer(s_previewCanvas, s_previewBuf,
                         VISION_PREVIEW_DISPLAY_SIZE, VISION_PREVIEW_DISPLAY_SIZE,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_previewCanvas, 0, 0);
    clear_preview_canvas();
  }

  s_busyLabel = lv_label_create(s_screenVision);
  style_label(s_busyLabel, UI_FONT_SM);
  lv_obj_set_width(s_busyLabel, 188);
  lv_label_set_long_mode(s_busyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_bg_color(s_busyLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_busyLabel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_align(s_busyLabel, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_add_flag(s_busyLabel, LV_OBJ_FLAG_HIDDEN);

  s_resultLabel = lv_label_create(s_screenVision);
  style_label(s_resultLabel, UI_FONT_SM);
  lv_obj_set_width(s_resultLabel, 180);
  lv_label_set_long_mode(s_resultLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_resultLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(s_resultLabel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(s_resultLabel, LV_OBJ_FLAG_HIDDEN);

  ensure_preview_task();
  show_preview_view();
}

void ui_vision_show(void) {
  ensure_preview_task();
  if (capture_running() || s_busy) {
    show_busy_view();
  } else {
    show_preview_view();
  }
  lv_scr_load(s_screenVision);
  lv_obj_invalidate(s_screenVision);
}

void ui_vision_leave(void) {
  set_preview_state(false, false);
  if (!capture_running()) {
    pause_preview_camera();
  }
}

void ui_vision_refresh(void) {
  if (s_screenVision == nullptr || lv_scr_act() != s_screenVision) {
    return;
  }
  if (s_view == VISION_VIEW_BUSY) {
    set_waiting_text(s_waitAnimDots);
  }
  lv_obj_invalidate(s_screenVision);
}

bool ui_vision_is_active(void) {
  return s_screenVision != nullptr && lv_scr_act() == s_screenVision;
}

bool ui_vision_is_result_view(void) {
  return s_view == VISION_VIEW_RESULT;
}

void ui_vision_restart_preview(void) {
  if (!ui_vision_is_active() || s_busy || capture_running()) {
    return;
  }
  show_preview_view();
}

bool ui_vision_request_capture(void) {
  if (s_busy || capture_running() || s_view != VISION_VIEW_PREVIEW) {
    return false;
  }
  s_captureRequested = true;
  return true;
}

bool ui_vision_consume_capture_request(void) {
  if (!s_captureRequested) {
    return false;
  }
  s_captureRequested = false;
  return true;
}

void ui_vision_set_busy(void) {
  s_waitAnimDots = 1;
  s_waitAnimLastMs = millis();
  show_busy_view();
}

bool ui_vision_run_capture(void) {
  if (capture_running()) {
    return false;
  }

  show_busy_view();

  uint8_t *jpeg = nullptr;
  size_t jpegLen = 0;
  const VisionResult prep = capture_frame_for_ai(&jpeg, &jpegLen);
  if (prep != VISION_RESULT_OK) {
    free(jpeg);
    show_result_view(vision_error_text(prep));
    return false;
  }

  portENTER_CRITICAL(&s_captureMux);
  s_captureRunning = true;
  s_captureDone = false;
  s_captureCode = VISION_RESULT_HTTP_FAIL;
  s_captureText[0] = '\0';
  s_captureJpeg = jpeg;
  s_captureJpegLen = jpegLen;
  portEXIT_CRITICAL(&s_captureMux);

  if (xTaskCreate(vision_capture_task, "vision", VISION_TASK_STACK_BYTES,
                  nullptr, 1, &s_captureTask) != pdPASS) {
    uint8_t *staleJpeg = nullptr;

    portENTER_CRITICAL(&s_captureMux);
    staleJpeg = s_captureJpeg;
    s_captureJpeg = nullptr;
    s_captureJpegLen = 0;
    s_captureRunning = false;
    s_captureDone = false;
    s_captureTask = nullptr;
    portEXIT_CRITICAL(&s_captureMux);

    free(staleJpeg);
    show_result_view(app_tr(TR_VISION_FAIL));
    Serial.println("[Vision] task create failed");
    return false;
  }

  return true;
}

bool ui_vision_service(UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }
  if (s_screenVision == nullptr) {
    return false;
  }

  bool done = false;
  VisionResult code = VISION_RESULT_HTTP_FAIL;
  char result[256];
  result[0] = '\0';

  portENTER_CRITICAL(&s_captureMux);
  if (s_captureDone) {
    done = true;
    code = s_captureCode;
    copy_text(result, sizeof(result), s_captureText);
    s_captureDone = false;
  }
  portEXIT_CRITICAL(&s_captureMux);

  if (done) {
    if (code == VISION_RESULT_OK) {
      show_result_view(result);
    } else {
      show_result_view(vision_error_text(code));
    }
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_NAV;
    }
    Serial.println("[Vision] capture pipeline done");
    return ui_vision_is_active();
  }

  if (!ui_vision_is_active()) {
    return false;
  }

  if (s_view == VISION_VIEW_PREVIEW && !s_busy) {
    if (!consume_preview_frame()) {
      return false;
    }
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_PREVIEW;
    }
    return true;
  }

  if (s_view != VISION_VIEW_BUSY) {
    return false;
  }

  const unsigned long now = millis();
  if ((unsigned long)(now - s_waitAnimLastMs) < VISION_WAIT_ANIM_MS) {
    return false;
  }

  s_waitAnimLastMs = now;
  s_waitAnimDots = (s_waitAnimDots % 3U) + 1U;
  set_waiting_text(s_waitAnimDots);
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_FAST;
  }
  return true;
}

bool ui_vision_is_busy(void) {
  return s_busy || capture_running();
}

lv_obj_t *ui_vision_get_screen(void) {
  return s_screenVision;
}
