#include "rss_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#define RSS_FEED_URL "https://sspai.com/feed"
#define RSS_HTTP_TIMEOUT_MS 15000UL
#define RSS_RETRY_INTERVAL_MS (5UL * 60UL * 1000)
#define RSS_HTTPS_MIN_INTERNAL_FREE  32000U
#define RSS_HTTPS_MIN_INTERNAL_BLOCK 24000U
#define RSS_FETCH_INTERVAL_MS (30UL * 60UL * 1000)
#define RSS_STREAM_CHUNK_BYTES   512U
#define RSS_ITEM_HEADER_CAP      768U
#define RSS_MAX_STREAM_BYTES     (160U * 1024U)
#define RSS_ITEM_TAG             "<item"
#define RSS_ITEM_TAG_LEN         5
#define RSS_ENTRY_TAG            "<entry"
#define RSS_ENTRY_TAG_LEN        6

typedef enum {
  RSS_PARSE_FIND = 0,
  RSS_PARSE_HEAD = 1,
} RssParsePhase;

typedef struct {
  RssParsePhase phase;
  int itemMatch;
  int entryMatch;
  char headBuf[RSS_ITEM_HEADER_CAP];
  size_t headLen;
} RssStreamParser;

typedef struct {
  RssItem items[RSS_MAX_ITEMS];
  int count;
  bool valid;
  unsigned long lastFetchMs;
} RssCache;

static bool extractTitleFromBlock(const char *blockStart, const char *blockEnd, char *out,
                                  size_t outLen);
static bool extractLinkFromBlock(const char *blockStart, const char *blockEnd, char *out,
                                 size_t outLen);

static RssCache s_cache = {};
static portMUX_TYPE s_rssMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_fetchRequested = false;
static volatile bool s_fetchBusy = false;
static volatile bool s_retryPending = false;
static volatile bool s_freshFetchPending = false;
static unsigned long s_lastAttemptMs = 0;
static RssFeedSnapshot s_parseOut = {};
static RssStreamParser s_streamParser = {};
static uint8_t s_streamChunk[RSS_STREAM_CHUNK_BYTES];

static size_t utf8PrefixBytes(const char *text, size_t maxBytes) {
  size_t used = 0;
  while (text != nullptr && used < maxBytes && text[used] != '\0') {
    const unsigned char lead = (unsigned char)text[used];
    size_t charLen = 0;
    if (lead < 0x80) {
      charLen = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      charLen = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      charLen = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      charLen = 4;
    } else {
      break;
    }

    if (used + charLen > maxBytes) {
      break;
    }
    for (size_t i = 1; i < charLen; i++) {
      if (((unsigned char)text[used + i] & 0xC0) != 0x80) {
        return used;
      }
    }
    used += charLen;
  }
  return used;
}

static void copyText(char *out, size_t outLen, const char *in) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  if (in == nullptr) {
    out[0] = '\0';
    return;
  }
  const size_t inputLen = strlen(in);
  size_t copyLen = inputLen < outLen ? inputLen : outLen - 1;
  memcpy(out, in, copyLen);
  if (copyLen < inputLen) {
    copyLen = utf8PrefixBytes(out, copyLen);
  }
  out[copyLen] = '\0';
}

void rss_service_init(void) {
  portENTER_CRITICAL(&s_rssMux);
  memset(&s_cache, 0, sizeof(s_cache));
  s_fetchRequested = false;
  s_fetchBusy = false;
  s_retryPending = false;
  s_freshFetchPending = false;
  s_lastAttemptMs = 0;
  portEXIT_CRITICAL(&s_rssMux);
  memset(&s_parseOut, 0, sizeof(s_parseOut));
  memset(&s_streamParser, 0, sizeof(s_streamParser));
}

static bool cache_is_stale(void) {
  portENTER_CRITICAL(&s_rssMux);
  const bool valid = s_cache.valid;
  const unsigned long lastFetchMs = s_cache.lastFetchMs;
  portEXIT_CRITICAL(&s_rssMux);
  if (!valid || lastFetchMs == 0) {
    return true;
  }
  return (millis() - lastFetchMs) >= RSS_FETCH_INTERVAL_MS;
}

bool rss_service_is_stale(void) {
  return cache_is_stale();
}

static bool rss_https_ram_ready(void) {
  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t internalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  return internalBlock >= RSS_HTTPS_MIN_INTERNAL_BLOCK &&
         internalFree >= RSS_HTTPS_MIN_INTERNAL_FREE;
}

static void snapshot_from_cache(RssFeedSnapshot *out) {
  if (out == nullptr) {
    return;
  }
  memset(out, 0, sizeof(*out));
  portENTER_CRITICAL(&s_rssMux);
  out->count = s_cache.count;
  out->valid = s_cache.valid;
  if (s_cache.valid && s_cache.count > 0) {
    memcpy(out->items, s_cache.items, sizeof(out->items));
  }
  portEXIT_CRITICAL(&s_rssMux);
}

static void trimText(char *text) {
  if (text == nullptr) {
    return;
  }
  size_t start = 0;
  while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') {
    start++;
  }
  if (start > 0) {
    memmove(text, text + start, strlen(text + start) + 1);
  }
  size_t end = strlen(text);
  while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                     text[end - 1] == '\n')) {
    end--;
  }
  text[end] = '\0';
}

static void decodeBasicEntities(char *text) {
  struct Pair {
    const char *entity;
    const char *value;
  };
  static const Pair kPairs[] = {
      {"&amp;", "&"},
      {"&lt;", "<"},
      {"&gt;", ">"},
      {"&quot;", "\""},
      {"&apos;", "'"},
      {"&#39;", "'"},
  };
  for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); i++) {
    char *pos = text;
    while ((pos = strstr(pos, kPairs[i].entity)) != nullptr) {
      const size_t entityLen = strlen(kPairs[i].entity);
      const size_t valueLen = strlen(kPairs[i].value);
      memmove(pos + valueLen, pos + entityLen, strlen(pos + entityLen) + 1);
      memcpy(pos, kPairs[i].value, valueLen);
      pos += valueLen;
    }
  }
}

static void stripHtmlTags(char *text) {
  char *write = text;
  const char *read = text;
  bool inTag = false;
  while (*read != '\0') {
    if (*read == '<') {
      inTag = true;
      read++;
      continue;
    }
    if (*read == '>') {
      inTag = false;
      read++;
      continue;
    }
    if (!inTag) {
      *write++ = *read;
    }
    read++;
  }
  *write = '\0';
}

static void copyRawXmlField(const char *contentStart, size_t rawLen, char *temp, size_t tempLen) {
  if (temp == nullptr || tempLen == 0) {
    return;
  }
  if (contentStart == nullptr || rawLen == 0) {
    temp[0] = '\0';
    return;
  }

  size_t copyLen = rawLen < tempLen ? rawLen : tempLen - 1;
  memcpy(temp, contentStart, copyLen);
  temp[copyLen] = '\0';

  if (rawLen >= tempLen) {
    copyLen = utf8PrefixBytes(temp, copyLen);
    temp[copyLen] = '\0';
  }

  if (strncmp(temp, "<![CDATA[", 9) == 0) {
    char *cdataStart = temp + 9;
    char *cdataEnd = strstr(cdataStart, "]]>");
    if (cdataEnd != nullptr) {
      *cdataEnd = '\0';
    }
    memmove(temp, cdataStart, strlen(cdataStart) + 1);
  }
  trimText(temp);
  decodeBasicEntities(temp);
  stripHtmlTags(temp);
  trimText(temp);
}

static bool headerReadyToParse(const char *buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }

  char scratch[RSS_ITEM_HEADER_CAP];
  if (len >= sizeof(scratch)) {
    len = sizeof(scratch) - 1;
  }
  memcpy(scratch, buf, len);
  scratch[len] = '\0';

  if (strstr(scratch, "</title>") == nullptr) {
    return false;
  }
  if (strstr(scratch, "</link>") != nullptr) {
    return true;
  }
  if (strstr(scratch, "href=\"http") != nullptr) {
    return true;
  }
  return false;
}

static bool tryCommitItem(RssStreamParser *parser, RssFeedSnapshot *out) {
  if (parser == nullptr || out == nullptr || parser->headLen == 0) {
    return false;
  }

  parser->headBuf[parser->headLen] = '\0';
  const char *blockEnd = parser->headBuf + parser->headLen;

  char title[RSS_TITLE_LEN];
  char link[RSS_LINK_LEN];
  link[0] = '\0';
  if (!extractTitleFromBlock(parser->headBuf, blockEnd, title, sizeof(title))) {
    return false;
  }

  extractLinkFromBlock(parser->headBuf, blockEnd, link, sizeof(link));
  copyText(out->items[out->count].title, RSS_TITLE_LEN, title);
  copyText(out->items[out->count].link, RSS_LINK_LEN, link);
  out->count++;
  return true;
}

static void parserResetHead(RssStreamParser *parser) {
  parser->phase = RSS_PARSE_FIND;
  parser->headLen = 0;
  parser->itemMatch = 0;
  parser->entryMatch = 0;
}

static void parserBeginHead(RssStreamParser *parser, const char *tag, size_t tagLen) {
  parser->phase = RSS_PARSE_HEAD;
  parser->headLen = 0;
  if (tag != nullptr && tagLen > 0 && tagLen < RSS_ITEM_HEADER_CAP) {
    memcpy(parser->headBuf, tag, tagLen);
    parser->headLen = tagLen;
    parser->headBuf[parser->headLen] = '\0';
  }
  parser->itemMatch = 0;
  parser->entryMatch = 0;
}

static void parserFeedByte(RssStreamParser *parser, RssFeedSnapshot *out, char c) {
  if (parser == nullptr || out == nullptr || out->count >= RSS_MAX_ITEMS) {
    return;
  }

  if (parser->phase == RSS_PARSE_FIND) {
    if (c == RSS_ITEM_TAG[parser->itemMatch]) {
      parser->itemMatch++;
      if (parser->itemMatch >= RSS_ITEM_TAG_LEN) {
        parserBeginHead(parser, RSS_ITEM_TAG, RSS_ITEM_TAG_LEN);
        return;
      }
    } else {
      parser->itemMatch = (c == RSS_ITEM_TAG[0]) ? 1 : 0;
    }

    if (c == RSS_ENTRY_TAG[parser->entryMatch]) {
      parser->entryMatch++;
      if (parser->entryMatch >= RSS_ENTRY_TAG_LEN) {
        parserBeginHead(parser, RSS_ENTRY_TAG, RSS_ENTRY_TAG_LEN);
        return;
      }
    } else {
      parser->entryMatch = (c == RSS_ENTRY_TAG[0]) ? 1 : 0;
    }
    return;
  }

  if (parser->headLen + 1 >= RSS_ITEM_HEADER_CAP) {
    parserResetHead(parser);
    parser->itemMatch = (c == RSS_ITEM_TAG[0]) ? 1 : 0;
    parser->entryMatch = (c == RSS_ENTRY_TAG[0]) ? 1 : 0;
    return;
  }

  parser->headBuf[parser->headLen++] = c;
  parser->headBuf[parser->headLen] = '\0';

  if (!headerReadyToParse(parser->headBuf, parser->headLen)) {
    return;
  }

  if (tryCommitItem(parser, out)) {
    parserResetHead(parser);
  } else {
    parserResetHead(parser);
    parser->itemMatch = (c == RSS_ITEM_TAG[0]) ? 1 : 0;
    parser->entryMatch = (c == RSS_ENTRY_TAG[0]) ? 1 : 0;
  }
}

static void parserFeed(RssStreamParser *parser, RssFeedSnapshot *out, const uint8_t *data,
                       size_t len) {
  if (parser == nullptr || out == nullptr || data == nullptr) {
    return;
  }

  for (size_t i = 0; i < len && out->count < RSS_MAX_ITEMS; i++) {
    parserFeedByte(parser, out, (char)data[i]);
  }
}

static bool extractTitleFromBlock(const char *blockStart, const char *blockEnd, char *out,
                                  size_t outLen) {
  const char *titleTag = strstr(blockStart, "<title");
  if (titleTag == nullptr || titleTag >= blockEnd) {
    return false;
  }
  const char *contentStart = strchr(titleTag, '>');
  if (contentStart == nullptr || contentStart >= blockEnd) {
    return false;
  }
  contentStart++;
  const char *contentEnd = strstr(contentStart, "</title>");
  if (contentEnd == nullptr || contentEnd > blockEnd) {
    return false;
  }

  const size_t rawLen = (size_t)(contentEnd - contentStart);
  if (rawLen == 0) {
    return false;
  }

  char temp[RSS_TITLE_LEN];
  copyRawXmlField(contentStart, rawLen, temp, sizeof(temp));
  if (temp[0] == '\0') {
    return false;
  }

  copyText(out, outLen, temp);
  return true;
}

static bool extractTagTextFromBlock(const char *blockStart, const char *blockEnd, const char *tagName,
                                    char *out, size_t outLen) {
  if (blockStart == nullptr || blockEnd == nullptr || tagName == nullptr || out == nullptr ||
      outLen == 0) {
    return false;
  }

  char openTag[16];
  char closeTag[20];
  snprintf(openTag, sizeof(openTag), "<%s", tagName);
  snprintf(closeTag, sizeof(closeTag), "</%s>", tagName);

  const char *tag = strstr(blockStart, openTag);
  if (tag == nullptr || tag >= blockEnd) {
    return false;
  }
  const char *contentStart = strchr(tag, '>');
  if (contentStart == nullptr || contentStart >= blockEnd) {
    return false;
  }
  contentStart++;
  const char *contentEnd = strstr(contentStart, closeTag);
  if (contentEnd == nullptr || contentEnd > blockEnd) {
    return false;
  }

  const size_t rawLen = (size_t)(contentEnd - contentStart);
  if (rawLen == 0) {
    return false;
  }

  char temp[RSS_LINK_LEN];
  copyRawXmlField(contentStart, rawLen, temp, sizeof(temp));
  if (temp[0] == '\0') {
    return false;
  }

  copyText(out, outLen, temp);
  return true;
}

static bool extractLinkFromBlock(const char *blockStart, const char *blockEnd, char *out,
                                 size_t outLen) {
  if (blockStart == nullptr || blockEnd == nullptr || out == nullptr || outLen == 0) {
    return false;
  }

  const char *cursor = blockStart;
  while (cursor < blockEnd) {
    const char *linkTag = strstr(cursor, "<link");
    if (linkTag == nullptr || linkTag >= blockEnd) {
      break;
    }

    const char *tagEnd = strchr(linkTag, '>');
    if (tagEnd == nullptr || tagEnd >= blockEnd) {
      break;
    }

    const char *href = strstr(linkTag, "href=\"");
    if (href != nullptr && href < tagEnd) {
      href += 6;
      const char *hrefEnd = strchr(href, '"');
      if (hrefEnd != nullptr && hrefEnd < blockEnd) {
        const size_t len = (size_t)(hrefEnd - href);
        if (len > 0 && len < outLen) {
          memcpy(out, href, len);
          out[len] = '\0';
          trimText(out);
          if (strncmp(out, "http", 4) == 0) {
            return true;
          }
        }
      }
    }

    if (tagEnd[-1] != '/') {
      char link[RSS_LINK_LEN];
      if (extractTagTextFromBlock(linkTag, blockEnd, "link", link, sizeof(link)) &&
          strncmp(link, "http", 4) == 0) {
        copyText(out, outLen, link);
        return true;
      }
    }

    cursor = tagEnd + 1;
  }

  char guid[RSS_LINK_LEN];
  if (extractTagTextFromBlock(blockStart, blockEnd, "guid", guid, sizeof(guid)) &&
      strncmp(guid, "http", 4) == 0) {
    copyText(out, outLen, guid);
    return true;
  }

  out[0] = '\0';
  return false;
}

static bool httpsStreamParse(RssFeedSnapshot *out) {
  if (out == nullptr || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!rss_https_ram_ready()) {
    Serial.printf("[RSS] deferred: internal free=%u block=%u\r\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return false;
  }

  WiFi.setSleep(WIFI_PS_NONE);
  yield();
  delay(20);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  client.setTimeout(RSS_HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setTimeout(RSS_HTTP_TIMEOUT_MS > 65535 ? 65535 : (uint16_t)RSS_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, RSS_FEED_URL)) {
    return false;
  }

  http.addHeader("Accept", "application/rss+xml, application/xml, text/xml, */*");
  http.addHeader("User-Agent", "Aink/1.0 RSS");

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[RSS] HTTP %d\r\n", code);
    http.end();
    client.stop();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    client.stop();
    return false;
  }

  out->count = 0;
  out->valid = false;
  memset(&s_streamParser, 0, sizeof(s_streamParser));
  size_t totalRead = 0;
  unsigned long lastDataMs = millis();

  while (http.connected() || stream->available() > 0) {
    if (out->count >= RSS_MAX_ITEMS) {
      break;
    }

    const int avail = stream->available();
    if (avail <= 0) {
      if (!http.connected()) {
        break;
      }
      if ((millis() - lastDataMs) >= RSS_HTTP_TIMEOUT_MS) {
        Serial.println("[RSS] stream idle timeout");
        break;
      }
      delay(1);
      yield();
      continue;
    }

    const size_t toRead =
        (size_t)avail > sizeof(s_streamChunk) ? sizeof(s_streamChunk) : (size_t)avail;
    const int n = stream->readBytes(s_streamChunk, toRead);
    if (n <= 0) {
      break;
    }

    lastDataMs = millis();
    totalRead += (size_t)n;
    if (totalRead > RSS_MAX_STREAM_BYTES) {
      Serial.println("[RSS] stream limit reached");
      break;
    }

    parserFeed(&s_streamParser, out, s_streamChunk, (size_t)n);
    yield();
  }

  http.end();
  client.stop();

  Serial.printf("[RSS] stream read %u bytes\r\n", (unsigned)totalRead);
  return out->count > 0;
}

static bool rss_service_fetch_internal(void) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[RSS] WiFi offline");
    return false;
  }

  Serial.printf("[RSS] fetch %s\r\n", RSS_FEED_URL);

  s_parseOut = {};
  if (!httpsStreamParse(&s_parseOut)) {
    Serial.println("[RSS] parse failed");
    return false;
  }

  portENTER_CRITICAL(&s_rssMux);
  s_cache.count = s_parseOut.count;
  s_cache.valid = s_parseOut.count > 0;
  if (s_cache.valid) {
    memcpy(s_cache.items, s_parseOut.items, sizeof(s_cache.items));
  } else {
    s_cache.count = 0;
  }
  s_cache.lastFetchMs = millis();
  portEXIT_CRITICAL(&s_rssMux);

  Serial.printf("[RSS] %d headlines from SSPai\r\n", s_parseOut.count);
  return s_parseOut.count > 0;
}

void rss_service_poll(bool allowNetwork) {
  if (!allowNetwork || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long nowMs = millis();
  bool shouldFetch = false;
  portENTER_CRITICAL(&s_rssMux);
  const bool retryDue = s_retryPending &&
                        (s_lastAttemptMs == 0 ||
                         (nowMs - s_lastAttemptMs) >= RSS_RETRY_INTERVAL_MS);
  if (!s_fetchBusy && (s_fetchRequested || retryDue)) {
    shouldFetch = true;
  }
  portEXIT_CRITICAL(&s_rssMux);

  if (!shouldFetch) {
    return;
  }

  if (!rss_https_ram_ready()) {
    static unsigned long s_lastRamDeferLogMs = 0;
    if (s_lastRamDeferLogMs == 0 || (nowMs - s_lastRamDeferLogMs) >= 5000UL) {
      s_lastRamDeferLogMs = nowMs;
      Serial.printf("[RSS] waiting RAM free=%u block=%u\r\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
    return;
  }

  portENTER_CRITICAL(&s_rssMux);
  const bool retryStillDue = s_retryPending &&
                             (s_lastAttemptMs == 0 ||
                              (nowMs - s_lastAttemptMs) >= RSS_RETRY_INTERVAL_MS);
  if (!s_fetchBusy && (s_fetchRequested || retryStillDue)) {
    s_fetchRequested = false;
    s_retryPending = false;
    s_fetchBusy = true;
    s_lastAttemptMs = nowMs;
  } else {
    shouldFetch = false;
  }
  portEXIT_CRITICAL(&s_rssMux);

  if (!shouldFetch) {
    return;
  }

  const bool ok = rss_service_fetch_internal();

  portENTER_CRITICAL(&s_rssMux);
  if (ok) {
    s_freshFetchPending = true;
    s_retryPending = false;
  } else {
    s_retryPending = true;
  }
  s_fetchBusy = false;
  portEXIT_CRITICAL(&s_rssMux);
}

void rss_service_request_fetch(bool force) {
  if (!force && !cache_is_stale()) {
    return;
  }

  bool queued = false;
  portENTER_CRITICAL(&s_rssMux);
  if (force) {
    s_retryPending = false;
    s_fetchRequested = true;
    queued = true;
  } else if (!s_fetchBusy && !s_fetchRequested && !s_retryPending) {
    s_fetchRequested = true;
    queued = true;
  }
  portEXIT_CRITICAL(&s_rssMux);

  if (queued) {
    Serial.println(WiFi.status() == WL_CONNECTED ?
                       "[RSS] fetch queued" :
                       "[RSS] fetch queued; waiting for WiFi");
  }
}

bool rss_service_is_busy(void) {
  portENTER_CRITICAL(&s_rssMux);
  const bool busy = s_fetchBusy || s_fetchRequested;
  portEXIT_CRITICAL(&s_rssMux);
  return busy;
}

bool rss_service_consume_fresh_fetch(void) {
  portENTER_CRITICAL(&s_rssMux);
  if (!s_freshFetchPending) {
    portEXIT_CRITICAL(&s_rssMux);
    return false;
  }
  s_freshFetchPending = false;
  portEXIT_CRITICAL(&s_rssMux);
  return true;
}

void rss_service_get_snapshot(RssFeedSnapshot *out) {
  snapshot_from_cache(out);
}

bool rss_service_get_tile_preview(char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }

  out[0] = '\0';
  portENTER_CRITICAL(&s_rssMux);
  const bool available = s_cache.valid && s_cache.count > 0;
  if (available) {
    copyText(out, outLen, s_cache.items[0].title);
  }
  portEXIT_CRITICAL(&s_rssMux);
  return available;
}
