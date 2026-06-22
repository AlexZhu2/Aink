#include "ui_stock_detail.h"

#include "app_locale.h"
#include "stock_service.h"
#include "ui_fonts.h"

#include <Arduino.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define DETAIL_CHART_W   188
#define DETAIL_CHART_H   86
#define DETAIL_CHART_Y   88
#define DETAIL_LINE_W    3
#define DETAIL_PRICE_X   6
#define DETAIL_PRICE_Y   22
#define DETAIL_PRICE_GAP 4
#define DETAIL_CURRENCY_CN_W  44
#define DETAIL_CURRENCY_US_W  28

static lv_obj_t *s_screenDetail = nullptr;
static lv_obj_t *s_nameLabel = nullptr;
static lv_obj_t *s_priceCurrencyLabel = nullptr;
static lv_obj_t *s_priceAmountLabel = nullptr;
static lv_obj_t *s_changeLabel = nullptr;
static lv_obj_t *s_chartFrame = nullptr;
static lv_obj_t *s_chartCanvas = nullptr;
static lv_obj_t *s_chartEmptyLabel = nullptr;
static lv_color_t s_chartBuf[DETAIL_CHART_W * DETAIL_CHART_H];

static int s_quoteIndex = -1;
static StockIntradaySeries s_series = {};

static void style_label(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
}

static void canvas_set_px_safe(lv_obj_t *canvas, int x, int y, lv_color_t color) {
  if (x < 0 || y < 0 || x >= DETAIL_CHART_W || y >= DETAIL_CHART_H) {
    return;
  }
  lv_canvas_set_px(canvas, x, y, color);
}

static void canvas_draw_line(lv_obj_t *canvas, int x0, int y0, int x1, int y1, lv_color_t color) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    for (int t = -(DETAIL_LINE_W / 2); t <= (DETAIL_LINE_W / 2); t++) {
      canvas_set_px_safe(canvas, x0, y0 + t, color);
      if (DETAIL_LINE_W > 2) {
        canvas_set_px_safe(canvas, x0 + t, y0, color);
      }
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void draw_chart(const StockIntradaySeries *series) {
  const lv_color_t white = lv_color_white();
  const lv_color_t black = lv_color_black();

  for (int y = 0; y < DETAIL_CHART_H; y++) {
    for (int x = 0; x < DETAIL_CHART_W; x++) {
      lv_canvas_set_px(s_chartCanvas, x, y, white);
    }
  }

  if (series == nullptr || !series->valid || series->count < 2) {
    if (s_chartEmptyLabel != nullptr) {
      lv_obj_clear_flag(s_chartEmptyLabel, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  if (s_chartEmptyLabel != nullptr) {
    lv_obj_add_flag(s_chartEmptyLabel, LV_OBJ_FLAG_HIDDEN);
  }

  int minP = INT_MAX;
  int maxP = INT_MIN;
  for (int i = 0; i < series->count; i++) {
    const int p = series->priceX100[i];
    if (p < minP) {
      minP = p;
    }
    if (p > maxP) {
      maxP = p;
    }
  }
  if (minP == maxP) {
    minP -= 5;
    maxP += 5;
  }

  const int padX = 2;
  const int padY = 6;
  const int plotW = DETAIL_CHART_W - padX * 2;
  const int plotH = DETAIL_CHART_H - padY * 2;
  const int range = maxP - minP;

  const int openY =
      padY + plotH -
      (int)((int64_t)(series->priceX100[0] - minP) * plotH / range);
  for (int x = padX + 2; x < DETAIL_CHART_W - padX - 2; x += 6) {
    canvas_set_px_safe(s_chartCanvas, x, openY, black);
  }

  for (int i = 1; i < series->count; i++) {
    const int x0 = padX + (i - 1) * plotW / (series->count - 1);
    const int x1 = padX + i * plotW / (series->count - 1);
    const int y0 = padY + plotH - (int)((int64_t)(series->priceX100[i - 1] - minP) * plotH / range);
    const int y1 = padY + plotH - (int)((int64_t)(series->priceX100[i] - minP) * plotH / range);
    canvas_draw_line(s_chartCanvas, x0, y0, x1, y1, black);
  }
}

static void layout_price_row(bool isCn) {
  const lv_coord_t currencyW = isCn ? DETAIL_CURRENCY_CN_W : DETAIL_CURRENCY_US_W;
  lv_obj_set_width(s_priceCurrencyLabel, currencyW);
  lv_obj_set_pos(s_priceCurrencyLabel, DETAIL_PRICE_X, DETAIL_PRICE_Y);
  lv_obj_set_pos(s_priceAmountLabel, DETAIL_PRICE_X + currencyW + DETAIL_PRICE_GAP, DETAIL_PRICE_Y);
}

static void bind_quote_labels(const StockQuote *quote) {
  char label[STOCK_NAME_LEN];
  char price[24];
  char change[32];

  if (quote == nullptr) {
    lv_label_set_text(s_nameLabel, "--");
    lv_label_set_text(s_priceCurrencyLabel, "");
    lv_label_set_text(s_priceAmountLabel, "--");
    lv_label_set_text(s_changeLabel, "--");
    layout_price_row(true);
    return;
  }

  stock_service_format_display_label(quote, label, sizeof(label));
  lv_label_set_text(s_nameLabel, label);

  if (!quote->quoteValid) {
    lv_label_set_text(s_priceCurrencyLabel, "");
    lv_label_set_text(s_priceAmountLabel, "--");
    lv_label_set_text(s_changeLabel, "--");
    layout_price_row(stock_service_is_cn_symbol(quote->symbol));
    return;
  }

  stock_service_format_price_plain(quote, price, sizeof(price));
  stock_service_format_change_detail(quote, change, sizeof(change));
  const bool isCn = stock_service_is_cn_symbol(quote->symbol);
  if (isCn) {
    lv_label_set_text(s_priceCurrencyLabel, STOCK_CURRENCY_YUAN_UTF8);
  } else {
    lv_label_set_text(s_priceCurrencyLabel, "$");
  }
  lv_label_set_text(s_priceAmountLabel, price);
  lv_label_set_text(s_changeLabel, change);
  layout_price_row(isCn);
}

static bool load_intraday(const StockQuote *quote) {
  s_series = {};
  if (quote == nullptr || quote->symbol[0] == '\0') {
    return false;
  }

  if (stock_service_fetch_intraday(quote->symbol, &s_series)) {
    return true;
  }

  if (quote->quoteValid) {
    const int prevX100 = quote->priceX100 - quote->changeAbsX100;
    s_series.priceX100[0] = (int16_t)prevX100;
    s_series.priceX100[1] = (int16_t)quote->priceX100;
    s_series.count = 2;
    s_series.valid = true;
    return true;
  }

  return false;
}

void ui_stock_detail_init(void) {
  s_screenDetail = lv_obj_create(nullptr);
  lv_obj_set_size(s_screenDetail, 200, 180);
  lv_obj_set_style_bg_color(s_screenDetail, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenDetail, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenDetail, LV_OBJ_FLAG_SCROLLABLE);

  s_nameLabel = lv_label_create(s_screenDetail);
  style_label(s_nameLabel, UI_FONT_SM);
  lv_obj_set_width(s_nameLabel, 188);
  lv_label_set_long_mode(s_nameLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_nameLabel, 6, 4);

  s_priceCurrencyLabel = lv_label_create(s_screenDetail);
  style_label(s_priceCurrencyLabel, UI_FONT_CLOCK);
  lv_label_set_long_mode(s_priceCurrencyLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s_priceCurrencyLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  s_priceAmountLabel = lv_label_create(s_screenDetail);
  style_label(s_priceAmountLabel, UI_FONT_CLOCK);
  lv_obj_set_width(s_priceAmountLabel, 140);
  lv_label_set_long_mode(s_priceAmountLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(s_priceAmountLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
  layout_price_row(true);

  s_changeLabel = lv_label_create(s_screenDetail);
  style_label(s_changeLabel, UI_FONT_SM);
  lv_obj_set_width(s_changeLabel, 188);
  lv_label_set_long_mode(s_changeLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(s_changeLabel, 6, 66);

  s_chartFrame = lv_obj_create(s_screenDetail);
  lv_obj_set_size(s_chartFrame, DETAIL_CHART_W, DETAIL_CHART_H);
  lv_obj_set_pos(s_chartFrame, 6, DETAIL_CHART_Y);
  lv_obj_set_style_bg_color(s_chartFrame, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_chartFrame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_chartFrame, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_chartFrame, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_chartFrame, LV_OBJ_FLAG_SCROLLABLE);

  s_chartCanvas = lv_canvas_create(s_chartFrame);
  lv_canvas_set_buffer(s_chartCanvas, s_chartBuf, DETAIL_CHART_W, DETAIL_CHART_H,
                       LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(s_chartCanvas, 0, 0);

  s_chartEmptyLabel = lv_label_create(s_chartFrame);
  style_label(s_chartEmptyLabel, UI_FONT_SM);
  lv_label_set_text(s_chartEmptyLabel, app_tr(TR_STOCK_CHART_NA));
  lv_obj_center(s_chartEmptyLabel);
  lv_obj_add_flag(s_chartEmptyLabel, LV_OBJ_FLAG_HIDDEN);
}

void ui_stock_detail_show(int quoteIndex) {
  const StockQuote *quote = stock_service_get_quote(quoteIndex);
  if (quote == nullptr) {
    return;
  }

  s_quoteIndex = quoteIndex;
  bind_quote_labels(quote);

  s_series = {};
  if (quote->quoteValid && quote->priceX100 >= 0) {
    s_series.priceX100[0] = (int16_t)(quote->priceX100 - quote->changeAbsX100);
    s_series.priceX100[1] = (int16_t)quote->priceX100;
    s_series.count = 2;
    s_series.valid = true;
  }
  draw_chart(&s_series);

  lv_scr_load(s_screenDetail);
  lv_obj_invalidate(s_screenDetail);

  if (load_intraday(quote)) {
    draw_chart(&s_series);
    lv_obj_invalidate(s_chartCanvas);
  }
}

void ui_stock_detail_back_to_list(UiRefreshMode *outRefreshMode) {
  s_quoteIndex = -1;
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NAV;
  }
}

void ui_stock_detail_refresh(void) {
  if (!ui_stock_detail_is_active() || s_quoteIndex < 0) {
    return;
  }

  const StockQuote *quote = stock_service_get_quote(s_quoteIndex);
  bind_quote_labels(quote);
  if (s_chartEmptyLabel != nullptr) {
    lv_label_set_text(s_chartEmptyLabel, app_tr(TR_STOCK_CHART_NA));
  }
  lv_obj_invalidate(s_screenDetail);
}

bool ui_stock_detail_refresh_chart(void) {
  if (!ui_stock_detail_is_active() || s_quoteIndex < 0) {
    return false;
  }

  const StockQuote *quote = stock_service_get_quote(s_quoteIndex);
  load_intraday(quote);
  draw_chart(&s_series);
  lv_obj_invalidate(s_chartCanvas);
  return true;
}

bool ui_stock_detail_is_active(void) {
  return s_screenDetail != nullptr && lv_scr_act() == s_screenDetail;
}

lv_obj_t *ui_stock_detail_get_screen(void) {
  return s_screenDetail;
}
