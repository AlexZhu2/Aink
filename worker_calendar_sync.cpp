#include "worker_calendar_sync.h"

#include "worker_calendar_internal.h"
#include "weather_gzip.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define CAL_HTTPS_MIN_INTERNAL_FREE  32000U
#define CAL_HTTPS_MIN_INTERNAL_BLOCK 24000U
#define CAL_HTTP_TIMEOUT_MS            15000UL
#define CAL_HTTP_CONNECT_MS             8000UL
#define CAL_MAX_DAYS_PER_YEAR              64

struct ParsedDay {
  int ymd;
  char name[16];
  bool isOffDay;
};

static bool s_syncBusy = false;

static bool calendarHttpsRamReady(const char *tag) {
  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t internalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (internalBlock >= CAL_HTTPS_MIN_INTERNAL_BLOCK &&
      internalFree >= CAL_HTTPS_MIN_INTERNAL_FREE) {
    return true;
  }
  if (tag != nullptr) {
    Serial.printf("[Calendar] %s deferred: internal free=%u block=%u\r\n", tag,
                  (unsigned)internalFree, (unsigned)internalBlock);
  }
  return false;
}

static bool calendarHttpsGet(const char *url, String *outBody) {
  if (url == nullptr || outBody == nullptr || !calendarHttpsRamReady("fetch")) {
    return false;
  }

  WiFi.setSleep(WIFI_PS_NONE);
  yield();
  delay(20);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  client.setConnectionTimeout(CAL_HTTP_TIMEOUT_MS);
  client.setTimeout(CAL_HTTP_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(CAL_HTTP_CONNECT_MS > 65535 ? 65535 : (uint16_t)CAL_HTTP_CONNECT_MS);
  http.setTimeout(CAL_HTTP_TIMEOUT_MS > 65535 ? 65535 : (uint16_t)CAL_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    Serial.println("[Calendar] HTTP begin failed");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.setAcceptEncoding("gzip");

  const int code = http.GET();
  *outBody = http.getString();
  http.end();
  client.stop();

  if (code != HTTP_CODE_OK) {
    Serial.printf("[Calendar] HTTP %d (%s)\r\n", code, http.errorToString(code).c_str());
    return false;
  }

  if (!weather_gzip_decompress(outBody)) {
    Serial.println("[Calendar] gzip decompress failed");
    return false;
  }
  return true;
}

static int parseIsoDateYmd(const char *iso) {
  if (iso == nullptr || strlen(iso) < 10) {
    return 0;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  if (sscanf(iso, "%d-%d-%d", &year, &month, &day) != 3) {
    return 0;
  }
  return year * 10000 + month * 100 + day;
}

static bool copyJsonStringValue(const String &body, int valueStart, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }
  out[0] = '\0';
  int pos = valueStart;
  while (pos < (int)body.length() && (body.charAt(pos) == ' ' || body.charAt(pos) == '\t')) {
    pos++;
  }
  if (pos >= (int)body.length() || body.charAt(pos) != '"') {
    return false;
  }
  pos++;
  size_t outIdx = 0;
  while (pos < (int)body.length() && body.charAt(pos) != '"') {
    if (outIdx + 1 < outLen) {
      out[outIdx++] = (char)body.charAt(pos);
    }
    pos++;
  }
  out[outIdx] = '\0';
  return outIdx > 0;
}

static bool parseJsonBool(const String &body, int valueStart, bool *outVal) {
  if (outVal == nullptr) {
    return false;
  }
  int pos = valueStart;
  while (pos < (int)body.length() && (body.charAt(pos) == ' ' || body.charAt(pos) == '\t')) {
    pos++;
  }
  if (body.startsWith("true", pos)) {
    *outVal = true;
    return true;
  }
  if (body.startsWith("false", pos)) {
    *outVal = false;
    return true;
  }
  return false;
}

static int weekdayFromYmd(int ymd) {
  struct tm tmValue = {};
  tmValue.tm_year = ymd / 10000 - 1900;
  tmValue.tm_mon = (ymd / 100) % 100 - 1;
  tmValue.tm_mday = ymd % 100;
  tmValue.tm_isdst = -1;
  const time_t ts = mktime(&tmValue);
  if (ts == (time_t)-1) {
    return 0;
  }
  localtime_r(&ts, &tmValue);
  return tmValue.tm_wday;
}

static int compareParsedDay(const void *a, const void *b) {
  const ParsedDay *left = (const ParsedDay *)a;
  const ParsedDay *right = (const ParsedDay *)b;
  return left->ymd - right->ymd;
}

static int parseHolidayCnJson(const String &body, ParsedDay *days, int maxDays) {
  if (days == nullptr || maxDays <= 0) {
    return 0;
  }

  int pos = body.indexOf("\"days\"");
  if (pos < 0) {
    return 0;
  }
  pos = body.indexOf('[', pos);
  if (pos < 0) {
    return 0;
  }

  int count = 0;
  while (count < maxDays) {
    const int dateKey = body.indexOf("\"date\"", pos);
    if (dateKey < 0) {
      break;
    }
    const int colon = body.indexOf(':', dateKey);
    if (colon < 0) {
      break;
    }

    char dateIso[16];
    if (!copyJsonStringValue(body, colon + 1, dateIso, sizeof(dateIso))) {
      pos = dateKey + 6;
      continue;
    }

    const int objStart = body.lastIndexOf('{', dateKey);
    const int objEnd = body.indexOf('}', dateKey);
    if (objStart < 0 || objEnd < 0 || objEnd <= objStart) {
      pos = dateKey + 6;
      continue;
    }

    ParsedDay day = {};
    day.ymd = parseIsoDateYmd(dateIso);
    if (day.ymd <= 0) {
      pos = objEnd + 1;
      continue;
    }

    const int nameKey = body.indexOf("\"name\"", objStart);
    if (nameKey >= objStart && nameKey < objEnd) {
      const int nameColon = body.indexOf(':', nameKey);
      copyJsonStringValue(body, nameColon + 1, day.name, sizeof(day.name));
    }

    const int offKey = body.indexOf("\"isOffDay\"", objStart);
    if (offKey >= objStart && offKey < objEnd) {
      const int offColon = body.indexOf(':', offKey);
      parseJsonBool(body, offColon + 1, &day.isOffDay);
    }

    days[count++] = day;
    pos = objEnd + 1;
  }

  return count;
}

static bool buildStoreFromDays(const ParsedDay *days, int dayCount, int year,
                               WorkerHolidayRange *holidays, int *holidayCount,
                               WorkerMakeupDay *makeupDays, int *makeupCount) {
  if (days == nullptr || holidays == nullptr || holidayCount == nullptr || makeupDays == nullptr ||
      makeupCount == nullptr) {
    return false;
  }

  *holidayCount = 0;
  *makeupCount = 0;

  ParsedDay sorted[CAL_MAX_DAYS_PER_YEAR];
  int sortedCount = 0;
  for (int i = 0; i < dayCount && sortedCount < CAL_MAX_DAYS_PER_YEAR; i++) {
    if (days[i].ymd / 10000 != year) {
      continue;
    }
    sorted[sortedCount++] = days[i];
  }
  if (sortedCount == 0) {
    return false;
  }

  qsort(sorted, (size_t)sortedCount, sizeof(ParsedDay), compareParsedDay);

  for (int i = 0; i < sortedCount;) {
    const ParsedDay *day = &sorted[i];
    if (!day->isOffDay) {
      const int wday = weekdayFromYmd(day->ymd);
      if ((wday == 0 || wday == 6) && *makeupCount < WORKER_CAL_MAX_MAKEUP) {
        WorkerMakeupDay makeup = {};
        makeup.ymd = day->ymd;
        strncpy(makeup.noteZh, day->name, sizeof(makeup.noteZh) - 1);
        strncpy(makeup.noteEn, day->name, sizeof(makeup.noteEn) - 1);
        makeupDays[(*makeupCount)++] = makeup;
      }
      i++;
      continue;
    }

    int startYmd = day->ymd;
    int endYmd = day->ymd;
    char name[16];
    strncpy(name, day->name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    int j = i + 1;
    while (j < sortedCount && sorted[j].isOffDay && strcmp(sorted[j].name, name) == 0) {
      struct tm prev = {};
      prev.tm_year = endYmd / 10000 - 1900;
      prev.tm_mon = (endYmd / 100) % 100 - 1;
      prev.tm_mday = endYmd % 100;
      prev.tm_isdst = -1;
      mktime(&prev);
      prev.tm_mday += 1;
      mktime(&prev);
      const int expectedNext =
          (prev.tm_year + 1900) * 10000 + (prev.tm_mon + 1) * 100 + prev.tm_mday;
      if (sorted[j].ymd != expectedNext) {
        break;
      }
      endYmd = sorted[j].ymd;
      j++;
    }

    if (*holidayCount < WORKER_CAL_MAX_HOLIDAYS) {
      WorkerHolidayRange range = {};
      range.startYmd = startYmd;
      range.endYmd = endYmd;
      strncpy(range.nameZh, name, sizeof(range.nameZh) - 1);
      strncpy(range.nameEn, name, sizeof(range.nameEn) - 1);
      holidays[(*holidayCount)++] = range;
    }
    i = j;
  }

  return *holidayCount > 0 || *makeupCount > 0;
}

static bool fetchAndMergeYear(int year) {
  char url[128];
  snprintf(url, sizeof(url), WORKER_CAL_URL_TEMPLATE, year);

  String body;
  Serial.printf("[Calendar] Fetching %d\r\n", year);
  if (!calendarHttpsGet(url, &body)) {
    return false;
  }

  ParsedDay days[CAL_MAX_DAYS_PER_YEAR];
  const int dayCount = parseHolidayCnJson(body, days, CAL_MAX_DAYS_PER_YEAR);
  if (dayCount <= 0) {
    Serial.printf("[Calendar] Parse failed for %d\r\n", year);
    return false;
  }

  WorkerHolidayRange holidays[WORKER_CAL_MAX_HOLIDAYS];
  WorkerMakeupDay makeupDays[WORKER_CAL_MAX_MAKEUP];
  int holidayCount = 0;
  int makeupCount = 0;
  if (!buildStoreFromDays(days, dayCount, year, holidays, &holidayCount, makeupDays, &makeupCount)) {
    Serial.printf("[Calendar] No usable days for %d\r\n", year);
    return false;
  }

  if (!worker_calendar_store_merge_year(year, holidays, holidayCount, makeupDays, makeupCount)) {
    Serial.printf("[Calendar] Merge failed for %d\r\n", year);
    return false;
  }

  Serial.printf("[Calendar] Cached %d holidays, %d makeup for %d\r\n", holidayCount, makeupCount,
                year);
  return true;
}

void worker_calendar_init(void) {
  worker_calendar_store_init();
}

bool worker_calendar_sync_service(bool allowNetwork) {
  if (!allowNetwork || s_syncBusy) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  struct tm timeinfo = {};
  if (!getLocalTime(&timeinfo, 0)) {
    return false;
  }

  const int year = timeinfo.tm_year + 1900;
  const int month = timeinfo.tm_mon + 1;
  const uint32_t currentYm = (uint32_t)(year * 100 + month);
  const uint32_t lastCheckYm = worker_calendar_store_get_check_ym();

  if (lastCheckYm == currentYm) {
    return false;
  }

  const bool hasCurrent = worker_calendar_store_has_year(year);
  const bool hasNext = worker_calendar_store_has_year(year + 1);

  if (hasCurrent && hasNext) {
    worker_calendar_store_set_check_ym(currentYm);
    Serial.printf("[Calendar] YM %u already cached\r\n", (unsigned)currentYm);
    return false;
  }

  if (!calendarHttpsRamReady("sync")) {
    return false;
  }

  s_syncBusy = true;
  bool updated = false;

  if (!hasCurrent && fetchAndMergeYear(year)) {
    updated = true;
  }
  if (!hasNext && fetchAndMergeYear(year + 1)) {
    updated = true;
  }

  if (updated) {
    worker_calendar_store_save_nvs();
  }

  if (worker_calendar_store_has_year(year) && worker_calendar_store_has_year(year + 1)) {
    worker_calendar_store_set_check_ym(currentYm);
  }

  s_syncBusy = false;
  return updated;
}
