#ifndef WORKER_CALENDAR_SYNC_H
#define WORKER_CALENDAR_SYNC_H

#include <stdbool.h>

void worker_calendar_init(void);
bool worker_calendar_sync_service(bool allowNetwork);

#endif
