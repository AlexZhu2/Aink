#include "weather_gzip.h"

#include <esp_heap_caps.h>

#include "puff.h"

#include <stdlib.h>
#include <string.h>

static size_t gzipHeaderSize(const uint8_t *data, size_t len) {
  if (len < 10 || data[0] != 0x1f || data[1] != 0x8b) {
    return 0;
  }

  size_t pos = 10;
  const uint8_t flags = data[3];

  if (flags & 0x04) {
    if (pos + 2 > len) {
      return 0;
    }
    const size_t xlen = data[pos] | (static_cast<size_t>(data[pos + 1]) << 8);
    pos += 2 + xlen;
  }
  if (flags & 0x08) {
    while (pos < len && data[pos] != 0) {
      pos++;
    }
    pos++;
  }
  if (flags & 0x10) {
    while (pos < len && data[pos] != 0) {
      pos++;
    }
    pos++;
  }
  if (flags & 0x02) {
    pos += 2;
  }

  return pos <= len ? pos : 0;
}

bool weather_gzip_decompress(String *body) {
  if (body == nullptr || body->length() < 2) {
    return false;
  }

  const uint8_t *src = reinterpret_cast<const uint8_t *>(body->c_str());
  if (src[0] != 0x1f || src[1] != 0x8b) {
    return true;
  }

  const size_t hdr = gzipHeaderSize(src, body->length());
  if (hdr == 0 || hdr + 8 > body->length()) {
    return false;
  }

  unsigned long srcLen = static_cast<unsigned long>(body->length() - hdr - 8);
  unsigned long destLen = srcLen * 8;
  if (destLen < 4096) {
    destLen = 4096;
  }
  if (destLen > 65536) {
    destLen = 65536;
  }

  uint8_t *dest = static_cast<uint8_t *>(
      heap_caps_malloc(destLen, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
  if (dest == nullptr) {
    dest = static_cast<uint8_t *>(malloc(destLen));
  }
  if (dest == nullptr) {
    return false;
  }

  const int puffRet = puff(dest, &destLen, src + hdr, &srcLen);
  if (puffRet != 0 || destLen == 0) {
    free(dest);
    return false;
  }

  String out;
  out.reserve(destLen + 1);
  for (unsigned long i = 0; i < destLen; i++) {
    out += static_cast<char>(dest[i]);
  }
  free(dest);

  Serial.printf("[Weather] gzip %u -> %u bytes\r\n", (unsigned)body->length(),
                (unsigned)out.length());
  *body = out;
  return true;
}
