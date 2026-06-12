#include "ui_vision.h"

#include "app_locale.h"
#include "camera_service.h"
#include "ui_fonts.h"
#include "vision_service.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_screenVision = nullptr;
static lv_obj_t *s_titleLabel = nullptr;
static lv_obj_t *s_hintLabel = nullptr;
static lv_obj_t *s_bodyLabel = nullptr;
static bool s_captureRequested = false;
static bool s_busy = false;

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
  show_idle();
  lv_scr_load(s_screenVision);
  lv_obj_invalidate(s_screenVision);
}

void ui_vision_leave(void) {
  camera_service_pause();
}

void ui_vision_refresh(void) {
  if (s_screenVision == nullptr) {
    return;
  }
  if (lv_scr_act() != s_screenVision) {
    return;
  }
  lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
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
  if (s_busy) {
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
  lv_label_set_text(s_titleLabel, app_tr(TR_VISION_TITLE));
  lv_label_set_text(s_hintLabel, app_tr(TR_VISION_ANALYZING));
  lv_label_set_text(s_bodyLabel, app_tr(TR_VISION_ANALYZING));
  lv_obj_invalidate(s_screenVision);
}

bool ui_vision_run_capture(void) {
  char result[256];
  const VisionResult code = vision_service_describe_camera(result, sizeof(result));
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
  return code == VISION_RESULT_OK;
}

lv_obj_t *ui_vision_get_screen(void) {
  return s_screenVision;
}
