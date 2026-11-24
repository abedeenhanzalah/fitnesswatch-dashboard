#include <assert.h>
#include <string.h>
#include "battery_level.h"

int main(void)
{
 char label[5];

 /* The schematic's 1 MΩ / 510 kΩ divider yields ~1.35 V for a 4.0 V cell. */
 assert(watch_battery_millivolts(1351) == 4000);
 assert(watch_battery_label(label, false, 4200) == 100);
 assert(strcmp(label, "100%") == 0);
 assert(watch_battery_label(label, false, 3800) == 50);
 assert(strcmp(label, "50%") == 0);
 assert(watch_battery_label(label, false, 3300) == 0);
 assert(strcmp(label, "0%") == 0);
 assert(watch_battery_label(label, false, -1) < 0);
 assert(strcmp(label, "--%") == 0);
 assert(watch_battery_label(label, true, 4200) < 0);
 assert(strcmp(label, "USB") == 0);
 assert(watch_battery_fill(-1, 10) == 0);
 assert(watch_battery_fill(0, 10) == 0);
 assert(watch_battery_fill(1, 10) == 1);
 assert(watch_battery_fill(50, 10) == 5);
 assert(watch_battery_fill(100, 10) == 10);
}
