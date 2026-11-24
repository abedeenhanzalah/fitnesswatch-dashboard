/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WATCH_DISPLAY_CYCLE_H
#define WATCH_DISPLAY_CYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum watch_display_page {
 WATCH_DISPLAY_SPLASH = -1,
 WATCH_DISPLAY_TIME,
 WATCH_DISPLAY_BPM,
 WATCH_DISPLAY_STEPS,
 WATCH_DISPLAY_AQI,
 WATCH_DISPLAY_PAGE_COUNT,
};

struct watch_screen_order {
 uint8_t count;
 uint8_t pages[WATCH_DISPLAY_PAGE_COUNT];
};

#define WATCH_DEFAULT_SCREEN_ORDER { .count = WATCH_DISPLAY_PAGE_COUNT, \
 .pages = {WATCH_DISPLAY_TIME, WATCH_DISPLAY_BPM, WATCH_DISPLAY_STEPS, WATCH_DISPLAY_AQI} }
#define WATCH_SCREEN_PACKET_MAX (1 + WATCH_DISPLAY_PAGE_COUNT)

static inline bool watch_screen_order_decode(struct watch_screen_order *order,
                                            const uint8_t *data, size_t len)
{
 if (len < 2 || len > WATCH_SCREEN_PACKET_MAX || data[0] != 1) return false;
 struct watch_screen_order candidate = {.count = len - 1};
 unsigned seen = 0;
 for (size_t i = 0; i < candidate.count; i++) {
  uint8_t page = data[1 + i];
  if (page >= WATCH_DISPLAY_PAGE_COUNT || (seen & (1U << page))) return false;
  seen |= 1U << page;
  candidate.pages[i] = page;
 }
 *order = candidate;
 return true;
}

static inline size_t watch_screen_order_encode(const struct watch_screen_order *order,
                                              uint8_t data[WATCH_SCREEN_PACKET_MAX])
{
 data[0] = 1;
 for (size_t i = 0; i < order->count; i++) data[i + 1] = order->pages[i];
 return 1 + order->count;
}

static inline enum watch_display_page watch_configured_page_at(uint64_t elapsed_ms,
                                                              uint64_t changed_ms,
                                                              const struct watch_screen_order *order)
{
 const uint64_t splash_ms = 3000U;
 const uint64_t page_duration_ms = 5000U;
 if (elapsed_ms < splash_ms) return WATCH_DISPLAY_SPLASH;
 uint64_t cycle_start = changed_ms > splash_ms ? changed_ms : splash_ms;
 uint64_t cycle_elapsed = elapsed_ms > cycle_start ? elapsed_ms - cycle_start : 0;
 return order->pages[(cycle_elapsed / page_duration_ms) % order->count];
}

#endif
