#include "ui_vision.h"

#include "app_locale.h"
#include "camera_service.h"
#include "ui_fonts.h"
#include "vision_service.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#define VISION_TASK_STACK_BYTES 16384
#define VISION_WAIT_ANIM_MS    900U

static lv_obj_t *s_screenVision = nullptr;
static lv_obj_t *s_titleLabel = nullptr;
static lv_obj_t *s_hintLabel = nullptr;
static lv_obj_t *s_bodyLabel = nullptr;
static bool s_captureRequested = false;
static bool s_busy = false;
static TaskHandle_t s_captureTask = nullptr;
static portMUX_TYPE s_captureMux = portMUX_INITIALIZER_UNLOCKED;
static bool s_captureRunning = false;
static bool s_captureDone = false;
static VisionResult s_captureCode = VISION_RESULT_HTTP_FAIL;
static char s_captureText[256];
static unsigned long s_waitAnimLastMs = 0;
static uint8_t s_waitAnimDots = 1;

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

static void set_waiting_text(uint8_t dots) {
  char text[48];
  build_waiting_text(text, sizeof(text), dots);
  lv_label_set_text(s_hintLabel, "");
  lv_label_set_text(s_bodyLabel, text);
  lv_obj_invalidate(s_hintLabel);
  lv_obj_invalidate(s_bodyLabel);
}

static bool capture_running(void) {
  portENTER_CRITICAL(&s_captureMux);
  const bool running = s_captureRunning;
  portEXIT_CRITICAL(&s_captureMux);
  return running;
}

static void vision_capture_task(void *param) {
  (void)param;

  char result[256];
  const VisionResult code = vision_service_describe_camera(result, sizeof(result));

  portENTER_CRITICAL(&s_captureMux);
  s_captureCode = code;
  copy_text(s_captureText, sizeof(s_captureText), result);
  s_captureDone = true;
  s_captureRunning = false;
  s_captureTask = nullptr;
  portEXIT_CRITICAL(&s_captureMux);

  vTaskDelete(nullptr);
}

static void show_idle(void) {
  s_busy = false;
  lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
  lv_label_set_text(s_hintLabel, app_tr(TR_VISION_HINT));
  lv_label_set_text(s_bodyLabel, "-");
}

void ui_vision_init(void) {
  s_screenVision = lv_obj_create(nullptr);
  lv_obj_set_size(s_screenVision, 200, 180);
  lv_obj_set_style_bg_color(s_screenVision, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenVision, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenVision, LV_OBJ_FLAG_SCROLLABLE);

  s_titleLabel = lv_label_create(s_screenVision);
  style_label(s_titleLabel, UI_FONT_MD);
  lv_obj_align(s_titleLabel, LV_ALIGN_TOP_MID, 0, 8);

  s_hintLabel = lv_label_create(s_screenVision);
  style_label(s_hintLabel, UI_FONT_SM);
  lv_obj_set_width(s_hintLabel, 188);
  lv_label_set_long_mode(s_hintLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_hintLabel, LV_ALIGN_TOP_MID, 0, 30);

  s_bodyLabel = lv_label_create(s_screenVision);
  style_label(s_bodyLabel, UI_FONT_SM);
  lv_obj_set_width(s_bodyLabel, 188);
  lv_label_set_long_mode(s_bodyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_bodyLabel, LV_ALIGN_TOP_MID, 0, 56);

  show_idle();
}

void ui_vision_show(void) {
  if (capture_running()) {
    s_busy = true;
    lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
    set_waiting_text(s_waitAnimDots);
  } else {
    show_idle();
  }
  lv_scr_load(s_screenVision);
  lv_obj_invalidate(s_screenVision);
}

void ui_vision_leave(void) {
  if (!capture_running()) {
    camera_service_pause();
  }
}

void ui_vision_refresh(void) {
  if (s_screenVision == nullptr) {
    return;
  }
  if (lv_scr_act() != s_screenVision) {
    return;
  }
  lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
  if (s_busy) {
    set_waiting_text(s_waitAnimDots);
    lv_obj_invalidate(s_screenVision);
    return;
  }
  if (strcmp(lv_label_get_text(s_bodyLabel), app_tr(TR_VISION_ANALYZING)) != 0 &&
      s_hintLabel != nullptr) {
    lv_label_set_text(s_hintLabel, app_tr(TR_VISION_HINT));
  }
  lv_obj_invalidate(s_screenVision);
}

bool ui_vision_is_active(void) {
  return s_screenVision != nullptr && lv_scr_act() == s_screenVision;
}

bool ui_vision_request_capture(void) {
  if (s_busy || capture_running()) {
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
  s_busy = true;
  s_waitAnimDots = 1;
  s_waitAnimLastMs = millis();
  lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
  set_waiting_text(s_waitAnimDots);
  lv_obj_invalidate(s_screenVision);
}

bool ui_vision_run_capture(void) {
  bool alreadyRunning = false;
  portENTER_CRITICAL(&s_captureMux);
  alreadyRunning = s_captureRunning;
  if (!s_captureRunning) {
    s_captureRunning = true;
    s_captureDone = false;
    s_captureCode = VISION_RESULT_HTTP_FAIL;
    s_captureText[0] = '\0';
  }
  portEXIT_CRITICAL(&s_captureMux);

  if (alreadyRunning) {
    return false;
  }

  if (xTaskCreate(vision_capture_task, "vision", VISION_TASK_STACK_BYTES,
                  nullptr, 1, &s_captureTask) != pdPASS) {
    portENTER_CRITICAL(&s_captureMux);
    s_captureRunning = false;
    s_captureDone = false;
    s_captureTask = nullptr;
    portEXIT_CRITICAL(&s_captureMux);

    s_busy = false;
    lv_label_set_text(s_hintLabel, app_tr(TR_VISION_HINT));
    lv_label_set_text(s_bodyLabel, app_tr(TR_VISION_FAIL));
    lv_obj_invalidate(s_screenVision);
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
    s_busy = false;
    lv_label_set_text(s_hintLabel, app_tr(TR_VISION_HINT));
    if (code == VISION_RESULT_OK) {
      lv_label_set_text(s_bodyLabel, result);
    } else {
      lv_label_set_text(s_bodyLabel, vision_error_text(code));
    }
    lv_obj_invalidate(s_titleLabel);
    lv_obj_invalidate(s_hintLabel);
    lv_obj_invalidate(s_bodyLabel);
    lv_obj_invalidate(s_screenVision);
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_NAV;
    }
    Serial.println("[Vision] capture pipeline done");
    return ui_vision_is_active();
  }

  if (!s_busy || !ui_vision_is_active()) {
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
