#ifndef UI_STOCK_H
#define UI_STOCK_H

#include "btn_input.h"
#include "ui_refresh.h"

#include <lvgl.h>
#include <stdbool.h>

void ui_stock_init(void);
void ui_stock_show(void);
void ui_stock_refresh(void);
bool ui_stock_is_active(void);
bool ui_stock_is_list_active(void);
bool ui_stock_service(UiRefreshMode *outRefreshMode);
bool ui_stock_nav_handle(BtnAction action, UiRefreshMode *outRefreshMode);

lv_obj_t *ui_stock_get_screen(void);

#endif
