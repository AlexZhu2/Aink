#ifndef WALLPAPER_SERVICE_H
#define WALLPAPER_SERVICE_H

#include "DEV_Config.h"

#include <stddef.h>
#include <stdint.h>

#define WALLPAPER_STORE_SIZE  240U
#define WALLPAPER_UPLOAD_MAX  (150U * 1024U)

bool wallpaper_service_init(void);
bool wallpaper_service_has_wallpaper(void);
bool wallpaper_service_delete(void);

bool wallpaper_service_save_from_jpeg(const uint8_t *data, size_t len);

void wallpaper_service_draw_full(UBYTE *buffer, UWORD logicalWidth, UWORD logicalHeight);

bool wallpaper_view_is_active(void);
void wallpaper_view_set_active(bool active);
bool wallpaper_view_toggle(void);

#endif
