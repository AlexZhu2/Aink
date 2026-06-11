#include "epaper_canvas.h"

#include <Arduino.h>
#include <string.h>

#include "EPD_1in54_V2.h"

static UBYTE s_blackImage[5000];
static bool s_epaperPartialReady = false;
static bool s_portalMirror = false;

static const UWORD kRowBytes = (EPD_1IN54_V2_WIDTH + 7) / 8;

static void mapToBuffer(UWORD lx, UWORD ly, UWORD *px, UWORD *py) {
#if EPAPER_ROTATE_CW90
  *px = ly;
  *py = lx;
  if (s_portalMirror) {
    *py = EPD_1IN54_V2_WIDTH - lx - 1;
  }
#else
  *px = lx;
  *py = ly;
  if (s_portalMirror) {
    *px = EPD_1IN54_V2_WIDTH - lx - 1;
  }
#endif
}

UBYTE *epaper_get_buffer(void) {
  return s_blackImage;
}

void epaper_set_portal_mirror(bool enabled) {
  s_portalMirror = enabled;
}

void epaper_set_pixel(UWORD lx, UWORD ly, bool black) {
  UWORD x;
  UWORD y;
  mapToBuffer(lx, ly, &x, &y);
  if (x >= EPD_1IN54_V2_WIDTH || y >= EPD_1IN54_V2_HEIGHT) {
    return;
  }
  const UWORD index = (x / 8) + y * kRowBytes;
  const UBYTE mask = 0x80 >> (x % 8);
  if (black) {
    s_blackImage[index] &= ~mask;
  } else {
    s_blackImage[index] |= mask;
  }
}

void epaper_clear_white(void) {
  memset(s_blackImage, 0xFF, sizeof(s_blackImage));
}

void epaper_clear_main_area(void) {
  for (UWORD ly = EPAPER_STATUS_BAR_HEIGHT; ly < EPD_1IN54_V2_HEIGHT; ly++) {
    for (UWORD lx = 0; lx < EPD_1IN54_V2_WIDTH; lx++) {
      epaper_set_pixel(lx, ly, false);
    }
  }
}

bool epaper_is_partial_ready(void) {
  return s_epaperPartialReady;
}

void epaper_upload(bool fullRefresh) {
  epaper_upload_mode(fullRefresh, false);
}

void epaper_upload_mode(bool fullInit, bool fastPartial) {
  if (fullInit || !s_epaperPartialReady) {
    Serial.println("[EPD] upload full init...");
    EPD_1IN54_V2_Init();
    EPD_1IN54_V2_DisplayPartBaseImage(s_blackImage);
    EPD_1IN54_V2_Init_Partial();
    s_epaperPartialReady = true;
    return;
  }

  if (fastPartial) {
    EPD_1IN54_V2_DisplayPart(s_blackImage);
    return;
  }

  EPD_1IN54_V2_DisplayPartBaseImage(s_blackImage);
}

void epaper_draw_1bit_fullscreen(const uint8_t *bits, uint16_t bitsW, uint16_t bitsH) {
  if (bits == nullptr || bitsW == 0 || bitsH == 0) {
    return;
  }

  const UWORD dstW = EPD_1IN54_V2_WIDTH;
  const UWORD dstH = EPD_1IN54_V2_HEIGHT;
  const uint16_t offX = (bitsW > dstW) ? static_cast<uint16_t>((bitsW - dstW) / 2) : 0;
  const uint16_t offY = (bitsH > dstH) ? static_cast<uint16_t>((bitsH - dstH) / 2) : 0;
  const UWORD srcRowBytes = static_cast<UWORD>((bitsW + 7) / 8);

  epaper_clear_white();
  for (UWORD ly = 0; ly < dstH; ly++) {
    for (UWORD lx = 0; lx < dstW; lx++) {
      const uint16_t sx = offX + lx;
      const uint16_t sy = offY + ly;
      const size_t index = static_cast<size_t>(sy) * srcRowBytes + sx / 8;
      const uint8_t mask = static_cast<uint8_t>(0x80 >> (sx % 8));
      const bool black = (bits[index] & mask) == 0;

      UWORD px;
      UWORD py;
      mapToBuffer(lx, ly, &px, &py);
      if (px >= EPD_1IN54_V2_WIDTH || py >= EPD_1IN54_V2_HEIGHT) {
        continue;
      }
      const UWORD bufIndex = (px / 8) + py * kRowBytes;
      const UBYTE bufMask = static_cast<UBYTE>(0x80 >> (px % 8));
      if (black) {
        s_blackImage[bufIndex] &= static_cast<UBYTE>(~bufMask);
      } else {
        s_blackImage[bufIndex] |= bufMask;
      }
    }
  }
}
