#include "lvgl_memory.h"

#include <esp_heap_caps.h>
#include <stdlib.h>

extern "C" void *lvgl_psram_malloc(size_t size) {
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr == nullptr) {
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  return ptr;
}

extern "C" void lvgl_psram_free(void *ptr) {
  free(ptr);
}

extern "C" void *lvgl_psram_realloc(void *ptr, size_t size) {
  void *next = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (next == nullptr) {
    next = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  return next;
}
