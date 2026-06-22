#include "ui_life.h"

#include "app_locale.h"
#include "EPD_1in54_V2.h"
#include "ui_fonts.h"
#include "ui_lvgl.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <string.h>

#define LIFE_COLS           50
#define LIFE_ROWS           50
#define LIFE_CELL_PX        4
#define LIFE_CANVAS_W       (LIFE_COLS * LIFE_CELL_PX)
#define LIFE_CANVAS_H       (LIFE_ROWS * LIFE_CELL_PX)
#define LIFE_TICK_MS        600U
#define LIFE_RANDOM_DENSITY 28U

#define LIFE_MENU_COUNT     6
#define LIFE_MENU_ROW_H     24
#define LIFE_MENU_ROW_Y0    30
#define LIFE_MENU_ROW_W     188

static_assert(LIFE_CANVAS_W == EPD_1IN54_V2_WIDTH, "life grid width must fill display");
static_assert(LIFE_CANVAS_H == EPD_1IN54_V2_HEIGHT, "life grid height must fill display");

typedef struct {
  AppStrId labelId;
  const int8_t (*cells)[2];
  int cellCount;
  bool anchorLeft;
} LifePatternDef;

#define LIFE_PATTERN_COUNT(cells) ((int)(sizeof(cells) / sizeof((cells)[0])))

static const int8_t kPulsarCells[][2] = {
    {2, 0},  {3, 0},  {4, 0},  {8, 0},  {9, 0},  {10, 0}, {2, 1},  {4, 1},  {8, 1},  {10, 1},
    {0, 2},  {12, 2}, {0, 3},  {12, 3}, {0, 4},  {12, 4},  {2, 5},  {3, 5},  {4, 5},  {8, 5},
    {9, 5},  {10, 5}, {2, 6},  {4, 6},  {8, 6},  {10, 6}, {2, 7},  {3, 7},  {4, 7},  {8, 7},
    {9, 7},  {10, 7}, {0, 8},  {12, 8}, {0, 9},  {12, 9},  {0, 10}, {12, 10}, {2, 11}, {4, 11},
    {8, 11}, {10, 11}, {2, 12}, {3, 12}, {4, 12}, {8, 12}, {9, 12}, {10, 12},
};

static const int8_t kLwssCells[][2] = {
    {1, 0}, {2, 0}, {3, 0}, {0, 1}, {3, 1}, {1, 2}, {3, 2}, {3, 3},
};

static const int8_t kPentCells[][2] = {
    {4, 0}, {3, 1}, {4, 1}, {5, 1}, {2, 2}, {6, 2}, {1, 3}, {7, 3}, {0, 4}, {8, 4},
    {1, 5}, {7, 5}, {2, 6}, {6, 6}, {3, 7}, {4, 7}, {5, 7}, {4, 8},
};

static const int8_t kRpentCells[][2] = {
    {1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2},
};

static const int8_t kGunCells[][2] = {
    {23, 0}, {21, 1}, {22, 1}, {11, 2}, {16, 2}, {17, 2}, {22, 2}, {23, 2}, {24, 2},
    {10, 3}, {11, 3}, {12, 3}, {17, 3}, {18, 3}, {23, 3}, {24, 3}, {25, 3}, {4, 4},
    {13, 4}, {14, 4}, {1, 5}, {2, 5}, {3, 5}, {12, 5}, {14, 5}, {15, 5}, {0, 6},
    {11, 6}, {14, 6}, {15, 7}, {18, 7}, {16, 8}, {17, 8},
};

static const LifePatternDef kLifePatterns[LIFE_MENU_COUNT] = {
    {TR_LIFE_MENU_RANDOM, nullptr, 0, false},
    {TR_LIFE_MENU_PULSAR, kPulsarCells, LIFE_PATTERN_COUNT(kPulsarCells), false},
    {TR_LIFE_MENU_LWSS, kLwssCells, LIFE_PATTERN_COUNT(kLwssCells), false},
    {TR_LIFE_MENU_PENTADECATHLON, kPentCells, LIFE_PATTERN_COUNT(kPentCells), false},
    {TR_LIFE_MENU_RPENT, kRpentCells, LIFE_PATTERN_COUNT(kRpentCells), false},
    {TR_LIFE_MENU_GUN, kGunCells, LIFE_PATTERN_COUNT(kGunCells), true},
};

static lv_obj_t *s_screenLifeMenu = nullptr;
static lv_obj_t *s_screenLifeGame = nullptr;
static lv_obj_t *s_menuTitle = nullptr;
static lv_obj_t *s_menuHint = nullptr;
static lv_obj_t *s_menuRows[LIFE_MENU_COUNT] = {};
static lv_obj_t *s_menuLabels[LIFE_MENU_COUNT] = {};
static lv_obj_t *s_canvas = nullptr;
static lv_color_t *s_canvasBuf = nullptr;

static bool s_grid[LIFE_ROWS][LIFE_COLS];
static bool s_next[LIFE_ROWS][LIFE_COLS];
static bool s_running = false;
static int s_menuFocus = 0;
static unsigned long s_lastTickMs = 0;

static void style_label(lv_obj_t *label, const lv_font_t *font) {
  lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN);
  if (font != nullptr) {
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  }
}

static void style_menu_row(lv_obj_t *panel, bool focused) {
  lv_obj_set_style_bg_color(panel, focused ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, focused ? 0 : 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(panel, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

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

static void life_clear(void) {
  memset(s_grid, 0, sizeof(s_grid));
}

static void life_randomize(void) {
  for (int y = 0; y < LIFE_ROWS; y++) {
    for (int x = 0; x < LIFE_COLS; x++) {
      s_grid[y][x] = (esp_random() % 100U) < LIFE_RANDOM_DENSITY;
    }
  }
}

static void life_stamp_pattern(const LifePatternDef *pattern) {
  life_clear();
  if (pattern == nullptr || pattern->cells == nullptr || pattern->cellCount <= 0) {
    life_randomize();
    return;
  }

  int minX = 127;
  int maxX = -128;
  int minY = 127;
  int maxY = -128;
  for (int i = 0; i < pattern->cellCount; i++) {
    const int px = pattern->cells[i][0];
    const int py = pattern->cells[i][1];
    if (px < minX) {
      minX = px;
    }
    if (px > maxX) {
      maxX = px;
    }
    if (py < minY) {
      minY = py;
    }
    if (py > maxY) {
      maxY = py;
    }
  }

  const int width = maxX - minX + 1;
  const int height = maxY - minY + 1;
  int ox;
  int oy;
  if (pattern->anchorLeft) {
    ox = 3;
    oy = (LIFE_ROWS - height) / 2;
  } else {
    ox = (LIFE_COLS - width) / 2 - minX;
    oy = (LIFE_ROWS - height) / 2 - minY;
  }

  for (int i = 0; i < pattern->cellCount; i++) {
    const int x = ox + pattern->cells[i][0];
    const int y = oy + pattern->cells[i][1];
    if (x < 0 || x >= LIFE_COLS || y < 0 || y >= LIFE_ROWS) {
      continue;
    }
    s_grid[y][x] = true;
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

static void bind_menu_rows(void) {
  for (int i = 0; i < LIFE_MENU_COUNT; i++) {
    const bool focused = i == s_menuFocus;
    style_menu_row(s_menuRows[i], focused);
    lv_label_set_text(s_menuLabels[i], app_tr(kLifePatterns[i].labelId));
    lv_obj_set_style_text_color(s_menuLabels[i],
                                focused ? lv_color_white() : lv_color_black(),
                                LV_PART_MAIN);
  }
  if (s_menuTitle != nullptr) {
    lv_label_set_text(s_menuTitle, app_tr(TR_LIFE_MENU_TITLE));
  }
  if (s_menuHint != nullptr) {
    lv_label_set_text(s_menuHint, app_tr(TR_LIFE_MENU_HINT));
  }
}

static void show_life_menu(UiRefreshMode *outRefreshMode) {
  s_running = false;
  bind_menu_rows();
  lv_scr_load(s_screenLifeMenu);
  lv_obj_invalidate(s_screenLifeMenu);
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NAV;
  }
}

static void ensure_game_canvas(void) {
  if (s_screenLifeGame == nullptr) {
    return;
  }
  if (!ensure_canvas_buffer()) {
    return;
  }
  if (s_canvas != nullptr) {
    return;
  }

  s_canvas = lv_canvas_create(s_screenLifeGame);
  lv_canvas_set_buffer(s_canvas, s_canvasBuf, LIFE_CANVAS_W, LIFE_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_size(s_canvas, LIFE_CANVAS_W, LIFE_CANVAS_H);
  lv_obj_set_pos(s_canvas, 0, 0);
  lv_obj_set_style_pad_all(s_canvas, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_canvas, 0, LV_PART_MAIN);
}

static void start_life_game(int patternIndex) {
  if (patternIndex < 0 || patternIndex >= LIFE_MENU_COUNT) {
    patternIndex = 0;
  }
  ensure_game_canvas();
  life_stamp_pattern(&kLifePatterns[patternIndex]);
  s_running = true;
  s_lastTickMs = millis();
  lv_scr_load(s_screenLifeGame);
  life_render_canvas();
  lv_obj_invalidate(s_screenLifeGame);
}

static void create_menu_screen(void) {
  s_screenLifeMenu = lv_obj_create(nullptr);
  ui_lvgl_configure_fullscreen(s_screenLifeMenu);
  lv_obj_set_style_bg_color(s_screenLifeMenu, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenLifeMenu, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenLifeMenu, LV_OBJ_FLAG_SCROLLABLE);

  s_menuTitle = lv_label_create(s_screenLifeMenu);
  style_label(s_menuTitle, UI_FONT_SM);
  lv_obj_set_pos(s_menuTitle, 6, 6);
  lv_label_set_long_mode(s_menuTitle, LV_LABEL_LONG_CLIP);

  for (int i = 0; i < LIFE_MENU_COUNT; i++) {
    const lv_coord_t y = LIFE_MENU_ROW_Y0 + i * LIFE_MENU_ROW_H;
    s_menuRows[i] = lv_obj_create(s_screenLifeMenu);
    lv_obj_set_size(s_menuRows[i], LIFE_MENU_ROW_W, LIFE_MENU_ROW_H - 2);
    lv_obj_set_pos(s_menuRows[i], 6, y);

    s_menuLabels[i] = lv_label_create(s_menuRows[i]);
    style_label(s_menuLabels[i], UI_FONT_SM);
    lv_obj_set_pos(s_menuLabels[i], 8, 5);
    lv_obj_set_width(s_menuLabels[i], LIFE_MENU_ROW_W - 16);
    lv_label_set_long_mode(s_menuLabels[i], LV_LABEL_LONG_CLIP);
  }

  s_menuHint = lv_label_create(s_screenLifeMenu);
  style_label(s_menuHint, UI_FONT_SM);
  lv_obj_set_width(s_menuHint, 188);
  lv_label_set_long_mode(s_menuHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(s_menuHint, 6, 178);
}

static void create_game_screen(void) {
  s_screenLifeGame = lv_obj_create(nullptr);
  ui_lvgl_configure_fullscreen(s_screenLifeGame);
  lv_obj_set_style_bg_color(s_screenLifeGame, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_screenLifeGame, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_screenLifeGame, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_screenLifeGame, LV_OBJ_FLAG_SCROLLABLE);

  if (ensure_canvas_buffer()) {
    ensure_game_canvas();
  }
}

void ui_life_init(void) {
  create_menu_screen();
  create_game_screen();
  s_menuFocus = 0;
  s_running = false;
  s_lastTickMs = 0;
  life_clear();
  bind_menu_rows();
}

void ui_life_show(void) {
  s_menuFocus = 0;
  show_life_menu(nullptr);
}

void ui_life_leave(void) {
  s_running = false;
}

void ui_life_refresh_locale(void) {
  if (s_screenLifeMenu != nullptr && lv_scr_act() == s_screenLifeMenu) {
    bind_menu_rows();
    lv_obj_invalidate(s_screenLifeMenu);
  }
}

bool ui_life_is_active(void) {
  const lv_obj_t *scr = lv_scr_act();
  return scr == s_screenLifeMenu || scr == s_screenLifeGame;
}

static bool ui_life_menu_handle(BtnAction action, UiRefreshMode *outRefreshMode) {
  switch (action) {
    case BTN_ACTION_NEXT:
      s_menuFocus = (s_menuFocus + 1) % LIFE_MENU_COUNT;
      bind_menu_rows();
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_FAST;
      }
      return true;
    case BTN_ACTION_PREV:
      s_menuFocus = (s_menuFocus + LIFE_MENU_COUNT - 1) % LIFE_MENU_COUNT;
      bind_menu_rows();
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_FAST;
      }
      return true;
    case BTN_ACTION_CONFIRM:
      start_life_game(s_menuFocus);
      if (outRefreshMode != nullptr) {
        *outRefreshMode = UI_REFRESH_NAV;
      }
      return true;
    default:
      return false;
  }
}

static bool ui_life_game_handle(BtnAction action, UiRefreshMode *outRefreshMode) {
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
      show_life_menu(outRefreshMode);
      return true;
    case BTN_ACTION_BACK:
      show_life_menu(outRefreshMode);
      return true;
    default:
      return false;
  }
}

bool ui_life_handle(BtnAction action, UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }
  if (!ui_life_is_active() || action == BTN_ACTION_NONE) {
    return false;
  }

  if (lv_scr_act() == s_screenLifeMenu) {
    return ui_life_menu_handle(action, outRefreshMode);
  }
  if (lv_scr_act() == s_screenLifeGame) {
    return ui_life_game_handle(action, outRefreshMode);
  }
  return false;
}

bool ui_life_service(UiRefreshMode *outRefreshMode) {
  if (outRefreshMode != nullptr) {
    *outRefreshMode = UI_REFRESH_NONE;
  }
  if (lv_scr_act() != s_screenLifeGame || !s_running) {
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
  return lv_scr_act() == s_screenLifeGame ? s_screenLifeGame : s_screenLifeMenu;
}
