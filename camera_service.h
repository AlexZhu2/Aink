#ifndef CAMERA_SERVICE_H
#define CAMERA_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_camera.h"

bool camera_service_init(void);
bool camera_service_is_ready(void);

/** Stop sensor/DMA (e.g. during long HTTPS) to avoid cam_hal FB-OVF spam. */
void camera_service_pause(void);

camera_fb_t *camera_service_capture(void);
void camera_service_release(camera_fb_t *fb);

framesize_t camera_service_get_framesize(void);
bool camera_service_set_framesize(framesize_t size);

/**
 * Decode a captured frame to RGB888. Allocates from PSRAM when available.
 * Caller must free(*outRgb) with free().
 */
bool camera_service_frame_to_rgb888(const camera_fb_t *fb, uint8_t **outRgb, size_t *outSize);

#endif
