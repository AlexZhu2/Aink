#include "ui_clock.h"

#include "app_locale.h"
#include "clock_format.h"
#include "settings_api.h"
#include "ui_fonts.h"
#include "ui_home.h"
#include "ui_lvgl.h"
#include "worker_calendar.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOCK_SCREEN_W         200
#define CLOCK_TIME_Y             2
#define CLOCK_DATE_Y            42
#define CLOCK_BAR_ROW_Y0        58
#define CLOCK_BAR_ROW_H         16
#define CLOCK_BAR_LABEL_X        8
#define CLOCK_BAR_X             36
#define CLOCK_BAR_W            120
#define CLOCK_BAR_H             10
#define CLOCK_BAR_BORDER         1
#define CLOCK_BAR_INSET_X        2
#define CLOCK_BAR_INSET_TOP      1
#define CLOCK_BAR_INSET_BOTTOM   3
#define CLOCK_BAR_PCT_X        164
#define CLOCK_HOLIDAY_BOX_X      8
#define CLOCK_HOLIDAY_BOX_Y    108
#define CLOCK_HOLIDAY_BOX_W    184
#define CLOCK_HOLIDAY_BOX_H     42
#define CLOCK_MAKEUP_BAR_X       8
#define CLOCK_MAKEUP_BAR_Y     154
#define CLOCK_MAKEUP_BAR_W     184
#define CLOCK_MAKEUP_BAR_H       28
#define CLOCK_DASH_HINT_Y      186
#define CLOCK_CAL_CELL_W         28
#define CLOCK_CAL_X0             ((CLOCK_SCREEN_W - CLOCK_CAL_CELL_W * 7) / 2)
#define CLOCK_CAL_CELL_H         28
#define CLOCK_CAL_CELL_PX        (CLOCK_CAL_CELL_W - 1)
#define CLOCK_CAL_GRID_Y         26

typedef enum {
  CLOCK_PAGE_DASH = 0,
  CLOCK_PAGE_MONTH = 1,
} ClockPage;

typedef struct {
  lv_obj_t *label;
  lv_obj_t *track;
  lv_obj_t *fill;
  lv_obj_t *pctLabel;
} ProgressRowUi;

static lv_obj_t *s_screenClock = nullptr;
static lv_obj_t *s_pageDash = nullptr;
static lv_obj_t *s_pageMonth = nullptr;
static lv_obj_t *s_timeLabel = nullptr;
static lv_obj_t *s_dateLineLabel = nullptr;
static ProgressRowUi s_progRows[3];
static lv_obj_t *s_holidayBox = nullptr;
static lv_obj_t *s_holidayPrefixLabel = nullptr;
static lv_obj_t *s_holidayNameLabel = nullptr;
static lv_obj_t *s_holidayBadge = nullptr;
static lv_obj_t *s_holidayBadgeLabel = nullptr;
static lv_obj_t *s_makeupBar = nullptr;
static lv_obj_t *s_makeupBarLabel = nullptr;
static lv_obj_t *s_dashHintLabel = nullptr;
static lv_obj_t *s_monthTitleLabel = nullptr;
static lv_obj_t *s_calHeaderLabels[7];
static lv_obj_t *s_calCells[42];
static lv_obj_t *s_calDayLabels[42];
static lv_obj_t *s_monthHintLabel = nullptr;

static lv_color_t s_calCellBuf[42][CLOCK_CAL_CELL_PX * CLOCK_CAL_CELL_PX];
static ClockPage s_page = CLOCK_PAGE_DASH;
static int s_lastRenderedMinute = -1;
static int s_lastRenderedDay = -1;

static void style_small_label(lv_obj_t *label) {
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, UI_FONT_SM, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static void style_time_center(lv_obj_t *label) {
  lv_obj_set_style_text_font(label, UI_FONT_CLOCK, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(label, 0, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static void style_bar_track(lv_obj_t *track) {
  lv_obj_set_size(track, CLOCK_BAR_W, CLOCK_BAR_H);
  lv_obj_set_style_bg_color(track, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(track, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(track, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(track, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
  lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_bar_fill(lv_obj_t *fill) {
  lv_obj_set_style_bg_color(fill, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(fill, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);
  lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
}

static void progress_row_set(ProgressRowUi *row, uint8_t pct) {
  char pctText[8];
  snprintf(pctText, sizeof(pctText), "%d%%", pct);
  lv_label_set_text(row->pctLabel, pctText);

  const int innerW =
      CLOCK_BAR_W - 2 * CLOCK_BAR_BORDER - 2 * CLOCK_BAR_INSET_X;
  const int innerH = CLOCK_BAR_H - 2 * CLOCK_BAR_BORDER - CLOCK_BAR_INSET_TOP -
                     CLOCK_BAR_INSET_BOTTOM;
  const int fillW = (innerW * pct) / 100;
  if (fillW <= 0) {
    lv_obj_add_flag(row->fill, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(row->fill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(row->fill, fillW, innerH);
  lv_obj_set_pos(row->fill, CLOCK_BAR_BORDER + CLOCK_BAR_INSET_X,
                 CLOCK_BAR_BORDER + CLOCK_BAR_INSET_TOP);
}

static ProgressRowUi create_progress_row(lv_obj_t *parent, int rowIndex, AppStrId labelId) {
  ProgressRowUi row = {};
  const int y = CLOCK_BAR_ROW_Y0 + rowIndex * CLOCK_BAR_ROW_H;

  row.label = lv_label_create(parent);
  style_small_label(row.label);
  lv_obj_set_pos(row.label, CLOCK_BAR_LABEL_X, y + 1);
  lv_label_set_text(row.label, app_tr(labelId));

  row.track = lv_obj_create(parent);
  style_bar_track(row.track);
  lv_obj_set_pos(row.track, CLOCK_BAR_X, y + 3);

  row.fill = lv_obj_create(row.track);
  style_bar_fill(row.fill);
  lv_obj_add_flag(row.fill, LV_OBJ_FLAG_HIDDEN);

  row.pctLabel = lv_label_create(parent);
  style_small_label(row.pctLabel);
  lv_obj_set_pos(row.pctLabel, CLOCK_BAR_PCT_X, y + 1);
  lv_label_set_text(row.pctLabel, "0%");

  return row;
}

static const char *weekdayHeaderMonSun(int index) {
  if (index < 0 || index > 6) {
    return "-";
  }
  if (app_locale_get() == APP_LANG_ZH) {
    static const char *kDays[] = {"一", "二", "三", "四", "五", "六", "日"};
    return kDays[index];
  }
  static const char *kDays[] = {"M", "T", "W", "T", "F", "S", "S"};
  return kDays[index];
}

static void update_dashboard(const struct tm *timeinfo) {
  WorkerCalendarView view = {};
  worker_calendar_build(timeinfo, &view);

  if (!view.dataAvailable) {
    lv_label_set_text(s_timeLabel, "--:--");
    lv_label_set_text(s_dateLineLabel, "");
    progress_row_set(&s_progRows[0], 0);
    progress_row_set(&s_progRows[1], 0);
    progress_row_set(&s_progRows[2], 0);
    lv_label_set_text(s_holidayPrefixLabel, app_tr(TR_WORKER_NO_DATA));
    lv_label_set_text(s_holidayNameLabel, "");
    lv_label_set_text(s_holidayBadgeLabel, "");
    lv_obj_add_flag(s_holidayBadge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_makeupBar, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char timeLine[16];
  clock_format_hm(timeLine, sizeof(timeLine), timeinfo, settings_api_clock_use_24h());
  lv_label_set_text(s_timeLabel, timeLine);

  char dateLine[28];
  if (app_locale_get() == APP_LANG_ZH) {
    snprintf(dateLine, sizeof(dateLine), "%s %d月%d日", app_tr_weekday(timeinfo->tm_wday),
             timeinfo->tm_mon + 1, timeinfo->tm_mday);
  } else {
    snprintf(dateLine, sizeof(dateLine), "%s %d/%d", app_tr_weekday(timeinfo->tm_wday),
             timeinfo->tm_mon + 1, timeinfo->tm_mday);
  }
  lv_label_set_text(s_dateLineLabel, dateLine);

  progress_row_set(&s_progRows[0], view.weekProgressPct);
  progress_row_set(&s_progRows[1], view.monthProgressPct);
  progress_row_set(&s_progRows[2], view.yearProgressPct);

  lv_label_set_text(s_holidayPrefixLabel, app_tr(TR_WORKER_NEXT_HOLIDAY));
  if (view.holidayNameShort[0] != '\0') {
    lv_label_set_text(s_holidayNameLabel, view.holidayNameShort);
  } else {
    lv_label_set_text(s_holidayNameLabel, app_tr(TR_WORKER_NO_HOLIDAY));
  }

  if (view.nextHolidayBadge[0] != '\0') {
    lv_label_set_text(s_holidayBadgeLabel, view.nextHolidayBadge);
    lv_obj_clear_flag(s_holidayBadge, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(s_holidayBadgeLabel, "");
    lv_obj_add_flag(s_holidayBadge, LV_OBJ_FLAG_HIDDEN);
  }

  if (view.hasMakeupBar) {
    lv_label_set_text(s_makeupBarLabel, view.makeupBarLine);
    lv_obj_clear_flag(s_makeupBar, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_makeupBar, LV_OBJ_FLAG_HIDDEN);
  }
}

static bool cal_cell_pixel_black(WorkerDayKind kind, bool isToday, bool isEmpty, int x, int y,
                                 int w, int h) {
  if (isEmpty) {
    return false;
  }
  if (isToday) {
    return true;
  }

  const bool isBorder = x == 0 || y == 0 || x == w - 1 || y == h - 1;
  if (isBorder) {
    return true;
  }

  if (kind == WORKER_DAY_HOLIDAY && y >= h - 4 && y <= h - 2) {
    return true;
  }
  if (kind == WORKER_DAY_MAKEUP && y >= 1 && y <= 2 && x >= 2 && x <= w - 3) {
    return true;
  }

  return false;
}

static void cal_cell_paint(lv_obj_t *canvas, WorkerDayKind kind, bool isToday, bool isEmpty) {
  const int w = CLOCK_CAL_CELL_PX;
  const int h = CLOCK_CAL_CELL_PX;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const bool black = cal_cell_pixel_black(kind, isToday, isEmpty, x, y, w, h);
      lv_canvas_set_px(canvas, x, y, black ? lv_color_black() : lv_color_white());
    }
  }
  lv_obj_invalidate(canvas);
}

static void style_cal_cell(lv_obj_t *cell, lv_obj_t *label, const WorkerMonthView *month,
                            int cellIndex) {
  const WorkerDayKind kind = month->dayKind[cellIndex];
  const int ymd = month->dayYmd[cellIndex];
  const bool isToday = ymd != 0 && ymd == month->todayYmd;
  const bool isEmpty = kind == WORKER_DAY_EMPTY;

  lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  cal_cell_paint(cell, kind, isToday, isEmpty);

  if (isEmpty) {
    lv_label_set_text(label, "");
    return;
  }

  char dayText[4];
  snprintf(dayText, sizeof(dayText), "%d", month->dayNum[cellIndex]);
  lv_label_set_text(label, dayText);
  lv_obj_center(label);

  if (isToday) {
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    return;
  }

  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
}

static void update_month_page(const struct tm *timeinfo) {
  WorkerMonthView month = {};
  worker_calendar_build_month(timeinfo, &month);

  if (!month.dataAvailable) {
    lv_label_set_text(s_monthTitleLabel, app_tr(TR_WORKER_NO_DATA));
    for (int i = 0; i < 42; i++) {
      lv_obj_clear_flag(s_calCells[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(s_calDayLabels[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_calDayLabels[i], "");
      cal_cell_paint(s_calCells[i], WORKER_DAY_EMPTY, false, true);
    }
    return;
  }

  char title[20];
  if (app_locale_get() == APP_LANG_ZH) {
    snprintf(title, sizeof(title), "%d年%d月", month.year, month.month);
  } else {
    snprintf(title, sizeof(title), "%d-%02d", month.year, month.month);
  }
  lv_label_set_text(s_monthTitleLabel, title);

  for (int i = 0; i < 7; i++) {
    lv_label_set_text(s_calHeaderLabels[i], weekdayHeaderMonSun(i));
  }

  for (int i = 0; i < 42; i++) {
    style_cal_cell(s_calCells[i], s_calDayLabels[i], &month, i);
  }
}

static void show_page(ClockPage page) {
  s_page = page;
  if (page == CLOCK_PAGE_DASH) {
    lv_obj_clear_flag(s_pageDash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pageMonth, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_pageDash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pageMonth, LV_OBJ_FLAG_HIDDEN);
  }
}

static void update_clock_view(const struct tm *timeinfo) {
  if (timeinfo == nullptr) {
    return;
  }
  update_dashboard(timeinfo);
  update_month_page(timeinfo);
}

void ui_clock_init(void) {
  s_screenClock = lv_obj_create(nullptr);
  ui_lvgl_configure_fullscreen(s_screenClock);
  lv_obj_set_style_bg_color(s_screenClock, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenClock, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenClock, LV_OBJ_FLAG_SCROLLABLE);

  s_pageDash = lv_obj_create(s_screenClock);
  lv_obj_set_size(s_pageDash, CLOCK_SCREEN_W, 200);
  lv_obj_set_pos(s_pageDash, 0, 0);
  lv_obj_set_style_bg_opa(s_pageDash, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_pageDash, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_pageDash, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_pageDash, LV_OBJ_FLAG_SCROLLABLE);

  s_timeLabel = lv_label_create(s_pageDash);
  style_time_center(s_timeLabel);
  lv_obj_set_width(s_timeLabel, CLOCK_SCREEN_W);
  lv_label_set_text(s_timeLabel, "--:--");
  lv_obj_set_pos(s_timeLabel, 0, CLOCK_TIME_Y);

  s_dateLineLabel = lv_label_create(s_pageDash);
  style_small_label(s_dateLineLabel);
  lv_obj_set_width(s_dateLineLabel, CLOCK_SCREEN_W);
  lv_obj_set_style_text_align(s_dateLineLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(s_dateLineLabel, 0, CLOCK_DATE_Y);

  s_progRows[0] = create_progress_row(s_pageDash, 0, TR_WORKER_LABEL_WEEK);
  s_progRows[1] = create_progress_row(s_pageDash, 1, TR_WORKER_LABEL_MONTH);
  s_progRows[2] = create_progress_row(s_pageDash, 2, TR_WORKER_LABEL_YEAR);

  s_holidayBox = lv_obj_create(s_pageDash);
  lv_obj_set_size(s_holidayBox, CLOCK_HOLIDAY_BOX_W, CLOCK_HOLIDAY_BOX_H);
  lv_obj_set_pos(s_holidayBox, CLOCK_HOLIDAY_BOX_X, CLOCK_HOLIDAY_BOX_Y);
  lv_obj_set_style_bg_color(s_holidayBox, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_holidayBox, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_holidayBox, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(s_holidayBox, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(s_holidayBox, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_holidayBox, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_holidayBox, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_holidayBox, LV_OBJ_FLAG_SCROLLABLE);

  s_holidayPrefixLabel = lv_label_create(s_holidayBox);
  style_small_label(s_holidayPrefixLabel);
  lv_obj_set_pos(s_holidayPrefixLabel, 8, 4);
  lv_label_set_text(s_holidayPrefixLabel, app_tr(TR_WORKER_NEXT_HOLIDAY));

  s_holidayNameLabel = lv_label_create(s_holidayBox);
  style_small_label(s_holidayNameLabel);
  lv_obj_set_width(s_holidayNameLabel, 96);
  lv_obj_set_pos(s_holidayNameLabel, 8, 20);
  lv_label_set_text(s_holidayNameLabel, "--");

  s_holidayBadge = lv_obj_create(s_holidayBox);
  lv_obj_set_size(s_holidayBadge, 72, 20);
  lv_obj_set_pos(s_holidayBadge, CLOCK_HOLIDAY_BOX_W - 80, 11);
  lv_obj_set_style_bg_color(s_holidayBadge, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_holidayBadge, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_holidayBadge, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_holidayBadge, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_holidayBadge, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_holidayBadge, LV_OBJ_FLAG_SCROLLABLE);

  s_holidayBadgeLabel = lv_label_create(s_holidayBadge);
  lv_obj_set_style_text_font(s_holidayBadgeLabel, UI_FONT_SM, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_holidayBadgeLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_holidayBadgeLabel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_width(s_holidayBadgeLabel, 72);
  lv_obj_set_style_text_align(s_holidayBadgeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(s_holidayBadgeLabel, LV_LABEL_LONG_CLIP);
  lv_obj_center(s_holidayBadgeLabel);
  lv_label_set_text(s_holidayBadgeLabel, "");

  s_makeupBar = lv_obj_create(s_pageDash);
  lv_obj_set_size(s_makeupBar, CLOCK_MAKEUP_BAR_W, CLOCK_MAKEUP_BAR_H);
  lv_obj_set_pos(s_makeupBar, CLOCK_MAKEUP_BAR_X, CLOCK_MAKEUP_BAR_Y);
  lv_obj_set_style_bg_color(s_makeupBar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_makeupBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_makeupBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_makeupBar, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_makeupBar, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_makeupBar, LV_OBJ_FLAG_SCROLLABLE);

  s_makeupBarLabel = lv_label_create(s_makeupBar);
  lv_obj_set_style_text_font(s_makeupBarLabel, UI_FONT_SM, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_makeupBarLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_makeupBarLabel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_width(s_makeupBarLabel, CLOCK_MAKEUP_BAR_W - 8);
  lv_obj_set_style_text_align(s_makeupBarLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(s_makeupBarLabel, LV_LABEL_LONG_CLIP);
  lv_obj_center(s_makeupBarLabel);
  lv_label_set_text(s_makeupBarLabel, "");

  s_dashHintLabel = lv_label_create(s_pageDash);
  style_small_label(s_dashHintLabel);
  lv_obj_set_width(s_dashHintLabel, CLOCK_SCREEN_W);
  lv_obj_set_style_text_align(s_dashHintLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_dashHintLabel, LV_OPA_50, LV_PART_MAIN);
  lv_label_set_text(s_dashHintLabel, app_tr(TR_CLOCK_SWITCH_MONTH));
  lv_obj_set_pos(s_dashHintLabel, 0, CLOCK_DASH_HINT_Y);

  s_pageMonth = lv_obj_create(s_screenClock);
  lv_obj_set_size(s_pageMonth, CLOCK_SCREEN_W, 200);
  lv_obj_set_pos(s_pageMonth, 0, 0);
  lv_obj_set_style_bg_opa(s_pageMonth, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_pageMonth, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_pageMonth, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_pageMonth, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_pageMonth, LV_OBJ_FLAG_HIDDEN);

  s_monthTitleLabel = lv_label_create(s_pageMonth);
  style_small_label(s_monthTitleLabel);
  lv_obj_set_width(s_monthTitleLabel, CLOCK_SCREEN_W);
  lv_obj_set_style_text_align(s_monthTitleLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(s_monthTitleLabel, 0, 0);

  for (int i = 0; i < 7; i++) {
    s_calHeaderLabels[i] = lv_label_create(s_pageMonth);
    style_small_label(s_calHeaderLabels[i]);
    lv_obj_set_width(s_calHeaderLabels[i], CLOCK_CAL_CELL_W);
    lv_obj_set_style_text_align(s_calHeaderLabels[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_calHeaderLabels[i], CLOCK_CAL_X0 + i * CLOCK_CAL_CELL_W, 14);
  }

  for (int i = 0; i < 42; i++) {
    const int row = i / 7;
    const int col = i % 7;
    const int cellX = CLOCK_CAL_X0 + col * CLOCK_CAL_CELL_W;
    const int cellY = CLOCK_CAL_GRID_Y + row * CLOCK_CAL_CELL_H;

    s_calCells[i] = lv_canvas_create(s_pageMonth);
    lv_canvas_set_buffer(s_calCells[i], s_calCellBuf[i], CLOCK_CAL_CELL_PX, CLOCK_CAL_CELL_PX,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_calCells[i], cellX, cellY);
    cal_cell_paint(s_calCells[i], WORKER_DAY_EMPTY, false, true);

    s_calDayLabels[i] = lv_label_create(s_calCells[i]);
    style_small_label(s_calDayLabels[i]);
    lv_obj_set_width(s_calDayLabels[i], CLOCK_CAL_CELL_PX);
    lv_obj_set_style_bg_opa(s_calDayLabels[i], LV_OPA_TRANSP, LV_PART_MAIN);
  }

  s_monthHintLabel = lv_label_create(s_pageMonth);
  style_small_label(s_monthHintLabel);
  lv_obj_set_width(s_monthHintLabel, CLOCK_SCREEN_W);
  lv_obj_set_style_text_align(s_monthHintLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_monthHintLabel, LV_OPA_30, LV_PART_MAIN);
  lv_label_set_text(s_monthHintLabel, app_tr(TR_CLOCK_SWITCH_DASH));
  lv_obj_add_flag(s_monthHintLabel, LV_OBJ_FLAG_HIDDEN);

  show_page(CLOCK_PAGE_DASH);
  s_lastRenderedMinute = -1;
  s_lastRenderedDay = -1;
}

void ui_clock_show(void) {
  struct tm timeinfo;
  s_page = CLOCK_PAGE_DASH;
  show_page(CLOCK_PAGE_DASH);
  if (getLocalTime(&timeinfo, 0)) {
    update_clock_view(&timeinfo);
    s_lastRenderedMinute = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    s_lastRenderedDay = timeinfo.tm_yday;
  } else {
    lv_label_set_text(s_timeLabel, "--:--");
    s_lastRenderedMinute = -1;
    s_lastRenderedDay = -1;
  }
  lv_scr_load(s_screenClock);
  lv_obj_invalidate(s_screenClock);
}

void ui_clock_refresh(void) {
  if (!ui_clock_is_active()) {
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) {
    return;
  }
  update_clock_view(&timeinfo);
  s_lastRenderedMinute = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  s_lastRenderedDay = timeinfo.tm_yday;
}

void ui_clock_refresh_if_minute(const struct tm *timeinfo) {
  if (!ui_clock_is_active() || timeinfo == nullptr) {
    return;
  }

  const int minuteKey = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  const bool dayChanged = s_lastRenderedDay != timeinfo->tm_yday;
  if (minuteKey == s_lastRenderedMinute && !dayChanged) {
    return;
  }

  update_clock_view(timeinfo);
  s_lastRenderedMinute = minuteKey;
  s_lastRenderedDay = timeinfo->tm_yday;
}

bool ui_clock_handle_btn(BtnAction action, UiRefreshMode *outRefreshMode) {
  if (action != BTN_ACTION_NEXT) {
    return false;
  }

  show_page(s_page == CLOCK_PAGE_DASH ? CLOCK_PAGE_MONTH : CLOCK_PAGE_DASH);
  s_lastRenderedMinute = -1;
  s_lastRenderedDay = -1;
  ui_clock_refresh();
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NAV;
  }
  return true;
}

void ui_clock_on_settings_changed(void) {
  s_lastRenderedMinute = -1;
  s_lastRenderedDay = -1;
  ui_clock_refresh();
  ui_home_refresh_clock();
}

void ui_clock_refresh_locale(void) {
  if (!ui_clock_is_active()) {
    return;
  }
  lv_label_set_text(s_dashHintLabel, app_tr(TR_CLOCK_SWITCH_MONTH));
  lv_label_set_text(s_monthHintLabel, app_tr(TR_CLOCK_SWITCH_DASH));
  lv_label_set_text(s_progRows[0].label, app_tr(TR_WORKER_LABEL_WEEK));
  lv_label_set_text(s_progRows[1].label, app_tr(TR_WORKER_LABEL_MONTH));
  lv_label_set_text(s_progRows[2].label, app_tr(TR_WORKER_LABEL_YEAR));
  lv_label_set_text(s_holidayPrefixLabel, app_tr(TR_WORKER_NEXT_HOLIDAY));
  s_lastRenderedDay = -1;
  ui_clock_refresh();
}

bool ui_clock_is_active(void) {
  return s_screenClock != nullptr && lv_scr_act() == s_screenClock;
}

lv_obj_t *ui_clock_get_screen(void) {
  return s_screenClock;
}
