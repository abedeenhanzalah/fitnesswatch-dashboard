/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WATCH_AQI_H
#define WATCH_AQI_H

#include <stdbool.h>
#include <stdint.h>

#define WATCH_AQI_PACKET_SIZE 5U
#define WATCH_AQI_MAX_VALID_SECONDS 7200U

struct watch_aqi {
 int64_t expires_ms;
 uint16_t value;
};

static inline bool watch_aqi_receive(struct watch_aqi *reading,
                                    const uint8_t data[WATCH_AQI_PACKET_SIZE], int64_t now)
{
 if (data[0] != 2) return false;
 uint16_t value = data[1] | ((uint16_t)data[2] << 8);
 uint16_t valid_seconds = data[3] | ((uint16_t)data[4] << 8);
 if (value > 500 || valid_seconds == 0 || valid_seconds > WATCH_AQI_MAX_VALID_SECONDS) return false;
 reading->value = value;
 reading->expires_ms = now + (int64_t)valid_seconds * 1000;
 return true;
}

static inline bool watch_aqi_current(const struct watch_aqi *reading, int64_t now,
                                    uint16_t *value)
{
 if (now >= reading->expires_ms) return false;
 *value = reading->value;
 return true;
}

/* For a current reading, its remaining lifetime encodes the age at sync. */
static inline uint16_t watch_aqi_age_minutes(const struct watch_aqi *reading, int64_t now)
{
 return (WATCH_AQI_MAX_VALID_SECONDS * 1000 - (reading->expires_ms - now)) / 60000;
}

static inline const char *watch_aqi_category(uint16_t value)
{
 if (value <= 50) return "Good";
 if (value <= 100) return "Satisfactory";
 if (value <= 200) return "Moderate";
 if (value <= 300) return "Poor";
 if (value <= 400) return "Very Poor";
 return "Severe";
}

#endif
