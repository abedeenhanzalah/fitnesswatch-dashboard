#ifndef WATCH_ACTIVITY_H
#define WATCH_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

struct watch_steps {
 uint32_t count;
 uint32_t last_peak_ms;
 uint32_t previous_interval_ms;
 float gravity;
 float dynamic;
 uint8_t streak;
 bool initialized;
 bool above;
};

static inline float watch_squared_magnitude(const int32_t axes[3])
{
 return (float)axes[0] * axes[0] + (float)axes[1] * axes[1] + (float)axes[2] * axes[2];
}

static inline void watch_steps_break(struct watch_steps *s)
{
 *s = (struct watch_steps){.count = s->count};
}

static inline void watch_steps_sample(struct watch_steps *s, uint32_t now, const int32_t accel[3])
{
 float magnitude = watch_squared_magnitude(accel);
 if (!s->initialized) {
  s->gravity = magnitude;
  s->initialized = true;
 }
 s->gravity += (magnitude - s->gravity) / 16;
 s->dynamic += (magnitude - s->gravity - s->dynamic) / 3;
 if (s->dynamic < 5000000.0f) s->above = false;
 if (s->above || s->dynamic < 19000000.0f) return;
 s->above = true;

 uint32_t interval = now - s->last_peak_ms;
 if (s->streak && interval < 280) return;
 s->last_peak_ms = now;
 if (!s->streak || interval > 1150) {
  s->streak = 1;
  s->previous_interval_ms = 0;
  return;
 }
 uint32_t previous = s->previous_interval_ms;
 uint32_t difference = interval > previous ? interval - previous : previous - interval;
 if (previous && difference > previous / 3) {
  s->streak = 1;
  s->previous_interval_ms = 0;
  return;
 }
 s->previous_interval_ms = interval;
 /* Credit the first three peaks once a walking cadence is established. */
 if (s->streak < 3) {
  if (++s->streak == 3) s->count += 3;
 } else {
  ++s->count;
 }
}

static inline bool watch_is_moving(const struct watch_steps *s, const int32_t gyro[3])
{
 return watch_squared_magnitude(gyro) >= 250000.0f ||
        s->dynamic > 12000000.0f || s->dynamic < -12000000.0f;
}

struct watch_clock {
 uint32_t epoch;
 int16_t offset_minutes;
 int64_t set_at_ms;
};

static inline bool watch_clock_set(struct watch_clock *clock, uint32_t epoch, int16_t offset,
                                   int64_t now)
{
 if (epoch < 946684800 || offset < -720 || offset > 840) return false;
 *clock = (struct watch_clock){.epoch = epoch, .offset_minutes = offset, .set_at_ms = now};
 return true;
}

static inline uint32_t watch_clock_seconds(const struct watch_clock *clock, int64_t now)
{
 if (!clock->epoch || now < clock->set_at_ms) return 0;
 uint64_t epoch = clock->epoch + (uint64_t)(now - clock->set_at_ms) / 1000;
 return epoch <= UINT32_MAX ? (uint32_t)epoch : 0;
}

#define WATCH_DAILY_STEP_GOAL 10000U

struct watch_daily_step_record {
 uint32_t day;
 uint32_t steps;
};

struct watch_daily_steps {
 struct watch_daily_step_record record;
 uint32_t last_session_count;
};

static inline void watch_daily_steps_update(struct watch_daily_steps *daily, uint32_t epoch,
                                            int16_t offset_minutes, uint32_t session_count)
{
 if (!epoch) return;
 uint32_t day = ((int64_t)epoch + (int32_t)offset_minutes * 60) / 86400;
 if (day != daily->record.day) {
  daily->record = (struct watch_daily_step_record){.day = day};
 }
 daily->record.steps += session_count - daily->last_session_count;
 daily->last_session_count = session_count;
}

static inline uint16_t watch_step_goal_fill(uint32_t steps, uint16_t width)
{
 if (steps >= WATCH_DAILY_STEP_GOAL) return width;
 return steps * width / WATCH_DAILY_STEP_GOAL;
}
#endif
