/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WATCH_BATTERY_LEVEL_H
#define WATCH_BATTERY_LEVEL_H

#include <stdbool.h>
#include <stdio.h>

static inline int watch_battery_millivolts(int divided_mv)
{
 return (divided_mv * 1510 + 255) / 510;
}

static inline int watch_battery_label(char label[5], bool usb_power, int battery_mv)
{
 if (usb_power) {
  snprintf(label, 5, "USB");
  return -1;
 } else if (battery_mv < 2500 || battery_mv > 4350) {
  snprintf(label, 5, "--%%");
  return -1;
 }
 /* Approximate a single-cell LiPo under watch load over 3.4–4.2 V. */
 int percent = battery_mv <= 3400 ? 0 :
               battery_mv >= 4200 ? 100 : (battery_mv - 3400 + 4) / 8;
 snprintf(label, 5, "%d%%", percent);
 return percent;
}

static inline int watch_battery_fill(int percent, int columns)
{
 if (percent <= 0) return 0;
 return (percent * columns + 99) / 100;
}

#endif
