#ifndef WATCH_PULSE_H
#define WATCH_PULSE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* The optical FIFO delivers 50 samples per second. */
enum {
 PULSE_RATE_HZ = 50,
 PULSE_SETTLE_SAMPLES = 25,
 PULSE_WINDOW_SAMPLES = 200,
 PULSE_EARLY_WINDOW_SAMPLES = 125,
 PULSE_MIN_WINDOW_SAMPLES = 150,
 PULSE_ESTIMATE_STEP = 25,
 PULSE_MIN_LAG = 15,
 PULSE_MAX_LAG = 75,
 PULSE_CONTACT_LOSS_SAMPLES = 100,
 PULSE_HELD_AFTER_SAMPLES = 2 * PULSE_RATE_HZ,
 PULSE_UPDATE_DELTA_BPM = 5,
 PULSE_MEDIAN_WINDOW = 5,
 PULSE_OCTAVE_MEMORY_SAMPLES = 12 * PULSE_RATE_HZ,
 PULSE_OCTAVE_CONFIRMATIONS = 8,
};

struct pulse_ir_filter {
 uint32_t samples[PULSE_MEDIAN_WINDOW];
 uint8_t next;
 bool initialized;
};

struct pulse_state {
 struct pulse_ir_filter ir_filter;
 int32_t baseline;
 int32_t slow;
 int32_t filtered;
 uint32_t contact_samples;
 float window[PULSE_WINDOW_SAMPLES];
 bool moving_window[PULSE_WINDOW_SAMPLES];
 uint16_t window_count;
 uint16_t window_next;
 uint16_t bpm; /* Last accepted value reported to the OLED and BLE. */
 uint16_t samples_since_estimate;
 uint32_t previous_lag_sample;
 uint32_t last_confirmed_sample;
 uint8_t previous_lag;
 uint8_t confirmed_lag;
 uint8_t octave_confirmations;
 uint8_t missing_contact_samples;
 bool contact;
};

/* A centered five-sample median suppresses up to two isolated bad ADC counts. */
static inline uint32_t pulse_filter_ir(struct pulse_ir_filter *filter, uint32_t ir)
{
 if (!filter->initialized) {
  for (unsigned i = 0; i < PULSE_MEDIAN_WINDOW; ++i) filter->samples[i] = ir;
  filter->initialized = true;
 }
 filter->samples[filter->next] = ir;
 filter->next = (filter->next + 1) % PULSE_MEDIAN_WINDOW;

 uint32_t ordered[PULSE_MEDIAN_WINDOW];
 for (unsigned i = 0; i < PULSE_MEDIAN_WINDOW; ++i) {
  uint32_t value = filter->samples[i];
  unsigned position = i;
  while (position > 0 && ordered[position - 1] > value) {
   ordered[position] = ordered[position - 1];
   --position;
  }
  ordered[position] = value;
 }
 return ordered[PULSE_MEDIAN_WINDOW / 2];
}

static inline float pulse_correlation_score(const float *samples, const bool *moving,
                                             unsigned count, unsigned lag)
{
 unsigned overlap = count - lag;
 float sum_a = 0, sum_b = 0, power_a = 0, power_b = 0, cross = 0, weight_sum = 0;
 for (unsigned i = 0; i < overlap; ++i) {
  float a = samples[i];
  float b = samples[i + lag];
  float weight = moving[i] || moving[i + lag] ? 0.25f : 1.0f;
  sum_a += weight * a;
  sum_b += weight * b;
  power_a += weight * a * a;
  power_b += weight * b * b;
  cross += weight * a * b;
  weight_sum += weight;
 }
 power_a -= sum_a * sum_a / weight_sum;
 power_b -= sum_b * sum_b / weight_sum;
 cross -= sum_a * sum_b / weight_sum;
 /* Ignore flat signals and negative correlation. Require at least six counts RMS. */
 if (cross <= 0 || power_a < weight_sum * 36.0f || power_b < weight_sum * 36.0f) return 0;
 return cross * cross / (power_a * power_b);
}

static inline bool pulse_is_peak(const float *scores, unsigned lag)
{
 return scores[lag] >= scores[lag - 1] && scores[lag] > scores[lag + 1];
}

static inline bool pulse_has_second_repeat(const float *scores, unsigned lag, float best)
{
 if (scores[lag] < 0.32f) return false;
 for (unsigned repeat = 2 * lag - 2; repeat <= 2 * lag + 2 && repeat <= PULSE_MAX_LAG; ++repeat) {
  if (pulse_is_peak(scores, repeat) && scores[repeat] >= 0.44f &&
      scores[repeat] >= best * 0.80f && scores[lag] >= scores[repeat] * 0.55f) return true;
 }
 return false;
}

static inline bool pulse_lags_are_octaves(unsigned first, unsigned second)
{
 unsigned faster = first < second ? first : second;
 unsigned slower = first < second ? second : first;
 return abs((int)slower - 2 * (int)faster) * 8 <= 2 * (int)faster;
}

static inline bool pulse_matches_cadence(unsigned lag, unsigned cadence)
{
 unsigned shorter = lag < cadence ? lag : cadence;
 unsigned longer = lag < cadence ? cadence : lag;
 unsigned multiple = (longer + shorter / 2) / shorter;
 return abs((int)longer - (int)(multiple * shorter)) * 8 <= (int)shorter;
}

static inline unsigned pulse_find_lag(const struct pulse_state *p, unsigned cadence)
{
 float samples[PULSE_WINDOW_SAMPLES];
 bool moving[PULSE_WINDOW_SAMPLES];
 float scores[PULSE_MAX_LAG + 2];
 unsigned disturbed = 0;
 unsigned position = (p->window_next + PULSE_WINDOW_SAMPLES - p->window_count) %
                     PULSE_WINDOW_SAMPLES;
 for (unsigned i = 0; i < p->window_count; ++i) {
  samples[i] = p->window[position];
  moving[i] = p->moving_window[position];
  disturbed += moving[i];
  position = (position + 1) % PULSE_WINDOW_SAMPLES;
 }
 /* A flat newest second cannot refresh a BPM from older samples. */
 unsigned newest = p->window_count - PULSE_RATE_HZ;
 if (pulse_correlation_score(samples + newest, moving + newest, PULSE_RATE_HZ, 0) == 0) {
  return 0;
 }
 bool mostly_moving = 4 * disturbed > p->window_count;
 if (mostly_moving && !cadence && 4 * disturbed >= 3 * p->window_count) return 0;
 float minimum = p->window_count < PULSE_MIN_WINDOW_SAMPLES ? 0.70f : 0.44f;
 if (mostly_moving && minimum < 0.64f) minimum = 0.64f;
 for (unsigned lag = PULSE_MIN_LAG - 1; lag <= PULSE_MAX_LAG + 1; ++lag) {
  scores[lag] = pulse_correlation_score(samples, moving, p->window_count, lag);
 }
 /* Scores are squared correlations. Select the first strong peak so a
  * repeat two beats later does not halve the estimate. */
 float best = 0;
 for (unsigned lag = PULSE_MIN_LAG; lag <= PULSE_MAX_LAG; ++lag) {
  if (cadence && pulse_matches_cadence(lag, cadence)) continue;
  if (pulse_is_peak(scores, lag) && scores[lag] > best) best = scores[lag];
 }
 for (unsigned lag = PULSE_MIN_LAG; lag <= PULSE_MAX_LAG; ++lag) {
  if (!pulse_is_peak(scores, lag)) continue;
  if (cadence && pulse_matches_cadence(lag, cadence)) continue;
  if (scores[lag] >= minimum && scores[lag] >= best * 0.80f) return lag;
  /* Alternating strong and weak beats can make the two-beat repeat stronger. */
  if (!cadence && minimum == 0.44f && pulse_has_second_repeat(scores, lag, best)) return lag;
 }
 return 0;
}

static inline void pulse_clear_window(struct pulse_state *p)
{
 p->contact_samples = 0;
 p->window_count = 0;
 p->window_next = 0;
 p->previous_lag = 0;
 p->confirmed_lag = 0;
 p->octave_confirmations = 0;
}

static inline void pulse_sample_motion(struct pulse_state *p, uint32_t ir, bool moving,
                                       unsigned cadence)
{
 ir = pulse_filter_ir(&p->ir_filter, ir);
 if (p->bpm && p->samples_since_estimate < UINT16_MAX) ++p->samples_since_estimate;
 if (ir < 1000 || ir >= 261000) {
  if (p->missing_contact_samples < PULSE_CONTACT_LOSS_SAMPLES) ++p->missing_contact_samples;
  if (p->missing_contact_samples == PULSE_CONTACT_LOSS_SAMPLES) p->bpm = 0;
  p->contact = false;
  pulse_clear_window(p);
  return;
 }
 p->missing_contact_samples = 0;
 p->contact = true;
 if (!p->contact_samples) {
  p->baseline = (int32_t)ir * 256;
  p->slow = 0;
  p->filtered = 0;
 }
 ++p->contact_samples;
 int32_t raw = (int32_t)ir * 256;
 /* Two high-pass stages suppress pressure drift. The low-pass stage attenuates noise. */
 p->baseline += (raw - p->baseline) / 8;
 int32_t above_baseline = raw - p->baseline;
 p->slow += (above_baseline - p->slow) / 8;
 p->filtered += (above_baseline - p->slow - p->filtered) / 4;
 if (p->contact_samples <= PULSE_SETTLE_SAMPLES) return;

 p->window[p->window_next] = p->filtered / 256.0f;
 p->moving_window[p->window_next] = moving;
 p->window_next = (p->window_next + 1) % PULSE_WINDOW_SAMPLES;
 if (p->window_count < PULSE_WINDOW_SAMPLES) ++p->window_count;
 if (p->window_count < PULSE_EARLY_WINDOW_SAMPLES || p->contact_samples % PULSE_ESTIMATE_STEP) return;

 unsigned lag = pulse_find_lag(p, cadence);
 unsigned previous = p->previous_lag;
 unsigned separation = p->contact_samples - p->previous_lag_sample;
 if (!lag) {
  p->octave_confirmations = 0;
  return;
 }
 p->previous_lag = lag;
 p->previous_lag_sample = p->contact_samples;
 if (!previous || separation > 2 * PULSE_ESTIMATE_STEP ||
     abs((int)lag - (int)previous) * 12 > (int)previous) {
  p->octave_confirmations = 0;
  return;
 }

 /* A new lag near twice or half the tracked lag needs stronger evidence. */
 if (p->confirmed_lag && p->contact_samples - p->last_confirmed_sample < PULSE_OCTAVE_MEMORY_SAMPLES &&
     pulse_lags_are_octaves(lag, p->confirmed_lag)) {
  if (++p->octave_confirmations < PULSE_OCTAVE_CONFIRMATIONS) return;
 }
 p->octave_confirmations = 0;
 p->confirmed_lag = lag;
 p->last_confirmed_sample = p->contact_samples;
 uint16_t candidate = (60 * PULSE_RATE_HZ + lag / 2) / lag;
 p->samples_since_estimate = 0;
 if (abs((int)candidate - p->bpm) >= PULSE_UPDATE_DELTA_BPM) p->bpm = candidate;
}

static inline void pulse_sample(struct pulse_state *p, uint32_t ir)
{
 pulse_sample_motion(p, ir, false, 0);
}

#endif
