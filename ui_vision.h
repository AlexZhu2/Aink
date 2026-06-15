#ifndef UI_VISION_H
#define UI_VISION_H

#include <lvgl.h>
#include <stdbool.h>

#include "btn_input.h"
#include "ui_refresh.h"

void ui_vision_init(void);
void ui_vision_show(void);
void ui_vision_leave(void);
void ui_vision_refresh(void);
bool ui_vision_is_active(void);

bool ui_vision_request_capture(void);
bool ui_vision_consume_capture_request(void);
void ui_vision_set_busy(void);
bool ui_vision_run_capture(void);
bool ui_vision_service(UiRefreshMode *outRefreshMode);
bool ui_vision_is_busy(void);

lv_obj_t *ui_vision_get_screen(void);

#endif
