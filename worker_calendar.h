#ifndef WORKER_CALENDAR_H
#define WORKER_CALENDAR_H

#include <stdint.h>
#include <time.h>

typedef struct {
  char todayLine[48];
  char weekLine[24];
  char holidayLine[40];
  char makeupLine[40];
  char holidayNameShort[16];
  uint8_t weekWorkMask;
  uint8_t weekDoneMask;
  uint8_t weekWorked;
  uint8_t weekTotal;
  uint8_t weekProgressPct;
  uint8_t monthWorked;
  uint8_t monthTotal;
  uint8_t monthProgressPct;
  uint16_t yearWorked;
  uint16_t yearTotal;
  uint8_t yearProgressPct;
  uint8_t todayProgressPct;
  uint8_t holidayProgressPct;
  uint16_t daysToHoliday;
  char nextHolidayBadge[20];
  char makeupBarLine[48];
  bool todayProgressActive;
  bool onHoliday;
  bool hasMakeupBar;
  bool dataAvailable;
  int8_t hoursToOffWork;
} WorkerCalendarView;

typedef enum {
  WORKER_DAY_EMPTY = 0,
  WORKER_DAY_NORMAL,
  WORKER_DAY_WEEKEND,
  WORKER_DAY_HOLIDAY,
  WORKER_DAY_MAKEUP,
} WorkerDayKind;

typedef struct {
  int year;
  int month;
  int todayYmd;
  int firstWeekdayMon0;
  int daysInMonth;
  bool dataAvailable;
  WorkerDayKind dayKind[42];
  int dayYmd[42];
  int dayNum[42];
} WorkerMonthView;

void worker_calendar_build(const struct tm *timeinfo, WorkerCalendarView *out);
void worker_calendar_build_month(const struct tm *timeinfo, WorkerMonthView *out);

#endif
