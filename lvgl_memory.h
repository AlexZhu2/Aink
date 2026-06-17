#ifndef LVGL_MEMORY_H
#define LVGL_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *lvgl_psram_malloc(size_t size);
void lvgl_psram_free(void *ptr);
void *lvgl_psram_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif
