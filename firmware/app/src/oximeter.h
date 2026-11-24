#ifndef WATCH_OXIMETER_H
#define WATCH_OXIMETER_H

#include <math.h>
#include "pulse.h"

enum { OXIMETER_WINDOW = 4 * PULSE_MAX_LAG };

struct oximeter_channel {
 struct pulse_ir_filter median;
 float baseline;
 float slow;
 float filtered;
 float dc_level;
};

struct watch_oximeter {
 struct oximeter_channel red;
 struct oximeter_channel ir;
 float filtered[OXIMETER_WINDOW][2];
 unsigned samples;
 unsigned next;
};

static inline void oximeter_reset(struct watch_oximeter *state)
{
 state->red = (struct oximeter_channel){0};
 state->ir = (struct oximeter_channel){0};
 state->samples = 0;
 state->next = 0;
}

static inline void oximeter_channel_sample(struct oximeter_channel *channel, float value,
                                           bool first)
{
 if (first) {
  channel->baseline = value;
  channel->dc_level = value;
 }
 channel->dc_level += (value - channel->dc_level) / 100;
 channel->baseline += (value - channel->baseline) / 8;
 float high = value - channel->baseline;
 channel->slow += (high - channel->slow) / 8;
 channel->filtered += (high - channel->slow - channel->filtered) / 4;
}

static inline void oximeter_sample(struct watch_oximeter *state, uint32_t red, uint32_t ir,
                                  bool moving)
{
 if (moving) {
  oximeter_reset(state);
  return;
 }
 red = pulse_filter_ir(&state->red.median, red);
 ir = pulse_filter_ir(&state->ir.median, ir);
 if (red < 1000 || red >= 261000 || ir < 1000 || ir >= 261000) {
  oximeter_reset(state);
  return;
 }
 bool first = state->samples == 0;
 oximeter_channel_sample(&state->red, red, first);
 oximeter_channel_sample(&state->ir, ir, first);
 state->filtered[state->next][0] = state->red.filtered;
 state->filtered[state->next][1] = state->ir.filtered;
 state->next = (state->next + 1) % OXIMETER_WINDOW;
 if (state->samples < OXIMETER_WINDOW) ++state->samples;
}

struct oximeter_projection {
 float real;
 float imaginary;
 float sum;
 float power;
};

/* Compare the pulse-frequency component in both channels, excluding energy
 * outside that band from the red/IR amplitude ratio. */
static inline float oximeter_segment_ratio(const struct watch_oximeter *state,
                                            unsigned start, unsigned lag)
{
 const unsigned length = 2 * lag;
 float rotation = 6.283185307f / lag;
 float step_cos = cosf(rotation), step_sin = sinf(rotation);
 float phase_cos = 1, phase_sin = 0;
 struct oximeter_projection channel[2] = {0};
 for (unsigned i = 0; i < length; ++i) {
  for (unsigned c = 0; c < 2; ++c) {
   float value = state->filtered[start][c];
   channel[c].real += value * phase_cos;
   channel[c].imaginary += value * phase_sin;
   channel[c].sum += value;
   channel[c].power += value * value;
  }
  float next_cos = phase_cos * step_cos - phase_sin * step_sin;
  phase_sin = phase_sin * step_cos + phase_cos * step_sin;
  phase_cos = next_cos;
  start = (start + 1) % OXIMETER_WINDOW;
 }

 float dc[2] = {state->red.dc_level, state->ir.dc_level};
 float magnitude_squared[2];
 for (unsigned c = 0; c < 2; ++c) {
  struct oximeter_projection p = channel[c];
  magnitude_squared[c] = p.real * p.real + p.imaginary * p.imaginary;
  float pulse_power = 2 * magnitude_squared[c] / length;
  float band_power = p.power - p.sum * p.sum / length;
  float minimum_rms = dc[c] * 0.0005f;
  if (pulse_power < minimum_rms * minimum_rms * length ||
      pulse_power < 0.26f * band_power) return 0;
 }
 float phase_match = channel[0].real * channel[1].real +
                    channel[0].imaginary * channel[1].imaginary;
 if (phase_match <= 0 || phase_match * phase_match <
                         0.64f * magnitude_squared[0] * magnitude_squared[1]) return 0;
 return sqrtf(magnitude_squared[0] / magnitude_squared[1]) * dc[1] / dc[0];
}

static inline uint8_t oximeter_trial(const struct watch_oximeter *state, unsigned lag)
{
 if (lag < PULSE_MIN_LAG || lag > PULSE_MAX_LAG ||
     state->samples < 4 * lag || state->samples < 4 * PULSE_RATE_HZ) return 0;
 unsigned start = (state->next + OXIMETER_WINDOW - 4 * lag) % OXIMETER_WINDOW;
 unsigned middle = (start + 2 * lag) % OXIMETER_WINDOW;
 float previous = oximeter_segment_ratio(state, start, lag);
 float recent = oximeter_segment_ratio(state, middle, lag);
 if (previous <= 0 || recent <= 0) return 0;
 float ratio = (previous + recent) / 2;
 if (fabsf(previous - recent) > 0.25f * ratio) return 0;
 /* Published MAX30102 curve for an uncalibrated bench estimate.
  * https://support.analog.com/zh-CN/knowledgebase/article/KA-06685 */
 float estimate = -45.060f * ratio * ratio + 30.354f * ratio + 94.845f;
 if (estimate < 70 || estimate > 100) return 0;
 return (uint8_t)(estimate + 0.5f);
}
#endif
