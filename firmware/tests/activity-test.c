#include <assert.h>
#include <math.h>
#include <string.h>
#include "activity.h"
#include "telemetry.h"

int main(void)
{
 struct watch_steps steps = {0};
 const int32_t still[3] = {0, 0, 9800};
 const int32_t no_rotation[3] = {0, 0, 0};
 const int32_t rotating[3] = {0, 0, 700};
 for (uint32_t t = 0; t < 4000; t += 40) watch_steps_sample(&steps, t, still);
 assert(steps.count == 0);
 assert(!watch_is_moving(&steps, no_rotation));
 assert(watch_is_moving(&steps, rotating));

 for (uint32_t t = 4000; t < 10000; t += 40) {
  int32_t walking[3] = {0, 0, 9800 + (int)(2300 * sin((t - 4000) * 6.2831853 / 600))};
  watch_steps_sample(&steps, t, walking);
 }
 assert(steps.count >= 9 && steps.count <= 11);
 uint32_t walked = steps.count;
 watch_steps_break(&steps);
 for (uint32_t t = 10000; t < 11000; t += 40) watch_steps_sample(&steps, t, still);
 assert(steps.count == walked);

 struct watch_clock clock = {0};
 assert(watch_clock_seconds(&clock, 12000) == 0);
 assert(!watch_clock_set(&clock, 300, 330, 12300));
 assert(!watch_clock_set(&clock, 1700000000, 900, 12300));
 assert(watch_clock_set(&clock, 1700000000, 330, 12300));
 assert(watch_clock_seconds(&clock, 14600) == 1700000002);

 const uint32_t midnight_in_india = 20000U * 86400U - 330U * 60U;
 struct watch_daily_steps daily = {.record = {.day = 19999, .steps = 2500}};
 watch_daily_steps_update(&daily, 0, 330, 5);
 assert(daily.record.steps == 2500);
 watch_daily_steps_update(&daily, midnight_in_india - 1, 330, 7);
 assert(daily.record.day == 19999 && daily.record.steps == 2507);
 watch_daily_steps_update(&daily, midnight_in_india, 330, 7);
 assert(daily.record.day == 20000 && daily.record.steps == 0);
 watch_daily_steps_update(&daily, midnight_in_india + 1, 330, 8);
 assert(daily.record.steps == 1);
 assert(watch_step_goal_fill(0, 124) == 0);
 assert(watch_step_goal_fill(5000, 124) == 62);
 assert(watch_step_goal_fill(10000, 124) == 124);
 assert(watch_step_goal_fill(UINT32_MAX, 124) == 124);

 uint8_t packet[WATCH_ACTIVITY_PACKET_SIZE];
 const uint8_t expected[] = {1, 0x17, 0x05, 0x00, 0x00, 0x00,
                            0x02, 0xf1, 0x53, 0x65, 0x4a, 0x01, 0xfd, 0xff};
 watch_encode_activity(packet, 0x17, 5, 1700000002, 330, -3);
 assert(memcmp(packet, expected, sizeof(packet)) == 0);
}
