#include "ui_rss.h"

#include "app_locale.h"
#include "rss_service.h"
#include "ui_home.h"
#include "ui_fonts.h"
#include "ui_lvgl.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "qrcode.h"
}

/* Layout B: source + index on top, title-first, QR bottom-right. */
#define RSS_HEADER_Y             4
#define RSS_TITLE_X              8
#define RSS_TITLE_Y             22
#define RSS_TITLE_W            184
#define RSS_TITLE_H             90
#define RSS_QR_PX               90
#define RSS_QR_X               104
#define RSS_QR_Y               104
#define RSS_QR_VERSION           7
#define RSS_QR_MAX_URL_BYTES   154
#define RSS_QR_BUFFER_SIZE     350
#define RSS_AUTO_ADVANCE_MS   30000UL

static_assert(RSS_LINK_LEN - 1 <= RSS_QR_MAX_URL_BYTES,
              "RSS link buffer exceeds QR version 7-L byte capacity");

static lv_obj_t *s_screenRss = nullptr;
static lv_obj_t *s_sourceLabel = nullptr;
static lv_obj_t *s_indexLabel = nullptr;
static lv_obj_t *s_bodyLabel = nullptr;
static lv_obj_t *s_qrCanvas = nullptr;
static lv_color_t *s_qrCanvasBuf = nullptr;
static int s_currentIndex = 0;
static unsigned long s_lastAutoAdvanceMs = 0;
static RssFeedSnapshot s_snap;

static void reset_auto_advance_timer(void) {
  s_lastAutoAdvanceMs = millis();
}

static void reload_snap(void) {
  rss_service_get_snapshot(&s_snap);
}

static void style_label(lv_obj_t *label) {
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static bool ensure_qr_canvas_buffer(void) {
  if (s_qrCanvasBuf != nullptr) {
    return true;
  }

  const size_t bufSize = (size_t)RSS_QR_PX * (size_t)RSS_QR_PX * sizeof(lv_color_t);
  s_qrCanvasBuf = static_cast<lv_color_t *>(
      heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (s_qrCanvasBuf == nullptr) {
    s_qrCanvasBuf = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_8BIT));
  }
  if (s_qrCanvasBuf == nullptr) {
    Serial.printf("[RSS] QR canvas alloc failed size=%u\r\n", (unsigned)bufSize);
    return false;
  }
  return true;
}

static void qr_canvas_clear(void) {
  if (s_qrCanvas == nullptr) {
    return;
  }
  for (int y = 0; y < RSS_QR_PX; y++) {
    for (int x = 0; x < RSS_QR_PX; x++) {
      lv_canvas_set_px(s_qrCanvas, x, y, lv_color_white());
    }
  }
}

static bool qr_canvas_encode_url(const char *url) {
  if (s_qrCanvas == nullptr || url == nullptr || url[0] == '\0') {
    return false;
  }
  if (strncmp(url, "http", 4) != 0) {
    return false;
  }

  static uint8_t qrBuffer[RSS_QR_BUFFER_SIZE];
  QRCode qrcode;
  const size_t urlLen = strlen(url);
  if (urlLen > RSS_QR_MAX_URL_BYTES ||
      qrcode_getBufferSize(RSS_QR_VERSION) > sizeof(qrBuffer) ||
      qrcode_initText(&qrcode, qrBuffer, RSS_QR_VERSION, ECC_LOW, url) != 0) {
    Serial.println("[RSS] QR encode failed");
    qr_canvas_clear();
    return false;
  }

  qr_canvas_clear();

  const uint8_t moduleCount = qrcode.size;
  int scale = RSS_QR_PX / moduleCount;
  if (scale < 1) {
    scale = 1;
  }

  const int qrPixels = moduleCount * scale;
  const int originX = (RSS_QR_PX - qrPixels) / 2;
  const int originY = (RSS_QR_PX - qrPixels) / 2;

  for (uint8_t row = 0; row < moduleCount; row++) {
    for (uint8_t col = 0; col < moduleCount; col++) {
      if (!qrcode_getModule(&qrcode, col, row)) {
        continue;
      }
      for (int sy = 0; sy < scale; sy++) {
        for (int sx = 0; sx < scale; sx++) {
          lv_canvas_set_px(s_qrCanvas, originX + col * scale + sx, originY + row * scale + sy,
                           lv_color_black());
        }
      }
    }
  }
  return true;
}

static void render_current(void) {
  reload_snap();

  lv_label_set_text(s_sourceLabel, app_tr(TR_RSS_SOURCE));

  if (rss_service_is_busy() && (!s_snap.valid || s_snap.count <= 0)) {
    lv_label_set_text(s_indexLabel, "");
    lv_label_set_text(s_bodyLabel, app_tr(TR_RSS_LOADING));
    if (s_qrCanvas != nullptr) {
      lv_obj_add_flag(s_qrCanvas, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (!s_snap.valid || s_snap.count <= 0) {
    lv_label_set_text(s_indexLabel, "");
    lv_label_set_text(s_bodyLabel, app_tr(TR_RSS_NO_DATA));
    if (s_qrCanvas != nullptr) {
      lv_obj_add_flag(s_qrCanvas, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (s_currentIndex >= s_snap.count) {
    s_currentIndex = 0;
  }
  if (s_currentIndex < 0) {
    s_currentIndex = s_snap.count - 1;
  }

  char indexLine[16];
  snprintf(indexLine, sizeof(indexLine), "%d/%d", s_currentIndex + 1, s_snap.count);
  lv_label_set_text(s_indexLabel, indexLine);
  lv_label_set_text(s_bodyLabel, s_snap.items[s_currentIndex].title);

  if (s_qrCanvas == nullptr) {
    return;
  }

  const bool hasQr = qr_canvas_encode_url(s_snap.items[s_currentIndex].link);
  lv_obj_invalidate(s_qrCanvas);
  if (hasQr) {
    lv_obj_clear_flag(s_qrCanvas, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_qrCanvas, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool advance_to_next(UiRefreshMode *outRefreshMode) {
  reload_snap();
  if (!s_snap.valid || s_snap.count <= 0) {
    return false;
  }
  s_currentIndex++;
  reset_auto_advance_timer();
  render_current();
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_FAST;
  }
  return true;
}

void ui_rss_init(void) {
  s_screenRss = lv_obj_create(nullptr);
  ui_lvgl_configure_fullscreen(s_screenRss);
  lv_obj_set_style_bg_color(s_screenRss, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenRss, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenRss, LV_OBJ_FLAG_SCROLLABLE);

  s_sourceLabel = lv_label_create(s_screenRss);
  style_label(s_sourceLabel);
  lv_obj_set_pos(s_sourceLabel, 8, RSS_HEADER_Y);

  s_indexLabel = lv_label_create(s_screenRss);
  style_label(s_indexLabel);
  lv_obj_set_width(s_indexLabel, 56);
  lv_obj_set_style_text_align(s_indexLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_pos(s_indexLabel, 200 - 64, RSS_HEADER_Y);

  s_bodyLabel = lv_label_create(s_screenRss);
  style_label(s_bodyLabel);
  lv_obj_set_width(s_bodyLabel, RSS_TITLE_W);
  lv_obj_set_height(s_bodyLabel, RSS_TITLE_H);
  lv_label_set_long_mode(s_bodyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_bodyLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  lv_obj_set_pos(s_bodyLabel, RSS_TITLE_X, RSS_TITLE_Y);
  lv_label_set_text(s_bodyLabel, app_tr(TR_RSS_NO_DATA));

  if (ensure_qr_canvas_buffer()) {
    s_qrCanvas = lv_canvas_create(s_screenRss);
    lv_canvas_set_buffer(s_qrCanvas, s_qrCanvasBuf, RSS_QR_PX, RSS_QR_PX, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_qrCanvas, RSS_QR_X, RSS_QR_Y);
    lv_obj_set_style_border_width(s_qrCanvas, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_qrCanvas, LV_OBJ_FLAG_HIDDEN);
    qr_canvas_clear();
  }

  s_currentIndex = 0;
}

void ui_rss_show(void) {
  s_currentIndex = 0;
  reload_snap();
  if (!s_snap.valid || s_snap.count <= 0 || rss_service_is_stale()) {
    rss_service_request_fetch(false);
  }
  render_current();
  lv_scr_load(s_screenRss);
  lv_obj_invalidate(s_screenRss);
  reset_auto_advance_timer();
}

void ui_rss_refresh(void) {
  if (!ui_rss_is_active()) {
    return;
  }
  render_current();
  lv_obj_invalidate(s_screenRss);
}

void ui_rss_refresh_locale(void) {
  ui_rss_refresh();
}

bool ui_rss_is_active(void) {
  return s_screenRss != nullptr && lv_scr_act() == s_screenRss;
}

bool ui_rss_handle_btn(BtnAction action, UiRefreshMode *outRefreshMode) {
  if (action == BTN_ACTION_NEXT) {
    return advance_to_next(outRefreshMode);
  }
  if (action == BTN_ACTION_PREV) {
    if (rss_service_is_busy()) {
      return true;
    }
    rss_service_request_fetch(true);
    reset_auto_advance_timer();
    render_current();
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_FAST;
    }
    return true;
  }
  return false;
}

bool ui_rss_service(UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }

  static bool s_wasBusy = false;

  if (rss_service_consume_fresh_fetch()) {
    s_wasBusy = false;
    ui_home_refresh_rss();
    if (ui_rss_is_active()) {
      ui_rss_refresh();
      reset_auto_advance_timer();
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_FAST;
      }
      return true;
    }
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_FAST;
    }
    return true;
  }

  if (!ui_rss_is_active()) {
    return false;
  }

  if (rss_service_is_busy()) {
    s_wasBusy = true;
    return false;
  }

  if (s_wasBusy) {
    s_wasBusy = false;
    ui_rss_refresh();
    reset_auto_advance_timer();
    if (outRefreshMode != nullptr) {
      *outRefreshMode = UI_REFRESH_FAST;
    }
    return true;
  }

  reload_snap();
  if (s_snap.valid && s_snap.count > 1) {
    const unsigned long nowMs = millis();
    if (s_lastAutoAdvanceMs == 0 ||
        (nowMs - s_lastAutoAdvanceMs) >= RSS_AUTO_ADVANCE_MS) {
      return advance_to_next(outRefreshMode);
    }
  }

  return false;
}
