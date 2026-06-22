#ifndef UI_LIFE_H
#define UI_LIFE_H

#include "btn_input.h"
#include "ui_refresh.h"

#include <lvgl.h>

void ui_life_init(void);
void ui_life_show(void);
void ui_life_leave(void);
void ui_life_refresh_locale(void);
bool ui_life_is_active(void);
bool ui_life_handle(BtnAction action, UiRefreshMode *outRefreshMode);
bool ui_life_service(UiRefreshMode *outRefreshMode);

lv_obj_t *ui_life_get_screen(void);

#endif
