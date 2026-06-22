#ifndef UI_STOCK_DETAIL_H
#define UI_STOCK_DETAIL_H

#include "ui_refresh.h"

#include <lvgl.h>
#include <stdbool.h>

void ui_stock_detail_init(void);
void ui_stock_detail_show(int quoteIndex);
void ui_stock_detail_back_to_list(UiRefreshMode *outRefreshMode);
void ui_stock_detail_refresh(void);
bool ui_stock_detail_is_active(void);
bool ui_stock_detail_refresh_chart(void);

lv_obj_t *ui_stock_detail_get_screen(void);

#endif
