#include "worker_calendar_internal.h"

#include <Preferences.h>
#include <string.h>

#define PREFS_NAMESPACE       "epaper"
#define PREFS_KEY_CAL_BLOB    "cal_blob"
#define PREFS_KEY_CAL_CHK_YM  "cal_chk_ym"

struct WorkerCalendarBlob {
  uint16_t magic;
  uint16_t reserved;
  uint32_t yearMask;
  uint8_t holidayCount;
  uint8_t makeupCount;
  WorkerHolidayRange holidays[WORKER_CAL_MAX_HOLIDAYS];
  WorkerMakeupDay makeup[WORKER_CAL_MAX_MAKEUP];
};

static WorkerCalendarBlob s_blob = {};
static bool s_loaded = false;

static int yearToBit(int year) {
  if (year < WORKER_CAL_YEAR_BASE || year >= WORKER_CAL_YEAR_BASE + 32) {
    return -1;
  }
  return year - WORKER_CAL_YEAR_BASE;
}

static void ensureLoaded(void) {
  if (s_loaded) {
    return;
  }
  worker_calendar_store_init();
}

void worker_calendar_store_init(void) {
  if (s_loaded) {
    return;
  }

  memset(&s_blob, 0, sizeof(s_blob));
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, true);
  const size_t blobLen = prefs.getBytesLength(PREFS_KEY_CAL_BLOB);
  if (blobLen == sizeof(WorkerCalendarBlob)) {
    prefs.getBytes(PREFS_KEY_CAL_BLOB, &s_blob, sizeof(s_blob));
  }
  prefs.end();

  if (s_blob.magic != WORKER_CAL_MAGIC) {
    memset(&s_blob, 0, sizeof(s_blob));
    worker_calendar_store_load_builtin_fallback();
  }
  s_loaded = true;
}

bool worker_calendar_store_has_year(int year) {
  ensureLoaded();
  const int bit = yearToBit(year);
  if (bit < 0) {
    return false;
  }
  return (s_blob.yearMask & (1UL << bit)) != 0;
}

uint32_t worker_calendar_store_year_mask(void) {
  ensureLoaded();
  return s_blob.yearMask;
}

static bool yearInRange(int ymd, int year) {
  return ymd / 10000 == year;
}

void worker_calendar_store_clear_year(int year) {
  ensureLoaded();

  WorkerHolidayRange keptH[WORKER_CAL_MAX_HOLIDAYS];
  WorkerMakeupDay keptM[WORKER_CAL_MAX_MAKEUP];
  int hCount = 0;
  int mCount = 0;

  for (int i = 0; i < s_blob.holidayCount; i++) {
    if (!yearInRange(s_blob.holidays[i].startYmd, year) && hCount < WORKER_CAL_MAX_HOLIDAYS) {
      keptH[hCount++] = s_blob.holidays[i];
    }
  }
  for (int i = 0; i < s_blob.makeupCount; i++) {
    if (!yearInRange(s_blob.makeup[i].ymd, year) && mCount < WORKER_CAL_MAX_MAKEUP) {
      keptM[mCount++] = s_blob.makeup[i];
    }
  }

  memcpy(s_blob.holidays, keptH, sizeof(WorkerHolidayRange) * (size_t)hCount);
  memcpy(s_blob.makeup, keptM, sizeof(WorkerMakeupDay) * (size_t)mCount);
  s_blob.holidayCount = (uint8_t)hCount;
  s_blob.makeupCount = (uint8_t)mCount;

  const int bit = yearToBit(year);
  if (bit >= 0) {
    s_blob.yearMask &= ~(1UL << bit);
  }
}

bool worker_calendar_store_merge_year(int year,
                                      const WorkerHolidayRange *holidays,
                                      int holidayCount,
                                      const WorkerMakeupDay *makeupDays,
                                      int makeupCount) {
  ensureLoaded();
  if (holidays == nullptr || makeupDays == nullptr || holidayCount < 0 || makeupCount < 0) {
    return false;
  }

  worker_calendar_store_clear_year(year);

  for (int i = 0; i < holidayCount; i++) {
    if (s_blob.holidayCount >= WORKER_CAL_MAX_HOLIDAYS) {
      return false;
    }
    s_blob.holidays[s_blob.holidayCount++] = holidays[i];
  }
  for (int i = 0; i < makeupCount; i++) {
    if (s_blob.makeupCount >= WORKER_CAL_MAX_MAKEUP) {
      return false;
    }
    s_blob.makeup[s_blob.makeupCount++] = makeupDays[i];
  }

  const int bit = yearToBit(year);
  if (bit < 0) {
    return false;
  }
  s_blob.yearMask |= (1UL << bit);
  s_blob.magic = WORKER_CAL_MAGIC;
  return true;
}

bool worker_calendar_store_load_builtin_fallback(void) {
  static const WorkerHolidayRange kHolidays[] = {
      {20250101, 20250101, "元旦", "New Year"},
      {20250128, 20250204, "春节", "Spring Festival"},
      {20250404, 20250406, "清明", "Qingming"},
      {20250501, 20250505, "劳动节", "Labor Day"},
      {20250531, 20250602, "端午", "Dragon Boat"},
      {20251001, 20251008, "国庆", "National Day"},
      {20260101, 20260103, "元旦", "New Year"},
      {20260215, 20260223, "春节", "Spring Festival"},
      {20260404, 20260406, "清明", "Qingming"},
      {20260501, 20260505, "劳动节", "Labor Day"},
      {20260619, 20260621, "端午", "Dragon Boat"},
      {20260925, 20260927, "中秋", "Mid-Autumn"},
      {20261001, 20261007, "国庆", "National Day"},
  };
  static const WorkerMakeupDay kMakeupDays[] = {
      {20250126, "春节前", "Pre-Spring Festival"},
      {20250208, "春节后", "Post-Spring Festival"},
      {20250427, "劳动节前", "Pre-Labor Day"},
      {20250510, "劳动节后", "Post-Labor Day"},
      {20250928, "国庆前", "Pre-National Day"},
      {20251011, "国庆后", "Post-National Day"},
      {20260104, "元旦后", "Post-New Year"},
      {20260214, "春节前", "Pre-Spring Festival"},
      {20260228, "春节后", "Post-Spring Festival"},
      {20260509, "劳动节后", "Post-Labor Day"},
      {20260920, "国庆前", "Pre-National Day"},
      {20261010, "国庆后", "Post-National Day"},
  };

  memset(&s_blob, 0, sizeof(s_blob));
  s_blob.magic = WORKER_CAL_MAGIC;
  s_blob.holidayCount = (uint8_t)(sizeof(kHolidays) / sizeof(kHolidays[0]));
  s_blob.makeupCount = (uint8_t)(sizeof(kMakeupDays) / sizeof(kMakeupDays[0]));
  memcpy(s_blob.holidays, kHolidays, sizeof(kHolidays));
  memcpy(s_blob.makeup, kMakeupDays, sizeof(kMakeupDays));
  s_blob.yearMask = (1UL << yearToBit(2025)) | (1UL << yearToBit(2026));
  return true;
}

bool worker_calendar_store_save_nvs(void) {
  ensureLoaded();
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  const size_t written = prefs.putBytes(PREFS_KEY_CAL_BLOB, &s_blob, sizeof(s_blob));
  prefs.end();
  return written == sizeof(s_blob);
}

const WorkerHolidayRange *worker_calendar_store_holidays(int *outCount) {
  ensureLoaded();
  if (outCount != nullptr) {
    *outCount = s_blob.holidayCount;
  }
  return s_blob.holidays;
}

const WorkerMakeupDay *worker_calendar_store_makeup_days(int *outCount) {
  ensureLoaded();
  if (outCount != nullptr) {
    *outCount = s_blob.makeupCount;
  }
  return s_blob.makeup;
}

uint32_t worker_calendar_store_get_check_ym(void) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, true);
  const uint32_t value = prefs.getUInt(PREFS_KEY_CAL_CHK_YM, 0);
  prefs.end();
  return value;
}

void worker_calendar_store_set_check_ym(uint32_t checkYm) {
  Preferences prefs;
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putUInt(PREFS_KEY_CAL_CHK_YM, checkYm);
  prefs.end();
}
