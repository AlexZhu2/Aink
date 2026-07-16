#ifndef UI_STATUS_BAR_H
#define UI_STATUS_BAR_H

#include "weather_icons.h"

#include <stdbool.h>

void ui_status_bar_init(void);
void ui_status_bar_set_visible(bool visible);
void ui_status_bar_update(int batteryPercent, bool wifiConnected,
                          bool showWeather, WeatherIconKind weatherIcon,
                          int weatherTempC);

/* Top-layer sleep banner (visible on home and fullscreen pages). */
void ui_modem_sleep_overlay_set(bool active);
/* Optional detail under the banner title (e.g. last sleep duration). */
void ui_modem_sleep_overlay_set_detail(const char *detail);
void ui_modem_sleep_overlay_refresh_locale(void);

#endif
