#ifndef WORKER_CALENDAR_INTERNAL_H
#define WORKER_CALENDAR_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define WORKER_CAL_MAGIC           0xCA1EU
#define WORKER_CAL_MAX_HOLIDAYS    24
#define WORKER_CAL_MAX_MAKEUP        16
#define WORKER_CAL_YEAR_BASE         2020
#define WORKER_CAL_URL_TEMPLATE \
  "https://cdn.jsdelivr.net/gh/NateScarlet/holiday-cn@master/%d.json"

struct WorkerHolidayRange {
  int32_t startYmd;
  int32_t endYmd;
  char nameZh[16];
  char nameEn[24];
};

struct WorkerMakeupDay {
  int32_t ymd;
  char noteZh[16];
  char noteEn[24];
};

void worker_calendar_store_init(void);
bool worker_calendar_store_has_year(int year);
uint32_t worker_calendar_store_year_mask(void);
void worker_calendar_store_clear_year(int year);
bool worker_calendar_store_merge_year(int year,
                                      const WorkerHolidayRange *holidays,
                                      int holidayCount,
                                      const WorkerMakeupDay *makeupDays,
                                      int makeupCount);
bool worker_calendar_store_load_builtin_fallback(void);
bool worker_calendar_store_save_nvs(void);

uint32_t worker_calendar_store_get_check_ym(void);
void worker_calendar_store_set_check_ym(uint32_t checkYm);

const WorkerHolidayRange *worker_calendar_store_holidays(int *outCount);
const WorkerMakeupDay *worker_calendar_store_makeup_days(int *outCount);

#endif
