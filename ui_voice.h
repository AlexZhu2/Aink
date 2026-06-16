#ifndef UI_VOICE_H
#define UI_VOICE_H

#include "ui_refresh.h"

#include <stdbool.h>
#include <lvgl.h>

void ui_voice_init(void);
void ui_voice_show(void);
void ui_voice_refresh(void);
bool ui_voice_is_active(void);
bool ui_voice_service(UiRefreshMode *outRefreshMode);
lv_obj_t *ui_voice_get_screen(void);

#endif
