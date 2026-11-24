#ifndef WATCH_TELEMETRY_H
#define WATCH_TELEMETRY_H
#include <stdbool.h>
#include <stdint.h>

#define WATCH_RAW_PACKET_SIZE 10
#define WATCH_ACTIVITY_PACKET_SIZE 14
#define WATCH_CLOCK_PACKET_SIZE 7

/* Version, flags, steps, UTC seconds, UTC offset minutes, and IMU die °C × 100. */
static inline void watch_encode_activity(uint8_t *p, uint8_t flags, uint32_t steps,
                                         uint32_t epoch, int16_t offset, int16_t temperature)
{
 p[0] = 1;
 p[1] = flags;
 for (int i = 0; i < 4; ++i) {
  p[2 + i] = steps >> (8 * i);
  p[6 + i] = epoch >> (8 * i);
 }
 p[10] = (uint16_t)offset;
 p[11] = (uint16_t)offset >> 8;
 p[12] = (uint16_t)temperature;
 p[13] = (uint16_t)temperature >> 8;
}

/* Version, sample availability, then red and IR as little-endian uint32s. */
static inline void watch_encode_raw(uint8_t *p, bool available, uint32_t red, uint32_t ir)
{
 p[0] = 1;
 p[1] = available;
 for (int i = 0; i < 4; ++i) {
  p[2 + i] = available ? red >> (8 * i) : 0;
  p[6 + i] = available ? ir >> (8 * i) : 0;
 }
}

static inline void watch_encode(uint8_t *p, uint8_t status, uint16_t seq,
                                uint32_t seconds, uint16_t bpm, uint8_t spo2)
{
 p[0]=1; p[1]=status; p[2]=seq; p[3]=seq>>8;
 for (int i=0;i<4;i++) p[4+i]=seconds>>(8*i);
 p[8]=bpm; p[9]=bpm>>8; p[10]=spo2; p[11]=0;
}
static inline uint16_t watch_bpm(const uint8_t *p)
{
 uint32_t n=((uint32_t)p[2]<<24)|((uint32_t)p[3]<<16)|((uint32_t)p[4]<<8)|p[5];
 return n>=25 && n<=250 ? (uint16_t)n : 0;
}
#endif
