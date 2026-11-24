#include <assert.h>
#include "display_cycle.h"

int main(void)
{
 const struct watch_screen_order defaults = WATCH_DEFAULT_SCREEN_ORDER;
 const uint8_t reordered[] = {1, 3, 2, 0};
 const uint8_t duplicate[] = {1, 3, 3};
 const uint8_t unknown[] = {1, 4};
 const uint8_t empty[] = {1};
 const uint8_t bad_version[] = {2, 0};
 const uint8_t time_only[] = {1, 0};
 struct watch_screen_order screens = defaults;
 uint8_t packet[WATCH_SCREEN_PACKET_MAX];

 assert(watch_screen_order_decode(&screens, reordered, sizeof(reordered)));
 assert(!watch_screen_order_decode(&screens, duplicate, sizeof(duplicate)));
 assert(!watch_screen_order_decode(&screens, unknown, sizeof(unknown)));
 assert(!watch_screen_order_decode(&screens, empty, sizeof(empty)));
 assert(!watch_screen_order_decode(&screens, bad_version, sizeof(bad_version)));
 assert(watch_screen_order_encode(&screens, packet) == sizeof(reordered));
 for (size_t i = 0; i < sizeof(reordered); i++) assert(packet[i] == reordered[i]);
 assert(watch_configured_page_at(2999, 2000, &screens) == WATCH_DISPLAY_SPLASH);
 assert(watch_configured_page_at(3000, 2000, &screens) == WATCH_DISPLAY_AQI);
 assert(watch_configured_page_at(9600, 9600, &screens) == WATCH_DISPLAY_AQI);
 assert(watch_configured_page_at(14599, 9600, &screens) == WATCH_DISPLAY_AQI);
 assert(watch_configured_page_at(14600, 9600, &screens) == WATCH_DISPLAY_STEPS);
 assert(watch_configured_page_at(19600, 9600, &screens) == WATCH_DISPLAY_TIME);
 assert(watch_configured_page_at(24600, 9600, &screens) == WATCH_DISPLAY_AQI);
 assert(watch_screen_order_decode(&screens, time_only, sizeof(time_only)));
 assert(watch_configured_page_at(86400000, 13800, &screens) == WATCH_DISPLAY_TIME);

 assert(watch_configured_page_at(0, 0, &defaults) == WATCH_DISPLAY_SPLASH);
 assert(watch_configured_page_at(2999, 0, &defaults) == WATCH_DISPLAY_SPLASH);
 assert(watch_configured_page_at(3000, 0, &defaults) == WATCH_DISPLAY_TIME);
 assert(watch_configured_page_at(7999, 0, &defaults) == WATCH_DISPLAY_TIME);
 assert(watch_configured_page_at(8000, 0, &defaults) == WATCH_DISPLAY_BPM);
 assert(watch_configured_page_at(12999, 0, &defaults) == WATCH_DISPLAY_BPM);
 assert(watch_configured_page_at(13000, 0, &defaults) == WATCH_DISPLAY_STEPS);
 assert(watch_configured_page_at(17999, 0, &defaults) == WATCH_DISPLAY_STEPS);
 assert(watch_configured_page_at(18000, 0, &defaults) == WATCH_DISPLAY_AQI);
 assert(watch_configured_page_at(22999, 0, &defaults) == WATCH_DISPLAY_AQI);
 assert(watch_configured_page_at(23000, 0, &defaults) == WATCH_DISPLAY_TIME);
 assert(watch_configured_page_at(28000, 0, &defaults) == WATCH_DISPLAY_BPM);
}
