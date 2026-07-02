#include "worker_calendar.h"

#include "app_locale.h"
#include "worker_calendar_internal.h"

#include <stdio.h>
#include <string.h>

#define WORK_DAY_START_MIN   (9 * 60)
#define WORK_DAY_END_MIN     (18 * 60)
#define HOLIDAY_COUNTDOWN_MAX_DAYS 45

static int days_in_month(int year, int month) {
  static const int8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 30;
  }
  int days = kDays[month - 1];
  const bool leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
  if (month == 2 && leap) {
    days = 29;
  }
  return days;
}

static int makeYmd(int year, int month, int day) {
  return year * 10000 + month * 100 + day;
}

static bool parseYmd(int ymd, struct tm *out) {
  if (out == nullptr || ymd < 20200101 || ymd > 20511231) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->tm_year = ymd / 10000 - 1900;
  out->tm_mon = (ymd / 100) % 100 - 1;
  out->tm_mday = ymd % 100;
  out->tm_isdst = -1;
  return true;
}

static time_t ymdToTime(int ymd) {
  struct tm tmValue = {};
  if (!parseYmd(ymd, &tmValue)) {
    return (time_t)-1;
  }
  return mktime(&tmValue);
}

static int daysBetween(int fromYmd, int toYmd) {
  const time_t fromTs = ymdToTime(fromYmd);
  const time_t toTs = ymdToTime(toYmd);
  if (fromTs == (time_t)-1 || toTs == (time_t)-1) {
    return 0;
  }
  return (int)((toTs - fromTs) / 86400L);
}

static int weekdayFromYmd(int ymd) {
  struct tm tmValue = {};
  if (!parseYmd(ymd, &tmValue)) {
    return 0;
  }
  const time_t ts = mktime(&tmValue);
  if (ts == (time_t)-1) {
    return 0;
  }
  localtime_r(&ts, &tmValue);
  return tmValue.tm_wday;
}

static const WorkerHolidayRange *findHoliday(int ymd) {
  int count = 0;
  const WorkerHolidayRange *holidays = worker_calendar_store_holidays(&count);
  for (int i = 0; i < count; i++) {
    if (ymd >= holidays[i].startYmd && ymd <= holidays[i].endYmd) {
      return &holidays[i];
    }
  }
  return nullptr;
}

static const WorkerMakeupDay *findMakeup(int ymd) {
  int count = 0;
  const WorkerMakeupDay *makeupDays = worker_calendar_store_makeup_days(&count);
  for (int i = 0; i < count; i++) {
    if (makeupDays[i].ymd == ymd) {
      return &makeupDays[i];
    }
  }
  return nullptr;
}

static const char *holidayName(const WorkerHolidayRange *range) {
  if (range == nullptr) {
    return "";
  }
  return app_locale_get() == APP_LANG_ZH ? range->nameZh : range->nameEn;
}

static const char *makeupNote(const WorkerMakeupDay *day) {
  if (day == nullptr) {
    return "";
  }
  return app_locale_get() == APP_LANG_ZH ? day->noteZh : day->noteEn;
}

static bool isMakeupWorkday(int ymd) {
  return findMakeup(ymd) != nullptr;
}

static bool isHoliday(int ymd) {
  return findHoliday(ymd) != nullptr;
}

static bool isWorkday(int ymd) {
  if (isMakeupWorkday(ymd)) {
    return true;
  }
  if (isHoliday(ymd)) {
    return false;
  }
  const int wday = weekdayFromYmd(ymd);
  return wday >= 1 && wday <= 5;
}

static int mondayOfWeek(int todayYmd) {
  const int wday = weekdayFromYmd(todayYmd);
  const int daysFromMonday = (wday + 6) % 7;
  struct tm tmValue = {};
  if (!parseYmd(todayYmd, &tmValue)) {
    return todayYmd;
  }
  tmValue.tm_mday -= daysFromMonday;
  tmValue.tm_isdst = -1;
  const time_t ts = mktime(&tmValue);
  if (ts == (time_t)-1) {
    return todayYmd;
  }
  localtime_r(&ts, &tmValue);
  return makeYmd(tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday);
}

static int addDays(int ymd, int deltaDays) {
  struct tm tmValue = {};
  if (!parseYmd(ymd, &tmValue)) {
    return ymd;
  }
  tmValue.tm_mday += deltaDays;
  tmValue.tm_isdst = -1;
  const time_t ts = mktime(&tmValue);
  if (ts == (time_t)-1) {
    return ymd;
  }
  localtime_r(&ts, &tmValue);
  return makeYmd(tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday);
}

static void formatShortDate(int ymd, char *out, size_t outLen) {
  const int month = (ymd / 100) % 100;
  const int day = ymd % 100;
  const int wday = weekdayFromYmd(ymd);
  if (app_locale_get() == APP_LANG_ZH) {
    snprintf(out, outLen, "%d/%d(%s)", month, day, app_tr_weekday(wday));
  } else {
    snprintf(out, outLen, "%d/%d %s", month, day, app_tr_weekday(wday));
  }
}

static void buildWeekProgress(int todayYmd, WorkerCalendarView *out) {
  const int mondayYmd = mondayOfWeek(todayYmd);
  int worked = 0;
  int total = 0;

  out->weekWorkMask = 0;
  out->weekDoneMask = 0;

  for (int i = 0; i < 5; i++) {
    const int dayYmd = addDays(mondayYmd, i);
    if (!isWorkday(dayYmd)) {
      continue;
    }
    total++;
    out->weekWorkMask |= (uint8_t)(1U << i);
    if (dayYmd <= todayYmd) {
      worked++;
      out->weekDoneMask |= (uint8_t)(1U << i);
    }
  }

  snprintf(out->weekLine, sizeof(out->weekLine), app_tr(TR_WORKER_WEEK_FMT), worked, total);
  out->weekWorked = (uint8_t)worked;
  out->weekTotal = (uint8_t)total;
  out->weekProgressPct = total > 0 ? (uint8_t)((worked * 100) / total) : 0;
}

static void countWorkdaysInMonth(int todayYmd, uint8_t *outWorked, uint8_t *outTotal) {
  const int year = todayYmd / 10000;
  const int month = (todayYmd / 100) % 100;
  const int monthStart = makeYmd(year, month, 1);
  const int monthEnd = makeYmd(year, month, days_in_month(year, month));
  int worked = 0;
  int total = 0;

  for (int ymd = monthStart; ymd <= monthEnd; ymd = addDays(ymd, 1)) {
    if (!isWorkday(ymd)) {
      continue;
    }
    total++;
    if (ymd <= todayYmd) {
      worked++;
    }
  }

  *outWorked = (uint8_t)worked;
  *outTotal = (uint8_t)total;
}

static void countWorkdaysInYear(int todayYmd, uint16_t *outWorked, uint16_t *outTotal) {
  const int year = todayYmd / 10000;
  const int yearStart = makeYmd(year, 1, 1);
  const int yearEnd = makeYmd(year, 12, 31);
  int worked = 0;
  int total = 0;

  for (int ymd = yearStart; ymd <= yearEnd; ymd = addDays(ymd, 1)) {
    if (!isWorkday(ymd)) {
      continue;
    }
    total++;
    if (ymd <= todayYmd) {
      worked++;
    }
  }

  *outWorked = (uint16_t)worked;
  *outTotal = (uint16_t)total;
}

static void buildMonthYearProgress(int todayYmd, WorkerCalendarView *out) {
  countWorkdaysInMonth(todayYmd, &out->monthWorked, &out->monthTotal);
  out->monthProgressPct =
      out->monthTotal > 0 ? (uint8_t)((out->monthWorked * 100) / out->monthTotal) : 0;

  countWorkdaysInYear(todayYmd, &out->yearWorked, &out->yearTotal);
  out->yearProgressPct =
      out->yearTotal > 0 ? (uint8_t)((out->yearWorked * 100) / out->yearTotal) : 0;
}

static void formatMakeupBarDate(int ymd, char *out, size_t outLen) {
  const int month = (ymd / 100) % 100;
  const int day = ymd % 100;
  const int wday = weekdayFromYmd(ymd);
  if (app_locale_get() == APP_LANG_ZH) {
    snprintf(out, outLen, "%d月%d日(%s)", month, day, app_tr_weekday(wday));
  } else {
    snprintf(out, outLen, "%d/%d (%s)", month, day, app_tr_weekday(wday));
  }
}

static void buildMakeupBar(int todayYmd, WorkerCalendarView *out) {
  out->hasMakeupBar = false;
  out->makeupBarLine[0] = '\0';

  const WorkerMakeupDay *next = nullptr;
  int makeupCount = 0;
  const WorkerMakeupDay *makeupDays = worker_calendar_store_makeup_days(&makeupCount);
  for (int i = 0; i < makeupCount; i++) {
    if (makeupDays[i].ymd >= todayYmd &&
        (next == nullptr || makeupDays[i].ymd < next->ymd)) {
      next = &makeupDays[i];
    }
  }

  if (next == nullptr) {
    return;
  }

  char datePart[24];
  formatMakeupBarDate(next->ymd, datePart, sizeof(datePart));
  snprintf(out->makeupBarLine, sizeof(out->makeupBarLine), app_tr(TR_WORKER_MAKEUP_BAR_FMT),
           datePart);
  out->hasMakeupBar = true;
}

static void buildNextHolidayBadge(int todayYmd, WorkerCalendarView *out) {
  out->nextHolidayBadge[0] = '\0';

  const WorkerHolidayRange *todayHoliday = findHoliday(todayYmd);
  if (todayHoliday != nullptr) {
    const int left = daysBetween(todayYmd, todayHoliday->endYmd);
    if (left <= 0) {
      snprintf(out->nextHolidayBadge, sizeof(out->nextHolidayBadge), "%s",
               app_tr(TR_WORKER_HOLIDAY_LAST_DAY));
    } else {
      snprintf(out->nextHolidayBadge, sizeof(out->nextHolidayBadge),
               app_tr(TR_WORKER_DAYS_LEFT_BADGE), left);
    }
    return;
  }

  if (out->daysToHoliday > 0) {
    snprintf(out->nextHolidayBadge, sizeof(out->nextHolidayBadge),
             app_tr(TR_WORKER_DAYS_LEFT_BADGE), (int)out->daysToHoliday);
  }
}

static void buildTodayLine(int todayYmd, WorkerCalendarView *out) {
  const WorkerHolidayRange *holiday = findHoliday(todayYmd);
  const WorkerMakeupDay *makeup = findMakeup(todayYmd);

  if (holiday != nullptr) {
    snprintf(out->todayLine, sizeof(out->todayLine), "%s · %s",
             app_tr(TR_WORKER_TODAY_HOLIDAY), holidayName(holiday));
    return;
  }
  if (makeup != nullptr) {
    snprintf(out->todayLine, sizeof(out->todayLine), "%s · %s",
             app_tr(TR_WORKER_TODAY_MAKEUP), makeupNote(makeup));
    return;
  }
  const int wday = weekdayFromYmd(todayYmd);
  if (wday == 0 || wday == 6) {
    snprintf(out->todayLine, sizeof(out->todayLine), "%s", app_tr(TR_WORKER_TODAY_WEEKEND));
    return;
  }
  snprintf(out->todayLine, sizeof(out->todayLine), "%s", app_tr(TR_WORKER_TODAY_WORK));
}

static void buildHolidayLine(int todayYmd, WorkerCalendarView *out) {
  const WorkerHolidayRange *todayHoliday = findHoliday(todayYmd);
  if (todayHoliday != nullptr) {
    const int left = todayHoliday->endYmd - todayYmd;
    if (left <= 0) {
      snprintf(out->holidayLine, sizeof(out->holidayLine), "%s · %s",
               holidayName(todayHoliday), app_tr(TR_WORKER_HOLIDAY_LAST_DAY));
    } else {
      snprintf(out->holidayLine, sizeof(out->holidayLine), app_tr(TR_WORKER_HOLIDAY_LEFT_FMT),
               holidayName(todayHoliday), left);
    }
    return;
  }

  const WorkerHolidayRange *next = nullptr;
  int nextStart = 0;
  int holidayCount = 0;
  const WorkerHolidayRange *holidays = worker_calendar_store_holidays(&holidayCount);
  for (int i = 0; i < holidayCount; i++) {
    if (holidays[i].startYmd > todayYmd &&
        (next == nullptr || holidays[i].startYmd < nextStart)) {
      next = &holidays[i];
      nextStart = holidays[i].startYmd;
    }
  }

  if (next == nullptr) {
    snprintf(out->holidayLine, sizeof(out->holidayLine), "%s", app_tr(TR_WORKER_NO_HOLIDAY));
    return;
  }

  const int days = daysBetween(todayYmd, nextStart);
  snprintf(out->holidayLine, sizeof(out->holidayLine), app_tr(TR_WORKER_DAYS_TO_FMT),
           holidayName(next), days);
}

static void buildMakeupLine(int todayYmd, WorkerCalendarView *out) {
  const WorkerMakeupDay *next = nullptr;
  int makeupCount = 0;
  const WorkerMakeupDay *makeupDays = worker_calendar_store_makeup_days(&makeupCount);
  for (int i = 0; i < makeupCount; i++) {
    if (makeupDays[i].ymd >= todayYmd &&
        (next == nullptr || makeupDays[i].ymd < next->ymd)) {
      next = &makeupDays[i];
    }
  }

  if (next == nullptr) {
    snprintf(out->makeupLine, sizeof(out->makeupLine), "%s", app_tr(TR_WORKER_NO_MAKEUP));
    return;
  }

  char dateLine[20];
  formatShortDate(next->ymd, dateLine, sizeof(dateLine));
  if (next->ymd == todayYmd) {
    snprintf(out->makeupLine, sizeof(out->makeupLine), "%s · %s",
             app_tr(TR_WORKER_TODAY_MAKEUP), dateLine);
    return;
  }

  const int days = daysBetween(todayYmd, next->ymd);
  snprintf(out->makeupLine, sizeof(out->makeupLine), app_tr(TR_WORKER_MAKEUP_FMT), dateLine, days);
}

static void buildTodayProgress(const struct tm *timeinfo, int todayYmd, WorkerCalendarView *out) {
  out->todayProgressActive = false;
  out->todayProgressPct = 0;
  out->hoursToOffWork = -1;

  if (timeinfo == nullptr || !isWorkday(todayYmd)) {
    return;
  }

  const int nowMin = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  out->todayProgressActive = true;
  if (nowMin <= WORK_DAY_START_MIN) {
    out->todayProgressPct = 0;
    out->hoursToOffWork = (int8_t)((WORK_DAY_END_MIN - nowMin + 59) / 60);
    return;
  }
  if (nowMin >= WORK_DAY_END_MIN) {
    out->todayProgressPct = 100;
    out->hoursToOffWork = 0;
    return;
  }

  const int elapsed = nowMin - WORK_DAY_START_MIN;
  const int span = WORK_DAY_END_MIN - WORK_DAY_START_MIN;
  out->todayProgressPct = (uint8_t)((elapsed * 100) / span);
  out->hoursToOffWork = (int8_t)((WORK_DAY_END_MIN - nowMin + 59) / 60);
}

static WorkerDayKind classifyDay(int ymd, int todayYmd) {
  if (findHoliday(ymd) != nullptr) {
    return WORKER_DAY_HOLIDAY;
  }
  if (findMakeup(ymd) != nullptr) {
    return WORKER_DAY_MAKEUP;
  }
  const int wday = weekdayFromYmd(ymd);
  if (wday == 0 || wday == 6) {
    return WORKER_DAY_WEEKEND;
  }
  (void)todayYmd;
  return WORKER_DAY_NORMAL;
}

void worker_calendar_build_month(const struct tm *timeinfo, WorkerMonthView *out) {
  if (out == nullptr) {
    return;
  }
  memset(out, 0, sizeof(*out));
  if (timeinfo == nullptr) {
    return;
  }

  const int year = timeinfo->tm_year + 1900;
  const int month = timeinfo->tm_mon + 1;
  if (!worker_calendar_store_has_year(year)) {
    return;
  }

  out->year = year;
  out->month = month;
  out->todayYmd = makeYmd(year, month, timeinfo->tm_mday);
  out->dataAvailable = true;
  out->daysInMonth = days_in_month(year, month);
  out->firstWeekdayMon0 = (weekdayFromYmd(makeYmd(year, month, 1)) + 6) % 7;

  for (int cell = 0; cell < 42; cell++) {
    const int dayOffset = cell - out->firstWeekdayMon0;
    if (dayOffset < 0 || dayOffset >= out->daysInMonth) {
      out->dayKind[cell] = WORKER_DAY_EMPTY;
      out->dayYmd[cell] = 0;
      out->dayNum[cell] = 0;
      continue;
    }

    const int day = dayOffset + 1;
    const int ymd = makeYmd(year, month, day);
    out->dayYmd[cell] = ymd;
    out->dayNum[cell] = day;
    out->dayKind[cell] = classifyDay(ymd, out->todayYmd);
  }
}

static void buildHolidayProgress(int todayYmd, WorkerCalendarView *out) {
  out->onHoliday = false;
  out->daysToHoliday = 0;
  out->holidayProgressPct = 0;
  out->holidayNameShort[0] = '\0';

  const WorkerHolidayRange *todayHoliday = findHoliday(todayYmd);
  if (todayHoliday != nullptr) {
    out->onHoliday = true;
    const int totalDays = daysBetween(todayHoliday->startYmd, todayHoliday->endYmd) + 1;
    const int dayIndex = daysBetween(todayHoliday->startYmd, todayYmd) + 1;
    out->daysToHoliday = (uint16_t)daysBetween(todayYmd, todayHoliday->endYmd);
    if (totalDays > 0) {
      out->holidayProgressPct = (uint8_t)((dayIndex * 100) / totalDays);
    }
    strncpy(out->holidayNameShort, holidayName(todayHoliday), sizeof(out->holidayNameShort) - 1);
    out->holidayNameShort[sizeof(out->holidayNameShort) - 1] = '\0';
    return;
  }

  const WorkerHolidayRange *next = nullptr;
  int nextStart = 0;
  int holidayCount = 0;
  const WorkerHolidayRange *holidays = worker_calendar_store_holidays(&holidayCount);
  for (int i = 0; i < holidayCount; i++) {
    if (holidays[i].startYmd > todayYmd &&
        (next == nullptr || holidays[i].startYmd < nextStart)) {
      next = &holidays[i];
      nextStart = holidays[i].startYmd;
    }
  }

  if (next == nullptr) {
    return;
  }

  const int days = daysBetween(todayYmd, nextStart);
  out->daysToHoliday = (uint16_t)days;
  strncpy(out->holidayNameShort, holidayName(next), sizeof(out->holidayNameShort) - 1);
  out->holidayNameShort[sizeof(out->holidayNameShort) - 1] = '\0';

  if (days < HOLIDAY_COUNTDOWN_MAX_DAYS) {
    out->holidayProgressPct =
        (uint8_t)(((HOLIDAY_COUNTDOWN_MAX_DAYS - days) * 100) / HOLIDAY_COUNTDOWN_MAX_DAYS);
  }
}

void worker_calendar_build(const struct tm *timeinfo, WorkerCalendarView *out) {
  if (out == nullptr) {
    return;
  }

  memset(out, 0, sizeof(*out));
  if (timeinfo == nullptr) {
    snprintf(out->holidayLine, sizeof(out->holidayLine), "%s", app_tr(TR_WORKER_NO_DATA));
    return;
  }

  const int todayYear = timeinfo->tm_year + 1900;
  const int todayYmd =
      makeYmd(todayYear, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  if (!worker_calendar_store_has_year(todayYear)) {
    snprintf(out->holidayLine, sizeof(out->holidayLine), "%s", app_tr(TR_WORKER_NO_DATA));
    return;
  }

  out->dataAvailable = true;
  buildTodayLine(todayYmd, out);
  buildWeekProgress(todayYmd, out);
  buildMonthYearProgress(todayYmd, out);
  buildHolidayLine(todayYmd, out);
  buildMakeupLine(todayYmd, out);
  buildTodayProgress(timeinfo, todayYmd, out);
  buildHolidayProgress(todayYmd, out);
  buildNextHolidayBadge(todayYmd, out);
  buildMakeupBar(todayYmd, out);
}
