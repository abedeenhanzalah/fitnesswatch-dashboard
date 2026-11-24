#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("line %d: bpm=%u\n", __LINE__, p.bpm); return 1; } } while (0)
#include <math.h>
#include "pulse.h"

static int rejects_optical_spikes(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 200; ++n) {
  uint32_t ir = 25000 + (int)(300 * sin(6.2831853 * 1.2 * n / 50));
  if (n % 17 == 5 || n % 17 == 6) ir = 262143;
  pulse_sample(&p, ir);
 }
 CHECK(p.bpm >= 70 && p.bpm <= 74);
 for (unsigned n = 0; n < 350; ++n) pulse_sample(&p, 25000);
 CHECK(p.bpm >= 70 && p.bpm <= 74 && p.samples_since_estimate >= PULSE_RATE_HZ);
 for (unsigned n = 0; n < 200; ++n) pulse_sample_motion(&p, 25000, true, 0);
 CHECK(p.contact && p.bpm >= 70 && p.bpm <= 74 && p.samples_since_estimate >= 200);
 return 0;
}

static int rejects_slow_pressure_changes(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 400; ++n) {
  double seconds = n / 50.0;
  uint32_t ir = 25000 + (int)(300 * sin(6.2831853 * 1.3 * seconds) +
                              1200 * sin(6.2831853 * 0.27 * seconds));
  pulse_sample(&p, ir);
 }
 CHECK(p.bpm >= 75 && p.bpm <= 81);
 return 0;
}

static int prefers_a_supported_single_beat(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 400; ++n) {
  double wave = n * 6.2831853 * (88.0 / 60) / 50;
  pulse_sample(&p, 25000 + (int)(210 * sin(wave) + 105 * sin(wave / 2)));
 }
 CHECK(p.bpm >= 85 && p.bpm <= 91);
 return 0;
}

static int preserves_a_slow_pulse(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 400; ++n) {
  double wave = n * 6.2831853 * (44.0 / 60) / 50;
  pulse_sample(&p, 25000 + (int)(300 * sin(wave)));
 }
 CHECK(p.bpm >= 42 && p.bpm <= 46);
 return 0;
}

static int delays_a_sudden_halving(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 400; ++n) {
  double wave = n * 6.2831853 * (88.0 / 60) / 50;
  pulse_sample(&p, 25000 + (int)(300 * sin(wave)));
 }
 CHECK(p.bpm >= 85 && p.bpm <= 91);
 for (unsigned n = 0; n < 250; ++n) {
  double wave = n * 6.2831853 * (44.0 / 60) / 50;
  pulse_sample(&p, 25000 + (int)(300 * sin(wave)));
  CHECK(p.bpm == 0 || p.bpm >= 80);
 }
 for (unsigned n = 250; n < 600; ++n) {
  double wave = n * 6.2831853 * (44.0 / 60) / 50;
  pulse_sample(&p, 25000 + (int)(300 * sin(wave)));
 }
 CHECK(p.bpm >= 42 && p.bpm <= 46);
 return 0;
}

static int tracks_pulse_through_short_movements(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 175; ++n) {
  double wave = n * 6.2831853 * 1.2 / 50;
  pulse_sample_motion(&p, 25000 + (int)(300 * sin(wave)), n % 30 < 4, 0);
 }
 CHECK(p.bpm >= 70 && p.bpm <= 74);
 return 0;
}

static int distinguishes_pulse_from_walking_cadence(void)
{
 struct pulse_state p = {0};
 const unsigned cadence = 25; /* 120 steps per minute at 50 samples/second. */
 for (unsigned n = 0; n < 600; ++n) {
  double footfall = n * 6.2831853 / cadence;
  pulse_sample_motion(&p, 25000 + (int)(300 * sin(footfall)), true, cadence);
 }
 CHECK(p.bpm == 0);

 p = (struct pulse_state){0};
 for (unsigned n = 0; n < 600; ++n) {
  double footfall = n * 6.2831853 / cadence;
  double pulse = n * 6.2831853 * (84.0 / 60) / 50;
  pulse_sample_motion(&p, 25000 + (int)(300 * sin(pulse) + 70 * sin(footfall)), true, cadence);
 }
 CHECK(p.bpm >= 81 && p.bpm <= 87);
 return 0;
}

int main(void)
{
 struct pulse_state p = {0};
 for (unsigned n = 0; n < 200; ++n)
  pulse_sample(&p, 25000 + (int)(300 * sin(6.2831853 * 1.2 * n / 50)));
 CHECK(p.bpm >= 71 && p.bpm <= 73);
 const uint16_t first_bpm = p.bpm;
 for (unsigned n = 0; n < 150; ++n) pulse_sample(&p, 25000);
 CHECK(p.bpm == first_bpm);
 for (unsigned n = 0; n < 500; ++n)
  pulse_sample(&p, 25000 + (int)(300 * sin(6.2831853 * (74.0 / 60) * n / 50)));
 CHECK(p.bpm == first_bpm);
 for (unsigned n = 0; n < 500; ++n)
  pulse_sample(&p, 25000 + (int)(300 * sin(6.2831853 * n / 50)));
 CHECK(p.bpm >= 59 && p.bpm <= 61);
 const uint16_t updated_bpm = p.bpm;
 for (unsigned n = 0; n < 99; ++n) pulse_sample(&p, 100);
 CHECK(p.bpm == updated_bpm);
 for (unsigned n = 0; n < 5; ++n) pulse_sample(&p, 25000);
 for (unsigned n = 0; n < 101; ++n) pulse_sample(&p, 100);
 CHECK(p.bpm == updated_bpm);
 pulse_sample(&p, 100);
 CHECK(p.bpm == 0 && !p.contact);
 if (rejects_optical_spikes()) return 1;
 if (rejects_slow_pressure_changes()) return 1;
 if (prefers_a_supported_single_beat()) return 1;
 if (preserves_a_slow_pulse()) return 1;
 if (delays_a_sudden_halving()) return 1;
 if (tracks_pulse_through_short_movements()) return 1;
 return distinguishes_pulse_from_walking_cadence();
}

