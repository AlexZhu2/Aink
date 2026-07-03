#ifndef UI_RSS_H
#define UI_RSS_H

#include "btn_input.h"
#include "ui_refresh.h"

void ui_rss_init(void);
void ui_rss_show(void);
void ui_rss_refresh(void);
void ui_rss_refresh_locale(void);
bool ui_rss_is_active(void);
bool ui_rss_handle_btn(BtnAction action, UiRefreshMode *outRefreshMode);
bool ui_rss_service(UiRefreshMode *outRefreshMode);

#endif
