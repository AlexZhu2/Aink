#include "ui_life.h"

#include "app_locale.h"
#include "epaper_canvas.h"
#include "EPD_1in54_V2.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <string.h>

#define LIFE_COLS           50
#define LIFE_ROWS           45
#define LIFE_CELL_PX        4
#define LIFE_CANVAS_W       (LIFE_COLS * LIFE_CELL_PX)
#define LIFE_CANVAS_H       (LIFE_ROWS * LIFE_CELL_PX)
#define LIFE_TICK_MS        600U
#define LIFE_RANDOM_DENSITY 28U

static_assert(LIFE_CANVAS_W == EPD_1IN54_V2_WIDTH, "life grid width must fill main area");
static_assert(LIFE_CANVAS_H == EPAPER_MAIN_HEIGHT, "life grid height must fill main area");

static lv_obj_t *s_screenLife = nullptr;
static lv_obj_t *s_canvas = nullptr;
static lv_color_t *s_canvasBuf = nullptr;

static bool s_grid[LIFE_ROWS][LIFE_COLS];
static bool s_next[LIFE_ROWS][LIFE_COLS];
static bool s_running = true;
static unsigned long s_lastTickMs = 0;

static int count_neighbors_toroidal(int row, int col) {
  int count = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      const int y = (row + dy + LIFE_ROWS) % LIFE_ROWS;
      const int x = (col + dx + LIFE_COLS) % LIFE_COLS;
      if (s_grid[y][x]) {
        count++;
      }
    }
  }
  return count;
}

static void life_step(void) {
  for (int y = 0; y < LIFE_ROWS; y++) {
    for (int x = 0; x < LIFE_COLS; x++) {
      const int n = count_neighbors_toroidal(y, x);
      const bool alive = s_grid[y][x];
      if (alive) {
        s_next[y][x] = (n == 2 || n == 3);
      } else {
        s_next[y][x] = (n == 3);
      }
    }
  }
  memcpy(s_grid, s_next, sizeof(s_grid));
}

static void life_randomize(void) {
  for (int y = 0; y < LIFE_ROWS; y++) {
    for (int x = 0; x < LIFE_COLS; x++) {
      s_grid[y][x] = (esp_random() % 100U) < LIFE_RANDOM_DENSITY;
    }
  }
}

static void life_render_canvas(void) {
  if (s_canvas == nullptr) {
    return;
  }

  const lv_color_t black = lv_color_black();
  const lv_color_t white = lv_color_white();

  for (int y = 0; y < LIFE_ROWS; y++) {
    for (int x = 0; x < LIFE_COLS; x++) {
      const lv_color_t color = s_grid[y][x] ? black : white;
      const int px0 = x * LIFE_CELL_PX;
      const int py0 = y * LIFE_CELL_PX;
      for (int dy = 0; dy < LIFE_CELL_PX; dy++) {
        for (int dx = 0; dx < LIFE_CELL_PX; dx++) {
          lv_canvas_set_px(s_canvas, px0 + dx, py0 + dy, color);
        }
      }
    }
  }
  lv_obj_invalidate(s_canvas);
}

static bool ensure_canvas_buffer(void) {
  if (s_canvasBuf != nullptr) {
    return true;
  }

  const size_t bufSize = (size_t)LIFE_CANVAS_W * (size_t)LIFE_CANVAS_H * sizeof(lv_color_t);
  s_canvasBuf = static_cast<lv_color_t *>(
      heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (s_canvasBuf == nullptr) {
    s_canvasBuf = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_8BIT));
  }
  if (s_canvasBuf == nullptr) {
    Serial.printf("[Life] canvas alloc failed size=%u\r\n", (unsigned)bufSize);
    return false;
  }
  return true;
}

void ui_life_init(void) {
  s_screenLife = lv_obj_create(nullptr);
  lv_obj_set_size(s_screenLife, EPD_1IN54_V2_WIDTH, EPAPER_MAIN_HEIGHT);
  lv_obj_set_style_bg_color(s_screenLife, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenLife, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_screenLife, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_screenLife, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenLife, LV_OBJ_FLAG_SCROLLABLE);

  if (ensure_canvas_buffer()) {
    s_canvas = lv_canvas_create(s_screenLife);
    lv_canvas_set_buffer(s_canvas, s_canvasBuf, LIFE_CANVAS_W, LIFE_CANVAS_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_canvas, LIFE_CANVAS_W, LIFE_CANVAS_H);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_canvas, 0, LV_PART_MAIN);
  }

  s_running = true;
  s_lastTickMs = 0;
  life_randomize();
  life_render_canvas();
}

void ui_life_show(void) {
  life_randomize();
  life_render_canvas();
  s_running = true;
  s_lastTickMs = millis();
  lv_scr_load(s_screenLife);
  lv_obj_invalidate(s_screenLife);
}

void ui_life_leave(void) {
  s_running = false;
}

void ui_life_refresh_locale(void) {
  (void)0;
}

bool ui_life_is_active(void) {
  return s_screenLife != nullptr && lv_scr_act() == s_screenLife;
}

bool ui_life_handle(BtnAction action, UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }
  if (!ui_life_is_active() || action == BTN_ACTION_NONE) {
    return false;
  }

  switch (action) {
    case BTN_ACTION_NEXT:
      s_running = !s_running;
      if (s_running) {
        s_lastTickMs = millis();
      }
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_FAST;
      }
      return true;
    case BTN_ACTION_CONFIRM:
      life_randomize();
      life_render_canvas();
      s_running = true;
      s_lastTickMs = millis();
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_NAV;
      }
      return true;
    default:
      return false;
  }
}

bool ui_life_service(UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }
  if (!ui_life_is_active() || !s_running) {
    return false;
  }

  const unsigned long now = millis();
  if ((unsigned long)(now - s_lastTickMs) < LIFE_TICK_MS) {
    return false;
  }
  s_lastTickMs = now;

  life_step();
  life_render_canvas();
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_FAST;
  }
  return true;
}

lv_obj_t *ui_life_get_screen(void) {
  return s_screenLife;
}
