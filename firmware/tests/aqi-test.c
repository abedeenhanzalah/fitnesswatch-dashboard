#include <assert.h>
#include <string.h>
#include "aqi.h"

int main(void)
{
 struct watch_aqi reading = {0};
 uint16_t value;
 uint16_t age_minutes;
 const uint8_t valid[] = {2, 44, 1, 60, 0};
 const uint8_t bad_version[] = {1, 5, 0, 60, 0};
 const uint8_t out_of_range[] = {2, 245, 1, 60, 0};
 const uint8_t expired[] = {2, 44, 1, 0, 0};
 const uint8_t zero[] = {2, 0, 0, 60, 0};
 const uint8_t observed_35_minutes_ago[] = {2, 21, 1, 236, 19};

 assert(!watch_aqi_current(&reading, 5000, &value));
 assert(watch_aqi_receive(&reading, valid, 5000));
 assert(!watch_aqi_receive(&reading, bad_version, 6000));
 assert(!watch_aqi_receive(&reading, out_of_range, 6000));
 assert(!watch_aqi_receive(&reading, expired, 6000));
 assert(watch_aqi_current(&reading, 64999, &value));
 assert(value == 300);
 assert(!watch_aqi_current(&reading, 65000, &value));
 assert(watch_aqi_receive(&reading, zero, 65001));
 assert(watch_aqi_current(&reading, 65001, &value));
 assert(value == 0);
 assert(watch_aqi_receive(&reading, observed_35_minutes_ago, 100000));
 age_minutes = watch_aqi_age_minutes(&reading, 100000);
 assert(age_minutes == 35);
 assert(watch_aqi_age_minutes(&reading, 160000) == 36);
 assert(strcmp(watch_aqi_category(50), "Good") == 0);
 assert(strcmp(watch_aqi_category(51), "Satisfactory") == 0);
 assert(strcmp(watch_aqi_category(100), "Satisfactory") == 0);
 assert(strcmp(watch_aqi_category(101), "Moderate") == 0);
 assert(strcmp(watch_aqi_category(200), "Moderate") == 0);
 assert(strcmp(watch_aqi_category(201), "Poor") == 0);
 assert(strcmp(watch_aqi_category(300), "Poor") == 0);
 assert(strcmp(watch_aqi_category(301), "Very Poor") == 0);
 assert(strcmp(watch_aqi_category(400), "Very Poor") == 0);
 assert(strcmp(watch_aqi_category(401), "Severe") == 0);
 assert(strcmp(watch_aqi_category(500), "Severe") == 0);
}
