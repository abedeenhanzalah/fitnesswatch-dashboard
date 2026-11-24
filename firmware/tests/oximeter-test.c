#include <assert.h>
#include <math.h>
#include "oximeter.h"

int main(void)
{
 struct watch_oximeter oxygen = {0};
 for (unsigned i = 0; i < 600; ++i) {
  double wave = sin(i * 6.2831853 * 1.2 / 50);
  oximeter_sample(&oxygen, 40000 + (int)(200 * wave), 50000 + (int)(500 * wave), false);
 }
 assert(oximeter_trial(&oxygen, 42) == 99);
 assert(oximeter_trial(&oxygen, 0) == 0);
 oximeter_sample(&oxygen, 40000, 50000, true);
 assert(oximeter_trial(&oxygen, 42) == 0);

 for (unsigned i = 0; i < 600; ++i) {
  uint32_t ir = 50000 + (int)(500 * sin(i * 6.2831853 * 1.2 / 50));
  oximeter_sample(&oxygen, 10, ir, false);
 }
 assert(oximeter_trial(&oxygen, 42) == 0);

 oxygen = (struct watch_oximeter){0};
 for (unsigned i = 0; i < 600; ++i) {
  uint32_t red = 40000 + (int)(200 * sin(i * 6.2831853 / 50));
  uint32_t ir = 50000 + (int)(500 * sin(i * 6.2831853 * 1.6 / 50));
  oximeter_sample(&oxygen, red, ir, false);
 }
 assert(oximeter_trial(&oxygen, 42) == 0);

 oxygen = (struct watch_oximeter){0};
 for (unsigned i = 0; i < 600; ++i) {
  double phase = i * 6.2831853 / 40;
  uint32_t red = 40000 + (int)(200 * sin(phase) + 300 * sin(phase * 2));
  uint32_t ir = 50000 + (int)(500 * sin(phase));
  oximeter_sample(&oxygen, red, ir, false);
 }
 assert(oximeter_trial(&oxygen, 40) == 99);
}
