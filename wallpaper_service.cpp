#include "wallpaper_service.h"

#include "epaper_canvas.h"

#include "esp_heap_caps.h"
#include "img_converters.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <string.h>

#define WALLPAPER_FILE_PATH   "/wallpaper.bin"
#define WALLPAPER_MAGIC       0x31505741U
#define WALLPAPER_MAX_DECODE  1024U

struct WallpaperHeader {
  uint32_t magic;
  uint16_t width;
  uint16_t height;
  uint16_t reserved;
};

static const uint8_t kBayerThreshold[16] = {
    0, 128, 32, 160,
    192, 64, 224, 96,
    48, 176, 16, 144,
    240, 112, 208, 80,
};

static bool s_fsReady = false;
static bool s_hasWallpaper = false;
static bool s_wallpaperViewActive = false;
static uint8_t s_bitData[(WALLPAPER_STORE_SIZE * WALLPAPER_STORE_SIZE + 7) / 8];

static bool readStoredHeader(WallpaperHeader *header) {
  if (header == nullptr || !s_fsReady) {
    return false;
  }

  File file = LittleFS.open(WALLPAPER_FILE_PATH, "r");
  if (!file) {
    return false;
  }

  const size_t readBytes = file.read(reinterpret_cast<uint8_t *>(header), sizeof(WallpaperHeader));
  file.close();
  if (readBytes != sizeof(WallpaperHeader)) {
    return false;
  }

  return header->magic == WALLPAPER_MAGIC &&
         header->width == WALLPAPER_STORE_SIZE &&
         header->height == WALLPAPER_STORE_SIZE;
}

static bool loadBitDataFromFs(void) {
  WallpaperHeader header = {};
  if (!readStoredHeader(&header)) {
    s_hasWallpaper = false;
    return false;
  }

  File file = LittleFS.open(WALLPAPER_FILE_PATH, "r");
  if (!file) {
    s_hasWallpaper = false;
    return false;
  }

  file.seek(sizeof(WallpaperHeader));
  const size_t expected = sizeof(s_bitData);
  const size_t readBytes = file.read(s_bitData, expected);
  file.close();

  s_hasWallpaper = readBytes == expected;
  return s_hasWallpaper;
}

static bool jpegGetSize(const uint8_t *data, size_t len, uint16_t *outW, uint16_t *outH) {
  if (data == nullptr || outW == nullptr || outH == nullptr || len < 4) {
    return false;
  }
  if (data[0] != 0xFF || data[1] != 0xD8) {
    return false;
  }

  size_t index = 2;
  while (index + 3 < len) {
    if (data[index] != 0xFF) {
      index++;
      continue;
    }

    const uint8_t marker = data[index + 1];
    if (marker == 0xD8 || marker == 0x01) {
      index += 2;
      continue;
    }
    if (marker == 0xD9) {
      break;
    }
    if (index + 3 >= len) {
      break;
    }

    const uint16_t segmentLen = static_cast<uint16_t>((data[index + 2] << 8) | data[index + 3]);
    if (segmentLen < 2 || index + 2 + segmentLen > len) {
      break;
    }

    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      if (segmentLen >= 7 && index + 8 < len) {
        *outH = static_cast<uint16_t>((data[index + 5] << 8) | data[index + 6]);
        *outW = static_cast<uint16_t>((data[index + 7] << 8) | data[index + 8]);
        return *outW > 0 && *outH > 0;
      }
    }

    index += 2 + segmentLen;
  }

  return false;
}

static uint8_t *allocRgbBuffer(size_t bytes) {
  uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<uint8_t *>(malloc(bytes));
  }
  return buffer;
}

static void centerCropToGray240(const uint8_t *rgb, uint16_t srcW, uint16_t srcH, uint8_t *grayOut) {
  const uint16_t cropSize = (srcW < srcH) ? srcW : srcH;
  const uint16_t offX = (srcW - cropSize) / 2;
  const uint16_t offY = (srcH - cropSize) / 2;

  for (uint16_t y = 0; y < WALLPAPER_STORE_SIZE; y++) {
    for (uint16_t x = 0; x < WALLPAPER_STORE_SIZE; x++) {
      const uint16_t sx = offX + static_cast<uint16_t>((static_cast<uint32_t>(x) * cropSize) / WALLPAPER_STORE_SIZE);
      const uint16_t sy = offY + static_cast<uint16_t>((static_cast<uint32_t>(y) * cropSize) / WALLPAPER_STORE_SIZE);
      const uint8_t *pixel = rgb + (static_cast<size_t>(sy) * srcW + sx) * 3;
      const uint16_t lum = static_cast<uint16_t>(pixel[0]) * 77U +
                           static_cast<uint16_t>(pixel[1]) * 150U +
                           static_cast<uint16_t>(pixel[2]) * 29U;
      grayOut[static_cast<size_t>(y) * WALLPAPER_STORE_SIZE + x] = static_cast<uint8_t>(lum >> 8);
    }
  }
}

static void grayTo1Bit(const uint8_t *gray, uint8_t *bitOut) {
  memset(bitOut, 0xFF, sizeof(s_bitData));
  for (uint16_t y = 0; y < WALLPAPER_STORE_SIZE; y++) {
    for (uint16_t x = 0; x < WALLPAPER_STORE_SIZE; x++) {
      const uint8_t value = gray[static_cast<size_t>(y) * WALLPAPER_STORE_SIZE + x];
      const uint8_t threshold = kBayerThreshold[(y & 3) * 4 + (x & 3)];
      const bool black = value < threshold;
      if (!black) {
        continue;
      }
      const size_t index = (static_cast<size_t>(y) * WALLPAPER_STORE_SIZE + x) / 8;
      const uint8_t mask = 0x80 >> (x % 8);
      bitOut[index] &= static_cast<uint8_t>(~mask);
    }
  }
}

static bool writeBitDataToFs(void) {
  if (!s_fsReady) {
    return false;
  }

  LittleFS.remove(WALLPAPER_FILE_PATH);
  File file = LittleFS.open(WALLPAPER_FILE_PATH, "w");
  if (!file) {
    Serial.println("[Wallpaper] LittleFS open for write failed");
    return false;
  }

  WallpaperHeader header = {};
  header.magic = WALLPAPER_MAGIC;
  header.width = WALLPAPER_STORE_SIZE;
  header.height = WALLPAPER_STORE_SIZE;

  if (file.write(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  if (file.write(s_bitData, sizeof(s_bitData)) != sizeof(s_bitData)) {
    file.close();
    return false;
  }

  file.close();
  s_hasWallpaper = true;
  return true;
}

bool wallpaper_service_init(void) {
  s_fsReady = LittleFS.begin(false);
  if (!s_fsReady) {
    Serial.println("[Wallpaper] LittleFS mount failed, formatting...");
    s_fsReady = LittleFS.begin(true);
  }
  if (!s_fsReady) {
    Serial.println("[Wallpaper] LittleFS unavailable");
    s_hasWallpaper = false;
    return false;
  }

  loadBitDataFromFs();
  Serial.printf("[Wallpaper] init ok has=%s\n", s_hasWallpaper ? "yes" : "no");
  return true;
}

bool wallpaper_service_has_wallpaper(void) {
  return s_hasWallpaper;
}

bool wallpaper_service_delete(void) {
  if (!s_fsReady) {
    return false;
  }

  LittleFS.remove(WALLPAPER_FILE_PATH);
  s_hasWallpaper = false;
  s_wallpaperViewActive = false;
  memset(s_bitData, 0xFF, sizeof(s_bitData));
  Serial.println("[Wallpaper] deleted");
  return true;
}

bool wallpaper_service_save_from_jpeg(const uint8_t *data, size_t len) {
  if (data == nullptr || len < 4 || !s_fsReady) {
    return false;
  }
  if (len > WALLPAPER_UPLOAD_MAX) {
    Serial.println("[Wallpaper] JPEG too large");
    return false;
  }

  uint16_t srcW = 0;
  uint16_t srcH = 0;
  if (!jpegGetSize(data, len, &srcW, &srcH)) {
    Serial.println("[Wallpaper] JPEG header parse failed");
    return false;
  }
  if (srcW > WALLPAPER_MAX_DECODE || srcH > WALLPAPER_MAX_DECODE) {
    Serial.printf("[Wallpaper] JPEG too big %ux%u\n", srcW, srcH);
    return false;
  }

  const size_t rgbSize = static_cast<size_t>(srcW) * srcH * 3;
  uint8_t *rgb = allocRgbBuffer(rgbSize);
  if (rgb == nullptr) {
    Serial.println("[Wallpaper] RGB alloc failed");
    return false;
  }

  if (!fmt2rgb888(data, len, PIXFORMAT_JPEG, rgb)) {
    free(rgb);
    Serial.println("[Wallpaper] JPEG decode failed");
    return false;
  }

  const size_t grayBytes = static_cast<size_t>(WALLPAPER_STORE_SIZE) * WALLPAPER_STORE_SIZE;
  uint8_t *gray = allocRgbBuffer(grayBytes);
  if (gray == nullptr) {
    free(rgb);
    Serial.println("[Wallpaper] gray buffer alloc failed");
    return false;
  }

  centerCropToGray240(rgb, srcW, srcH, gray);
  free(rgb);

  grayTo1Bit(gray, s_bitData);
  free(gray);
  if (!writeBitDataToFs()) {
    Serial.println("[Wallpaper] save failed");
    return false;
  }

  Serial.println("[Wallpaper] saved");
  return true;
}


void wallpaper_service_draw_full(UBYTE *buffer, UWORD logicalWidth, UWORD logicalHeight) {
  (void)buffer;
  (void)logicalWidth;
  (void)logicalHeight;
  if (!s_hasWallpaper) {
    epaper_clear_white();
    return;
  }

  epaper_draw_1bit_fullscreen(s_bitData, WALLPAPER_STORE_SIZE, WALLPAPER_STORE_SIZE);
}

bool wallpaper_view_is_active(void) {
  return s_wallpaperViewActive;
}

void wallpaper_view_set_active(bool active) {
  if (active && !s_hasWallpaper) {
    s_wallpaperViewActive = false;
    return;
  }
  s_wallpaperViewActive = active;
}

bool wallpaper_view_toggle(void) {
  if (!s_hasWallpaper) {
    return false;
  }
  s_wallpaperViewActive = !s_wallpaperViewActive;
  return s_wallpaperViewActive;
}
