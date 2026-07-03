#ifndef RSS_SERVICE_H
#define RSS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#define RSS_MAX_ITEMS   10
#define RSS_TITLE_LEN   96
#define RSS_LINK_LEN   128

typedef struct {
  char title[RSS_TITLE_LEN];
  char link[RSS_LINK_LEN];
} RssItem;

typedef struct {
  RssItem items[RSS_MAX_ITEMS];
  int count;
  bool valid;
} RssFeedSnapshot;

void rss_service_init(void);
void rss_service_request_fetch(bool force);
void rss_service_poll(bool allowNetwork);
bool rss_service_is_busy(void);
bool rss_service_consume_fresh_fetch(void);
bool rss_service_is_stale(void);
void rss_service_get_snapshot(RssFeedSnapshot *out);
const char *rss_service_tile_preview(void);

#endif
